
// http://saju.net.in/code/misc/openssl_aes.c.txt

#include <string.h>
#include <openssl/evp.h>

#define AES_BLOCK_SIZE 16

EVP_CIPHER_CTX en, de;

void
aes_init(void)
{
    EVP_CIPHER_CTX_init(&en);
    EVP_CIPHER_CTX_init(&de);
}

int
aes_encrypt(unsigned char *in, unsigned char *out, unsigned char *key, 
    unsigned char *iv, int len)
{
    int c_len = len + AES_BLOCK_SIZE, f_len = 0;

    EVP_EncryptInit_ex(&en, EVP_aes_256_cbc(), NULL, key, iv);
    EVP_EncryptUpdate(&en, out, &c_len, in, len);
    EVP_EncryptFinal_ex(&en, out+c_len, &f_len);

    len = c_len + f_len;
    memcpy(iv, out + len - 32, 32);

    return len;
}

int
aes_decrypt(unsigned char *in, unsigned char *out, unsigned char *key, 
    unsigned char *iv, int len)
{
    int p_len = len, f_len = 0;

    EVP_DecryptInit_ex(&de, EVP_aes_256_cbc(), NULL, key, iv);
    EVP_DecryptUpdate(&de, out, &p_len, in, len);
    EVP_DecryptFinal_ex(&de, out+p_len, &f_len);

    len = p_len + f_len;
    return len;
}

