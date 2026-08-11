#ifndef HYPERIAN_SECURITY_H
#define HYPERIAN_SECURITY_H

#include <stddef.h>

#define HYPERIAN_SECRET_SIZE 114

int hyperian_hash_secret(const char *secret, char output[HYPERIAN_SECRET_SIZE]);
int hyperian_verify_secret(const char *secret, const char *stored);
int hyperian_random_token(char *output, size_t bytes);

#endif
