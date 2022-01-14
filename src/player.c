#include <SDL2/SDL_video.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <string.h>
#include "stb_image_write.h"
#include <time.h>
#include <byteswap.h>
#include <libavcodec/avcodec.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <libavutil/opt.h>
#include <stdbool.h>
#include <libavutil/pixdesc.h>
#include "piu/PIUSocket.h"

#define WIDTH 1920
#define HEIGHT 1080
#define FPS 60
#define NANOSECS_PER_FRAME 16666667
#define DECODER_NAME "h264"
#define FIFO "./fifo"

#define INBUF_SIZE 32768
uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
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

static void measure(Bool end) {
    static long long m = 0;
    if (!end) {
        m = monotonic_clock();
    } else {
        long long t = monotonic_clock() - m;

        fprintf(stderr, "Clock: %lldms\n", t);
    }
}

static void measure_cpu(Bool end) {
    static clock_t m = 0;
    if (!end) {
        m = clock();
    } else {
        clock_t t = clock() - m;
        double time_taken = ((double)t) / CLOCKS_PER_SEC;

        fprintf(stderr, "Clock: %lf\n", time_taken);
    }
}

static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt, SDL_Texture* texture) {
    int ret;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            die("Error during decoding\n");
        }

        measure_t m;
        m.pts = frame->coded_picture_number;
        m.type = MEASURE_DECODER;

        if (fd != -1)
            write(fd, &m, sizeof m);
        int size[] = {frame->width * frame->height, (frame->width * frame->height)/4, (frame->width * frame->height)/4};

        // printf("%s\n", av_get_pix_fmt_name(frame->format));
     
        uint8_t* pixels;
        int pitch;

        if (SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch) < 0) {
            die("Failed to lock texture: %s\n");
        }

        uint8_t* ptr = pixels;

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < size[i]; j++) {
                *ptr++ = frame->data[i][j];
            }
        }

        SDL_UnlockTexture(texture);
    }
}

static void stop_loop() {
    piu_stop_loop();
}

int main(int argc, char* argv[]) {
    fd = -1;
    char* ip = "127.0.0.1";

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--measure") == 0 || strcmp(argv[1], "-m") == 0))
            fd = open(FIFO, O_WRONLY);
        else
            ip = argv[i];
    }

    piu_main_loop();
    atexit(stop_loop);

    PIUSocket *skt = piu_connect(ip, 4532);
    if (!skt) {
        die("failed to connect!\n");
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        die("nao inicializou o sdl!\n");
    }
    atexit(SDL_Quit);

    if (!SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "1" )) {
        fprintf(stderr, "Warning: Linear texture filtering not enabled!");
    }
 
    SDL_Window* window = SDL_CreateWindow( "LearnOpenGL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawColor( renderer, 0xFF, 0xFF, 0xFF, 0xFF );

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 1920, 1080);
    if (!texture) {
        die("nao carregou a texture!\n");
    }

    AVCodec *codec = avcodec_find_decoder_by_name(DECODER_NAME);
    if (!codec) {
        die("failed to find encoder %s\n", DECODER_NAME);
    }

    AVCodecParserContext *parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "parser not found\n");
        exit(1);
    }

    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) {
        die("failed to allocate video codec context\n");
    }
    c->pkt_timebase = (AVRational){1, FPS};

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        die("failed to allocate packet\n");
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        die("failed to allocate frame\n");
    }

    int err;
    err = avcodec_open2(c, codec, NULL);
    if (err < 0) {
        die("failed to open codec: %s\n", av_err2str(err));
    }

    SDL_Event e;

    struct timespec frame_timespec = {
        .tv_sec = 0,
        .tv_nsec = NANOSECS_PER_FRAME,
    };

    while(1) {
        bool quit = false;
        while (SDL_PollEvent( &e ) != 0) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }
        if (quit) break;

        int data_size = piu_recv(skt, inbuf, INBUF_SIZE);
        if (data_size <= 0) break;

        uint8_t *data = inbuf;
        while (data_size > 0) {
            int ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size, data,
                    data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

            if (ret < 0) {
                die("Error while parsing\n");
            }
            data += ret;
            data_size -= ret;

            if (pkt->size)
                decode(c, frame, pkt, texture);
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&c);
    av_parser_close(parser);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return 0;
}
