#ifndef _PIU_TEST_MODULE_LOSS_TEST_H
#define _PIU_TEST_MODULE_LOSS_TEST_H

void set_loss_test_seed(unsigned int seed);

int udp_ping_test(int port, int num_packets, int packet_size);
int piu_ping_test(int port, int num_packets, int packet_size);

#endif
