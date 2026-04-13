#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#define TOUCH_DEV    "/dev/input/event3"
#define SCREEN_X_MAX 1079
#define SCREEN_Y_MAX 2399

static int ufd;

static void emit(int type, int code, int val) {
    struct input_event ev = {.type=type, .code=code, .value=val};
    ssize_t r = write(ufd, &ev, sizeof(ev)); (void)r;
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }

static void tap(int x, int y) {
    emit(EV_ABS, ABS_MT_SLOT, 0);
    emit(EV_ABS, ABS_MT_TRACKING_ID, 1);
    emit(EV_ABS, ABS_MT_POSITION_X, x);
    emit(EV_ABS, ABS_MT_POSITION_Y, y);
    emit(EV_KEY, BTN_TOUCH, 1);
    syn();
    usleep(80000);
    emit(EV_ABS, ABS_MT_SLOT, 0);
    emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
    emit(EV_KEY, BTN_TOUCH, 0);
    syn();
}

int main(void) {
    ufd = open(TOUCH_DEV, O_WRONLY);
    if (ufd < 0) { perror(TOUCH_DEV); return 1; }

    int points[][2] = {
        {SCREEN_X_MAX/2,   SCREEN_Y_MAX/2},
        {SCREEN_X_MAX/4,   SCREEN_Y_MAX/4},
        {SCREEN_X_MAX*3/4, SCREEN_Y_MAX/4},
        {SCREEN_X_MAX/4,   SCREEN_Y_MAX*3/4},
        {SCREEN_X_MAX*3/4, SCREEN_Y_MAX*3/4},
    };
    for (int i = 0; i < 5; i++) {
        printf("Tap %d: x=%d y=%d\n", i+1, points[i][0], points[i][1]);
        fflush(stdout);
        tap(points[i][0], points[i][1]);
        usleep(500000);
    }
    printf("Done.\n");
    close(ufd);
    return 0;
}
