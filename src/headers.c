
#include <string.h>

#include <svpn.h>
#include <headers.h>

int
get_headers(const unsigned char *buf, char *source_id, char *dest_id)
{
    memcpy(source_id, buf, ID_SIZE);
    memcpy(dest_id, buf + ID_SIZE, ID_SIZE);
    return 0;
}

int
set_headers(unsigned char *buf, const char *source_id, const char *dest_id)
{
    // some weird handling goes on here to ensure we get a null terminated
    // string out
    memcpy(buf, source_id, ID_SIZE-1);
    buf[ID_SIZE-1] = '\0';
    memcpy(buf + ID_SIZE, dest_id, ID_SIZE-1);
    buf[ID_SIZE*2-1] = '\0';
    return 0;
}
