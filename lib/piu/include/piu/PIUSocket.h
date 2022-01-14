#ifndef _PIU_PIUSOCKET_H
#define _PIU_PIUSOCKET_H

#include <stdint.h>
#include <stdbool.h>

struct PIUSocket;
typedef struct PIUSocket PIUSocket;

struct PIUServer;
typedef struct PIUServer PIUServer;

PIUSocket* piu_connect(char* addr, uint16_t port);
PIUServer* piu_bind(uint16_t port);

PIUSocket* piu_accept(PIUServer* srv);

char* piu_socket_addr(PIUSocket* skt);
uint16_t piu_socket_port(PIUSocket* skt);

int piu_recv(PIUSocket* skt, void* buf, uint32_t size);
bool piu_send(PIUSocket* skt, const void* buf, uint32_t size);

bool piu_main_loop();
bool piu_stop_loop();

void piu_close_socket(PIUSocket* skt);
void piu_close_server(PIUServer* srv);

#endif

// TODO: IPV6
// TODO: Error Handling 
