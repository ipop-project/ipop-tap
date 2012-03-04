
#ifndef _ENCRYPTION_H_
#define _ENCRYPTION_H_

void aes_init(void);

int aes_encrypt(unsigned char *in, unsigned char *out, unsigned char *key, 
    unsigned char *iv, int len);

int aes_decrypt(unsigned char *in, unsigned char *out, unsigned char *key, 
    unsigned char *iv, int len);

#endif

