#include "PIUPacket.h"

#include <malloc.h>
#include <string.h>
#include "arpa/inet.h"

#define PTR_U8(x) ((uint8_t*)(x))
#define PTR_U32(x) ((uint32_t*)(x))

void piu_packet_init(PIUPacket* pkt, int id, uint8_t type, const void* payload, uint32_t payload_len) {
    // Header
    pkt->id = id;
    pkt->type = type;
    pkt->payload_len = payload_len;

    pkt->size = PKT_HEADER_BYTES + payload_len;
    pkt->data = malloc(pkt->size);

    pkt->payload = pkt->data + PKT_HEADER_BYTES;

    *PTR_U32(pkt->data) = htonl(id);
    *PTR_U8(pkt->data + 4) = type;
    *PTR_U32(pkt->data + 5) = htonl(payload_len);

    if (payload_len > 0)
        memcpy(pkt->payload, payload, payload_len);
}

bool piu_packet_parse(PIUPacket* pkt, void* data, uint32_t size) {
    if (size < PKT_HEADER_BYTES)
        return false;

    pkt->data = malloc(size);
    pkt->size = size;
    memcpy(pkt->data, data, size);

    pkt->id = ntohl(*PTR_U32(pkt->data));
    pkt->type = *PTR_U8(pkt->data + 4);
    pkt->payload_len = ntohl(*PTR_U32(pkt->data + 5)); // TODO: Ensure payload_len is right

    pkt->payload = pkt->data + PKT_HEADER_BYTES;
    return true;
}

void piu_packet_copy(PIUPacket* dst, const PIUPacket* src) {
    dst->size = src->size;
    dst->id = src->id;
    dst->type = src->type;

    dst->data = malloc(dst->size);
    memcpy(dst->data, src->data, dst->size);

    dst->payload = dst->data + PKT_HEADER_BYTES;
    dst->payload_len = src->payload_len;

    dst->was_ack = src->was_ack;
}

void piu_packet_free(PIUPacket *pkt) {
    free(pkt->data);
}
