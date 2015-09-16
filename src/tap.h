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

#if defined(LINUX) || defined(ANDROID)

#ifndef _TAP_H_
#define _TAP_H_

#ifdef __cplusplus
extern "C" {
#endif

int tap_open(const char *device, char *mac);
int tap_set_base_flags();
int tap_unset_noarp_flags();
int tap_set_up();
int tap_set_down();
int tap_set_mtu(int mtu);
int tap_set_ipv4_addr(const char *presentation, unsigned int prefix_len, char *my_ip4);
int tap_set_ipv6_addr(const char *presentation, unsigned int prefix_len);
int tap_set_ipv4_route(const char *presentation, unsigned short prefix_len,
                       unsigned int metric);
int tap_set_ipv6_route(const char *presentation, unsigned short prefix_len,
                       unsigned int metric);
int tap_disable_ipv6_autoconfig();
int tap_set_ipv4_proc_option(const char *option, const char *value);
int tap_set_ipv6_proc_option(const char *option, const char *value);
void tap_close();

#ifdef __cplusplus
}
#endif

#endif

#endif
