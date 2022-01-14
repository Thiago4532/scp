#ifndef _PIU_INTERNAL_PIUBUFF_H
#define _PIU_INTERNAL_PIUBUFF_H

#include "PIUPacket.h"
#include <pthread.h>

typedef struct PIUBuffNode PIUBuffNode;
struct PIUBuffNode {
    PIUPacket pkt;

    PIUBuffNode *next;
};

typedef struct PIUBuff {
    PIUBuffNode *tail, *head;
    pthread_mutex_t lock;
} PIUBuff;

void piu_buff_init(PIUBuff *buf);
PIUPacket* piu_buff_push(PIUBuff *buf);
PIUPacket* piu_buff_push_id(PIUBuff *buf, int id);
void piu_buff_pop(PIUBuff *buf);

void piu_buff_free(PIUBuff *buf);

static inline int piu_buff_lock(PIUBuff *buf) {
    return pthread_mutex_lock(&buf->lock);
}

static inline int piu_buff_unlock(PIUBuff *buf) {
    return pthread_mutex_unlock(&buf->lock);
}

#endif
