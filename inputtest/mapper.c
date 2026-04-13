/*
 * mapper.c — Android touch keymap injector
 *
 * Runs inside a chroot Ubuntu on a rooted Android device.
 * Reads raw keyboard/mouse events from /dev/input, translates them
 * using the SFA keymap layout, and injects multitouch events directly
 * into the real touchscreen device (/dev/input/event3).
 *
 * No ADB, no scrcpy, no display server required.
 *
 * Architecture:
 *   - Main thread: polls keyboard (event9) + mouse (event6, event7)
 *   - Steer wheel (WASD): handled inline on main thread (single continuous touch)
 *   - Button clicks: async threads with minimum hold enforcement
 *   - Drags (E/Q/X/2): async threads that interpolate touch movement
 *   - All touch writes are mutex-protected for safe multitouch
 *   - BTN_TOUCH is managed globally: 1 on first finger, 0 on last lift
 *
 * Toggle: Press Ctrl (left or right) to enter/exit keymap mode.
 *         In keymap mode Android is blind to keyboard/mouse input.
 *
 * Keymap (from sfa.json, landscape 2400x1080 → portrait 1080x2400):
 *   WASD        — steer wheel (combinable, continuous hold)
 *   Left click  — light/heavy attack (short=tap, hold=hold)
 *   Right click — secondary attack (short=tap, hold=hold)
 *   Fwd button  — forward action
 *   Back button — back action
 *   E           — drag right
 *   Q           — drag left
 *   X           — drag down
 *   2           — drag up
 *   5           — multi-click sequence
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <linux/input.h>
#include <sys/ioctl.h>

/* ── Device paths ─────────────────────────────────────────────────────────── */
#define TOUCH_DEV   "/dev/input/event3"   /* fts_ts real touchscreen          */
#define KBD_DEV     "/dev/input/event9"   /* USB keyboard                     */
#define MOUSE_DEV   "/dev/input/event6"   /* USB mouse (buttons + movement)   */
#define MOUSE2_DEV  "/dev/input/event7"   /* USB mouse (extra keys interface) */

/* ── Screen geometry (portrait physical) ─────────────────────────────────── */
#define SCREEN_X_MAX 1079
#define SCREEN_Y_MAX 2399

/*
 * Coordinate transform: sfa.json uses landscape (2400×1080) relative coords.
 * Physical screen is portrait (1080×2400), so axes are swapped + Y mirrored.
 *   phys_x = (1 - rel_y) * SCREEN_X_MAX
 *   phys_y =  rel_x      * SCREEN_Y_MAX
 */
#define PX(rx,ry) ((int)((1.0f-(ry))*SCREEN_X_MAX))
#define PY(rx,ry) ((int)((rx)*SCREEN_Y_MAX))

/* ── Key codes ────────────────────────────────────────────────────────────── */
#define KEY_LCTRL 29
#define KEY_RCTRL 97
#define KEY_W     17
#define KEY_A     30
#define KEY_S     31
#define KEY_D     32
#define KEY_X     45
#define KEY_2     3
#define KEY_E     18
#define KEY_Q     16
#define KEY_5     6

/* ── Touch slots (each concurrent finger needs its own slot 0-9) ─────────── */
#define SLOT_STEER  0   /* WASD steer wheel                  */
#define SLOT_MOUSE  1   /* mouse look (unused currently)     */
#define SLOT_DRAG   2   /* E/Q/X/2 drag gestures             */
#define SLOT_LCLICK 3   /* left mouse button                 */
#define SLOT_RCLICK 4   /* right mouse button                */
#define SLOT_FWD    5   /* forward/back mouse side buttons   */

/* ── Globals ──────────────────────────────────────────────────────────────── */
static int tfd, kfd, mfd, mfd2;
static volatile int running = 1;
static volatile int ctrl_gate = 1; /* 1=Ctrl toggle active, 0=blocked via SIGUSR1 */
static int grabbed = 0;

/* Multitouch slot tracking — ensures BTN_TOUCH is correct */
static pthread_mutex_t touch_mtx = PTHREAD_MUTEX_INITIALIZER;
static int slot_active[10] = {0};

/* Steer wheel key state */
static int sw_w=0, sw_a=0, sw_s=0, sw_d=0, sw_active=0;

/* Click hold state (for light vs heavy attack detection) */
static int lclick_held=0, rclick_held=0;

static void sig(int x)      { (void)x; running=0; }
static void sig_usr1(int x) { (void)x; ctrl_gate=!ctrl_gate; } /* SIGUSR1 flips gate */

/* ── Low-level touch emit (call only while holding touch_mtx) ────────────── */
static void _emit(int type, int code, int val) {
    struct input_event ev = {.type=type, .code=code, .value=val};
    ssize_t r = write(tfd, &ev, sizeof(ev)); (void)r;
}
static void _syn(void) { _emit(EV_SYN, SYN_REPORT, 0); }

static int active_count(void) {
    int n=0; for (int i=0;i<10;i++) n+=slot_active[i]; return n;
}

/* ── Public multitouch API (mutex-safe) ──────────────────────────────────── */
static void slot_down(int slot, int tid, int x, int y) {
    pthread_mutex_lock(&touch_mtx);
    int was_any = active_count();
    slot_active[slot] = 1;
    _emit(EV_ABS, ABS_MT_SLOT,        slot);
    _emit(EV_ABS, ABS_MT_TRACKING_ID, tid);
    _emit(EV_ABS, ABS_MT_POSITION_X,  x);
    _emit(EV_ABS, ABS_MT_POSITION_Y,  y);
    if (!was_any) _emit(EV_KEY, BTN_TOUCH, 1);
    _syn();
    pthread_mutex_unlock(&touch_mtx);
}

static void slot_move(int slot, int x, int y) {
    pthread_mutex_lock(&touch_mtx);
    _emit(EV_ABS, ABS_MT_SLOT,       slot);
    _emit(EV_ABS, ABS_MT_POSITION_X, x);
    _emit(EV_ABS, ABS_MT_POSITION_Y, y);
    _syn();
    pthread_mutex_unlock(&touch_mtx);
}

static void slot_up(int slot) {
    pthread_mutex_lock(&touch_mtx);
    if (!slot_active[slot]) { pthread_mutex_unlock(&touch_mtx); return; }
    slot_active[slot] = 0;
    _emit(EV_ABS, ABS_MT_SLOT,        slot);
    _emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    if (!active_count()) _emit(EV_KEY, BTN_TOUCH, 0);
    _syn();
    pthread_mutex_unlock(&touch_mtx);
}

/* ── Steer wheel ─────────────────────────────────────────────────────────── */
static void update_steer(void) {
    float tx=0.2134f, ty=0.5404f;
    if (sw_a) tx-=0.075f;
    if (sw_d) tx+=0.075f;
    if (sw_w) ty-=0.100f;
    if (sw_s) ty+=0.100f;
    int any = sw_w||sw_a||sw_s||sw_d;
    if (!any && sw_active) {
        slot_up(SLOT_STEER); sw_active=0;
    } else if (any) {
        if (!sw_active) { slot_down(SLOT_STEER,10,PX(tx,ty),PY(tx,ty)); sw_active=1; }
        else            { slot_move(SLOT_STEER,    PX(tx,ty),PY(tx,ty)); }
    }
}

/* ── Click thread: enforces minimum hold, supports actual hold ───────────── */
typedef struct { int slot,tid; float rx,ry; int *held; } ClickArgs;

static void* click_thread(void *arg) {
    ClickArgs *c = arg;
    slot_down(c->slot, c->tid, PX(c->rx,c->ry), PY(c->rx,c->ry));
    usleep(120000); /* minimum 120ms — prevents accidental heavy attacks */
    if (!*(c->held)) slot_up(c->slot); /* released early → lift now */
    /* still held → main thread will call slot_up on mouse button release */
    free(c);
    return NULL;
}
static void async_click(int slot, int tid, float rx, float ry, int *held) {
    ClickArgs *c = malloc(sizeof(ClickArgs));
    *c = (ClickArgs){slot,tid,rx,ry,held};
    pthread_t th; pthread_create(&th,NULL,click_thread,c); pthread_detach(th);
}

/* ── Tap thread: fixed-duration tap (for side buttons, multi-click) ──────── */
typedef struct { int slot,tid; float rx,ry; int hold_us; } TapArgs;

static void* tap_thread(void *arg) {
    TapArgs *t = arg;
    slot_down(t->slot, t->tid, PX(t->rx,t->ry), PY(t->rx,t->ry));
    usleep(t->hold_us);
    slot_up(t->slot);
    free(t);
    return NULL;
}
static void async_tap(int slot, int tid, float rx, float ry, int hold_us) {
    TapArgs *t = malloc(sizeof(TapArgs));
    *t = (TapArgs){slot,tid,rx,ry,hold_us};
    pthread_t th; pthread_create(&th,NULL,tap_thread,t); pthread_detach(th);
}

/* ── Drag thread: interpolated swipe gesture ─────────────────────────────── */
typedef struct { float sx,sy,ex,ey; int dur_us; } DragArgs;

static void* drag_thread(void *arg) {
    DragArgs *da = arg;
    float dx=da->ex-da->sx, dy=da->ey-da->sy;
    int steps=10;
    slot_down(SLOT_DRAG,20,PX(da->sx,da->sy),PY(da->sx,da->sy));
    for (int i=1;i<=steps;i++) {
        float t=(float)i/steps;
        slot_move(SLOT_DRAG, PX(da->sx+dx*t, da->sy+dy*t),
                             PY(da->sx+dx*t, da->sy+dy*t));
        usleep(da->dur_us/steps);
    }
    slot_up(SLOT_DRAG);
    free(da);
    return NULL;
}
static void async_drag(float sx,float sy,float ex,float ey,int ms) {
    DragArgs *da = malloc(sizeof(DragArgs));
    *da = (DragArgs){sx,sy,ex,ey,ms*1000};
    pthread_t th; pthread_create(&th,NULL,drag_thread,da); pthread_detach(th);
}

/* ── Grab control ────────────────────────────────────────────────────────── */
static void set_grab(int on) {
    ioctl(kfd, EVIOCGRAB, on);
    ioctl(mfd, EVIOCGRAB, on);
    ioctl(mfd2,EVIOCGRAB, on);
    printf("[mapper] keymap %s\n", on?"ON":"OFF"); fflush(stdout);
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(void) {
    signal(SIGINT, sig);
    signal(SIGTERM, sig);
    signal(SIGUSR1, sig_usr1); /* sent by app to flip ctrl_gate */

    tfd  = open(TOUCH_DEV,  O_WRONLY);
    kfd  = open(KBD_DEV,    O_RDONLY|O_NONBLOCK);
    mfd  = open(MOUSE_DEV,  O_RDONLY|O_NONBLOCK);
    mfd2 = open(MOUSE2_DEV, O_RDONLY|O_NONBLOCK);
    if (tfd<0||kfd<0||mfd<0||mfd2<0) { perror("open"); return 1; }

    /* Write PID so the app can send signals */
    FILE *pf = fopen("/data/local/tmp/mapper.pid", "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    printf("Ready. Press Ctrl to toggle keymap mode.\n"); fflush(stdout);

    struct pollfd pfds[3] = {
        {kfd,  POLLIN, 0},
        {mfd,  POLLIN, 0},
        {mfd2, POLLIN, 0},
    };
    struct input_event ev;

    while (running) {
        if (poll(pfds,3,500)<=0) continue;
        for (int i=0;i<3;i++) {
            if (!(pfds[i].revents&POLLIN)) continue;
            int fd = i==0?kfd : i==1?mfd : mfd2;
            while (read(fd,&ev,sizeof(ev))==sizeof(ev)) {

                /* Ctrl toggles keymap mode — only if ctrl_gate is enabled */
                if (ev.type==EV_KEY&&(ev.code==KEY_LCTRL||ev.code==KEY_RCTRL)) {
                    if (ev.value==1 && ctrl_gate) {
                        grabbed=!grabbed; set_grab(grabbed);
                        if (!grabbed) {
                            sw_w=sw_a=sw_s=sw_d=0; update_steer();
                            lclick_held=0; slot_up(SLOT_LCLICK);
                            rclick_held=0; slot_up(SLOT_RCLICK);
                            slot_up(SLOT_FWD);
                        }
                    }
                    continue;
                }

                if (!grabbed) continue;
                if (ev.type!=EV_KEY||ev.value==2) continue; /* skip non-key/repeat */
                int dn = ev.value; /* 1=down, 0=up */

                /* Mouse buttons */
                if (ev.code==272) { /* left click → light/heavy attack */
                    if (dn) { lclick_held=1; async_click(SLOT_LCLICK,40,0.9054f,0.5703f,&lclick_held); }
                    else    { lclick_held=0; slot_up(SLOT_LCLICK); }
                    continue;
                }
                if (ev.code==273) { /* right click → secondary attack */
                    if (dn) { rclick_held=1; async_click(SLOT_RCLICK,41,0.8421f,0.6968f,&rclick_held); }
                    else    { rclick_held=0; slot_up(SLOT_RCLICK); }
                    continue;
                }
                if (ev.code==276) { /* forward side button */
                    if (dn) slot_down(SLOT_FWD,42,PX(0.8563f,0.406f), PY(0.8563f,0.406f));
                    else    slot_up(SLOT_FWD);
                    continue;
                }
                if (ev.code==275) { /* back side button — shares slot with fwd (not simultaneous) */
                    if (dn) slot_down(SLOT_FWD,42,PX(0.803f,0.5229f),PY(0.803f,0.5229f));
                    else    slot_up(SLOT_FWD);
                    continue;
                }

                /* WASD steer wheel */
                if (ev.code==KEY_W) { sw_w=dn; update_steer(); continue; }
                if (ev.code==KEY_A) { sw_a=dn; update_steer(); continue; }
                if (ev.code==KEY_S) { sw_s=dn; update_steer(); continue; }
                if (ev.code==KEY_D) { sw_d=dn; update_steer(); continue; }

                if (!dn) continue; /* remaining actions are key-down only */

                /* Drag gestures */
                if (ev.code==KEY_E) { async_drag(0.69f,0.88f,1.20f,0.88f,150); continue; } /* right */
                if (ev.code==KEY_Q) { async_drag(0.69f,0.88f,0.00f,0.88f,150); continue; } /* left  */
                if (ev.code==KEY_X) { async_drag(0.69f,0.88f,0.69f,1.40f,150); continue; } /* down  */
                if (ev.code==KEY_2) { async_drag(0.69f,0.88f,0.69f,0.55f,150); continue; } /* up    */

                /* Multi-click sequence */
                if (ev.code==KEY_5) {
                    async_tap(SLOT_LCLICK,50,0.8563f,0.206f,80000);
                    usleep(110000);
                    async_tap(SLOT_RCLICK,51,0.7863f,0.206f,80000);
                    continue;
                }
            }
        }
    }

    /* Clean up all active touches on exit */
    if (grabbed) {
        sw_w=sw_a=sw_s=sw_d=0; update_steer();
        lclick_held=0; slot_up(SLOT_LCLICK);
        rclick_held=0; slot_up(SLOT_RCLICK);
        slot_up(SLOT_FWD);
        set_grab(0);
    }
    close(tfd); close(kfd); close(mfd); close(mfd2);
    printf("Quit.\n");
    return 0;
}
