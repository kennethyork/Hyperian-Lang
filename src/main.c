#include "hyperian.h"
#include "platform.h"
#include "security.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define chmod _chmod
#define chdir _chdir
#define close _close
#define fdopen _fdopen
#define fileno _fileno
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#else
#include <unistd.h>
#endif

static int ends_with(const char *text, const char *suffix);

static int temporary_bytecode(char *path, size_t size) {
    int descriptor = hyperian_temporary_file(path, size, "bytecode");
    if (descriptor < 0) { fprintf(stderr, "error: cannot create temporary bytecode: %s\n", strerror(errno)); return 0; }
    if (close(descriptor)) { unlink(path); fprintf(stderr, "error: cannot finish temporary bytecode: %s\n", strerror(errno)); return 0; }
    return 1;
}

static int write_u64(FILE *file, uint64_t value) {
    unsigned char bytes[8]; for (int i = 0; i < 8; i++) bytes[i] = (unsigned char)(value >> (i * 8));
    return fwrite(bytes, 1, 8, file) == 8;
}

static int read_u64(FILE *file, uint64_t *value) {
    unsigned char bytes[8]; if (fread(bytes, 1, 8, file) != 8) return 0; *value = 0;
    for (int i = 0; i < 8; i++) *value |= (uint64_t)bytes[i] << (i * 8);
    return 1;
}

static FILE *running_executable(const char *fallback) {
    char path[PATH_MAX];
    if (hyperian_executable_path(fallback, path, sizeof(path))) {
        FILE *file = fopen(path, "rb"); if (file) return file;
    }
    return fopen(fallback, "rb");
}

static int embedded_bytecode(FILE *executable, uint64_t *offset, uint64_t *length, int *bundled) {
    if (fseek(executable, 0, SEEK_END)) return 0;
    long end = ftell(executable);
    if (end < 12 || fseek(executable, end - 12, SEEK_SET)) return 0;
    char magic[4]; if (fread(magic, 1, 4, executable) != 4 ||
        (memcmp(magic, "HYEX", 4) && memcmp(magic, "HYBN", 4)) || !read_u64(executable, length)) return 0;
    if (bundled) *bundled = !memcmp(magic, "HYBN", 4);
    if (*length > (uint64_t)end - 12) return 0;
    *offset = (uint64_t)end - 12 - *length; return 1;
}

static int copy_bytes(FILE *from, FILE *to, uint64_t count) {
    unsigned char buffer[65536];
    while (count) {
        size_t wanted = count < sizeof(buffer) ? (size_t)count : sizeof(buffer);
        size_t got = fread(buffer, 1, wanted, from); if (!got || fwrite(buffer, 1, got, to) != got) return 0;
        count -= got;
    }
    return 1;
}

static int build_executable_with_runtime(const char *source, const char *output, const char *runtime_path,
    const char *self_path, int bundled) {
    char bytecode_path[128]; if (!temporary_bytecode(bytecode_path, sizeof(bytecode_path))) return 1;
    int result = compile_file(source, bytecode_path); if (result) { unlink(bytecode_path); return result; }
    FILE *runtime = runtime_path ? fopen(runtime_path, "rb") : running_executable(self_path);
    FILE *bytecode = fopen(bytecode_path, "rb");
    if (!runtime || !bytecode) { fprintf(stderr, "error: cannot read the runtime or compiled bytecode\n"); if (runtime) fclose(runtime); if (bytecode) fclose(bytecode); unlink(bytecode_path); return 1; }
    if (fseek(runtime, 0, SEEK_END) || fseek(bytecode, 0, SEEK_END)) { fclose(runtime); fclose(bytecode); unlink(bytecode_path); return 1; }
    long runtime_size = ftell(runtime), bytecode_size = ftell(bytecode); uint64_t old_offset, old_length; int old_bundle;
    if (embedded_bytecode(runtime, &old_offset, &old_length, &old_bundle)) runtime_size = (long)old_offset;
    rewind(runtime); rewind(bytecode);
    struct stat runtime_info, output_info;
    if (!fstat(fileno(runtime), &runtime_info) && !stat(output, &output_info) && runtime_info.st_dev == output_info.st_dev && runtime_info.st_ino == output_info.st_ino) {
        fprintf(stderr, "error: output cannot replace the compiler while it is running\n"); fclose(runtime); fclose(bytecode); unlink(bytecode_path); return 1;
    }
    FILE *built = fopen(output, "wb");
    int okay = built && runtime_size >= 0 && bytecode_size >= 0 && copy_bytes(runtime, built, (uint64_t)runtime_size) &&
        copy_bytes(bytecode, built, (uint64_t)bytecode_size) && fwrite(bundled ? "HYBN" : "HYEX", 1, 4, built) == 4 && write_u64(built, (uint64_t)bytecode_size);
    if (built && fclose(built)) okay = 0;
    fclose(runtime); fclose(bytecode); unlink(bytecode_path);
    if (!okay || chmod(output, 0755)) { fprintf(stderr, "error: could not create executable %s\n", output); return 1; }
    printf("Built standalone executable %s.\n", output); return 0;
}

static int build_executable(const char *source, const char *output, const char *self_path, int bundled) {
    return build_executable_with_runtime(source, output, NULL, self_path, bundled);
}

static int enter_bundle_directory(const char *fallback) {
    char path[PATH_MAX];
    if (!hyperian_executable_path(fallback, path, sizeof(path))) { fprintf(stderr, "error: cannot locate this application bundle\n"); return 0; }
    char *slash = strrchr(path, '/');
    if (!slash) return 1;
    *slash = 0;
    if (chdir(path)) { fprintf(stderr, "error: cannot enter application bundle %s: %s\n", path, strerror(errno)); return 0; }
    return 1;
}

static int run_embedded_program(int argc, char **argv, int *found) {
    *found = 0; FILE *executable = running_executable(argv[0]); if (!executable) return 0;
    uint64_t offset, length; int bundled; if (!embedded_bytecode(executable, &offset, &length, &bundled)) { fclose(executable); return 0; }
    *found = 1; int port = 0;
    if (argc == 3 && !strcmp(argv[1], "--port")) port = atoi(argv[2]);
    else if (argc != 1) { fprintf(stderr, "usage: %s [--port 9000]\n", argv[0]); fclose(executable); return 2; }
    if (port < 0 || port > 65535) { fprintf(stderr, "error: invalid port\n"); fclose(executable); return 2; }
    char path[128]; if (!temporary_bytecode(path, sizeof(path))) { fclose(executable); return 1; }
    FILE *bytecode = fopen(path, "wb");
    int okay = bytecode && !fseek(executable, (long)offset, SEEK_SET) && copy_bytes(executable, bytecode, length);
    if (bytecode && fclose(bytecode)) okay = 0;
    fclose(executable);
    if (!okay) { unlink(path); fprintf(stderr, "error: could not load embedded Hyperian bytecode\n"); return 1; }
    if (bundled && !enter_bundle_directory(argv[0])) { unlink(path); return 1; }
    int result = run_bytecode(path, port); unlink(path); return result;
}

static int project_name_is_safe(const char *name) {
    if (!*name) return 0;
    for (const char *at = name; *at; at++)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') || (*at >= '0' && *at <= '9') || *at == '-' || *at == '_')) return 0;
    return 1;
}

static int make_project_directory(const char *path) {
    if (!mkdir(path, 0755)) return 1;
    fprintf(stderr, "error: cannot create folder %s: %s\n", path, strerror(errno)); return 0;
}

static int write_project_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wx");
    if (!file) { fprintf(stderr, "error: cannot create file %s: %s\n", path, strerror(errno)); return 0; }
    int okay = fputs(contents, file) != EOF;
    if (fclose(file)) okay = 0;
    if (!okay) fprintf(stderr, "error: could not finish file %s\n", path);
    return okay;
}

static int write_left_aligned_source(const char *path, const char *contents) {
    char output[16384]; size_t used = 0; unsigned widths[64] = {0}; int level = 0;
    for (const char *line = contents; *line;) {
        const char *ending = strchr(line, '\n'); size_t length = ending ? (size_t)(ending - line) : strlen(line);
        unsigned width = 0; while (width < length && (line[width] == ' ' || line[width] == '\t')) width++;
        if (width < length) {
            while (level > 0 && width < widths[level]) {
                const char *closing = "that is all\n"; size_t closing_length = strlen(closing);
                if (used + closing_length >= sizeof(output)) goto too_large;
                memcpy(output + used, closing, closing_length); used += closing_length; level--;
            }
            if (width > widths[level]) {
                if (level + 1 >= 64) goto too_large;
                widths[++level] = width;
            }
            if (used + length - width + 1 >= sizeof(output)) goto too_large;
            memcpy(output + used, line + width, length - width); used += length - width;
        } else if (used + 1 >= sizeof(output)) goto too_large;
        output[used++] = '\n'; line = ending ? ending + 1 : line + length;
    }
    while (level > 0) {
        const char *closing = "that is all\n"; size_t closing_length = strlen(closing);
        if (used + closing_length >= sizeof(output)) goto too_large;
        memcpy(output + used, closing, closing_length); used += closing_length; level--;
    }
    output[used] = 0; return write_project_file(path, output);
too_large:
    fprintf(stderr, "error: generated Hyperian source for %s is too large\n", path); return 0;
}

static int copy_bundle_file(const char *source, const char *destination, int mode) {
    FILE *from = fopen(source, "rb"), *to = from ? fopen(destination, "wb") : NULL; int okay = 1;
    if (!from || !to) okay = 0;
    unsigned char data[65536]; size_t count;
    while (okay && (count = fread(data, 1, sizeof(data), from)) != 0)
        if (fwrite(data, 1, count, to) != count) okay = 0;
    if (from && ferror(from)) okay = 0;
    if (to && fclose(to)) okay = 0;
    if (from) fclose(from);
    if (okay && chmod(destination, mode & 0777)) okay = 0;
    if (!okay) fprintf(stderr, "error: cannot copy bundle file %s\n", source);
    return okay;
}

static int copy_new_file(const char *source, const char *destination, int mode) {
    FILE *from = fopen(source, "rb"), *to = from ? fopen(destination, "wbx") : NULL; int okay = from && to;
    unsigned char data[65536]; size_t count;
    while (okay && (count = fread(data, 1, sizeof(data), from)) != 0)
        if (fwrite(data, 1, count, to) != count) okay = 0;
    if (from && ferror(from)) okay = 0;
    if (to && fclose(to)) okay = 0;
    if (from) fclose(from);
    if (okay && chmod(destination, mode & 0777)) okay = 0;
    if (!okay) {
        if (to) unlink(destination);
        fprintf(stderr, "error: cannot create mobile application artifact %s: %s\n", destination, strerror(errno));
    }
    return okay;
}

static int copy_bundle_tree(const char *source, const char *destination) {
    struct stat info; int is_link = 0;
    if (hyperian_path_information(source, &info, &is_link)) return errno == ENOENT;
    if (is_link) { fprintf(stderr, "error: bundle assets cannot be symbolic links: %s\n", source); return 0; }
    if (!S_ISDIR(info.st_mode)) { fprintf(stderr, "error: bundle asset %s must be a folder\n", source); return 0; }
    if (mkdir(destination, info.st_mode & 0777)) { fprintf(stderr, "error: cannot create bundle folder %s: %s\n", destination, strerror(errno)); return 0; }
    HyperianDirectory *directory = hyperian_directory_open(source); if (!directory) return 0; int okay = 1; const char *name;
    while (okay && (name = hyperian_directory_next(directory))) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        char from[PATH_MAX], to[PATH_MAX];
        if (snprintf(from, sizeof(from), "%s/%s", source, name) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", destination, name) >= (int)sizeof(to)) { okay = 0; break; }
        if (hyperian_path_information(from, &info, &is_link)) { okay = 0; break; }
        if (is_link) { fprintf(stderr, "error: bundle assets cannot contain symbolic links: %s\n", from); okay = 0; }
        else if (S_ISDIR(info.st_mode)) okay = copy_bundle_tree(from, to);
        else if (S_ISREG(info.st_mode)) okay = copy_bundle_file(from, to, info.st_mode);
        else { fprintf(stderr, "error: unsupported bundle asset %s\n", from); okay = 0; }
    }
    if (hyperian_directory_close(directory)) okay = 0;
    return okay;
}

static int remove_bundle_tree(const char *path) {
    struct stat info; int is_link = 0;
    if (hyperian_path_information(path, &info, &is_link)) return errno == ENOENT;
    if (!S_ISDIR(info.st_mode) || is_link) return !unlink(path);
    HyperianDirectory *directory = hyperian_directory_open(path); if (!directory) return 0; int okay = 1; const char *name;
    while (okay && (name = hyperian_directory_next(directory))) {
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        char child[PATH_MAX];
        if (snprintf(child, sizeof(child), "%s/%s", path, name) >= (int)sizeof(child) || !remove_bundle_tree(child)) okay = 0;
    }
    if (hyperian_directory_close(directory)) okay = 0;
    if (okay && rmdir(path)) okay = 0;
    return okay;
}

static const char *host_platform_name(void) {
#if defined(__x86_64__) || defined(_M_X64)
    const char *architecture = "x64";
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    const char *architecture = "arm64";
#else
    const char *architecture = NULL;
#endif
#if defined(__APPLE__)
    static char platform[32]; if (architecture) snprintf(platform, sizeof(platform), "macos-%s", architecture); return architecture ? platform : NULL;
#elif defined(__linux__)
    static char platform[32]; if (architecture) snprintf(platform, sizeof(platform), "linux-%s", architecture); return architecture ? platform : NULL;
#elif defined(__FreeBSD__)
    static char platform[32]; if (architecture) snprintf(platform, sizeof(platform), "freebsd-%s", architecture); return architecture ? platform : NULL;
#elif defined(_WIN32)
    static char platform[32]; if (architecture) snprintf(platform, sizeof(platform), "windows-%s", architecture); return architecture ? platform : NULL;
#else
    (void)architecture; return NULL;
#endif
}

static int safe_platform_name(const char *value) {
    if (!value || !*value) return 0;
    for (const char *at = value; *at; at++)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= '0' && *at <= '9') || *at == '-')) return 0;
    return 1;
}

static int copy_runtime_prefix(FILE *runtime, const char *destination) {
    if (fseek(runtime, 0, SEEK_END)) return 0;
    long size = ftell(runtime); uint64_t offset, length; int bundled;
    if (embedded_bytecode(runtime, &offset, &length, &bundled)) size = (long)offset;
    rewind(runtime); FILE *output = fopen(destination, "wbx");
    int okay = output && size >= 0 && copy_bytes(runtime, output, (uint64_t)size);
    if (output && fclose(output)) okay = 0;
    if (!okay) unlink(destination);
    return okay && !chmod(destination, 0755);
}

static int create_runtime_pack(const char *output, const char *self_path) {
    const char *platform = host_platform_name();
    if (!platform) { fprintf(stderr, "error: this operating system or processor cannot be named as a Hyperian runtime pack yet\n"); return 1; }
    struct stat existing;
    if (!stat(output, &existing)) { fprintf(stderr, "error: runtime pack %s already exists\n", output); return 1; }
    if (errno != ENOENT || mkdir(output, 0755)) { fprintf(stderr, "error: cannot create runtime pack %s: %s\n", output, strerror(errno)); return 1; }
    char runtime_path[PATH_MAX], manifest_path[PATH_MAX];
    const char *runtime_name = !strncmp(platform, "windows-", 8) ? "runtime.exe" : "runtime";
    int paths_fit = snprintf(runtime_path, sizeof(runtime_path), "%s/%s", output, runtime_name) < (int)sizeof(runtime_path) &&
        snprintf(manifest_path, sizeof(manifest_path), "%s/hyperian.runtime", output) < (int)sizeof(manifest_path);
    FILE *runtime = paths_fit ? running_executable(self_path) : NULL;
    int okay = runtime && copy_runtime_prefix(runtime, runtime_path); if (runtime) fclose(runtime);
    char manifest[512], checksum[65]; if (okay) okay = hyperian_sha256_file(runtime_path, checksum);
    if (okay) {
        snprintf(manifest, sizeof(manifest),
            "Hyperian native runtime pack\nformat: HYRP1\ntoolchain: %s\nbytecode: %s\nplatform: %s\nexecutable: %s\nchecksum: %s\n",
            HYPERIAN_VERSION, HYC_MAGIC, platform, runtime_name, checksum);
        okay = write_project_file(manifest_path, manifest);
    }
    if (!okay) { remove_bundle_tree(output); fprintf(stderr, "error: could not finish runtime pack %s\n", output); return 1; }
    printf("Packed the native Hyperian runtime for %s in %s.\n", platform, output); return 0;
}

static int create_runtime_archive(const char *output, const char *self_path) {
    const char *platform = host_platform_name();
    if (!platform) { fprintf(stderr, "error: this operating system or processor cannot be named as a Hyperian runtime pack yet\n"); return 1; }
    struct stat existing;
    if (!stat(output, &existing)) { fprintf(stderr, "error: runtime pack %s already exists\n", output); return 1; }
    if (errno != ENOENT) { fprintf(stderr, "error: cannot create runtime pack %s: %s\n", output, strerror(errno)); return 1; }
    char runtime_path[PATH_MAX]; int descriptor = hyperian_temporary_file(runtime_path, sizeof(runtime_path), "runtime");
    if (descriptor < 0 || close(descriptor) || unlink(runtime_path)) {
        if (descriptor >= 0) unlink(runtime_path);
        fprintf(stderr, "error: cannot prepare a native runtime archive: %s\n", strerror(errno)); return 1;
    }
    FILE *running = running_executable(self_path);
    int okay = running && copy_runtime_prefix(running, runtime_path); if (running) fclose(running);
    char checksum[65] = "", manifest[512] = ""; if (okay) okay = hyperian_sha256_file(runtime_path, checksum);
    if (okay) snprintf(manifest, sizeof(manifest),
        "Hyperian native runtime pack\nformat: HYRP1\ntoolchain: %s\nbytecode: %s\nplatform: %s\nexecutable: %s\nchecksum: %s\n",
        HYPERIAN_VERSION, HYC_MAGIC, platform, !strncmp(platform, "windows-", 8) ? "runtime.exe" : "runtime", checksum);
    FILE *runtime = okay ? fopen(runtime_path, "rb") : NULL;
    FILE *archive = runtime ? fopen(output, "wbx") : NULL;
    long runtime_size = -1;
    if (runtime && (!fseek(runtime, 0, SEEK_END))) runtime_size = ftell(runtime);
    if (runtime) rewind(runtime);
    size_t manifest_size = strlen(manifest);
    okay = archive && runtime_size > 0 && fwrite("HYRPACK1", 1, 8, archive) == 8 &&
        write_u64(archive, (uint64_t)manifest_size) && write_u64(archive, (uint64_t)runtime_size) &&
        fwrite(manifest, 1, manifest_size, archive) == manifest_size && copy_bytes(runtime, archive, (uint64_t)runtime_size);
    if (archive && fclose(archive)) okay = 0;
    if (runtime) fclose(runtime);
    unlink(runtime_path);
    if (!okay) { unlink(output); fprintf(stderr, "error: could not finish runtime archive %s\n", output); return 1; }
    if (chmod(output, 0644)) { unlink(output); fprintf(stderr, "error: cannot finish runtime archive %s\n", output); return 1; }
    printf("Packed the native Hyperian runtime for %s as %s.\n", platform, output); return 0;
}

static void clean_manifest_value(char *value) {
    size_t length = strlen(value); while (length && (value[length - 1] == '\n' || value[length - 1] == '\r')) value[--length] = 0;
}

static int validate_runtime_manifest(char *contents, const char *pack, const char *wanted_platform,
    char *executable_output, size_t executable_size, char checksum_output[65]) {
    char line[1024], format[32] = "", toolchain[32] = "", bytecode[32] = "", platform[64] = "";
    char executable_value[64] = "", checksum_value[80] = "";
    char *cursor = contents;
    while (*cursor) {
        char *ending = strchr(cursor, '\n'); size_t length = ending ? (size_t)(ending - cursor) : strlen(cursor);
        if (length >= sizeof(line)) { fprintf(stderr, "error: %s has a damaged Hyperian runtime manifest\n", pack); return 0; }
        memcpy(line, cursor, length); line[length] = 0; cursor = ending ? ending + 1 : cursor + length;
        char *value = strchr(line, ':'); if (!value) continue; *value++ = 0; if (*value == ' ') value++; clean_manifest_value(value);
        if (!strcmp(line, "format")) snprintf(format, sizeof(format), "%s", value);
        else if (!strcmp(line, "toolchain")) snprintf(toolchain, sizeof(toolchain), "%s", value);
        else if (!strcmp(line, "bytecode")) snprintf(bytecode, sizeof(bytecode), "%s", value);
        else if (!strcmp(line, "platform")) snprintf(platform, sizeof(platform), "%s", value);
        else if (!strcmp(line, "executable")) snprintf(executable_value, sizeof(executable_value), "%s", value);
        else if (!strcmp(line, "checksum")) snprintf(checksum_value, sizeof(checksum_value), "%s", value);
    }
    int checksum_valid = strlen(checksum_value) == 64;
    for (const char *at = checksum_value; checksum_valid && *at; at++) checksum_valid = (*at >= '0' && *at <= '9') || (*at >= 'a' && *at <= 'f');
    if (strcmp(format, "HYRP1") || strcmp(bytecode, HYC_MAGIC) || !safe_platform_name(platform) || !checksum_valid ||
        (strcmp(executable_value, "runtime") && strcmp(executable_value, "runtime.exe"))) {
        fprintf(stderr, "error: %s has a damaged Hyperian runtime manifest\n", pack); return 0;
    }
    if (strcmp(toolchain, HYPERIAN_VERSION)) {
        fprintf(stderr, "error: runtime pack %s uses Hyperian %s but this compiler is Hyperian %s\n", pack, toolchain, HYPERIAN_VERSION); return 0;
    }
    if (strcmp(platform, wanted_platform)) {
        fprintf(stderr, "error: runtime pack %s is for %s, not %s\n", pack, platform, wanted_platform); return 0;
    }
    snprintf(executable_output, executable_size, "%s", executable_value);
    memcpy(checksum_output, checksum_value, 65); return 1;
}

static int runtime_pack_executable(const char *pack, const char *wanted_platform, char *runtime_path,
    size_t runtime_path_size, int *temporary) {
    *temporary = 0; struct stat pack_info; int pack_is_link = 0;
    if (hyperian_path_information(pack, &pack_info, &pack_is_link) || pack_is_link) {
        fprintf(stderr, "error: %s is not a Hyperian native runtime pack\n", pack); return 0;
    }
    char manifest_contents[4096], executable[64], wanted_checksum[65];
    if (S_ISDIR(pack_info.st_mode)) {
        char manifest_path[PATH_MAX];
        if (snprintf(manifest_path, sizeof(manifest_path), "%s/hyperian.runtime", pack) >= (int)sizeof(manifest_path)) {
            fprintf(stderr, "error: runtime pack path is too long\n"); return 0;
        }
        FILE *manifest = fopen(manifest_path, "rb");
        if (!manifest) { fprintf(stderr, "error: %s is not a Hyperian native runtime pack\n", pack); return 0; }
        size_t count = fread(manifest_contents, 1, sizeof(manifest_contents) - 1, manifest);
        int read_failed = ferror(manifest) || (!feof(manifest) && count == sizeof(manifest_contents) - 1);
        fclose(manifest); if (read_failed) { fprintf(stderr, "error: %s has a damaged Hyperian runtime manifest\n", pack); return 0; }
        manifest_contents[count] = 0;
        if (!validate_runtime_manifest(manifest_contents, pack, wanted_platform, executable, sizeof(executable), wanted_checksum)) return 0;
        if (snprintf(runtime_path, runtime_path_size, "%s/%s", pack, executable) >= (int)runtime_path_size) {
            fprintf(stderr, "error: runtime executable path is too long\n"); return 0;
        }
    } else if (S_ISREG(pack_info.st_mode)) {
        FILE *archive = fopen(pack, "rb"); uint64_t manifest_size = 0, runtime_size = 0; char magic[8];
        int okay = archive && pack_info.st_size >= 24 && fread(magic, 1, sizeof(magic), archive) == sizeof(magic) && !memcmp(magic, "HYRPACK1", 8) &&
            read_u64(archive, &manifest_size) && read_u64(archive, &runtime_size) && manifest_size > 0 &&
            manifest_size < sizeof(manifest_contents) && manifest_size <= (uint64_t)pack_info.st_size - UINT64_C(24) && runtime_size > 0 &&
            runtime_size == (uint64_t)pack_info.st_size - UINT64_C(24) - manifest_size &&
            fread(manifest_contents, 1, (size_t)manifest_size, archive) == (size_t)manifest_size;
        if (!okay) { if (archive) fclose(archive); fprintf(stderr, "error: %s is a damaged Hyperian runtime archive\n", pack); return 0; }
        manifest_contents[manifest_size] = 0;
        if (!validate_runtime_manifest(manifest_contents, pack, wanted_platform, executable, sizeof(executable), wanted_checksum)) {
            fclose(archive); return 0;
        }
        char path[PATH_MAX]; int descriptor = hyperian_temporary_file(path, sizeof(path), "runtime"); FILE *runtime = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
        if (descriptor >= 0 && !runtime) close(descriptor);
        okay = runtime && copy_bytes(archive, runtime, runtime_size); if (runtime && fclose(runtime)) okay = 0;
        fclose(archive);
        if (!okay || chmod(path, 0755) || snprintf(runtime_path, runtime_path_size, "%s", path) >= (int)runtime_path_size) {
            unlink(path); fprintf(stderr, "error: cannot unpack native runtime archive %s\n", pack); return 0;
        }
        *temporary = 1;
    } else {
        fprintf(stderr, "error: %s is not a Hyperian native runtime pack\n", pack); return 0;
    }
    struct stat info; int runtime_is_link = 0;
    if (hyperian_path_information(runtime_path, &info, &runtime_is_link) || !S_ISREG(info.st_mode) || runtime_is_link) {
        if (*temporary) unlink(runtime_path);
        fprintf(stderr, "error: runtime pack %s does not contain its native executable\n", pack); return 0;
    }
    char actual_checksum[65];
    if (!hyperian_sha256_file(runtime_path, actual_checksum) || strcmp(wanted_checksum, actual_checksum)) {
        if (*temporary) unlink(runtime_path);
        fprintf(stderr, "error: runtime executable in %s does not match its manifest\n", pack); return 0;
    }
    return 1;
}

static int build_for_native_platform(const char *source, const char *platform, const char *pack, const char *output) {
    if (!safe_platform_name(platform)) { fprintf(stderr, "error: native platform names use lowercase letters, numbers, and hyphens\n"); return 2; }
    char runtime_path[PATH_MAX]; int temporary;
    if (!runtime_pack_executable(pack, platform, runtime_path, sizeof(runtime_path), &temporary)) return 1;
    int result = build_executable_with_runtime(source, output, runtime_path, NULL, 0); if (temporary) unlink(runtime_path);
    if (!result) printf("Used the %s native runtime pack.\n", platform);
    return result;
}
static void source_directory(const char *source, char *directory, size_t size) {
    const char *slash = hyperian_last_path_separator(source);
    if (!slash) snprintf(directory, size, ".");
    else if (slash == source) snprintf(directory, size, "/");
    else { size_t length = (size_t)(slash - source); if (length >= size) length = size - 1; memcpy(directory, source, length); directory[length] = 0; }
}

static int bundle_application_with_runtime(const char *source, const char *output, const char *runtime_path,
    const char *self_path, const char *platform) {
    struct stat existing;
    if (!stat(output, &existing)) { fprintf(stderr, "error: bundle output %s already exists\n", output); return 1; }
    if (errno != ENOENT || mkdir(output, 0755)) { fprintf(stderr, "error: cannot create bundle %s: %s\n", output, strerror(errno)); return 1; }
    char executable[PATH_MAX], manifest[PATH_MAX], project[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    const char *executable_name = platform && !strncmp(platform, "windows-", 8) ? "run.exe" : "run";
    if (snprintf(executable, sizeof(executable), "%s/%s", output, executable_name) >= (int)sizeof(executable) ||
        snprintf(manifest, sizeof(manifest), "%s/hyperian.bundle", output) >= (int)sizeof(manifest)) {
        fprintf(stderr, "error: bundle path is too long\n"); remove_bundle_tree(output); return 1;
    }
    if (build_executable_with_runtime(source, executable, runtime_path, self_path, 1)) { remove_bundle_tree(output); return 1; }
    source_directory(source, project, sizeof(project));
    const char *folders[] = {"assets", "public"};
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", project, folders[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", output, folders[i]) >= (int)sizeof(to) || !copy_bundle_tree(from, to)) {
            remove_bundle_tree(output); return 1;
        }
    }
    char contents[512]; snprintf(contents, sizeof(contents),
        "Hyperian application bundle\nformat: HYBN1\ntoolchain: %s\nplatform: %s\nexecutable: %s\nassets: assets, public\n",
        HYPERIAN_VERSION, platform ? platform : "unknown", executable_name);
    if (!write_project_file(manifest, contents)) { remove_bundle_tree(output); return 1; }
    printf("Bundled %s with its assets for %s in %s.\n", source, platform ? platform : "this computer", output); return 0;
}

static int bundle_application(const char *source, const char *output, const char *self_path) {
    return bundle_application_with_runtime(source, output, NULL, self_path, host_platform_name());
}

static int bundle_for_native_platform(const char *source, const char *platform, const char *pack, const char *output) {
    if (!safe_platform_name(platform)) { fprintf(stderr, "error: native platform names use lowercase letters, numbers, and hyphens\n"); return 2; }
    char runtime_path[PATH_MAX]; int temporary;
    if (!runtime_pack_executable(pack, platform, runtime_path, sizeof(runtime_path), &temporary)) return 1;
    int result = bundle_application_with_runtime(source, output, runtime_path, NULL, platform); if (temporary) unlink(runtime_path);
    return result;
}

static int resource_folders(const char *self_path, const char *platform, char *adapter, size_t adapter_size, char *runtime, size_t runtime_size) {
    char executable[PATH_MAX];
    struct stat installed;
    if (hyperian_real_path(self_path, executable, sizeof(executable))) {
        char *slash = strrchr(executable, '/'); if (slash) { *slash = 0; slash = strrchr(executable, '/'); }
        if (slash) {
            *slash = 0; snprintf(adapter, adapter_size, "%s/share/hyperian/platform/%s", executable, platform);
            snprintf(runtime, runtime_size, "%s/share/hyperian/runtime", executable);
            if (!stat(adapter, &installed) && S_ISDIR(installed.st_mode) && !stat(runtime, &installed) && S_ISDIR(installed.st_mode)) return 1;
        }
    }
#ifdef HYPERIAN_SOURCE_ROOT
    snprintf(adapter, adapter_size, "%s/platform/%s", HYPERIAN_SOURCE_ROOT, platform);
    snprintf(runtime, runtime_size, "%s/src", HYPERIAN_SOURCE_ROOT);
    if (!stat(adapter, &installed) && S_ISDIR(installed.st_mode) && !stat(runtime, &installed) && S_ISDIR(installed.st_mode)) return 1;
#endif
    return 0;
}

static void xml_text(const char *input, char *output, size_t output_size) {
    size_t used = 0;
    for (; *input && used + 1 < output_size; input++) {
        const char *escaped = *input == '&' ? "&amp;" : *input == '<' ? "&lt;" : *input == '>' ? "&gt;" :
            *input == '"' ? "&quot;" : *input == '\'' ? "&apos;" : NULL;
        if (escaped) {
            size_t length = strlen(escaped); if (length >= output_size - used) break;
            memcpy(output + used, escaped, length); used += length;
        } else output[used++] = *input;
    }
    output[used] = 0;
}

static int add_android_project(const char *output, const char *name, const char *self_path) {
    char template[PATH_MAX], runtime[PATH_MAX], sqlite[PATH_MAX], ignored[PATH_MAX], destination[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    if (!resource_folders(self_path, "android", template, sizeof(template), runtime, sizeof(runtime))) {
        fprintf(stderr, "error: cannot find Hyperian's installed Android adapter resources\n"); return 0;
    }
    if (snprintf(destination, sizeof(destination), "%s/android", output) >= (int)sizeof(destination) ||
        !copy_bundle_tree(template, destination)) return 0;
    if (!resource_folders(self_path, "sqlite", sqlite, sizeof(sqlite), ignored, sizeof(ignored))) {
        fprintf(stderr, "error: cannot find Hyperian's embedded SQLite resources\n"); return 0;
    }
    const char *sources[] = {"mobile.c", "runtime.c", "bytecode.c", "network.c", "security.c", "security.h", "desktop.c", "game.c", "hyperian.h"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", runtime, sources[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/android/app/src/main/cpp/hyperian/%s", output, sources[i]) >= (int)sizeof(to) ||
            !copy_bundle_file(from, to, 0644)) return 0;
    }
    const char *sqlite_sources[] = {"sqlite3.c", "sqlite3.h"};
    for (size_t i = 0; i < sizeof(sqlite_sources) / sizeof(sqlite_sources[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", sqlite, sqlite_sources[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/android/app/src/main/cpp/sqlite/%s", output, sqlite_sources[i]) >= (int)sizeof(to) ||
            !copy_bundle_file(from, to, 0644)) return 0;
    }
    if (snprintf(from, sizeof(from), "%s/application.hyc", output) >= (int)sizeof(from) ||
        snprintf(to, sizeof(to), "%s/android/app/src/main/assets/application.hyc", output) >= (int)sizeof(to) ||
        !copy_bundle_file(from, to, 0644)) return 0;
    const char *asset_folders[] = {"assets", "public"};
    for (size_t i = 0; i < sizeof(asset_folders) / sizeof(asset_folders[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", output, asset_folders[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/android/app/src/main/assets/%s", output, asset_folders[i]) >= (int)sizeof(to) ||
            !copy_bundle_tree(from, to)) return 0;
    }
    char safe_name[512], strings[768]; xml_text(name, safe_name, sizeof(safe_name));
    snprintf(strings, sizeof(strings), "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources><string name=\"app_name\">%s</string></resources>\n", safe_name);
    if (snprintf(to, sizeof(to), "%s/android/app/src/main/res/values/strings.xml", output) >= (int)sizeof(to)) return 0;
    unlink(to); return write_project_file(to, strings);
}

static int add_ios_project(const char *output, const char *name, const char *self_path) {
    char template[PATH_MAX], runtime[PATH_MAX], destination[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    if (!resource_folders(self_path, "ios", template, sizeof(template), runtime, sizeof(runtime))) {
        fprintf(stderr, "error: cannot find Hyperian's installed iOS adapter resources\n"); return 0;
    }
    if (snprintf(destination, sizeof(destination), "%s/ios", output) >= (int)sizeof(destination) ||
        !copy_bundle_tree(template, destination)) return 0;
    const char *sources[] = {"mobile.c", "runtime.c", "bytecode.c", "network.c", "security.c", "security.h", "desktop.c", "game.c", "hyperian.h"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", runtime, sources[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/ios/HyperianIOS/Runtime/%s", output, sources[i]) >= (int)sizeof(to) ||
            !copy_bundle_file(from, to, 0644)) return 0;
    }
    if (snprintf(from, sizeof(from), "%s/application.hyc", output) >= (int)sizeof(from) ||
        snprintf(to, sizeof(to), "%s/ios/HyperianIOS/Resources/application.hyc", output) >= (int)sizeof(to) ||
        !copy_bundle_file(from, to, 0644)) return 0;
    const char *asset_folders[] = {"assets", "public"};
    for (size_t i = 0; i < sizeof(asset_folders) / sizeof(asset_folders[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", output, asset_folders[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/ios/HyperianIOS/Resources/%s", output, asset_folders[i]) >= (int)sizeof(to) ||
            !copy_bundle_tree(from, to)) return 0;
    }
    char safe_name[512], plist[1536]; xml_text(name, safe_name, sizeof(safe_name));
    snprintf(plist, sizeof(plist), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\"><dict>\n    <key>CFBundleDisplayName</key><string>%s</string>\n    <key>UILaunchScreen</key><dict/>\n    <key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationPortrait</string></array>\n</dict></plist>\n", safe_name);
    if (snprintf(to, sizeof(to), "%s/ios/HyperianIOS/Info.plist", output) >= (int)sizeof(to)) return 0;
    unlink(to); return write_project_file(to, plist);
}

static int export_mobile_application(const char *source, const char *platform, const char *output, const char *self_path) {
    if (strcmp(platform, "android") && strcmp(platform, "ios")) {
        fprintf(stderr, "error: export for android or ios\n"); return 2;
    }
    char temporary[128]; if (!temporary_bytecode(temporary, sizeof(temporary))) return 1;
    int result = compile_file(source, temporary); if (result) { unlink(temporary); return result; }
    Bytecode code; bytecode_init(&code); char error[256];
    if (!bytecode_read(&code, temporary, error, sizeof(error))) {
        unlink(temporary); fprintf(stderr, "error: %s\n", error); return 1;
    }
    const char *name = "Hyperian App", *target = "web";
    for (size_t i = 0; i < code.count; i++) {
        if (code.items[i].opcode == OP_APPLICATION) name = code.items[i].args[0];
        if (code.items[i].opcode == OP_TARGET) target = code.items[i].args[0];
    }
    if (strcmp(target, "mobile")) {
        bytecode_free(&code); unlink(temporary);
        fprintf(stderr, "error: only an application declared as mobile can be exported for a phone\n"); return 1;
    }
    struct stat existing;
    if (!stat(output, &existing)) {
        bytecode_free(&code); unlink(temporary); fprintf(stderr, "error: mobile export %s already exists\n", output); return 1;
    }
    if (errno != ENOENT || mkdir(output, 0755)) {
        bytecode_free(&code); unlink(temporary); fprintf(stderr, "error: cannot create mobile export %s: %s\n", output, strerror(errno)); return 1;
    }
    char bytecode_path[PATH_MAX], manifest[PATH_MAX], readme[PATH_MAX], project[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    int okay = snprintf(bytecode_path, sizeof(bytecode_path), "%s/application.hyc", output) < (int)sizeof(bytecode_path) &&
        snprintf(manifest, sizeof(manifest), "%s/hyperian.mobile", output) < (int)sizeof(manifest) &&
        snprintf(readme, sizeof(readme), "%s/README.md", output) < (int)sizeof(readme) &&
        copy_bundle_file(temporary, bytecode_path, 0644);
    unlink(temporary);
    source_directory(source, project, sizeof(project));
    const char *folders[] = {"assets", "public"};
    for (size_t i = 0; okay && i < sizeof(folders) / sizeof(folders[0]); i++) {
        okay = snprintf(from, sizeof(from), "%s/%s", project, folders[i]) < (int)sizeof(from) &&
            snprintf(to, sizeof(to), "%s/%s", output, folders[i]) < (int)sizeof(to) && copy_bundle_tree(from, to);
    }
    if (okay && !strcmp(platform, "android")) okay = add_android_project(output, name, self_path);
    if (okay && !strcmp(platform, "ios")) okay = add_ios_project(output, name, self_path);
    char contents[1024];
    if (okay) {
        snprintf(contents, sizeof(contents),
            "Hyperian mobile deployment package\nformat: HYMB1\ntoolchain: %s\nruntime interface: 1\napplication: %s\ntarget: mobile\nplatform: %s\nbytecode: application.hyc\nassets: assets, public\n",
            HYPERIAN_VERSION, name, platform);
        okay = write_project_file(manifest, contents);
    }
    if (okay) {
        snprintf(contents, sizeof(contents),
            "# %s\n\nThis is a Hyperian mobile deployment package for %s. It contains compiled MVC bytecode and application assets for Hyperian's native mobile runtime library.\n\nEnglish-like Hyperian source remains the application authority. %s\n",
            name, !strcmp(platform, "ios") ? "iOS" : "Android", !strcmp(platform, "android") ?
            "A self-contained native Android Studio project is in the android folder. Build and sign it with your Android SDK, NDK, and signing key." :
            "A self-contained native Xcode project is in the ios folder. Build and sign it with Xcode and your Apple Developer identity.");
        okay = write_project_file(readme, contents);
    }
    bytecode_free(&code);
    if (!okay) { fprintf(stderr, "error: could not finish mobile export %s\n", output); return 1; }
    printf("Exported %s for %s to %s.\n", source, !strcmp(platform, "ios") ? "iOS" : "Android", output); return 0;
}

static int safe_application_id(const char *value) {
    if (!value || !*value || !strchr(value, '.')) return 0;
    int starts_segment = 1;
    for (const char *at = value; *at; at++) {
        int letter = (*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z');
        int digit = *at >= '0' && *at <= '9';
        if (*at == '.') { if (starts_segment) return 0; starts_segment = 1; continue; }
        if ((starts_segment && !letter) || (!letter && !digit && *at != '_')) return 0;
        starts_segment = 0;
    }
    return !starts_segment;
}

static int safe_team_id(const char *value) {
    if (!value || strlen(value) != 10) return 0;
    for (const char *at = value; *at; at++)
        if (!((*at >= 'A' && *at <= 'Z') || (*at >= '0' && *at <= '9'))) return 0;
    return 1;
}

static int run_mobile_tool(const char *directory, char *const arguments[]) {
    int result = hyperian_run_process(directory, arguments);
    if (result < 0) { fprintf(stderr, "error: cannot start %s: %s\n", arguments[0], strerror(errno)); return 0; }
    if (result) {
        fprintf(stderr, "error: %s did not finish the mobile build successfully\n", arguments[0]); return 0;
    }
    return 1;
}

static int build_android_application(const char *package, const char *kind, const char *output) {
    const char *identifier = getenv("HYPERIAN_APPLICATION_ID"), *keystore = getenv("HYPERIAN_ANDROID_KEYSTORE");
    const char *alias = getenv("HYPERIAN_ANDROID_KEY_ALIAS"), *store_password = getenv("HYPERIAN_ANDROID_STORE_PASSWORD");
    const char *key_password = getenv("HYPERIAN_ANDROID_KEY_PASSWORD"); struct stat info;
    if (!safe_application_id(identifier)) {
        fprintf(stderr, "error: set HYPERIAN_APPLICATION_ID to a reverse-domain name such as com.example.myapp\n"); return 0;
    }
    char resolved_keystore[PATH_MAX];
    if (!keystore || !*keystore || !hyperian_real_path(keystore, resolved_keystore, sizeof(resolved_keystore)) || stat(resolved_keystore, &info) || !S_ISREG(info.st_mode)) {
        fprintf(stderr, "error: set HYPERIAN_ANDROID_KEYSTORE to your release keystore file\n"); return 0;
    }
    if (!alias || !*alias || !store_password || !*store_password || !key_password || !*key_password) {
        fprintf(stderr, "error: set HYPERIAN_ANDROID_KEY_ALIAS, HYPERIAN_ANDROID_STORE_PASSWORD, and HYPERIAN_ANDROID_KEY_PASSWORD\n"); return 0;
    }
    if (hyperian_set_environment("HYPERIAN_ANDROID_KEYSTORE", resolved_keystore)) {
        fprintf(stderr, "error: cannot prepare the Android signing environment: %s\n", strerror(errno)); return 0;
    }
    char project[PATH_MAX], artifact[PATH_MAX];
    if (snprintf(project, sizeof(project), "%s/android", package) >= (int)sizeof(project) ||
        snprintf(artifact, sizeof(artifact), "%s/app/build/outputs/%s/release/app-release.%s", project,
            !strcmp(kind, "apk") ? "apk" : "bundle", kind) >= (int)sizeof(artifact)) {
        fprintf(stderr, "error: Android build path is too long\n"); return 0;
    }
    const char *configured = getenv("HYPERIAN_GRADLE"); char *tool = (char *)(configured && *configured ? configured : "gradle");
    char *arguments[] = {tool, "--no-daemon", !strcmp(kind, "apk") ? ":app:assembleRelease" : ":app:bundleRelease", NULL};
    if (!run_mobile_tool(project, arguments)) return 0;
    if (stat(artifact, &info) || !S_ISREG(info.st_mode)) {
        fprintf(stderr, "error: Android finished without creating the expected signed %s\n", kind); return 0;
    }
    return copy_new_file(artifact, output, 0644);
}

static int write_ios_export_options(const char *path, const char *team, const char *method) {
    char contents[1024]; snprintf(contents, sizeof(contents),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "<key>method</key><string>%s</string>\n<key>signingStyle</key><string>automatic</string>\n"
        "<key>teamID</key><string>%s</string>\n</dict></plist>\n", method, team);
    return write_project_file(path, contents);
}

static int build_ios_application(const char *temporary_root, const char *package, const char *output) {
    const char *identifier = getenv("HYPERIAN_APPLICATION_ID"), *team = getenv("HYPERIAN_IOS_TEAM");
    const char *method = getenv("HYPERIAN_IOS_DISTRIBUTION"); if (!method || !*method) method = "app-store-connect";
    if (!safe_application_id(identifier)) {
        fprintf(stderr, "error: set HYPERIAN_APPLICATION_ID to a reverse-domain name such as com.example.myapp\n"); return 0;
    }
    if (!safe_team_id(team)) { fprintf(stderr, "error: set HYPERIAN_IOS_TEAM to your 10-character Apple development team identifier\n"); return 0; }
    if (strcmp(method, "app-store-connect") && strcmp(method, "ad-hoc") && strcmp(method, "development") && strcmp(method, "enterprise")) {
        fprintf(stderr, "error: HYPERIAN_IOS_DISTRIBUTION must be app-store-connect, ad-hoc, development, or enterprise\n"); return 0;
    }
    char project[PATH_MAX], archive[PATH_MAX], export_directory[PATH_MAX], options[PATH_MAX], artifact[PATH_MAX];
    if (snprintf(project, sizeof(project), "%s/ios", package) >= (int)sizeof(project) ||
        snprintf(archive, sizeof(archive), "%s/HyperianIOS.xcarchive", temporary_root) >= (int)sizeof(archive) ||
        snprintf(export_directory, sizeof(export_directory), "%s/export", temporary_root) >= (int)sizeof(export_directory) ||
        snprintf(options, sizeof(options), "%s/ExportOptions.plist", temporary_root) >= (int)sizeof(options) ||
        snprintf(artifact, sizeof(artifact), "%s/HyperianIOS.ipa", export_directory) >= (int)sizeof(artifact)) {
        fprintf(stderr, "error: iOS build path is too long\n"); return 0;
    }
    if (!write_ios_export_options(options, team, method)) return 0;
    const char *configured = getenv("HYPERIAN_XCODEBUILD"); char *tool = (char *)(configured && *configured ? configured : "xcodebuild");
    char team_setting[64], identifier_setting[512];
    snprintf(team_setting, sizeof(team_setting), "DEVELOPMENT_TEAM=%s", team);
    snprintf(identifier_setting, sizeof(identifier_setting), "PRODUCT_BUNDLE_IDENTIFIER=%s", identifier);
    char *archive_arguments[] = {tool, "-project", "HyperianIOS.xcodeproj", "-scheme", "HyperianIOS", "-configuration", "Release",
        "-archivePath", archive, team_setting, identifier_setting, "-allowProvisioningUpdates", "archive", NULL};
    char *export_arguments[] = {tool, "-exportArchive", "-archivePath", archive, "-exportPath", export_directory,
        "-exportOptionsPlist", options, "-allowProvisioningUpdates", NULL};
    if (!run_mobile_tool(project, archive_arguments) || !run_mobile_tool(project, export_arguments)) return 0;
    struct stat info;
    if (stat(artifact, &info) || !S_ISREG(info.st_mode)) {
        fprintf(stderr, "error: Xcode finished without creating the expected signed IPA\n"); return 0;
    }
    return copy_new_file(artifact, output, 0644);
}

static int build_mobile_application(const char *source, const char *platform, const char *output, const char *self_path) {
    const char *kind = ends_with(output, ".apk") ? "apk" : ends_with(output, ".aab") ? "aab" : ends_with(output, ".ipa") ? "ipa" : NULL;
    if ((!strcmp(platform, "android") && (!kind || !strcmp(kind, "ipa"))) || (!strcmp(platform, "ios") && (!kind || strcmp(kind, "ipa"))) ||
        (strcmp(platform, "android") && strcmp(platform, "ios"))) {
        fprintf(stderr, "error: build Android as a .apk or .aab file, or iOS as a .ipa file\n"); return 2;
    }
    struct stat existing;
    if (!stat(output, &existing)) { fprintf(stderr, "error: mobile application output %s already exists\n", output); return 1; }
    if (errno != ENOENT) { fprintf(stderr, "error: cannot inspect mobile output %s: %s\n", output, strerror(errno)); return 1; }
    char temporary_root[PATH_MAX];
    if (!hyperian_temporary_directory(temporary_root, sizeof(temporary_root), "mobile-build")) { fprintf(stderr, "error: cannot create temporary mobile build folder: %s\n", strerror(errno)); return 1; }
    char package[PATH_MAX]; int okay = snprintf(package, sizeof(package), "%s/package", temporary_root) < (int)sizeof(package) &&
        !export_mobile_application(source, platform, package, self_path);
    if (okay && !strcmp(platform, "android")) okay = build_android_application(package, kind, output);
    if (okay && !strcmp(platform, "ios")) okay = build_ios_application(temporary_root, package, output);
    if (!remove_bundle_tree(temporary_root)) fprintf(stderr, "warning: could not remove temporary mobile build folder %s\n", temporary_root);
    if (!okay) return 1;
    printf("Built signed %s application %s.\n", !strcmp(platform, "ios") ? "iOS" : "Android", output); return 0;
}

static int doctor(void) {
    puts("Hyperian " HYPERIAN_VERSION " toolchain check\n"
         "  compiler and bytecode VM: ready\n"
         "  canonical left-aligned English source: ready\n"
         "  web, installable PWA, API, console, service: ready");
#ifdef HYPERIAN_HAVE_GTK3
    puts("  desktop and mobile preview with English input events (GTK3): ready");
#else
    puts("  desktop and mobile preview (GTK3): not included in this build");
#endif
#ifdef HYPERIAN_HAVE_SDL2
    puts("  game runtime with English physics and animation (SDL2): ready");
    if (hyperian_game_mixer_available()) puts("  compressed effects and streaming music (SDL2_mixer): ready");
    else puts("  compressed effects and streaming music (SDL2_mixer): not installed; WAV effects are ready");
#else
    puts("  game runtime (SDL2): not included in this build");
    puts("  game audio: unavailable because the SDL2 game runtime is not included");
#endif
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
    puts("  PNG, JPEG, and WebP game images (SDL2_image): ready");
#elif defined(HYPERIAN_HAVE_SDL2)
    puts("  PNG, JPEG, and WebP game images (SDL2_image): not included; BMP images are ready");
#else
    puts("  game image loading: unavailable because the SDL2 game runtime is not included");
#endif
#ifdef HYPERIAN_HAVE_SQLITE3
    puts("  SQLite storage: ready");
#else
    puts("  SQLite storage: not included; native HDB storage is ready");
#endif
    puts("  filtered and ordered native model collections: ready");
    puts("  native input, lifecycle, tap, hold, and four-direction swipe events: ready");
#ifdef HYPERIAN_HAVE_CURL
    puts("  HTTP and HTTPS client: ready");
#else
    puts("  HTTP client: basic HTTP only; libcurl HTTPS is not included");
#endif
    const char *platform = host_platform_name();
    if (platform) printf("  native runtime folder and downloadable archive creation (%s): ready\n", platform);
    else puts("  native runtime pack creation: unavailable on this processor");
    puts("  standalone executables and host asset bundles: ready\n"
         "  verified cross-platform executables and asset bundles: ready with a runtime pack\n"
         "  Android and iOS bytecode deployment packages: ready\n"
         "  native mobile runtime bridge library: ready\n"
         "  generated native Android Studio projects: ready\n"
         "  generated native iOS Xcode projects: ready\n"
         "  signed APK, AAB, and IPA build automation: ready when platform SDKs and identities are installed");
    return 0;
}

static int create_project(const char *name, const char *target) {
    static const char *targets[] = {"web", "pwa", "console", "api", "service", "desktop", "mobile", "game"}; int known = 0;
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) if (!strcmp(target, targets[i])) known = 1;
    if (!project_name_is_safe(name)) { fprintf(stderr, "error: use only letters, numbers, hyphens, or underscores in a project name\n"); return 1; }
    if (!known) { fprintf(stderr, "error: target must be web, pwa, console, api, service, desktop, mobile, or game\n"); return 1; }
    char path[1024], source[4096];
    if (!make_project_directory(name)) return 1;
    const char *folders[] = {"models", "controllers", "views", "public", "packages"};
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", name, folders[i]); if (!make_project_directory(path)) return 1;
    }
    const char *network_setup = !strcmp(target, "pwa") ?
        "listen on 8000\nserve files from \"public\" at \"/assets\"\nserve files from \"public\" at \"/\"" :
        (!strcmp(target, "web") || !strcmp(target, "api")) ? "listen on 8000\nserve files from \"public\" at \"/assets\"" : "";
    const char *source_target = !strcmp(target, "pwa") ? "installable web application" : target;
    snprintf(source, sizeof(source), "application \"%s\" is %s\n%s\ninclude \"models/item.hyp\"\ninclude \"controllers/items.hyp\"\n%s",
        name, source_target, network_setup,
        strcmp(target, "api") ? "include \"views/main.hyp\"\n" : "");
    snprintf(path, sizeof(path), "%s/app.hyp", name); if (!write_left_aligned_source(path, source)) return 1;
    snprintf(path, sizeof(path), "%s/models/item.hyp", name);
    if (!write_left_aligned_source(path, "model Item\n    field \"item name\" is text required\n    field \"item kind\" is text\n")) return 1;
    if (!strcmp(target, "web") || !strcmp(target, "pwa")) snprintf(source, sizeof(source),
        "controller Items\n    when someone visits \"/\"\n        find all Item as items\n        show view \"main\" with items\n");
    else if (!strcmp(target, "api")) snprintf(source, sizeof(source),
        "controller Items\n    when someone visits \"/items\"\n        find all Item as items\n        show json items\n");
    else if (!strcmp(target, "desktop") || !strcmp(target, "mobile")) snprintf(source, sizeof(source),
        "controller Items\n    action initialize\n        set status to ready\n        collect every Item \"item name\" ordered by \"item name\" as \"item names\"\n\n    action activate\n        create a Item using the current values as \"item number\"\n        count all Item records as \"item count\"\n        collect every Item \"item name\" ordered by \"item name\" as \"item names\"\n        set status to \"Saved item:\" joined with value called \"item number\"\n        open view \"main\"\n\n    when application starts\n        run action initialize\n        show view \"main\"\n\n    when input \"item name\" changes\n        set status to \"Typing:\" joined with value called \"item name\"\n\n    when input \"item name\" is submitted\n        run action activate\n\n    when window gains focus\n        set status to focused\n\n    when window loses focus\n        set status to unfocused\n%s",
        !strcmp(target, "mobile") ? "\n    when someone taps\n        set status to \"Tapped\"\n\n    when someone presses and holds\n        set status to \"Pressed and held\"\n\n    when someone swipes left\n        set status to \"Swiped left\"\n" : "");
    else if (!strcmp(target, "game")) snprintf(source, sizeof(source),
        "controller Items\n    action initialize\n        set \"player left\" to 100\n        set \"player top\" to 100\n        set \"horizontal speed\" to 0\n        set \"vertical speed\" to 0\n        set \"ball horizontal center\" to 500\n        set \"ball vertical center\" to 320\n        set \"ball radius\" to 45\n        set glow to 0\n        set \"animation frame\" to 1\n\n    action \"move right\"\n        set \"horizontal speed\" to 200\n\n    when application starts\n        run action initialize\n        show view \"main\"\n\n    when player presses right\n        run action \"move right\"\n\n    when game updates\n        move value glow toward 1 at 2 per second\n        advance animation \"animation frame\" from 1 through 4 every 100 milliseconds\n        apply gravity 300 to \"vertical speed\"\n        move position \"player left\" \"player top\" using velocity \"horizontal speed\" \"vertical speed\"\n        keep position \"player left\" \"player top\" inside 960 by 540 sized 100 by 100\n        check whether rectangle at \"player left\" \"player top\" sized 100 by 100 touches circle centered at \"ball horizontal center\" \"ball vertical center\" with radius \"ball radius\" as \"player hit\"\n        check whether line from 0 0 to 960 540 touches circle centered at \"ball horizontal center\" \"ball vertical center\" with radius \"ball radius\" as \"laser hit\"\n        check whether polygon through 430 250 then 530 250 then 480 360 touches circle centered at \"ball horizontal center\" \"ball vertical center\" with radius \"ball radius\" as \"polygon hit\"\n");
    else snprintf(source, sizeof(source),
        "controller Items\n    when application starts\n        find all Item as items\n        show view \"main\" with items\n");
    snprintf(path, sizeof(path), "%s/controllers/items.hyp", name); if (!write_left_aligned_source(path, source)) return 1;
    if (strcmp(target, "api")) {
        snprintf(path, sizeof(path), "%s/views/main.hyp", name);
        const char *view = (!strcmp(target, "desktop") || !strcmp(target, "mobile")) ?
            (!strcmp(target, "mobile") ?
            "view \"main\"\n    heading \"Mobile application\"\n    show the following in a card\n        arrange the following in a column\n            input \"Your name\" as \"item name\"\n            choose \"Item kind\" as \"item kind\"\n                offer \"Personal\" as personal\n                offer \"Work\" as work\n            arrange the following in a row\n                button \"Save item\" runs action activate\n            show status\n    show the following in a card\n        show the following in a table\n            use \"Saved items\" as a table heading\n            for each \"saved item\" in \"item names\" show\n                show the following in a table row\n                    show \"saved item\" in a table cell\n" :
            "view \"main\"\n    heading \"Native desktop application\"\n    show the following in a card\n        arrange the following in a column\n            input \"Your name\" as \"item name\"\n            choose \"Item kind\" as \"item kind\"\n                offer \"Personal\" as personal\n                offer \"Work\" as work\n            arrange the following in a row\n                button \"Save item\" runs action activate\n            show status\n    show the following in a card\n        show the following in a table\n            use \"Saved items\" as a table heading\n            for each \"saved item\" in \"item names\" show\n                show the following in a table row\n                    show \"saved item\" in a table cell\n") :
            !strcmp(target, "game") ?
            "view \"main\"\n    fill background with color 18 24 38\n    draw rectangle at \"player left\" \"player top\" sized 100 by 100 with color 70 170 255\n    draw circle centered at \"ball horizontal center\" \"ball vertical center\" with radius \"ball radius\" and color 255 110 90\n    draw line from 0 0 to 960 540 with color 120 230 160\n    draw polygon through 430 250 then 530 250 then 480 360 with color 180 100 240\n" :
            !strcmp(target, "pwa") ?
            "view \"main\"\n    title \"Installable Hyperian application\"\n    style \"/assets/app.css\"\n    heading \"Installable Hyperian application\"\n    arrange the following in a row\n        show the following in a card\n            text \"This English MVC application works online.\"\n        show the following in a card\n            text \"It can also be installed.\"\n" :
            "view \"main\"\n    heading \"Welcome to Hyperian\"\n    arrange the following in a row\n        show the following in a card\n            text \"Your foldered MVC application is ready.\"\n        show the following in a card\n            text \"Add your next view here.\"\n";
        if (!write_left_aligned_source(path, view)) return 1;
    }
    snprintf(path, sizeof(path), "%s/public/app.css", name);
    if (!write_project_file(path, "body { font-family: system-ui, sans-serif; margin: 3rem auto; max-width: 48rem; }\n")) return 1;
    if (!strcmp(target, "pwa")) {
        snprintf(path, sizeof(path), "%s/public/manifest.webmanifest", name);
        snprintf(source, sizeof(source), "{\n  \"name\": \"%s\",\n  \"short_name\": \"%s\",\n  \"start_url\": \"/\",\n  \"display\": \"standalone\",\n  \"background_color\": \"#111827\",\n  \"theme_color\": \"#2563eb\",\n  \"icons\": [{\"src\": \"/assets/icon.svg\", \"sizes\": \"any\", \"type\": \"image/svg+xml\", \"purpose\": \"any maskable\"}]\n}\n", name, name);
        if (!write_project_file(path, source)) return 1;
        snprintf(path, sizeof(path), "%s/public/service-worker.js", name);
        if (!write_project_file(path, "const CACHE='hyperian-pwa-v1';\nconst CORE=['/','/assets/app.css','/assets/manifest.webmanifest','/assets/icon.svg'];\nself.addEventListener('install',event=>event.waitUntil(caches.open(CACHE).then(cache=>cache.addAll(CORE)).then(()=>self.skipWaiting())));\nself.addEventListener('activate',event=>event.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(key=>key!==CACHE).map(key=>caches.delete(key)))).then(()=>self.clients.claim())));\nself.addEventListener('fetch',event=>{if(event.request.method!=='GET')return;event.respondWith(fetch(event.request).then(response=>{const copy=response.clone();caches.open(CACHE).then(cache=>cache.put(event.request,copy));return response}).catch(()=>caches.match(event.request).then(cached=>cached||caches.match('/'))))});\n")) return 1;
        snprintf(path, sizeof(path), "%s/public/icon.svg", name);
        if (!write_project_file(path, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\"><rect width=\"512\" height=\"512\" rx=\"96\" fill=\"#2563eb\"/><path d=\"M128 112h72v108h112V112h72v288h-72V286H200v114h-72z\" fill=\"white\"/></svg>\n")) return 1;
    }
    snprintf(path, sizeof(path), "%s/README.md", name);
    snprintf(source, sizeof(source), "# %s\n\nRun this %s with:\n\n    hyperian run app.hyp\n", name,
        !strcmp(target, "pwa") ? "installable web application" : target);
    if (!write_project_file(path, source)) return 1;
    printf("Created %s as a foldered Hyperian %s.\n", name,
        !strcmp(target, "pwa") ? "installable web application" : target); return 0;
}

static int format_source(const char *path, int write_back) {
    FILE *file = fopen(path, "r"); if (!file) { fprintf(stderr, "error: cannot open %s\n", path); return 1; }
    FILE *output = stdout; char temporary[PATH_MAX] = {0}; int mode = 0644;
    if (write_back) {
        struct stat information; if (!stat(path, &information)) mode = information.st_mode;
        int descriptor = hyperian_temporary_sibling(temporary, sizeof(temporary), path, "format");
        output = descriptor >= 0 ? fdopen(descriptor, "w") : NULL;
        if (!output) { if (descriptor >= 0) close(descriptor); fclose(file); fprintf(stderr, "error: cannot create formatted source near %s\n", path); return 1; }
    }
    char line[4096]; int has_closing_phrases = 0;
    while (fgets(line, sizeof(line), file)) {
        char *start = line; while (*start == ' ' || *start == '\t') start++;
        start[strcspn(start, "\r\n")] = 0;
        if (!strcmp(start, "end") || !strcmp(start, "that is all")) { has_closing_phrases = 1; break; }
    }
    rewind(file); unsigned widths[64] = {0}; int level = 0;
    while (fgets(line, sizeof(line), file)) {
        char *start = line; unsigned width = 0;
        while (*start == ' ' || *start == '\t') { width += *start == '\t' ? 4 : 1; start++; }
        start[strcspn(start, "\r\n")] = 0;
        if (!*start) { fputc('\n', output); continue; }
        if (!has_closing_phrases) {
            int branch = !strcmp(start, "otherwise") || !strncmp(start, "when it fails as ", 17);
            while (level > 0 && width < widths[level]) {
                level--;
                if (!(branch && width == widths[level])) fputs("that is all\n", output);
            }
            if (width > widths[level] && level + 1 < 64) widths[++level] = width;
        }
        fprintf(output, "%s\n", !strcmp(start, "end") ? "that is all" : start);
    }
    while (!has_closing_phrases && level > 0) { fputs("that is all\n", output); level--; }
    int failed = ferror(file) || ferror(output); fclose(file);
    if (write_back) {
        if (fclose(output)) failed = 1;
        if (!failed && chmod(temporary, mode & 0777)) failed = 1;
        if (!failed && hyperian_replace_file(temporary, path)) {
            fprintf(stderr, "error: cannot replace %s with its formatted source: %s\n", path, strerror(errno)); failed = 1;
        }
        if (failed) unlink(temporary);
        else printf("Formatted %s as left-aligned Hyperian.\n", path);
    }
    return failed ? 1 : 0;
}

static void help(void) {
    puts("Hyperian " HYPERIAN_VERSION " - English-like MVC for every kind of app\n"
         "\n"
         "Usage:\n"
         "  hyperian run app.hyp                  Run your English source application\n"
         "  hyperian help run                     Explain how .hyp applications run\n"
         "  hyperian compile app.hyp -o app.hyc   Compile source to bytecode\n"
         "  hyperian build app.hyp -o MyApp       Build one executable application\n"
         "  hyperian build app.hyp for android as App.aab\n"
         "                                          Build and sign a phone application\n"
         "  hyperian build app.hyp for PLATFORM using RuntimePack as App\n"
         "                                          Build with another native runtime\n"
         "  hyperian bundle app.hyp -o App        Bundle an executable and assets\n"
         "  hyperian bundle app.hyp for PLATFORM using RuntimePack to App\n"
         "                                          Bundle for another native runtime\n"
         "  hyperian pack runtime to RuntimePack  Create a runtime-pack folder\n"
         "  hyperian pack runtime as Runtime.hyr  Create a downloadable runtime archive\n"
         "  hyperian export app.hyp for android to App\n"
         "                                          Export a phone deployment package\n"
         "  hyperian new MyApp [--target web]     Create a foldered MVC project\n"
         "  hyperian check app.hyp                Check source for mistakes\n"
         "  hyperian run app.hyc [--port 9000]    Run compiled bytecode\n"
         "  hyperian debug app.hyp                Trace its start event and state\n"
         "  hyperian debug app.hyp --event PHRASE Trace an event such as \"input name changes\"\n"
         "  hyperian debug app.hyp --action NAME  Trace a particular action\n"
         "  hyperian inspect app.hyc              Show compiled instructions\n"
         "  hyperian format app.hyp               Print canonical left-aligned source\n"
         "  hyperian format app.hyp --write       Rewrite source in canonical form\n"
         "  hyperian test app.hyp                 Run English test blocks\n"
         "  hyperian migrate app.hyp              Apply pending data migrations\n"
         "  hyperian doctor                       Check available native backends\n"
         "  hyperian platform                     Show this runtime's native platform\n"
         "  hyperian version                      Show the version\n");
}

static void run_help(void) {
    puts("Run a Hyperian .hyp source program\n"
         "\n"
         "Your .hyp file contains the left-aligned English MVC source. Run it directly:\n"
         "  hyperian run app.hyp\n"
         "  hyperian run MyProject/app.hyp\n"
         "\n"
         "For a web, installable web, or API application, you may choose a port:\n"
         "  hyperian run app.hyp --port 9000\n"
         "\n"
         "Hyperian compiles the .hyp file to temporary bytecode and starts the correct runtime.\n"
         "Console and service apps use the terminal. Web and API apps print their local address.\n"
         "Desktop and mobile-preview apps open a native window. Game apps open a game window.\n"
         "Use 'hyperian doctor' if a native window or game cannot start.\n"
         "\n"
         "You do not need a .hyr file to run a .hyp file on this computer. A .hyr file is a\n"
         "downloadable native runtime pack used when building for a different platform.\n");
}

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix); return a >= b && !strcmp(text + a - b, suffix);
}

static int run_application_file(const char *path, int port) {
    if (ends_with(path, ".hyp")) {
        char temporary[128]; if (!temporary_bytecode(temporary, sizeof(temporary))) return 1;
        int result = compile_file(path, temporary);
        if (!result) result = run_bytecode(temporary, port);
        unlink(temporary); return result;
    }
    return run_bytecode(path, port);
}

int main(int argc, char **argv) {
    int embedded = 0, embedded_result = run_embedded_program(argc, argv, &embedded); if (embedded) return embedded_result;
    if (argc >= 2 && (ends_with(argv[1], ".hyp") || ends_with(argv[1], ".hyc"))) {
        int port = 0;
        if (argc == 4 && !strcmp(argv[2], "--port")) port = atoi(argv[3]);
        else if (argc != 2) { fprintf(stderr, "usage: hyperian app.hyp [--port 9000]\n"); return 2; }
        if (port < 0 || port > 65535) { fprintf(stderr, "error: invalid port\n"); return 2; }
        return run_application_file(argv[1], port);
    }
    if ((argc == 3 && !strcmp(argv[1], "help") && !strcmp(argv[2], "run")) ||
        (argc == 3 && !strcmp(argv[1], "run") && (!strcmp(argv[2], "--help") || !strcmp(argv[2], "-h")))) {
        run_help(); return 0;
    }
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) { help(); return 0; }
    if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) { puts("Hyperian " HYPERIAN_VERSION); return 0; }
    if (!strcmp(argv[1], "platform")) {
        if (argc != 2) { fprintf(stderr, "usage: hyperian platform\n"); return 2; }
        const char *platform = host_platform_name();
        if (!platform) { fprintf(stderr, "error: this operating system or processor does not have a Hyperian platform name yet\n"); return 1; }
        puts(platform); return 0;
    }
    if (!strcmp(argv[1], "doctor")) {
        if (argc != 2) { fprintf(stderr, "usage: hyperian doctor\n"); return 2; }
        return doctor();
    }
    if (!strcmp(argv[1], "new")) {
        if (argc != 3 && !(argc == 5 && !strcmp(argv[3], "--target"))) {
            fprintf(stderr, "usage: hyperian new MyApp [--target web]\n"); return 2;
        }
        return create_project(argv[2], argc == 5 ? argv[4] : "web");
    }
    if (!strcmp(argv[1], "compile")) {
        if (argc != 5 || strcmp(argv[3], "-o")) { fprintf(stderr, "usage: hyperian compile app.hyp -o app.hyc\n"); return 2; }
        return compile_file(argv[2], argv[4]);
    }
    if (!strcmp(argv[1], "build")) {
        if (argc == 5 && !strcmp(argv[3], "-o")) return build_executable(argv[2], argv[4], argv[0], 0);
        if (argc == 7 && !strcmp(argv[3], "for") && !strcmp(argv[5], "as"))
            return build_mobile_application(argv[2], argv[4], argv[6], argv[0]);
        if (argc == 9 && !strcmp(argv[3], "for") && !strcmp(argv[5], "using") && !strcmp(argv[7], "as"))
            return build_for_native_platform(argv[2], argv[4], argv[6], argv[8]);
        fprintf(stderr, "usage: hyperian build app.hyp -o MyApp\n"
                        "   or: hyperian build app.hyp for android as App.aab\n"
                        "   or: hyperian build app.hyp for PLATFORM using RuntimePack as MyApp\n"); return 2;
    }
    if (!strcmp(argv[1], "pack")) {
        if (argc == 5 && !strcmp(argv[2], "runtime") && !strcmp(argv[3], "to")) return create_runtime_pack(argv[4], argv[0]);
        if (argc == 5 && !strcmp(argv[2], "runtime") && !strcmp(argv[3], "as")) return create_runtime_archive(argv[4], argv[0]);
        fprintf(stderr, "usage: hyperian pack runtime to RuntimePack\n"
                        "   or: hyperian pack runtime as Runtime.hyr\n"); return 2;
    }
    if (!strcmp(argv[1], "bundle")) {
        if (argc == 5 && !strcmp(argv[3], "-o")) return bundle_application(argv[2], argv[4], argv[0]);
        if (argc == 9 && !strcmp(argv[3], "for") && !strcmp(argv[5], "using") && !strcmp(argv[7], "to"))
            return bundle_for_native_platform(argv[2], argv[4], argv[6], argv[8]);
        fprintf(stderr, "usage: hyperian bundle app.hyp -o App\n"
                        "   or: hyperian bundle app.hyp for PLATFORM using RuntimePack to App\n"); return 2;
    }
    if (!strcmp(argv[1], "export")) {
        if (argc != 7 || strcmp(argv[3], "for") || strcmp(argv[5], "to")) {
            fprintf(stderr, "usage: hyperian export app.hyp for android to App\n"); return 2;
        }
        return export_mobile_application(argv[2], argv[4], argv[6], argv[0]);
    }
    if (!strcmp(argv[1], "check")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian check app.hyp\n"); return 2; }
        char temporary[128]; if (!temporary_bytecode(temporary, sizeof(temporary))) return 1;
        int result = compile_file(argv[2], temporary); unlink(temporary);
        if (!result) puts("No mistakes found.");
        return result;
    }
    if (!strcmp(argv[1], "inspect")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian inspect app.hyc\n"); return 2; }
        return inspect_bytecode(argv[2]);
    }
    if (!strcmp(argv[1], "debug")) {
        if (argc < 3) { fprintf(stderr, "usage: hyperian debug app.hyp [--event NAME | --action NAME] [--input VALUE]\n"); return 2; }
        const char *event = "START", *action = NULL, *input = NULL;
        for (int i = 3; i < argc; i += 2) {
            if (i + 1 >= argc) { fprintf(stderr, "error: %s needs a value\n", argv[i]); return 2; }
            if (!strcmp(argv[i], "--event") && !action) event = argv[i + 1];
            else if (!strcmp(argv[i], "--action") && event && !strcmp(event, "START")) { action = argv[i + 1]; event = NULL; }
            else if (!strcmp(argv[i], "--input")) input = argv[i + 1];
            else { fprintf(stderr, "error: use either --event NAME or --action NAME, with optional --input VALUE\n"); return 2; }
        }
        if (input && !action) { fprintf(stderr, "error: --input can only be used with --action\n"); return 2; }
        if (ends_with(argv[2], ".hyc")) return debug_bytecode(argv[2], event, action, input);
        char temporary[128]; if (!temporary_bytecode(temporary, sizeof(temporary))) return 1;
        int result = compile_file(argv[2], temporary);
        if (!result) result = debug_bytecode(temporary, event, action, input);
        unlink(temporary); return result;
    }
    if (!strcmp(argv[1], "format")) {
        if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "--write"))) {
            fprintf(stderr, "usage: hyperian format app.hyp [--write]\n"); return 2;
        }
        return format_source(argv[2], argc == 4);
    }
    if (!strcmp(argv[1], "test")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian test app.hyp\n"); return 2; }
        if (ends_with(argv[2], ".hyc")) return test_bytecode(argv[2]);
        char temporary[128]; if (!temporary_bytecode(temporary, sizeof(temporary))) return 1;
        int result = compile_file(argv[2], temporary);
        if (!result) result = test_bytecode(temporary);
        unlink(temporary); return result;
    }
    if (!strcmp(argv[1], "migrate")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian migrate app.hyp\n"); return 2; }
        if (ends_with(argv[2], ".hyc")) return migrate_bytecode(argv[2]);
        char temporary[128]; if (!temporary_bytecode(temporary, sizeof(temporary))) return 1;
        int result = compile_file(argv[2], temporary);
        if (!result) result = migrate_bytecode(temporary);
        unlink(temporary); return result;
    }
    if (!strcmp(argv[1], "run")) {
        if (argc < 3) { run_help(); return 2; }
        if (ends_with(argv[2], ".hyr")) {
            fprintf(stderr, "error: a .hyr file is a native runtime pack, not an application\n"
                            "run your English source instead: hyperian run app.hyp\n"); return 2;
        }
        if (!ends_with(argv[2], ".hyp") && !ends_with(argv[2], ".hyc")) {
            fprintf(stderr, "error: run a Hyperian .hyp source file or a compiled .hyc bytecode file\n"); return 2;
        }
        int port = 0;
        if (argc == 5 && !strcmp(argv[3], "--port")) port = atoi(argv[4]);
        else if (argc != 3) { fprintf(stderr, "usage: hyperian run app.hyp [--port 9000]\n"); return 2; }
        if (port < 0 || port > 65535) { fprintf(stderr, "error: invalid port\n"); return 2; }
        return run_application_file(argv[2], port);
    }
    fprintf(stderr, "error: unknown command %s\n", argv[1]); help(); return 2;
}
