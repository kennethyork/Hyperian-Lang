#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *hyperian_last_path_separator(const char *path) {
    const char *forward = strrchr(path, '/');
#ifdef _WIN32
    const char *backward = strrchr(path, '\\');
    return !forward || (backward && backward > forward) ? backward : forward;
#else
    return forward;
#endif
}

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>

struct HyperianDirectory {
    HANDLE handle;
    WIN32_FIND_DATAA entry;
    int first;
    char pattern[4096];
};

static void normalize_path(char *path) {
    for (; *path; path++) if (*path == '\\') *path = '/';
}

int hyperian_temporary_file(char *path, size_t size, const char *purpose) {
    char folder[MAX_PATH + 1], generated[MAX_PATH + 1];
    DWORD length = GetTempPathA((DWORD)sizeof(folder), folder);
    char prefix[4] = {'h', 'y', 'p', 0};
    if (purpose && *purpose) {
        prefix[1] = purpose[0];
        prefix[2] = purpose[1] ? purpose[1] : 'x';
    }
    if (!length || length >= sizeof(folder) || !GetTempFileNameA(folder, prefix, 0, generated)) {
        errno = EIO; return -1;
    }
    normalize_path(generated);
    if (strlen(generated) + 1 > size) { DeleteFileA(generated); errno = ENAMETOOLONG; return -1; }
    strcpy(path, generated);
    int descriptor = _open(path, _O_RDWR | _O_BINARY);
    if (descriptor < 0) DeleteFileA(path);
    return descriptor;
}

int hyperian_temporary_sibling(char *path, size_t size, const char *original, const char *purpose) {
    DWORD process = GetCurrentProcessId();
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        if (snprintf(path, size, "%s.%s-%lu-%u.tmp", original, purpose, (unsigned long)process, attempt) >= (int)size) {
            errno = ENAMETOOLONG; return -1;
        }
        int descriptor = _open(path, _O_RDWR | _O_BINARY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
        if (descriptor >= 0 || errno != EEXIST) return descriptor;
    }
    errno = EEXIST; return -1;
}

int hyperian_temporary_directory(char *path, size_t size, const char *purpose) {
    int descriptor = hyperian_temporary_file(path, size, purpose);
    if (descriptor < 0) return 0;
    _close(descriptor);
    if (!DeleteFileA(path) || !CreateDirectoryA(path, NULL)) { DeleteFileA(path); errno = EIO; return 0; }
    return 1;
}

int hyperian_executable_path(const char *fallback, char *path, size_t size) {
    (void)fallback;
    DWORD length = GetModuleFileNameA(NULL, path, (DWORD)size);
    if (!length || length >= size) { errno = ENAMETOOLONG; return 0; }
    normalize_path(path); return 1;
}

int hyperian_real_path(const char *input, char *path, size_t size) {
    if (!_fullpath(path, input, size)) return 0;
    normalize_path(path); return 1;
}

int hyperian_path_information(const char *path, struct stat *information, int *is_link) {
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) { errno = ENOENT; return -1; }
    if (is_link) *is_link = !!(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    return stat(path, information);
}

HyperianDirectory *hyperian_directory_open(const char *path) {
    HyperianDirectory *directory = calloc(1, sizeof(*directory));
    if (!directory) return NULL;
    if (snprintf(directory->pattern, sizeof(directory->pattern), "%s/*", path) >= (int)sizeof(directory->pattern)) {
        free(directory); errno = ENAMETOOLONG; return NULL;
    }
    directory->handle = FindFirstFileA(directory->pattern, &directory->entry);
    if (directory->handle == INVALID_HANDLE_VALUE) { free(directory); errno = ENOENT; return NULL; }
    directory->first = 1; return directory;
}

const char *hyperian_directory_next(HyperianDirectory *directory) {
    if (directory->first) { directory->first = 0; return directory->entry.cFileName; }
    return FindNextFileA(directory->handle, &directory->entry) ? directory->entry.cFileName : NULL;
}

int hyperian_directory_close(HyperianDirectory *directory) {
    if (!directory) return 0;
    int okay = FindClose(directory->handle) != 0; free(directory); return okay ? 0 : -1;
}

int hyperian_run_process(const char *directory, char *const arguments[]) {
    char original[4096];
    if (!_getcwd(original, sizeof(original)) || _chdir(directory)) return -1;
    const char *tool = arguments[0];
    char **shell_arguments = NULL;
    size_t length = strlen(tool), count = 0;
    if (length >= 3 && !_stricmp(tool + length - 3, ".sh")) {
        while (arguments[count]) count++;
        shell_arguments = malloc((count + 2) * sizeof(*shell_arguments));
        if (!shell_arguments) {
            int saved = errno; _chdir(original); errno = saved; return -1;
        }
        shell_arguments[0] = "sh";
        for (size_t index = 0; index <= count; index++) shell_arguments[index + 1] = arguments[index];
        tool = shell_arguments[0];
        arguments = shell_arguments;
    }
    intptr_t result = _spawnvp(_P_WAIT, tool, (const char *const *)arguments);
    int saved = errno; int restored = !_chdir(original); errno = saved;
    free(shell_arguments);
    return result < 0 || !restored ? -1 : (int)result;
}

int hyperian_set_environment(const char *name, const char *value) { return _putenv_s(name, value); }
int hyperian_replace_file(const char *replacement, const char *destination) {
    if (MoveFileExA(replacement, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
    errno = EIO; return -1;
}
uint64_t hyperian_monotonic_milliseconds(void) { return (uint64_t)GetTickCount64(); }
void hyperian_sleep_milliseconds(uint64_t milliseconds) { Sleep((DWORD)(milliseconds > UINT32_MAX ? UINT32_MAX : milliseconds)); }

#else

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct HyperianDirectory { DIR *handle; };

int hyperian_temporary_file(char *path, size_t size, const char *purpose) {
    if (snprintf(path, size, "/tmp/hyperian-%s-XXXXXX", purpose && *purpose ? purpose : "temporary") >= (int)size) {
        errno = ENAMETOOLONG; return -1;
    }
    return mkstemp(path);
}

int hyperian_temporary_sibling(char *path, size_t size, const char *original, const char *purpose) {
    if (snprintf(path, size, "%s.%s-XXXXXX", original, purpose) >= (int)size) {
        errno = ENAMETOOLONG; return -1;
    }
    return mkstemp(path);
}

int hyperian_temporary_directory(char *path, size_t size, const char *purpose) {
    if (snprintf(path, size, "/tmp/hyperian-%s-XXXXXX", purpose && *purpose ? purpose : "temporary") >= (int)size) {
        errno = ENAMETOOLONG; return 0;
    }
    return mkdtemp(path) != NULL;
}

int hyperian_executable_path(const char *fallback, char *path, size_t size) {
    ssize_t length = readlink("/proc/self/exe", path, size - 1);
    if (length >= 0 && (size_t)length < size) { path[length] = 0; return 1; }
    if (strchr(fallback, '/') && realpath(fallback, path)) return 1;
    const char *search = getenv("PATH");
    while (search && *search) {
        const char *ending = strchr(search, ':'); size_t part = ending ? (size_t)(ending - search) : strlen(search);
        if (part && part + strlen(fallback) + 2 <= size) {
            char candidate[PATH_MAX];
            memcpy(candidate, search, part); candidate[part] = '/'; strcpy(candidate + part + 1, fallback);
            if (!access(candidate, X_OK) && realpath(candidate, path)) return 1;
        }
        if (!ending) break;
        search = ending + 1;
    }
    return realpath(fallback, path) != NULL;
}

int hyperian_real_path(const char *input, char *path, size_t size) {
    (void)size; return realpath(input, path) != NULL;
}

int hyperian_path_information(const char *path, struct stat *information, int *is_link) {
    if (lstat(path, information)) return -1;
    if (is_link) *is_link = S_ISLNK(information->st_mode);
    return 0;
}

HyperianDirectory *hyperian_directory_open(const char *path) {
    DIR *handle = opendir(path); if (!handle) return NULL;
    HyperianDirectory *directory = malloc(sizeof(*directory));
    if (!directory) { closedir(handle); return NULL; }
    directory->handle = handle; return directory;
}

const char *hyperian_directory_next(HyperianDirectory *directory) {
    struct dirent *entry = readdir(directory->handle); return entry ? entry->d_name : NULL;
}

int hyperian_directory_close(HyperianDirectory *directory) {
    if (!directory) return 0;
    int result = closedir(directory->handle); free(directory); return result;
}

int hyperian_run_process(const char *directory, char *const arguments[]) {
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        if (chdir(directory)) _exit(127);
        execvp(arguments[0], arguments); _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int hyperian_set_environment(const char *name, const char *value) { return setenv(name, value, 1); }
int hyperian_replace_file(const char *replacement, const char *destination) { return rename(replacement, destination); }
uint64_t hyperian_monotonic_milliseconds(void) {
    struct timespec time; clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}
void hyperian_sleep_milliseconds(uint64_t milliseconds) {
    struct timespec pause = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000};
    nanosleep(&pause, NULL);
}

#endif
