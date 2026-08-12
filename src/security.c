#include "security.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t state[8]; uint64_t bits; unsigned char block[64]; size_t used; } Sha256;

static const uint32_t constants[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void transform(Sha256 *s) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)s->block[i*4] << 24 | (uint32_t)s->block[i*4+1] << 16 | (uint32_t)s->block[i*4+2] << 8 | s->block[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t a = rotate(w[i-15],7) ^ rotate(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t b = rotate(w[i-2],17) ^ rotate(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + a + w[i-7] + b;
    }
    uint32_t a=s->state[0],b=s->state[1],c=s->state[2],d=s->state[3],e=s->state[4],f=s->state[5],g=s->state[6],h=s->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25), choose=(e&f)^((~e)&g);
        uint32_t first=h+s1+choose+constants[i]+w[i], s0=rotate(a,2)^rotate(a,13)^rotate(a,22), majority=(a&b)^(a&c)^(b&c);
        uint32_t second=s0+majority; h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
    }
    s->state[0]+=a;s->state[1]+=b;s->state[2]+=c;s->state[3]+=d;s->state[4]+=e;s->state[5]+=f;s->state[6]+=g;s->state[7]+=h;
}

static void sha_init(Sha256 *s) {
    uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->state,initial,sizeof(initial)); s->bits=0; s->used=0;
}
static void sha_add(Sha256 *s, const unsigned char *data, size_t length) {
    s->bits += (uint64_t)length * 8;
    while (length) { size_t take=64-s->used; if(take>length)take=length; memcpy(s->block+s->used,data,take); s->used+=take; data+=take; length-=take; if(s->used==64){transform(s);s->used=0;} }
}
static void sha_finish(Sha256 *s, unsigned char out[32]) {
    s->block[s->used++]=0x80;
    if(s->used>56){while(s->used<64)s->block[s->used++]=0;transform(s);s->used=0;}
    while(s->used<56)s->block[s->used++]=0;
    for(int i=7;i>=0;i--)s->block[s->used++]=(unsigned char)(s->bits>>(i*8));
    transform(s);
    for(int i=0;i<8;i++){out[i*4]=(unsigned char)(s->state[i]>>24);out[i*4+1]=(unsigned char)(s->state[i]>>16);out[i*4+2]=(unsigned char)(s->state[i]>>8);out[i*4+3]=(unsigned char)s->state[i];}
}
static void hex(const unsigned char *bytes, size_t count, char *output) {
    static const char digits[]="0123456789abcdef"; for(size_t i=0;i<count;i++){output[i*2]=digits[bytes[i]>>4];output[i*2+1]=digits[bytes[i]&15];} output[count*2]=0;
}
static int unhex(const char *text, unsigned char *bytes, size_t count) {
    for(size_t i=0;i<count;i++){unsigned a=(unsigned)(text[i*2]>='a'?text[i*2]-'a'+10:text[i*2]-'0');unsigned b=(unsigned)(text[i*2+1]>='a'?text[i*2+1]-'a'+10:text[i*2+1]-'0');if(a>15||b>15)return 0;bytes[i]=(unsigned char)(a*16+b);}return 1;
}

static void sha256_bytes(const unsigned char *data, size_t length, unsigned char output[32]) {
    Sha256 sha; sha_init(&sha); sha_add(&sha, data, length); sha_finish(&sha, output);
}

int hyperian_sha256_file(const char *path, char output[65]) {
    FILE *file = fopen(path, "rb"); if (!file) return 0;
    Sha256 sha; sha_init(&sha); unsigned char data[65536], digest[32]; size_t count;
    while ((count = fread(data, 1, sizeof(data), file)) != 0) sha_add(&sha, data, count);
    int okay = !ferror(file); if (fclose(file)) okay = 0; if (!okay) return 0;
    sha_finish(&sha, digest); hex(digest, sizeof(digest), output); return 1;
}

static void hmac_sha256(const unsigned char *key, size_t key_length, const unsigned char *data, size_t length, unsigned char output[32]) {
    unsigned char key_block[64] = {0}, inner_pad[64], outer_pad[64], inner[32];
    if (key_length > 64) { sha256_bytes(key, key_length, key_block); key_length = 32; }
    else memcpy(key_block, key, key_length);
    for (int i = 0; i < 64; i++) { inner_pad[i] = key_block[i] ^ 0x36; outer_pad[i] = key_block[i] ^ 0x5c; }
    Sha256 sha; sha_init(&sha); sha_add(&sha, inner_pad, 64); sha_add(&sha, data, length); sha_finish(&sha, inner);
    sha_init(&sha); sha_add(&sha, outer_pad, 64); sha_add(&sha, inner, 32); sha_finish(&sha, output);
}

static void pbkdf2(const char *secret, const unsigned char salt[16], unsigned iterations, unsigned char output[32]) {
    unsigned char first_input[20], current[32]; memcpy(first_input, salt, 16);
    first_input[16] = first_input[17] = first_input[18] = 0; first_input[19] = 1;
    hmac_sha256((const unsigned char *)secret, strlen(secret), first_input, sizeof(first_input), current);
    memcpy(output, current, 32);
    for (unsigned round = 1; round < iterations; round++) {
        hmac_sha256((const unsigned char *)secret, strlen(secret), current, 32, current);
        for (int i = 0; i < 32; i++) output[i] ^= current[i];
    }
}

int hyperian_random_token(char *output, size_t bytes) {
    unsigned char random[64]; if(bytes>sizeof(random))return 0; FILE *file=fopen("/dev/urandom","rb");
    if(!file)return 0;
    int okay=fread(random,1,bytes,file)==bytes; fclose(file); if(!okay)return 0; hex(random,bytes,output); return 1;
}
int hyperian_hash_secret(const char *secret, char output[HYPERIAN_SECRET_SIZE]) {
    const unsigned iterations = 120000; unsigned char salt[16], digest[32]; char salt_hex[33], digest_hex[65]; FILE *file=fopen("/dev/urandom","rb");
    if(!file)return 0;
    int okay=fread(salt,1,sizeof(salt),file)==sizeof(salt);fclose(file);if(!okay)return 0;
    pbkdf2(secret, salt, iterations, digest);
    hex(salt,sizeof(salt),salt_hex);hex(digest,sizeof(digest),digest_hex);snprintf(output,HYPERIAN_SECRET_SIZE,"$pbkdf2$%u$%s$%s",iterations,salt_hex,digest_hex);return 1;
}
int hyperian_verify_secret(const char *secret, const char *stored) {
    if(strncmp(stored,"$pbkdf2$",8))return 0;
    char *end; unsigned long iterations=strtoul(stored+8,&end,10);
    if(iterations<10000||iterations>1000000||*end!='$'||strlen(end+1)!=97||end[33]!='$')return 0;
    unsigned char salt[16],digest[32],wanted[32];if(!unhex(end+1,salt,16)||!unhex(end+34,wanted,32))return 0;
    pbkdf2(secret,salt,(unsigned)iterations,digest);
    unsigned difference=0;for(int i=0;i<32;i++)difference|=(unsigned)(digest[i]^wanted[i]);return difference==0;
}
