
#include <stdio.h>
#include <string.h>
#include <encryption.h>

int main()
{
    char en_out[200];
    char de_out[200];
    char in[] = "I'm hungry right now";

    char key[32];
    char en_iv[32];
    char de_iv[32];
    int len;

    aes_init();

    memcpy(de_iv, en_iv, 32);

    len = aes_encrypt(in, en_out, key, en_iv, sizeof(in));
 
    printf("%x %x\n", en_out[0], en_out[1]);

    printf("len %d\n", len);

    len = aes_decrypt(en_out, de_out, key, de_iv, len);

    printf("%x %x\n", de_out[0], de_out[1]);

    printf("len %d\n", len);

    printf("%s\n", de_out);

    char in2[] = "man this simple encryption was not that hard";

    memcpy(de_iv, en_iv, 32);

    len = aes_encrypt(in2, en_out, key, en_iv, sizeof(in2));
 
    printf("%x %x\n", en_out[0], en_out[1]);

    printf("len %d\n", len);

    len = aes_decrypt(en_out, de_out, key, de_iv, len);

    printf("%x %x\n", de_out[0], de_out[1]);

    printf("len %d\n", len);

    printf("%s\n", de_out);

}

