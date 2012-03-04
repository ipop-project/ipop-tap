
#ifndef _HEADERS_H_
#define _HEADERS_H_

int get_headers(unsigned const char *buf, char *source_id, char *dest_id, 
    unsigned char *iv);

int set_headers(unsigned char *buf, const char *source_id, const char *dest_id,
    const unsigned char *iv);

#endif

