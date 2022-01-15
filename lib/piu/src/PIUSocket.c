#include "piu/PIUSocket.h"

#include <arpa/inet.h>
#include <malloc.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "internal/PIUBuff.h"
#include "internal/log.h"

#define SKT_MAX_PACKET 52768
#define MAX_EPOLL_EVENTS 1024
#define MAX_FILE_DESCRIPTORS 4096 // TODO: Check file descriptors

static int HELLO_TIMEOUT[] = {100, 100, 150, 150, 200, 200, 250, 250, 300, 300};

struct PIUSocket {
    int fd;

    struct sockaddr_in addr;
    socklen_t addr_len;

    PIUBuff buf_read, buf_write;
    int read_id, write_id;
    pthread_cond_t data_ready;

    PIUSocket *prev, *next;
};

struct PIUServer {
    int fd;

    // Uncaptured sockets
    PIUSocket *head, *tail;
    pthread_cond_t cond;
};

static pthread_t thread_id = 0;
static int epollfd = -1;

PIUSocket* socket_map[MAX_FILE_DESCRIPTORS];
PIUServer* server_map[MAX_FILE_DESCRIPTORS];
pthread_mutex_t fd_lock[MAX_FILE_DESCRIPTORS];

static bool addrin_same(struct sockaddr_in* a, struct sockaddr_in* b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

char* piu_socket_addr(PIUSocket* skt) {
    return inet_ntoa(skt->addr.sin_addr);
}

unsigned short piu_socket_port(PIUSocket* skt) {
    return ntohs(skt->addr.sin_port);
}

PIUSocket* piu_connect(char* addr, uint16_t port) {
    struct sockaddr_in server;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        LOGE("socket");
        return NULL;
    }

    memset(&server, 0, sizeof server);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(addr);
    server.sin_port = htons(port);

    PIUPacket pkt;
    piu_packet_init(&pkt, PIU_PKT_HELLO_ID, PIU_PKT_HELLO, NULL, 0);

    struct pollfd pollfd; // TODO: Use a better method to prevent hanging on
    pollfd.fd = fd;
    pollfd.events = POLLIN;

    char buf[256];

    bool connection_estabilished = false;
    for (int i = 0; i < sizeof(HELLO_TIMEOUT) / sizeof(*HELLO_TIMEOUT); i++) {
        int x = piu_packet_sendto(fd, &pkt, (struct sockaddr*)&server, sizeof(server));

        int n = poll(&pollfd, 1, HELLO_TIMEOUT[i]);
        if (n == -1) {
            LOGE("poll");
            break;
        }

        if (n == 0)
            continue;

        struct sockaddr_in recvaddr;
        socklen_t recvaddr_len = sizeof(recvaddr);

        int r = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr*)&recvaddr,
                         &recvaddr_len);
        if (r == -1) {
            LOGE("recvfrom");
            break;
        }

        if (!addrin_same(&recvaddr, &server)) {
            continue;
        }

        PIUPacket ack;
        if (!piu_packet_parse(&ack, buf, recvaddr_len))
            continue;

        if (ack.type == PIU_PKT_ACK && ack.id == pkt.id) {
            connection_estabilished = true;
            piu_packet_free(&ack);
            break;
        }
        piu_packet_free(&ack);
    }

    piu_packet_free(&pkt);

    printf("Estabilished: %s\n", connection_estabilished ? "Yes" : "No");

    if (!connection_estabilished) {
        close(fd);
        return NULL;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    PIUSocket* skt = malloc(sizeof(PIUSocket));
    socket_map[fd] = skt;
    pthread_mutex_init(&fd_lock[fd], NULL);

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOGE("epoll_ctl");
        socket_map[fd] = NULL;
        pthread_mutex_destroy(&fd_lock[fd]);
        free(skt);
        close(fd);
        return NULL;
    }

    memcpy(&skt->addr, &server, sizeof(server));
    skt->addr_len = sizeof(server);

    skt->fd = fd;
    piu_buff_init(&skt->buf_read);
    piu_buff_init(&skt->buf_write);
    skt->prev = skt->next = NULL;
    skt->read_id = skt->write_id = 0;
    pthread_cond_init(&skt->data_ready, NULL);

    return skt;
}

PIUServer* piu_bind(uint16_t port) {
    struct sockaddr_in server;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        LOGE("socket");
        return NULL;
    }

    memset(&server, 0, sizeof server);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        LOGE("bind");
        close(fd);
        return NULL;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    PIUServer* srv = malloc(sizeof(PIUServer));
    server_map[fd] = srv;
    pthread_mutex_init(&fd_lock[fd], NULL);

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        LOGE("epoll_ctl");
        server_map[fd] = NULL;
        pthread_mutex_destroy(&fd_lock[fd]);
        free(srv);
        close(fd);
        return NULL;
    }

    srv->fd = fd;
    srv->head = srv->tail = NULL;
    pthread_cond_init(&srv->cond, NULL);
    return srv;
}

inline static int skt_sendto(PIUSocket* skt, const PIUPacket* pkt) {
    return piu_packet_sendto(skt->fd, pkt, (struct sockaddr*)&skt->addr, skt->addr_len);
}

static bool handle_hello(int fd, struct sockaddr_in* addr, socklen_t addr_len) {
    pthread_mutex_lock(&fd_lock[fd]);
    PIUServer* srv = server_map[fd];

    if (srv == NULL) {
        pthread_mutex_unlock(&fd_lock[fd]);
        return false;
    }

    // Retransmission
    for (PIUSocket *p = socket_map[fd]; p != NULL; p = p->prev) {
        if (addrin_same(&p->addr, addr)) {
            pthread_mutex_unlock(&fd_lock[fd]);

            PIUPacket ack;
            piu_packet_init(&ack, PIU_PKT_HELLO_ID, PIU_PKT_ACK, NULL, 0);

            skt_sendto(p, &ack);

            piu_packet_free(&ack);
            return true;
        }
    }

    PIUSocket *skt = malloc(sizeof(PIUSocket));
    skt->fd = srv->fd;
    skt->addr_len = addr_len;
    skt->prev = skt->next = NULL;
    memcpy(&skt->addr, addr, skt->addr_len);

    if (srv->tail == NULL) {
        srv->tail = srv->head = skt;
    } else {
        srv->tail->next = skt;
        skt->prev = srv->tail;

        srv->tail = skt;
    }

    pthread_cond_signal(&srv->cond);
    pthread_mutex_unlock(&fd_lock[fd]);
    return true;
}

static bool handle_data(int fd, const PIUPacket* pkt, struct sockaddr_in* addr, socklen_t addr_len) {
    pthread_mutex_lock(&fd_lock[fd]);

    PIUSocket* skt = NULL;
    for (PIUSocket *p = socket_map[fd]; p != NULL; p = p->prev) {
        if (addrin_same(&p->addr, addr)) {
            skt = p;
            break;
        }
    }

    if (skt == NULL) {
        pthread_mutex_unlock(&fd_lock[fd]);
        return false;
    }

    PIUPacket ack;
    piu_packet_init(&ack, pkt->id, PIU_PKT_ACK, NULL, 0);
    skt_sendto(skt, &ack);
    piu_packet_free(&ack);

    piu_buff_lock(&skt->buf_read);
    PIUPacket* pkt_r = piu_buff_push_id(&skt->buf_read, pkt->id);
    if (pkt_r != NULL) {
        piu_packet_copy(pkt_r, pkt);
        
        if (pkt->id == skt->read_id)
            pthread_cond_signal(&skt->data_ready);
    }
    piu_buff_unlock(&skt->buf_read);

    pthread_mutex_unlock(&fd_lock[fd]);
    return true;
}

static bool handle_ack(int fd, const PIUPacket* pkt, struct sockaddr_in* addr, socklen_t addr_len) {
    if (pkt->id == PIU_PKT_HELLO_ID) // Hello ACK
        return true;

    pthread_mutex_lock(&fd_lock[fd]);

    PIUSocket* skt = NULL;
    for (PIUSocket *p = socket_map[fd]; p != NULL; p = p->prev) {
        if (addrin_same(&p->addr, addr)) {
            skt = p;
            break;
        }
    }

    if (skt == NULL) {
        pthread_mutex_unlock(&fd_lock[fd]);
        return false;
    }

    int id = pkt->id;

    piu_buff_lock(&skt->buf_write);

    PIUBuffNode* p = skt->buf_write.tail;
    if (!p || p->pkt.id > id) // Already ACK
        return true;

    while (p->pkt.id != id) {
        if (!p->pkt.was_ack) {
            if (p->next->pkt.id != id)
                skt_sendto(skt, &p->pkt);
        }

        p = p->next;
    }

    p->pkt.was_ack = true;

    // Clearing already acknowledge packets
    while (skt->buf_write.tail && skt->buf_write.tail->pkt.was_ack)
        piu_buff_pop(&skt->buf_write);

    piu_buff_unlock(&skt->buf_write);

    pthread_mutex_unlock(&fd_lock[fd]);
    return true;
}

PIUSocket* piu_accept(PIUServer* srv) {
    pthread_mutex_lock(&fd_lock[srv->fd]);
    PIUSocket* skt = srv->head;
    if (skt == NULL) {
        pthread_cond_wait(&srv->cond, &fd_lock[srv->fd]);
        skt = srv->head;
        if (skt == NULL) {
            pthread_mutex_unlock(&fd_lock[srv->fd]);
            return NULL;
        }
    }
    srv->head = srv->head->next;
    if (srv->head == NULL)
        srv->tail = NULL;
    pthread_mutex_unlock(&fd_lock[srv->fd]);

    piu_buff_init(&skt->buf_read);
    piu_buff_init(&skt->buf_write);
    skt->read_id = skt->write_id = 0;
    pthread_cond_init(&skt->data_ready, NULL);

    PIUPacket ack;
    piu_packet_init(&ack, PIU_PKT_HELLO_ID, PIU_PKT_ACK, NULL, 0);

    skt_sendto(skt, &ack);

    if (socket_map[skt->fd] == NULL) {
        skt->prev = skt->next = NULL;
        socket_map[skt->fd] = skt;
    } else {
        socket_map[skt->fd]->next = skt;
        skt->prev = socket_map[skt->fd];
        skt->next = NULL;
    }
    piu_packet_free(&ack);

    return skt;
}

int piu_recv(PIUSocket* skt, void* buf, uint32_t size) {
    piu_buff_lock(&skt->buf_read);
    if (!skt->buf_read.tail || skt->buf_read.tail->pkt.id > skt->read_id) {
        pthread_cond_wait(&skt->data_ready, &skt->buf_read.lock);
    }

    PIUPacket* pkt = &skt->buf_read.tail->pkt;

    if (size > pkt->payload_len)
        size = pkt->payload_len;

    memcpy(buf, pkt->payload, size);
    piu_buff_pop(&skt->buf_read);

    skt->read_id++;
    piu_buff_unlock(&skt->buf_read);
    return size;
}

bool piu_send(PIUSocket* skt, const void* buf, uint32_t size) {
    if (size > SKT_MAX_PACKET) {
        LOG("packet too big: %u", size);
        return false;
    }

    piu_buff_lock(&skt->buf_write);

    PIUPacket* pkt = piu_buff_push(&skt->buf_write);
    if (pkt == NULL) {
        LOG("failed to push packet!");
        return false;
    }

    piu_packet_init(pkt, skt->write_id++, PIU_PKT_DATA, buf, size);
    skt_sendto(skt, pkt);

    piu_buff_unlock(&skt->buf_write);

    return true;
}

static void* main_loop() {
    struct epoll_event events[MAX_EPOLL_EVENTS];
    static char buf[SKT_MAX_PACKET];

    for (;;) {
        int n = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, -1);
        if (n == -1) {
            LOGE("epoll_wait");
            return NULL;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            struct sockaddr_in srvinfo;
            socklen_t srvinfo_len = sizeof(srvinfo);

            int size = recvfrom(fd, buf, SKT_MAX_PACKET, 0,
                                (struct sockaddr*)&srvinfo, &srvinfo_len);
            if (size < 0) {
                LOGE("recvfrom");
                continue;
            }

            // If skt != NULL, then a connection already exists
            PIUPacket pkt;
            if (!piu_packet_parse(&pkt, buf, size)) {
                LOG("Failed to parse packet!");
                continue;
            }

            switch (pkt.type) {
            case PIU_PKT_HELLO:
                handle_hello(fd, &srvinfo, srvinfo_len);
                break;
            case PIU_PKT_DATA:
                handle_data(fd, &pkt, &srvinfo, srvinfo_len);
                break;
            case PIU_PKT_ACK:
                handle_ack(fd, &pkt, &srvinfo, srvinfo_len);
                break;
            default:
                LOG("invalid packet type");
                break;
            }

            piu_packet_free(&pkt);
        }
    }
    return NULL;
}

bool piu_main_loop() {
    if (thread_id != 0) {
        LOG("loop is already running");
        return false;
    }

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        LOGE("epoll_create1");
        return false;
    }

    int err;
    // err = pthread_mutex_init(&socket_map_lock, NULL);
    // if (err != 0) {
    //     LOG("socket_map_lock: pthread_mutex_init: %s", strerror(err));

    //     close(epollfd);
    //     epollfd = -1;
    //     return false;
    // }

    err = pthread_create(&thread_id, NULL, main_loop, NULL);
    if (err != 0) {
        LOG("pthread_create: %s", strerror(err));

        close(epollfd);
        epollfd = -1;
        thread_id = 0;
        return false;
    }

    return true;
}

bool piu_stop_loop() {
    if (thread_id == 0)
        return false;

    pthread_cancel(thread_id);
    pthread_join(thread_id, NULL);
    thread_id = 0;

    return true;
}

void piu_close_socket(PIUSocket* skt) { // TODO: Socket closing
    if (skt == NULL)
        return;

    pthread_mutex_lock(&fd_lock[skt->fd]);

    if (skt->next == NULL) {
        socket_map[skt->fd] = skt->prev;
        if (skt->prev) skt->prev->next = NULL;
    } else {
        if (skt->prev) skt->prev->next = skt->next;
        skt->next->prev = skt->prev;
    }

    int fd = skt->fd;
    piu_buff_free(&skt->buf_read);
    piu_buff_free(&skt->buf_write);
    pthread_cond_destroy(&skt->data_ready);
    free(skt);

    if (socket_map[fd] == NULL && server_map[fd] == NULL) {
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
            LOGE("epollfd");
        }

        close(fd);
    }

    pthread_mutex_unlock(&fd_lock[fd]);
}

void piu_close_server(PIUServer* srv) {
    if (srv == NULL)
        return;
    pthread_mutex_lock(&fd_lock[srv->fd]);

    int fd = srv->fd;
    PIUSocket* head = srv->head;

    pthread_cond_destroy(&srv->cond);
    free(srv);

    if (socket_map[fd] == NULL && server_map[fd] == NULL) {
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1) {
            LOGE("epollfd");
        }

        close(fd);
    }

    pthread_mutex_unlock(&fd_lock[fd]);

    while (head != NULL) {
        PIUSocket *tmp = head;
        head = head->next;
        free(tmp);
    }
}
