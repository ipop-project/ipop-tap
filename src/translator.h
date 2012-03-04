
#ifndef _TRANSLATOR_H_
#define _TRANSLATOR_H_

int create_arp_response(unsigned char *buf);

int translate_headers(unsigned char *buf, const char *source, const char *dest,
    const char *mac, ssize_t len);

int translate_packet(unsigned char *buf, const char *source, const char *dest,
    ssize_t len);

#endif
