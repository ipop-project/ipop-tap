/*
 * ipop-tap
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// TODO - This limited table size breaks upnp translator when full
#define TABLE_SIZE 100

struct upnp_state {
    uint16_t c_port;
    int s_count;
    int s_ports[TABLE_SIZE];
    char server_ips[TABLE_SIZE][4];
};

struct upnp_state ustate = { 0, 0, { 0 }};

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

static int
is_upnp_endpoint(const char *source, uint16_t s_port)
{
    int i;
    for (i = 0; i < ustate.s_count; i++) {
        if (ustate.s_ports[i] == s_port &&
            memcmp(source, ustate.server_ips[i], 4) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
update_upnp(char *buf, const char *source, const char *dest, ssize_t len)
{
    char tmp[100] = {'\0'};
    int i, idx;
    uint16_t d_port = (buf[36] << 8 & 0xFF00) + (buf[37] & 0xFF);
    uint16_t s_port = (buf[34] << 8 & 0xFF00) + (buf[35] & 0xFF);

    if (source == NULL && buf[23] == 0x11 && d_port == 1900) {
        ustate.c_port = s_port;
    }
    else if (source != NULL && buf[23] == 0x11 && ustate.c_port == d_port) {
        i = 42;
        while (i < len && idx < TABLE_SIZE) {
            if (strncmp("http://172.", buf + i, 11) == 0) {
                idx = ustate.s_count++;
                memcpy(tmp, buf + i + 7, 12);
#ifndef WIN32
                inet_aton(tmp, (struct in_addr *)ustate.server_ips[idx]);
#else
                RtlIpv4AddressToString(tmp,
                    (struct in_addr *)ustate.server_ips[idx]);
#endif
                ustate.s_ports[idx] = atoi(buf + i + 20);
                sprintf(buf + i + 16, "%d", source[3]);
                buf[i + 19] = ':';
                break;
            }
            i++;
        }
    }
    else if (source != NULL && buf[23] == 0x06 &&
        is_upnp_endpoint(buf + 26, s_port)) {
        i = 66;
        while (i < len) {
            if (strncmp("http://172.", buf + i, 11) == 0) {
                sprintf(buf + i + 16, "%d", source[3]);
                buf[i + 19] = ':';
            }
            i++;
        }
    }
    return 0;
}

static int
update_sip(char *buf, const char *source, const char *dest,
    ssize_t len)
{
    char tmp;
    int i;

    uint16_t s_port = (buf[34] << 8 & 0xFF00) + (buf[35] & 0xFF);

    if (source != NULL && buf[23] == 0x11 && s_port == 5060) {
        i = 42;
        while (i < len) {
            if (strncmp("sip:", buf + i, 4) == 0) {
                while (i < len) {
                    i++;
                    if(strncmp("172.", buf + i, 4) == 0 &&
                       strncmp(".0.1", buf + i + 6, 4) == 0) {
                        if (strncmp("00", buf + i + 10, 2) == 0) {
                            tmp = buf[i + 12];
                            sprintf(buf + i + 9, "%d", source[3]);
                            buf[i + 12] = tmp;
                        }
                        else {
                            tmp = buf[i + 12];
                            sprintf(buf + i + 9, "%d", dest[3]);
                            buf[i + 12] = tmp;
                        }
                    }
                }
                break;
            }
            i++;
        }
    }

    return 0;
}

int
translate_mac(unsigned char *buf, const char *mac)
{
#ifndef SVPN_TEST
    memcpy(buf, mac, 6);
    memset(buf + 6, 0xFF, 6);
#endif
    return 0;
}

int
translate_headers(unsigned char *buf, const char *source, const char *dest,
                  ssize_t len)
{
#ifndef SVPN_TEST
    memcpy(buf + 26, source, 4);

    if ((buf[30] < 224 || buf[30] > 239) && buf[33] != 255) {
        // not multicast or broadcast
        memcpy(buf + 30, dest, 4);
    }
#endif

    update_checksum(buf, 14, 24, 20);

    if (buf[23] == 0x06) {
        update_checksum(buf, 34, 50, len);
    }
    else if (buf[23] == 0x11 && buf[21] == 0 && !(buf[20] & 0x01)) {
        // checksum disabled for UDP
        buf[40] = 0x00;
        buf[41] = 0x00;
    }
    return 0;
}

int
translate_packet(unsigned char *buf, const char *source, const char *dest,
                 ssize_t len)
{
    update_upnp((char *)buf, source, dest, len);
    update_sip((char*)buf, source, dest, len);
    return 0;
}
