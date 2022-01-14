#ifndef _PIU_INTERNAL_PIUPACKET_H
#define _PIU_INTERNAL_PIUPACKET_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>

#define PKT_MAX_BYTES 32768
#define PKT_HEADER_BYTES 9

#define PIU_PKT_HELLO_ID 0x7fffffff

enum { PIU_PKT_DATA, PIU_PKT_ACK, PIU_PKT_HELLO };

// Header (9) = ID (4) + Type (1) + Length (4)
typedef struct PIUPacket {
    // Packet structure
    int id;
    uint8_t type;
    int payload_len;
    char* payload;

    // Data to send
    uint32_t size;
    char* data;

    // Local-only members
    bool was_ack; // Only for PIU_PKT_DATA
} PIUPacket;

void piu_packet_init(PIUPacket* pkt, int id, uint8_t type, const void* payload, uint32_t payload_len);
bool piu_packet_parse(PIUPacket* pkt, void* data, uint32_t size);
void piu_packet_copy(PIUPacket* dst, const PIUPacket* src);
void piu_packet_free(PIUPacket *pkt);

inline static char* piu_packet_type2str(int type) {
    switch(type) {
    case PIU_PKT_HELLO:
        return "PIU_PKT_HELLO";
    case PIU_PKT_DATA:
        return "PIU_PKT_DATA";
    case PIU_PKT_ACK:
        return "PIU_PKT_ACK";
    default:
        return "PIU_PKT_UNKNOWN";
    }
}

// #include <stdio.h>
static int piu_packet_sendto(int fd, const PIUPacket* pkt, const struct sockaddr *addr, socklen_t addr_len) {
    // printf("Sending to %d an %s packet\n", fd, piu_packet_type2str(pkt->type));
    return sendto(fd, pkt->data, pkt->size, MSG_NOSIGNAL, addr, addr_len);
}

#endif
