
#ifndef _DTLS_H_
#define _DTLS_H_

int create_udp_socket(uint16_t port);

int init_dtls(thread_opts_t *opts);

int start_dtls_client(void *data);

int svpn_dtls_send(const unsigned char *buf, int len);

int svpn_dtls_process(const unsigned char *buf, int len);

#endif

