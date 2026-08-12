#include "security.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    char first[HYPERIAN_SECRET_SIZE], second[HYPERIAN_SECRET_SIZE], token[65], digest[65];
    if (argc != 2 || !hyperian_sha256_file(argv[1], digest) ||
        strcmp(digest, "d4a3269c03637171415ce7a290ca0d69db2981f3cc93fc8f5d7bc52af1127bcc")) return 8;
    if (!hyperian_hash_secret("correct horse battery staple", first)) return 1;
    if (!hyperian_hash_secret("correct horse battery staple", second)) return 2;
    if (!hyperian_verify_secret("correct horse battery staple", first)) return 3;
    if (hyperian_verify_secret("wrong", first)) return 4;
    if (!strcmp(first, second)) return 5;
    if (strncmp(first, "$pbkdf2$120000$", 15)) return 7;
    if (!hyperian_random_token(token, 32) || strlen(token) != 64) return 6;
    puts("native SHA-256, secret hashing, and random tokens work");
    return 0;
}
