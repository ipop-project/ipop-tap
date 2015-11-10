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

#ifndef _TRANSLATOR_H_
#define _TRANSLATOR_H_

#ifdef __cplusplus
extern "C" {
#endif

int translate_headers(unsigned char *buf, const char *source, const char *dest,
                      ssize_t len);

int translate_packet(unsigned char *buf, const char *source, const char *dest,
                     ssize_t len);

int update_mac(unsigned char *buf, const char* mac);

int create_arp_response(unsigned char *buf);

int create_arp_response_sw(unsigned char *buf, unsigned char *mac, unsigned char *my_ip4);

int is_nonunicast(const unsigned char *buf);

int is_broadcast(const unsigned char *buf);

int is_arp_req(const unsigned char *buf);

int is_arp_resp(const unsigned char *buf);

int is_my_ip4(const unsigned char *buf, const unsigned char *my_ip4);

int is_icc(const unsigned char * buf);

#ifdef __cplusplus
}
#endif

#endif
