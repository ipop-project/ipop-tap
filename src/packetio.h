
#ifndef _PACKETIO_H_
#define _PACKETIO_H_

#ifdef __cplusplus
extern "C" {
#endif

void *udp_send_thread(void *data);
void *udp_recv_thread(void *data);

#ifdef __cplusplus
}
#endif

#endif
