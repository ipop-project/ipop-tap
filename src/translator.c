#include <stdio.h>
#include <arpa/inet.h>

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
    int i;
    int mcast = ((unsigned char) buf[30]) >= 224 && 
                ((unsigned char) buf[26]) <= 239;

    uint16_t *nbuf = (uint16_t *) buf;
    uint16_t *nmac = (uint16_t *) mac;
    uint16_t *nsource = (uint16_t *) source;
    uint16_t *ndest = (uint16_t *) dest;

    for (i = 0; i < 3; i++) {
        nbuf[i] = nmac[i];
    }

    for (; i < 6; i++) {
        nbuf[i] = 0xFFFF;
    }

    for (i = 0; i < 2; i++) {
        nbuf[13+i] = nsource[i];

        if (!mcast) {
            nbuf[15+i] = ndest[i];
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

#if test
int main()
{
    char buf[] = { 0x44, 0xe4, 0xd9, 0x4f, 0x79, 0x46, 0x00, 0x23, 
                   0xae, 0x94, 0xf8, 0x57, 0x08, 0x00, 0x45, 0x00,
                   0x00, 0x34, 0xd7, 0xbb, 0x40, 0x00, 0x40, 0x06, 
                   0x5b, 0xaa, 0x0a, 0xf4, 0x12, 0x77, 0xad, 0xc2,
                   0x3c, 0x31, 0xd9, 0xa2, 0x00, 0x50, 0xb4, 0xe8, 
                   0xd0, 0xad, 0x98, 0xad, 0xee, 0xe5, 0x80, 0x10,
                   0x0d, 0xdd, 0x0f, 0xed, 0x00, 0x00, 0x01, 0x01, 
                   0x08, 0x0a, 0x1b, 0x33, 0x67, 0x0e, 0xad, 0xb6,
                   0x3a, 0x80};

    char source[] = {0};
    char dest[] = {0};
    char mac[] = {0};

    ssize_t len = 66;

    unsigned char* nbuf = (unsigned char*)buf;

    printf("ip checksum %x %x\n", nbuf[24], nbuf[25]);
    printf("tcp checksum %x %x\n", nbuf[50], nbuf[51]);

    translate_headers(buf, source, dest, mac, len);

    printf("ip checksum %x %x\n", nbuf[24], nbuf[25]);
    printf("tcp checksum %x %x\n", nbuf[50], nbuf[51]);
}
#endif

