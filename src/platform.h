#ifndef HYPERIAN_PLATFORM_H
#define HYPERIAN_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#endif

typedef struct HyperianDirectory HyperianDirectory;

int hyperian_temporary_file(char *path, size_t size, const char *purpose);
int hyperian_temporary_sibling(char *path, size_t size, const char *original, const char *purpose);
int hyperian_temporary_directory(char *path, size_t size, const char *purpose);
int hyperian_executable_path(const char *fallback, char *path, size_t size);
int hyperian_real_path(const char *input, char *path, size_t size);
const char *hyperian_last_path_separator(const char *path);
int hyperian_path_information(const char *path, struct stat *information, int *is_link);
HyperianDirectory *hyperian_directory_open(const char *path);
const char *hyperian_directory_next(HyperianDirectory *directory);
int hyperian_directory_close(HyperianDirectory *directory);
int hyperian_run_process(const char *directory, char *const arguments[]);
int hyperian_set_environment(const char *name, const char *value);
int hyperian_replace_file(const char *replacement, const char *destination);
uint64_t hyperian_monotonic_milliseconds(void);
void hyperian_sleep_milliseconds(uint64_t milliseconds);

#endif
