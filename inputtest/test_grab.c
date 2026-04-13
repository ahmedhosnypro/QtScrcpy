#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <linux/input.h>
#include <sys/ioctl.h>

static const char *devs[] = {
    "/dev/input/event6",
    "/dev/input/event7",
    "/dev/input/event8",
    "/dev/input/event9",
    "/dev/input/event10",
};
static const char *names[] = {"event6","event7","event8","event9","event10"};
#define NDEVS 5
#define KEY_LCTRL 29
#define KEY_RCTRL 97

static int fds[NDEVS];
static volatile int running = 1;
static void sig(int x){(void)x;running=0;}

static void set_grab(int on) {
    for (int i=0;i<NDEVS;i++) if(fds[i]>=0) ioctl(fds[i],EVIOCGRAB,on);
    printf("--- grab %s ---\n", on?"ON":"OFF"); fflush(stdout);
}

int main(void) {
    signal(SIGINT, sig);
    for (int i=0;i<NDEVS;i++) {
        fds[i]=open(devs[i],O_RDONLY|O_NONBLOCK);
        if (fds[i]<0) printf("skip %s\n",devs[i]);
    }
    printf("Press Ctrl to toggle grab. Then press back button.\n");
    fflush(stdout);

    struct pollfd pfds[NDEVS];
    for (int i=0;i<NDEVS;i++){pfds[i].fd=fds[i];pfds[i].events=POLLIN;}

    struct input_event ev;
    int grabbed=0;
    while (running) {
        if (poll(pfds,NDEVS,5000)<=0) continue;
        for (int i=0;i<NDEVS;i++) {
            if (!(pfds[i].revents&POLLIN)) continue;
            while (read(fds[i],&ev,sizeof(ev))==sizeof(ev)) {
                if (ev.type==EV_KEY&&(ev.code==KEY_LCTRL||ev.code==KEY_RCTRL)&&ev.value==1) {
                    grabbed=!grabbed; set_grab(grabbed); continue;
                }
                if (!grabbed) continue;
                if (ev.type==EV_KEY&&ev.value!=2)
                    printf("[%s] code=%u %s\n",names[i],ev.code,ev.value?"DOWN":"UP");
            }
        }
        fflush(stdout);
    }
    set_grab(0);
    for (int i=0;i<NDEVS;i++) if(fds[i]>=0) close(fds[i]);
    return 0;
}
