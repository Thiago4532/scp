#include "PIUBuff.h"

#include "log.h"
#include <stdlib.h>

#include <malloc.h>

void piu_buff_init(PIUBuff *buf) {
    buf->head = buf->tail = NULL;
    pthread_mutex_init(&buf->lock, NULL);
}

PIUPacket *piu_buff_push(PIUBuff *buf) {
    PIUBuffNode *node = malloc(sizeof(PIUBuffNode));
    node->next = NULL;

    if (buf->head == NULL) {
        buf->head = buf->tail = node;
    } else {
        buf->head->next = node;
        buf->head = node;
    }

    memset(&node->pkt, 0, sizeof node->pkt);
    return &node->pkt;
}

PIUPacket *piu_buff_push_id(PIUBuff *buf, int id) {
    PIUBuffNode *node = malloc(sizeof(PIUBuffNode));
    node->next = NULL;

    if (buf->head == NULL) {
        buf->head = buf->tail = node;
    } else if (buf->tail->pkt.id == id || buf->head->pkt.id == id) {
        free(node);
        return NULL;
    } else if (buf->tail->pkt.id > id) {
        node->next = buf->tail;
        buf->tail = node;
    } else if (buf->head->pkt.id < id) {
        buf->head->next = node;
        buf->head = node;
    } else {
        PIUBuffNode* p = buf->tail;
        while (p->next && p->next->pkt.id < id)
            p = p->next;

        if (!p->next) {
            LOG("buff packet not ordered: %d", id);
            p = buf->tail;
            while (p != NULL) {
                fprintf(stderr, "%d ", p->pkt.id);
                p = p->next;
            }
            fprintf(stderr, "\n");
            abort();
        }

        if (p->next->pkt.id == id) {
            free(node);
            return NULL;
        }

        node->next = p->next;
        p->next = node;
    }

    memset(&node->pkt, 0, sizeof node->pkt);
    return &node->pkt;
}

void piu_buff_pop(PIUBuff *buf) {
    if (buf->tail == NULL)
        return;

    PIUBuffNode *node = buf->tail;
    buf->tail = buf->tail->next;
    if (buf->tail == NULL)
        buf->head = NULL;

    piu_packet_free(&node->pkt);
    free(node);
}

void piu_buff_free(PIUBuff *buf) {
    PIUBuffNode *p = buf->tail;
    while (p != NULL) {
        PIUBuffNode *tmp = p;
        p = p->next;

        piu_packet_free(&tmp->pkt);
        free(tmp);
    }
    pthread_mutex_destroy(&buf->lock);
}
