#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include <linux/input.h>

#define LOG_FILE "/root/QtScrcpy/inputtest/input.log"

// event6 = mouse buttons+movement, event7 = mouse extra keys, event9 = keyboard
#define DEV_MOUSE  "/dev/input/event6"
#define DEV_MOUSE2 "/dev/input/event7"
#define DEV_KBD    "/dev/input/event9"

static FILE *logf;

static void log_event(const char *dev, struct input_event *e) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    const char *type_str = "UNKNOWN";
    char detail[64] = "";

    switch (e->type) {
        case EV_KEY:
            type_str = "EV_KEY";
            snprintf(detail, sizeof(detail), "code=%u val=%d(%s)",
                e->code, e->value,
                e->value == 0 ? "UP" : e->value == 1 ? "DOWN" : "REPEAT");
            break;
        case EV_REL:
            type_str = "EV_REL";
            snprintf(detail, sizeof(detail), "code=%u(%s) val=%d",
                e->code,
                e->code == REL_X ? "X" : e->code == REL_Y ? "Y" :
                e->code == REL_WHEEL ? "WHEEL" : "?",
                e->value);
            break;
        case EV_ABS:
            type_str = "EV_ABS";
            snprintf(detail, sizeof(detail), "code=%u val=%d", e->code, e->value);
            break;
        case EV_SYN:
            return; // skip sync noise
        case EV_MSC:
            return; // skip misc (scancode)
    }

    fprintf(logf, "[%ld.%06ld] %s type=%s %s\n",
        ts.tv_sec, ts.tv_nsec / 1000, dev, type_str, detail);
    fflush(logf);
}

int main(void) {
    logf = fopen(LOG_FILE, "w");
    if (!logf) { perror("fopen log"); return 1; }

    int fds[3];
    const char *names[3] = {"mouse(event6)", "mouse2(event7)", "kbd(event9)"};
    const char *devs[3]  = {DEV_MOUSE, DEV_MOUSE2, DEV_KBD};

    for (int i = 0; i < 3; i++) {
        fds[i] = open(devs[i], O_RDONLY | O_NONBLOCK);
        if (fds[i] < 0) {
            fprintf(logf, "WARN: cannot open %s\n", devs[i]);
            fflush(logf);
        } else {
            fprintf(logf, "OK: opened %s\n", devs[i]);
            fflush(logf);
        }
    }

    fprintf(logf, "--- Listening for events (Ctrl+C to stop) ---\n");
    fflush(logf);

    struct pollfd pfds[3];
    for (int i = 0; i < 3; i++) {
        pfds[i].fd = fds[i];
        pfds[i].events = POLLIN;
    }

    struct input_event ev;
    while (1) {
        int ret = poll(pfds, 3, 5000);
        if (ret == 0) {
            fprintf(logf, "[heartbeat - no events in 5s]\n");
            fflush(logf);
            continue;
        }
        for (int i = 0; i < 3; i++) {
            if (pfds[i].revents & POLLIN) {
                while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev))
                    log_event(names[i], &ev);
            }
        }
    }

    fclose(logf);
    return 0;
}
