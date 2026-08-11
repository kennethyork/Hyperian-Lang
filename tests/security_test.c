#include "security.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    char first[HYPERIAN_SECRET_SIZE], second[HYPERIAN_SECRET_SIZE], token[65];
    if (!hyperian_hash_secret("correct horse battery staple", first)) return 1;
    if (!hyperian_hash_secret("correct horse battery staple", second)) return 2;
    if (!hyperian_verify_secret("correct horse battery staple", first)) return 3;
    if (hyperian_verify_secret("wrong", first)) return 4;
    if (!strcmp(first, second)) return 5;
    if (strncmp(first, "$pbkdf2$120000$", 15)) return 7;
    if (!hyperian_random_token(token, 32) || strlen(token) != 64) return 6;
    puts("native secret hashing and random tokens work");
    return 0;
}
