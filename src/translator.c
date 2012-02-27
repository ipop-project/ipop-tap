
#include <translator.h>
#include <string.h>

int
create_arp_response(char *buf)
{
    uint16_t *nbuf = (uint16_t *)buf;
    int i;

    if (buf[40] == 0 && buf[41] == 2) {
        return -1;
    }

    for (i = 0; i < 3; i++) {
        nbuf[i] = nbuf[3 + i];
        nbuf[3 + i] = 0xFFFF;
    }

    buf[21] = 0x02;

    for (i = 0; i < 5; i++) {
        uint16_t tmp = nbuf[16 + i];
        nbuf[16 + i] = nbuf[11 + i];
        if (i < 3) {
            nbuf[11 + i] = 0xFFFF;
        }
        else {
            nbuf[11 + i] = tmp;
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
#ifndef SVPN_TEST
    memcpy(buf, mac, 6);
    memset(buf + 6, 0xFF, 6);
    memcpy(buf + 26, source, 4);

    unsigned char iprange = (unsigned char) buf[30];
    if (iprange < 224 || iprange > 239) {
        memcpy(buf + 30, dest, 4);
    }
#endif

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

