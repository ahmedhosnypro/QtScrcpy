/*
 * mapper.c — Full SFA keymap mapper with dynamic device config
 *
 * Config file: /data/data/com.keymap/files/devices.json
 * Format:
 *   {"keyboards":[{"path":"/dev/input/event9","enabled":true},...],
 *    "mice":[{"path":"/dev/input/event6","enabled":true},...]}
 *
 * Signals:
 *   SIGTERM/SIGINT — quit
 *   SIGUSR1        — flip ctrl_gate (enable/disable Ctrl toggle)
 *   SIGUSR2        — reload device config from file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <stdarg.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#define TOUCH_DEV    "/dev/input/event3"
#define CONFIG_FILE  "/root/mapper-test/mapper_devices.json"
#define PID_FILE     "/root/mapper-test/mapper.pid"
#define MAX_DEVICES  16
#define LOG_FILE     "/root/mapper-test/mapper.log"

static FILE *logf = NULL;
static int logging_enabled = 1;

static void mlog(const char *fmt, ...) {
    if (!logging_enabled || !logf) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(logf, fmt, ap); fprintf(logf, "\n"); fflush(logf);
    va_end(ap);
}

#define SCREEN_X_MAX 1079
#define SCREEN_Y_MAX 2399
#define PX(rx,ry) ((int)((1.0f-(ry))*SCREEN_X_MAX))
#define PY(rx,ry) ((int)((rx)*SCREEN_Y_MAX))

#define KEY_LCTRL 41  /* backtick ` for test mode */
#define KEY_RCTRL 41
#define KEY_W 17
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_X 45
#define KEY_2 3
#define KEY_E 18
#define KEY_Q 16
#define KEY_5 6

#define SLOT_STEER  0
#define SLOT_MOUSE  1
#define SLOT_DRAG   2
#define SLOT_LCLICK 3
#define SLOT_RCLICK 4
#define SLOT_FWD    5

/* ── Device config (reloaded on SIGUSR2) ─────────────────────────────────── */
typedef struct { char path[64]; int enabled; } DevEntry;
static DevEntry kbd_devs[MAX_DEVICES];
static DevEntry mouse_devs[MAX_DEVICES];
static int kbd_count = 0, mouse_count = 0;

/* Open fds — rebuilt on reload */
static int kbd_fds[MAX_DEVICES];
static int mouse_fds[MAX_DEVICES];
static int kbd_open = 0, mouse_open = 0;

static int tfd = -1;
static volatile int running = 1;
static volatile int ctrl_gate = 1;
static volatile int reload_config = 0;
static int grabbed = 0;

static void sig(int x)      { (void)x; running = 0; }
static void sig_usr1(int x) { (void)x; ctrl_gate = !ctrl_gate; }
static void sig_usr2(int x) { (void)x; reload_config = 1; }

/* ── Multitouch ──────────────────────────────────────────────────────────── */
static pthread_mutex_t touch_mtx = PTHREAD_MUTEX_INITIALIZER;
static int slot_active[10] = {0};
static int sw_w=0,sw_a=0,sw_s=0,sw_d=0,sw_active=0;
static int lclick_held=0,rclick_held=0;

static void _emit(int type, int code, int val) {
    struct input_event ev = {.type=type,.code=code,.value=val};
    ssize_t r = write(tfd,&ev,sizeof(ev)); (void)r;
}
static void _syn(void) { _emit(EV_SYN,SYN_REPORT,0); }
static int active_count(void) { int n=0; for(int i=0;i<10;i++) n+=slot_active[i]; return n; }

static void slot_down(int slot,int tid,int x,int y) {
    pthread_mutex_lock(&touch_mtx);
    int was=active_count(); slot_active[slot]=1;
    _emit(EV_ABS,ABS_MT_SLOT,slot); _emit(EV_ABS,ABS_MT_TRACKING_ID,tid);
    _emit(EV_ABS,ABS_MT_POSITION_X,x); _emit(EV_ABS,ABS_MT_POSITION_Y,y);
    if(!was) _emit(EV_KEY,BTN_TOUCH,1); _syn();
    pthread_mutex_unlock(&touch_mtx);
}
static void slot_move(int slot,int x,int y) {
    pthread_mutex_lock(&touch_mtx);
    _emit(EV_ABS,ABS_MT_SLOT,slot); _emit(EV_ABS,ABS_MT_POSITION_X,x); _emit(EV_ABS,ABS_MT_POSITION_Y,y); _syn();
    pthread_mutex_unlock(&touch_mtx);
}
static void slot_up(int slot) {
    pthread_mutex_lock(&touch_mtx);
    if(!slot_active[slot]){pthread_mutex_unlock(&touch_mtx);return;}
    slot_active[slot]=0;
    _emit(EV_ABS,ABS_MT_SLOT,slot); _emit(EV_ABS,ABS_MT_TRACKING_ID,-1);
    if(!active_count()) _emit(EV_KEY,BTN_TOUCH,0); _syn();
    pthread_mutex_unlock(&touch_mtx);
}

/* ── Steer wheel ─────────────────────────────────────────────────────────── */
static void update_steer(void) {
    float tx=0.2134f,ty=0.5404f;
    if(sw_a) tx-=0.075f; if(sw_d) tx+=0.075f;
    if(sw_w) ty-=0.100f; if(sw_s) ty+=0.100f;
    int any=sw_w||sw_a||sw_s||sw_d;
    if(!any&&sw_active){slot_up(SLOT_STEER);sw_active=0;}
    else if(any){
        if(!sw_active){slot_down(SLOT_STEER,10,PX(tx,ty),PY(tx,ty));sw_active=1;}
        else slot_move(SLOT_STEER,PX(tx,ty),PY(tx,ty));
    }
}

/* ── Click thread ────────────────────────────────────────────────────────── */
typedef struct{int slot,tid;float rx,ry;int*held;}ClickArgs;
static void* click_thread(void*arg){
    ClickArgs*c=arg;
    slot_down(c->slot,c->tid,PX(c->rx,c->ry),PY(c->rx,c->ry));
    usleep(120000);
    if(!*(c->held)) slot_up(c->slot);
    free(c); return NULL;
}
static void async_click(int slot,int tid,float rx,float ry,int*held){
    ClickArgs*c=malloc(sizeof(ClickArgs)); *c=(ClickArgs){slot,tid,rx,ry,held};
    pthread_t th; pthread_create(&th,NULL,click_thread,c); pthread_detach(th);
}

/* ── Tap thread ──────────────────────────────────────────────────────────── */
typedef struct{int slot,tid;float rx,ry;int hold_us;}TapArgs;
static void* tap_thread(void*arg){
    TapArgs*t=arg;
    slot_down(t->slot,t->tid,PX(t->rx,t->ry),PY(t->rx,t->ry));
    usleep(t->hold_us); slot_up(t->slot);
    free(t); return NULL;
}
static void async_tap(int slot,int tid,float rx,float ry,int hold_us){
    TapArgs*t=malloc(sizeof(TapArgs)); *t=(TapArgs){slot,tid,rx,ry,hold_us};
    pthread_t th; pthread_create(&th,NULL,tap_thread,t); pthread_detach(th);
}

/* ── Drag thread ─────────────────────────────────────────────────────────── */
typedef struct{float sx,sy,ex,ey;int dur_us;}DragArgs;
static void* drag_thread(void*arg){
    DragArgs*da=arg; float dx=da->ex-da->sx,dy=da->ey-da->sy; int steps=10;
    slot_down(SLOT_DRAG,20,PX(da->sx,da->sy),PY(da->sx,da->sy));
    for(int i=1;i<=steps;i++){
        float t=(float)i/steps;
        slot_move(SLOT_DRAG,PX(da->sx+dx*t,da->sy+dy*t),PY(da->sx+dx*t,da->sy+dy*t));
        usleep(da->dur_us/steps);
    }
    slot_up(SLOT_DRAG); free(da); return NULL;
}
static void async_drag(float sx,float sy,float ex,float ey,int ms){
    DragArgs*da=malloc(sizeof(DragArgs)); *da=(DragArgs){sx,sy,ex,ey,ms*1000};
    pthread_t th; pthread_create(&th,NULL,drag_thread,da); pthread_detach(th);
}

/* ── Grab helpers ────────────────────────────────────────────────────────── */
static void grab_all(int on) {
    for(int i=0;i<kbd_open;i++)   if(kbd_fds[i]>=0)   { ioctl(kbd_fds[i],  EVIOCGRAB,on); mlog("grab kbd fd=%d on=%d", kbd_fds[i], on); }
    for(int i=0;i<mouse_open;i++) if(mouse_fds[i]>=0) { ioctl(mouse_fds[i],EVIOCGRAB,on); mlog("grab mouse fd=%d on=%d", mouse_fds[i], on); }
}
static void close_input_fds(void) {
    for(int i=0;i<kbd_open;i++)   { if(kbd_fds[i]>=0)   close(kbd_fds[i]);   kbd_fds[i]=-1; }
    for(int i=0;i<mouse_open;i++) { if(mouse_fds[i]>=0) close(mouse_fds[i]); mouse_fds[i]=-1; }
    kbd_open=0; mouse_open=0;
}

/* ── Minimal JSON parser — extracts path+enabled from devices.json ───────── */
static void parse_config(void) {
    kbd_count=0; mouse_count=0;
    FILE*f=fopen(CONFIG_FILE,"r"); if(!f) return;
    char buf[4096]; size_t n=fread(buf,1,sizeof(buf)-1,f); buf[n]=0; fclose(f);

    /* Find each section and extract entries */
    DevEntry*arr=NULL; int*cnt=NULL;
    char*p=buf;
    while(*p) {
        if(strncmp(p,"\"keyboards\"",11)==0){arr=kbd_devs;cnt=&kbd_count;}
        else if(strncmp(p,"\"mice\"",6)==0){arr=mouse_devs;cnt=&mouse_count;}
        else if(arr && strncmp(p,"\"path\"",6)==0){
            char*s=strchr(p,':'); if(!s){p++;continue;}
            s=strchr(s,'"'); if(!s){p++;continue;} s++;
            char*e=strchr(s,'"'); if(!e){p++;continue;}
            if(*cnt<MAX_DEVICES){
                int j=0;
                for(char*c=s;c<e&&j<63;c++){
                    if(*c=='\\'&&*(c+1)=='/'){c++;arr[*cnt].path[j++]='/';}
                    else arr[*cnt].path[j++]=*c;
                }
                arr[*cnt].path[j]=0;
                arr[*cnt].enabled=1;
            }
        }
        else if(arr && strncmp(p,"\"enabled\"",9)==0){
            char*s=strchr(p,':'); if(!s){p++;continue;}
            while(*s==':'||*s==' ') s++;
            arr[*cnt].enabled=(strncmp(s,"true",4)==0)?1:0;
        }
        else if(*p=='}' && arr) { /* end of object, advance count */
            if(arr[*cnt].path[0]) (*cnt)++;
            if(*cnt>=MAX_DEVICES) arr=NULL;
        }
        p++;
    }
}

/* ── Open input devices from config ─────────────────────────────────────── */
static void open_input_fds(void) {
    close_input_fds();
    for(int i=0;i<kbd_count;i++){
        kbd_fds[kbd_open] = kbd_devs[i].enabled ? open(kbd_devs[i].path,O_RDONLY|O_NONBLOCK) : -1;
        kbd_open++;
    }
    for(int i=0;i<mouse_count;i++){
        mouse_fds[mouse_open] = mouse_devs[i].enabled ? open(mouse_devs[i].path,O_RDONLY|O_NONBLOCK) : -1;
        mouse_open++;
    }
    /* No fallback — if no devices configured, mapper waits with nothing open */
}

/* ── Process a single input event ───────────────────────────────────────── */
static void process_event(struct input_event*ev) {
    if(ev->type==EV_KEY&&(ev->code==KEY_LCTRL||ev->code==KEY_RCTRL)){
        if(ev->value==1&&ctrl_gate){
            grabbed=!grabbed; grab_all(grabbed);
            printf("[mapper] keymap %s\n",grabbed?"ON":"OFF"); fflush(stdout);
            if(!grabbed){
                sw_w=sw_a=sw_s=sw_d=0; update_steer();
                lclick_held=0; slot_up(SLOT_LCLICK);
                rclick_held=0; slot_up(SLOT_RCLICK);
                slot_up(SLOT_FWD);
            }
        }
        return;
    }
    if(!grabbed) return;
    if(ev->type!=EV_KEY||ev->value==2) return;
    int dn=ev->value;

    if(ev->code==272){if(dn){lclick_held=1;async_click(SLOT_LCLICK,40,0.9054f,0.5703f,&lclick_held);}else{lclick_held=0;slot_up(SLOT_LCLICK);}return;}
    if(ev->code==273){if(dn){rclick_held=1;async_click(SLOT_RCLICK,41,0.8421f,0.6968f,&rclick_held);}else{rclick_held=0;slot_up(SLOT_RCLICK);}return;}
    if(ev->code==276){if(dn)slot_down(SLOT_FWD,42,PX(0.8563f,0.406f),PY(0.8563f,0.406f));else slot_up(SLOT_FWD);return;}
    if(ev->code==275){if(dn)slot_down(SLOT_FWD,42,PX(0.803f,0.5229f),PY(0.803f,0.5229f));else slot_up(SLOT_FWD);return;}

    if(ev->code==KEY_W){sw_w=dn;update_steer();return;}
    if(ev->code==KEY_A){sw_a=dn;update_steer();return;}
    if(ev->code==KEY_S){sw_s=dn;update_steer();return;}
    if(ev->code==KEY_D){sw_d=dn;update_steer();return;}

    if(!dn) return;
    if(ev->code==KEY_E){async_drag(0.69f,0.88f,1.20f,0.88f,150);return;}
    if(ev->code==KEY_Q){async_drag(0.69f,0.88f,0.00f,0.88f,150);return;}
    if(ev->code==KEY_X){async_drag(0.69f,0.88f,0.69f,1.40f,150);return;}
    if(ev->code==KEY_2){async_drag(0.69f,0.88f,0.69f,0.55f,150);return;}
    if(ev->code==KEY_5){
        async_tap(SLOT_LCLICK,50,0.8563f,0.206f,80000);
        usleep(110000);
        async_tap(SLOT_RCLICK,51,0.7863f,0.206f,80000);
    }
}

int main(void) {
    signal(SIGINT,sig); signal(SIGTERM,sig);
    signal(SIGUSR1,sig_usr1); signal(SIGUSR2,sig_usr2);

    tfd=open(TOUCH_DEV,O_WRONLY);
    if(tfd<0){perror(TOUCH_DEV);return 1;}

    FILE*pf=fopen(PID_FILE,"w");
    if(pf){fprintf(pf,"%d\n",getpid());fclose(pf);}

    logf = fopen(LOG_FILE, "w");

    parse_config();
    open_input_fds();

    mlog("mapper started pid=%d", getpid());
    for(int i=0;i<kbd_open;i++)   mlog("kbd[%d] path=%s fd=%d", i, kbd_devs[i].path, kbd_fds[i]);
    for(int i=0;i<mouse_open;i++) mlog("mouse[%d] path=%s fd=%d", i, mouse_devs[i].path, mouse_fds[i]);

    /* Write status file for app to poll */
    FILE*sf=fopen("/root/mapper-test/mapper.status","w");
    if(sf){fprintf(sf,"running\nkbd=%d\nmouse=%d\n",kbd_open,mouse_open);fclose(sf);}

    printf("Ready. Press Ctrl to toggle keymap mode.\n"); fflush(stdout);

    struct pollfd pfds[MAX_DEVICES*2];
    struct input_event ev;

    while(running){
        if(reload_config){
            reload_config=0;
            int was_grabbed=grabbed;
            if(was_grabbed) grab_all(0);
            open_input_fds(); /* re-parses config internally */
            parse_config(); open_input_fds();
            for(int i=0;i<kbd_open;i++)   mlog("reload kbd[%d] path=%s fd=%d", i, kbd_devs[i].path, kbd_fds[i]);
            for(int i=0;i<mouse_open;i++) mlog("reload mouse[%d] path=%s fd=%d", i, mouse_devs[i].path, mouse_fds[i]);
            if(was_grabbed) grab_all(1);
            mlog("config reloaded");
            printf("[mapper] config reloaded\n"); fflush(stdout);
        }

        int nfds=0;
        for(int i=0;i<kbd_open;i++)   if(kbd_fds[i]>=0)  {pfds[nfds].fd=kbd_fds[i];  pfds[nfds].events=POLLIN;nfds++;}
        for(int i=0;i<mouse_open;i++) if(mouse_fds[i]>=0){pfds[nfds].fd=mouse_fds[i];pfds[nfds].events=POLLIN;nfds++;}

        if(nfds==0){usleep(100000);continue;}
        if(poll(pfds,nfds,500)<=0) continue;

        for(int i=0;i<nfds;i++){
            if(!(pfds[i].revents&POLLIN)) continue;
            while(read(pfds[i].fd,&ev,sizeof(ev))==sizeof(ev))
                process_event(&ev);
        }
    }

    if(grabbed){sw_w=sw_a=sw_s=sw_d=0;update_steer();lclick_held=0;slot_up(SLOT_LCLICK);rclick_held=0;slot_up(SLOT_RCLICK);slot_up(SLOT_FWD);grab_all(0);}
    close_input_fds();
    close(tfd);
    remove(PID_FILE);
    printf("Quit.\n");
    return 0;
}
