
#include <string.h>
#include <svpn.h>

int
get_headers(const unsigned char *buf, char *source_id, char *dest_id, 
    unsigned char *iv)
{
    memcpy(source_id, buf, ID_SIZE);
    memcpy(dest_id, buf + ID_SIZE, ID_SIZE);
    memcpy(iv, buf + 2 * ID_SIZE, KEY_SIZE);
    return 0;
}

int
set_headers(unsigned char *buf, const char *source_id, const char *dest_id, 
    const unsigned char *iv)
{
    memcpy(buf, source_id, ID_SIZE);
    memcpy(buf + ID_SIZE, dest_id, ID_SIZE);
    memcpy(buf + 2 * ID_SIZE, iv, KEY_SIZE);
    return 0;
}

