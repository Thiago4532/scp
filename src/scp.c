#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <X11/Xlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <errno.h>
#include <string.h>
#include "stb_image_write.h"
#include <time.h>
#include <byteswap.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include "piu/PIUSocket.h"

#define WIDTH 1920
#define HEIGHT 1080
#define FPS 60
#define NANOSECS_PER_FRAME 16666667
#define CODEC_NAME "h264_nvenc"
#define FIFO "./fifo"
int fd;

typedef struct {
    enum { MEASURE_ENCODER, MEASURE_DECODER } type;
    int pts;
} measure_t;

static void die(const char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(1);
}

static long long monotonic_clock() {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    return (long long)time.tv_sec * 1000ll + time.tv_nsec / 1000000;
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

static void stop_loop() {
    piu_stop_loop();
}

int main(int argc, char* argv[]) {
    piu_main_loop();
    atexit(stop_loop);

    PIUServer* srv = piu_bind(4532);
    if (srv == NULL) {
        die("failed to bind\n");
    }

    PIUSocket* skt = piu_accept(srv);
    if (skt == NULL) {
        die("failed to accept\n");
    }

    if (argc > 1 && (strcmp(argv[1], "--measure") == 0 || strcmp(argv[1], "-m") == 0))
        fd = open(FIFO, O_WRONLY);
    else
        fd = -1;

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

    // ffmpeg
    AVCodec *codec = avcodec_find_encoder_by_name(CODEC_NAME);
    if (!codec) {
        die("failed to find encoder %s\n", CODEC_NAME);
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) {
        die("failed to allocate video codec context\n");
    }

    AVPacket *pkt = av_packet_alloc();

    c->width = image->width;
    c->height = image->height;
    c->time_base = (AVRational){1, FPS};
    c->framerate = (AVRational){FPS, 1};

    c->pix_fmt = AV_PIX_FMT_RGB0;

    int err;
    err = av_opt_set_int(c->priv_data, "preset", 16, 0);
    if (err < 0) {
        die("failed to set option: %s\n", av_err2str(err));
    }

    // err = av_opt_set_int(c->priv_data, "tune", 4, 0);
    // if (err < 0) {
    //     die("failed to set option: %s\n", av_err2str(err));
    // }

    c->max_b_frames = 0;
    err = av_opt_set_int(c->priv_data, "qp", 18, 0);
    if (err < 0) {
        die("failed to set option: %s\n", av_err2str(err));
    } 

    err = avcodec_open2(c, codec, NULL);
    if (err < 0) {
        die("failed to open codec: %s\n", av_err2str(err));
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        die("failed to alloc frame");
    }

    frame->format = c->pix_fmt;
    frame->width = WIDTH;
    frame->height = HEIGHT;

    if (av_frame_get_buffer(frame, 0) < 0) {
        die("failed to allocate frame buffer");
    }

    XSync(dpy, False);
    int i = 0;
    do {
        struct timespec s, e, elapsed, rem;
        clock_gettime(CLOCK_MONOTONIC, &s);
        if (av_frame_make_writable(frame) < 0) {
            die("failed to make frame writable\n");
        }

        XShmGetImage(dpy, screen->root, image, 0, 0, AllPlanes);
        XSync(dpy, False);

        int size = 0;
        for (int i = 0; i < 4 * image->width * image->height; i+=4) {
            frame->data[0][size++] = image->data[i+2];
            frame->data[0][size++] = image->data[i+1];
            frame->data[0][size++] = image->data[i+0];
            frame->data[0][size++] = 0xff;
        }

        measure_t m;
        m.pts = i;
        m.type = MEASURE_ENCODER;

        frame->pts = i++;

        if (fd != -1)
            write(fd, &m, sizeof m);

        if (avcodec_send_frame(c, frame) < 0) {
            die("failed to send a frame for enconding\n");
        }

        int ret = 0;
        while (ret >= 0) {
            ret = avcodec_receive_packet(c, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0){
                die("error during enconding\n");
            }

            uint8_t* data = pkt->data;
            int size = pkt->size;

            while (size > 20000) {
                // write(STDOUT_FILENO, data, 30000);
                piu_send(skt, data, 20000);

                data += 20000;
                size -= 20000;
            }
            piu_send(skt, data, size);
            // write(STDOUT_FILENO, data, size);
        }

        clock_gettime(CLOCK_MONOTONIC, &e);

        diff_timespec(&elapsed, &e, &s);
        diff_timespec(&rem, &frame_timespec, &elapsed);

        nanosleep(&rem, NULL);
    } while(1);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    // free(data);
    XDestroyImage(image);
    shmdt(shminfo.shmaddr);
    XCloseDisplay(dpy);
    return 0;
}
