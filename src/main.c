#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <errno.h>
#include <string.h>
#include "stb_image_write.h"
#include <time.h>
#include <byteswap.h>

#define NANOSECS_PER_FRAME 16666667

static void die(const char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(1);
}

static void measure(Bool end) {
    static clock_t m = 0;
    if (!end) {
        m = clock();
    } else {
        clock_t t = clock() - m;
        double time_taken = ((double)t) / CLOCKS_PER_SEC;

        printf("Clock: %lf\n", time_taken);
    }
}

static void diff_timespec(struct timespec* res, const struct timespec *a, const struct timespec *b) {
    res->tv_sec = a->tv_sec - b->tv_sec;
    res->tv_nsec = a->tv_nsec - b->tv_nsec;

    if (res->tv_nsec < 0) {
        res->tv_nsec += 1000000000;
        res->tv_sec--;
    }
}

// Returns true if a < b
int comp_timespec(const struct timespec *a, const struct timespec *b) {
    if (a->tv_sec == b->tv_sec)
        return a->tv_nsec < b->tv_nsec;

    return a->tv_sec < b->tv_sec;
}

int main() {
    Display *dpy;
    Screen* screen;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        die("Cannot open display\n");
    }
    if (!XShmQueryExtension(dpy)) {
        XCloseDisplay(dpy) ;
        die("scp: the X server does not support the XSHM extension\n") ;
    }
    XShmSegmentInfo shminfo;

    screen = DefaultScreenOfDisplay(dpy);

    XImage* image = XShmCreateImage(dpy, XDefaultVisualOfScreen(screen), XDefaultDepthOfScreen(screen), ZPixmap,
            0, &shminfo, screen->width, screen->height);

    if (!image) {
        die("failed to create image");
    }

    shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height, IPC_CREAT | 0600);
    if (shminfo.shmid == -1) {
        die("shmget: %s\n", strerror(errno));
    }

    shminfo.shmaddr = image->data = shmat(shminfo.shmid, 0, 0);
    shminfo.readOnly = False;
    shmctl(shminfo.shmid, IPC_RMID, 0) ;

    if (!XShmAttach(dpy, &shminfo)) {
        die("failed to attach!\n");
    }

    struct timespec frame_timespec = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_FRAME,
    };

    char* data = malloc(image->width * image->height * 3);

    XSync(dpy, False);
    do {
        struct timespec s, e, elapsed, rem;
        clock_gettime(CLOCK_REALTIME, &s);

        XShmGetImage(dpy, screen->root, image, 0, 0, AllPlanes);
        XSync(dpy, False);

        int size = 0;
        for (int i = 0; i < 4 * image->width * image->height; i+=4) {
            data[size++] = image->data[i+2];
            data[size++] = image->data[i+1];
            data[size++] = image->data[i+0];
        }
        write(STDOUT_FILENO, data, size);

        clock_gettime(CLOCK_REALTIME, &e);

        diff_timespec(&elapsed, &e, &s);
        diff_timespec(&rem, &frame_timespec, &elapsed);

        nanosleep(&rem, NULL);

        clock_gettime(CLOCK_REALTIME, &e);

        diff_timespec(&elapsed, &e, &s);
    } while(1);

    free(data);
    XDestroyImage(image);
    shmdt(shminfo.shmaddr);
    XCloseDisplay(dpy);
    return 0;
}
