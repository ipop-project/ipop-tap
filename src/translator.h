
#ifndef _TRANSLATOR_H_
#define _TRANSLATOR_H_

#include <arpa/inet.h>

int create_arp_response(char *buf);

int translate_headers(char *buf, const char *source, const char *dest,
    const char *mac, ssize_t len);

#endif
