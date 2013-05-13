
#ifndef _TRANSLATOR_H_
#define _TRANSLATOR_H_

#ifdef __cplusplus
extern "C" {
#endif

int translate_mac(unsigned char *buf, const char *mac);

int translate_headers(unsigned char *buf, const char *source, const char *dest,
                      ssize_t len);

int translate_packet(unsigned char *buf, const char *source, const char *dest,
                     ssize_t len);

#ifdef __cplusplus
}
#endif

#endif
