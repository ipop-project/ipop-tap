
#ifndef _SVPN_H_
#define _SVPN_H_

#define MTU 1300
#define BUFLEN 2048
#define BUF_OFFSET 80
#define ID_SIZE 20
#define KEY_SIZE 32
#define ADDR_SIZE 32
#define TABLE_SIZE 10

typedef struct thread_opts {
    int sock4;
    int sock6;
    int tap;
    char mac[6];
    char *local_ip;
    char dtls_ip[16];
    int dtls_port;
    int dtls;
} thread_opts_t;

#endif
