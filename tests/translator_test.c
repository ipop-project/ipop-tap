
#include <stdio.h>

#include <translator.h>
#include <minunit.h>

static char * test(unsigned char first, unsigned char second)
{
    printf("%x == %x\n", first, second);
    mu_assert("MISMATH", first == second);
    return "MATCH";
}

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

    unsigned char pre_ip[2];
    unsigned char pre_tcp[2];

    pre_ip[0] = nbuf[24];
    pre_ip[1] = nbuf[25];

    pre_tcp[0] = nbuf[50];
    pre_tcp[1] = nbuf[51];

    translate_headers(buf, source, dest, mac, len);

    printf("%s\n", test(pre_ip[0], nbuf[24]));
    printf("%s\n", test(pre_ip[1], nbuf[25]));
    printf("%s\n", test(pre_tcp[0], nbuf[50]));
    printf("%s\n", test(pre_tcp[1], nbuf[51]));
}

