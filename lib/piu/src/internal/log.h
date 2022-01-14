#ifndef _PIU_INTERNAL_LOG
#define _PIU_INTERNAL_LOG

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define LOG(fmt, ...) \
    fprintf(stderr, "%s: " fmt "\n", __func__, ##__VA_ARGS__)

#define LOGE(fmt, ...) \
    fprintf(stderr, "%s: " fmt ": %s\n", __func__, ##__VA_ARGS__, strerror(errno))

#endif
