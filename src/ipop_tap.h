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

#ifndef _SVPN_H_
#define _SVPN_H_

#if defined(WIN32)
#include "win32_tap.h"
#endif

#define MTU 1280
#define BUFLEN 2048
#define BUF_OFFSET 40 // Gives room to store the headers
#define ID_SIZE 20
#define MAXBUF 1024

#define IPV6_ADDR_FILE "../ipv6_addr"

typedef struct thread_opts {
    int sock4;
    int sock6;
    int tap;
#if defined(WIN32)
    windows_tap* win32_tap;
#endif
    int translate;
    int switchmode;
    char mac[6];
    char my_ip4[4];
    const char *local_ip4;
    const char *local_ip6;
    int (*send_func)(const char *buf, size_t len);
    int (*recv_func)(char *buf, size_t len);
} thread_opts_t;

#endif
