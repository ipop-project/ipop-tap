#include <stdio.h>
#include <arpa/inet.h>

#define MCAST_MIN 0xe0
#define MCAST_MAX 0xef
#define DEST_IDX 30
#define SOURCE_IDX 26

int
create_arp_response(char *buf, char *mac)
{
    int i;
    if (buf[40] == 0 && buf[41] == 2) {
        return -1;
    }

    for (i = 0; i < 6; i++) {
        buf[i] = buf[6 + i];
        buf[6 + i] = 0xFF;
    }

    buf[21] = 0x02;

    for (i = 0; i < 10; i++) {
        char tmp = buf[32 + i];
        buf[32 + i] = buf[22 + i];
        if (i < 6) {
            buf[22 + i] = 0xFF;
        }
        else {
            buf[22 + i] = tmp;
        }
    }

    return 0;
}

static int
update_checksum(unsigned char *buf, const int start, const int idx, ssize_t len)
{
    int csum = 0;
    uint16_t first, second;

    buf[idx] = 0x00;
    buf[idx + 1] = 0x00;

    int i;
    if (len > 20) {
        for (i = 0; i < 8; i += 2) {
            first = (buf[26 + i] << 8) & 0xFF00;
            second = buf[27 + i] & 0xFF;
            csum += first + second;
        }
        len -= 34;
        csum += buf[23] + len;
    }

    for (i = 0; i < len; i += 2) {
        first = (buf[start + i] << 8) & 0xFF00;
        second = ((len == i + 1) ? 0 : buf[start + i + 1]) & 0xFF;
        csum += first + second;
    }

    while (csum >> 16) {
        csum = (csum & 0xFFFF) + (csum >> 16);
    }

    csum = ~csum;

    buf[idx] = ((csum >> 8) & 0xFF);
    buf[idx + 1] = (csum & 0xFF);

    return 0;
}

int
translate_headers(char *buf, const char *source, const char *dest, 
    const char *mac, ssize_t len)
{
    int i;
    int mcast = buf[DEST_IDX] >= MCAST_MIN && buf[DEST_IDX] <= MCAST_MAX;

    for (i = 0; i < 6; i++) {
        buf[i] = mac[i];
    }

    for (; i < 12; i++) {
        buf[i] = 0xFF;
    }

    for (i = 0; i < 4; i++) {
        buf[SOURCE_IDX+i] = source[i];

        if (!mcast) {
            buf[DEST_IDX+i] = dest[i];
        }
    }

    update_checksum(buf, 14, 24, 20);

    if (buf[23] == 0x06) {
        update_checksum(buf, 34, 50, len);
    }
    else if (buf[23] == 0x11) {
        // checksum disabled for UDP
        buf[40] = 0x00;
        buf[41] = 0x00;
    }
    return 0;
}


