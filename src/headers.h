
#ifndef _HEADERS_H_
#define _HEADERS_H_

#ifdef __cplusplus
extern "C" {
#endif

int get_headers(unsigned const char *buf, char *source_id, char *dest_id);

int set_headers(unsigned char *buf, const char *source_id, const char *dest_id);

#ifdef __cplusplus
}
#endif

#endif

