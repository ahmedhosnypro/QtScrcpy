#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <string.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <log_file> <device_paths...>\n", argv[0]);
        fprintf(stderr, "Example: %s intercept.log /dev/input/event6 /dev/input/event9\n", argv[0]);
        return 1;
    }

    const char *log_path = argv[1];
    FILE *log_fp = fopen(log_path, "w");
    if (!log_fp) {
        fprintf(stderr, "Failed to open log file %s: %s\n", log_path, strerror(errno));
        return 1;
    }
    
    // We will support opening multiple devices, but for a simple test we'll use one at a time via loop,
    // wait, read() is blocking. For multiple devices we need select() or poll() or epoll().
    // Let's use select() for multiple devices!
    
    int num_devices = argc - 2;
    int *fds = malloc(sizeof(int) * num_devices);
    int max_fd = 0;
    
    fprintf(log_fp, "Starting interception test...\n");
    printf("Starting interception test...\n");

    for (int i = 0; i < num_devices; i++) {
        const char *dev_path = argv[2 + i];
        fds[i] = open(dev_path, O_RDONLY);
        if (fds[i] < 0) {
            fprintf(stderr, "Failed to open %s: %s\n", dev_path, strerror(errno));
            continue;
        }

        char name[256] = "Unknown";
        ioctl(fds[i], EVIOCGNAME(sizeof(name)), name);
        printf("Opened: %s (%s)\n", dev_path, name);
        fprintf(log_fp, "Opened: %s (%s)\n", dev_path, name);

        // Grab the device to intercept events exclusively
        if (ioctl(fds[i], EVIOCGRAB, 1) < 0) {
            fprintf(stderr, "Failed to grab device %s: %s\n", dev_path, strerror(errno));
            close(fds[i]);
            fds[i] = -1;
            continue;
        }
        printf(" -> Grabbed successfully. Inputs from this device are hidden from Android.\n");
        fprintf(log_fp, " -> Grabbed successfully.\n");
        
        if (fds[i] > max_fd) max_fd = fds[i];
    }
    fflush(log_fp);

    printf("\nListening for events... Press Ctrl+C to stop.\n");

    struct input_event ev;
    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        for (int i = 0; i < num_devices; i++) {
            if (fds[i] >= 0) {
                FD_SET(fds[i], &readfds);
            }
        }

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < num_devices; i++) {
            if (fds[i] >= 0 && FD_ISSET(fds[i], &readfds)) {
                ssize_t n = read(fds[i], &ev, sizeof(ev));
                if (n == sizeof(ev)) {
                    if (ev.type == EV_SYN) {
                        fprintf(log_fp, "Device %d Event: -------------- SYN_REPORT ------------\n", i);
                    } else if (ev.type == EV_KEY) {
                        fprintf(log_fp, "Device %d Event: type EV_KEY (1), code %d, value %d\n", i, ev.code, ev.value);
                    } else if (ev.type == EV_REL) {
                        fprintf(log_fp, "Device %d Event: type EV_REL (2), code %d, value %d\n", i, ev.code, ev.value);
                    } else if (ev.type == EV_ABS) {
                        fprintf(log_fp, "Device %d Event: type EV_ABS (3), code %d, value %d\n", i, ev.code, ev.value);
                    }
                    fflush(log_fp);
                }
            }
        }
    }

    // Release grab before closing
    for (int i = 0; i < num_devices; i++) {
        if (fds[i] >= 0) {
            ioctl(fds[i], EVIOCGRAB, 0);
            close(fds[i]);
        }
    }
    
    fclose(log_fp);
    free(fds);
    return 0;
}