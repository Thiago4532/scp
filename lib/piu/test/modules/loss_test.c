#include "loss_test.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include "piu/PIUSocket.h"

#define DEFAULT_PORT 8888
#define DEFAULT_NUM_PACKETS 10
#define DEFAULT_PACKET_SIZE 2048

// This value should be less than 1 second
#define TIMEOUT_MS 100

#define PERROR(msg) \
    fprintf(stderr, "%s: %s: %s\n", __func__, msg, strerror(errno))

static bool seed_was_set = false;

void set_loss_test_seed(unsigned int seed) {
    srand(seed);
    seed_was_set = true;
}

int udp_ping_test(int port, int num_packets, int packet_size) {
    if (!seed_was_set)
        set_loss_test_seed(time(0));

    if (port == 0)
        port = DEFAULT_PORT;
    if (num_packets == 0)
        num_packets = DEFAULT_NUM_PACKETS;
    if (packet_size == 0)
        packet_size = DEFAULT_PACKET_SIZE;

    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    int clientfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientfd == -1) {
        PERROR("socket");
        return 1;
    }

    int serverfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverfd == -1) {
        PERROR("socket");
        close(clientfd);
        return 1;
    }    

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS * 1000;

    if (setsockopt(serverfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv,sizeof(tv)) < 0) {
        PERROR("setsockopt");
        close(clientfd);
        return 1;
    }

    if (bind(serverfd, (struct sockaddr*)&server, sizeof(server)) < 0) {
        PERROR("bind");
        close(serverfd);
        close(clientfd);
        return 1;
    }

    char* msg = malloc(packet_size);
    char* buf = malloc(packet_size);

    for(int j = 0; j < packet_size; j++)
        msg[j] = rand()%256;

    int count = 0;

    int ret = 0;
    for (int i = 0; i < num_packets; i++) {
        if (sendto(clientfd, msg, packet_size, 0, (struct sockaddr*)&server, sizeof(server)) < 0) {
            PERROR("sendto");

            ret = 1;
            goto stop;
        }
    }

    for (int i = 0; i < num_packets; i++) {
        memset(buf, 0, packet_size);


        int err = recvfrom(serverfd, buf, packet_size, 0, NULL, 0);
        if (err == 0)
            goto stop;

        if (err > 0) {
            printf("Packet %d: Received\n", i + 1);
            count++;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Packet %d: Packet was lost\n", i + 1);
        } else {
            PERROR("recvfrom");
            ret = 1;
            goto stop;
        }
    }

    printf("Packets received: %d/%d\n", count, num_packets);
    printf("Loss: %.2lf%%\n", 100.0*(num_packets - count)/num_packets);

stop:
    free(msg);
    free(buf);
    close(serverfd);
    close(clientfd);

    return ret;
}

static void* accept_connection(void* data) {
    int port = *(int*)data;

    PIUSocket* c = piu_connect("127.0.0.1", 8888);
    return c;
}

int piu_ping_test(int port, int num_packets, int packet_size) {
    if (!seed_was_set)
        set_loss_test_seed(time(0));

    if (port == 0)
        port = DEFAULT_PORT;
    if (num_packets == 0)
        num_packets = DEFAULT_NUM_PACKETS;
    if (packet_size == 0)
        packet_size = DEFAULT_PACKET_SIZE;

    piu_main_loop();

    PIUServer *aux = piu_bind(port);
    pthread_t thr;
    pthread_create(&thr, NULL, accept_connection, &port);

    PIUSocket* server = piu_accept(aux);

    PIUSocket* client = NULL;
    pthread_join(thr, (void**)&client);

    piu_close_server(aux);

    if (client == NULL || server == NULL) {
        fprintf(stderr, "Failed!\n");
        return 1;
    }

    char* msg = malloc(packet_size);
    char* buf = malloc(packet_size);

    for(int j = 0; j < packet_size; j++)
        msg[j] = rand()%256;

    int count = 0;

    int ret = 0;
    for (int i = 0; i < num_packets; i++) {
        if (!piu_send(client, msg, packet_size)) {
            PERROR("sendto");

            ret = 1;
            goto stop;
        }
    }

    for (int i = 0; i < num_packets; i++) {
        memset(buf, 0, packet_size);

        int err = piu_recv(server, buf, packet_size);
        if (err == 0)
            goto stop;

        if (err > 0) {
            printf("Packet %d: Received\n", i + 1);
            count++;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Packet %d: Packet was lost\n", i + 1);
        } else {
            PERROR("recvfrom");
            ret = 1;
            goto stop;
        }
    }

    printf("Packets received: %d/%d\n", count, num_packets);
    printf("Loss: %.2lf%%\n", 100.0*(num_packets - count)/num_packets);

stop:
    free(msg);
    free(buf);
    piu_close_socket(server);
    piu_close_socket(client);
    piu_stop_loop();

    return ret;
}
