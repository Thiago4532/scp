#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "piu/PIUSocket.h"

#include "modules/loss_test.h"

#include <sys/socket.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <sys/epoll.h>

static char* bTs(bool b) {
    return b ? "True" : "False";
}

void* func(void* ptr) {
    PIUSocket* c = piu_connect("127.0.0.1", 8888);

    char* msg = "thiago mota";
    piu_send(c, msg, strlen(msg));

    // char buf[8192];
    // int r = piu_recv(c, buf, sizeof(buf) - 1);
    // if (r == -1)
    //     printf("Recv vazio!\n");
    // else {
    //     buf[r] = '\0';
    //     printf("Mensagem: %s\n", buf);
    // }

    sleep(1);
    piu_close_socket(c);
    return NULL;
}

int main() {
    return piu_ping_test(8888, 100, 10);
    piu_main_loop();

    PIUServer* srv = piu_bind(8888);

    pthread_t thr;
    pthread_create(&thr, NULL, func, NULL);

    PIUSocket* s = piu_accept(srv);

    printf("Connection %s:%hu\n", piu_socket_addr(s), piu_socket_port(s));

    // char* msg = "italo pedro";
    // printf("Enviou: %s\n", bTs(piu_send(s, msg, strlen(msg))));

    char buf[8192];
    int r = piu_recv(s, buf, sizeof(buf) - 1);
    if (r == -1)
        printf("Recv vazio!\n");
    else {
        buf[r] = '\0';
        printf("Mensagem: %s\n", buf);
    }

    pthread_join(thr, NULL);

    piu_close_server(srv);
    piu_close_socket(s);

    piu_stop_loop();
    return 0;
}
