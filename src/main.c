#include "hyperian.h"

#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int starts_with(const char *text, const char *prefix) { return !strncmp(text, prefix, strlen(prefix)); }

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
    FILE *file = fopen("/proc/self/exe", "rb"); return file ? file : fopen(fallback, "rb");
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

static int build_executable(const char *source, const char *output, const char *self_path, int bundled) {
    char bytecode_path[128]; snprintf(bytecode_path, sizeof(bytecode_path), "/tmp/hyperian-build-%ld.hyc", (long)getpid());
    int result = compile_file(source, bytecode_path); if (result) return result;
    FILE *runtime = running_executable(self_path), *bytecode = fopen(bytecode_path, "rb");
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

static int enter_bundle_directory(const char *fallback) {
    char path[PATH_MAX]; ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length < 0) {
        if (!realpath(fallback, path)) { fprintf(stderr, "error: cannot locate this application bundle\n"); return 0; }
    } else path[length] = 0;
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
    char path[128]; snprintf(path, sizeof(path), "/tmp/hyperian-embedded-%ld.hyc", (long)getpid()); FILE *bytecode = fopen(path, "wb");
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

static int copy_bundle_file(const char *source, const char *destination, mode_t mode) {
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

static int copy_bundle_tree(const char *source, const char *destination) {
    struct stat info;
    if (lstat(source, &info)) return errno == ENOENT;
    if (!S_ISDIR(info.st_mode)) { fprintf(stderr, "error: bundle asset %s must be a folder\n", source); return 0; }
    if (mkdir(destination, info.st_mode & 0777)) { fprintf(stderr, "error: cannot create bundle folder %s: %s\n", destination, strerror(errno)); return 0; }
    DIR *directory = opendir(source); if (!directory) return 0; int okay = 1; struct dirent *entry;
    while (okay && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char from[PATH_MAX], to[PATH_MAX];
        if (snprintf(from, sizeof(from), "%s/%s", source, entry->d_name) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", destination, entry->d_name) >= (int)sizeof(to)) { okay = 0; break; }
        if (lstat(from, &info)) { okay = 0; break; }
        if (S_ISLNK(info.st_mode)) { fprintf(stderr, "error: bundle assets cannot contain symbolic links: %s\n", from); okay = 0; }
        else if (S_ISDIR(info.st_mode)) okay = copy_bundle_tree(from, to);
        else if (S_ISREG(info.st_mode)) okay = copy_bundle_file(from, to, info.st_mode);
        else { fprintf(stderr, "error: unsupported bundle asset %s\n", from); okay = 0; }
    }
    closedir(directory); return okay;
}

static void source_directory(const char *source, char *directory, size_t size) {
    const char *slash = strrchr(source, '/');
    if (!slash) snprintf(directory, size, ".");
    else if (slash == source) snprintf(directory, size, "/");
    else { size_t length = (size_t)(slash - source); if (length >= size) length = size - 1; memcpy(directory, source, length); directory[length] = 0; }
}

static int bundle_application(const char *source, const char *output, const char *self_path) {
    struct stat existing;
    if (!stat(output, &existing)) { fprintf(stderr, "error: bundle output %s already exists\n", output); return 1; }
    if (errno != ENOENT || mkdir(output, 0755)) { fprintf(stderr, "error: cannot create bundle %s: %s\n", output, strerror(errno)); return 1; }
    char executable[PATH_MAX], manifest[PATH_MAX], project[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    if (snprintf(executable, sizeof(executable), "%s/run", output) >= (int)sizeof(executable) ||
        snprintf(manifest, sizeof(manifest), "%s/hyperian.bundle", output) >= (int)sizeof(manifest)) {
        fprintf(stderr, "error: bundle path is too long\n"); return 1;
    }
    if (build_executable(source, executable, self_path, 1)) return 1;
    source_directory(source, project, sizeof(project));
    const char *folders[] = {"assets", "public"};
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", project, folders[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", output, folders[i]) >= (int)sizeof(to) || !copy_bundle_tree(from, to)) return 1;
    }
    char contents[512]; snprintf(contents, sizeof(contents),
        "Hyperian application bundle\nformat: HYBN1\ntoolchain: %s\nexecutable: run\nassets: assets, public\n", HYPERIAN_VERSION);
    if (!write_project_file(manifest, contents)) return 1;
    printf("Bundled %s with its assets in %s.\n", source, output); return 0;
}

static int resource_folders(const char *self_path, const char *platform, char *adapter, size_t adapter_size, char *runtime, size_t runtime_size) {
    char executable[PATH_MAX];
    struct stat installed;
    if (realpath(self_path, executable)) {
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
    char template[PATH_MAX], runtime[PATH_MAX], destination[PATH_MAX], from[PATH_MAX], to[PATH_MAX];
    if (!resource_folders(self_path, "android", template, sizeof(template), runtime, sizeof(runtime))) {
        fprintf(stderr, "error: cannot find Hyperian's installed Android adapter resources\n"); return 0;
    }
    if (snprintf(destination, sizeof(destination), "%s/android", output) >= (int)sizeof(destination) ||
        !copy_bundle_tree(template, destination)) return 0;
    const char *sources[] = {"mobile.c", "runtime.c", "bytecode.c", "network.c", "security.c", "security.h", "desktop.c", "game.c", "hyperian.h"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        if (snprintf(from, sizeof(from), "%s/%s", runtime, sources[i]) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/android/app/src/main/cpp/hyperian/%s", output, sources[i]) >= (int)sizeof(to) ||
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
    char temporary[128]; snprintf(temporary, sizeof(temporary), "/tmp/hyperian-mobile-%ld.hyc", (long)getpid());
    int result = compile_file(source, temporary); if (result) return result;
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

static int doctor(void) {
    puts("Hyperian " HYPERIAN_VERSION " toolchain check\n"
         "  compiler and bytecode VM: ready\n"
         "  web, installable PWA, API, console, service: ready");
#ifdef HYPERIAN_HAVE_GTK3
    puts("  desktop and mobile preview (GTK3): ready");
#else
    puts("  desktop and mobile preview (GTK3): not included in this build");
#endif
#ifdef HYPERIAN_HAVE_SDL2
    puts("  game runtime with English physics (SDL2): ready");
#else
    puts("  game runtime (SDL2): not included in this build");
#endif
#ifdef HYPERIAN_HAVE_SQLITE3
    puts("  SQLite storage: ready");
#else
    puts("  SQLite storage: not included; native HDB storage is ready");
#endif
#ifdef HYPERIAN_HAVE_CURL
    puts("  HTTP and HTTPS client: ready");
#else
    puts("  HTTP client: basic HTTP only; libcurl HTTPS is not included");
#endif
    puts("  standalone executables and asset bundles: ready\n"
         "  Android and iOS bytecode deployment packages: ready\n"
         "  native mobile runtime bridge library: ready\n"
         "  generated native Android Studio projects: ready\n"
         "  generated native iOS Xcode projects: ready\n"
         "  signed store applications: not implemented yet");
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
    snprintf(path, sizeof(path), "%s/app.hyp", name); if (!write_project_file(path, source)) return 1;
    snprintf(path, sizeof(path), "%s/models/item.hyp", name);
    if (!write_project_file(path, "model Item\n    field name is text required\nend\n")) return 1;
    if (!strcmp(target, "web") || !strcmp(target, "pwa")) snprintf(source, sizeof(source),
        "controller Items\n    when someone visits \"/\"\n        find all Item as items\n        show view \"main\" with items\n    end\nend\n");
    else if (!strcmp(target, "api")) snprintf(source, sizeof(source),
        "controller Items\n    when someone visits \"/items\"\n        find all Item as items\n        show json items\n    end\nend\n");
    else if (!strcmp(target, "desktop") || !strcmp(target, "mobile")) snprintf(source, sizeof(source),
        "controller Items\n    action initialize\n        set status to ready\n        collect every Item name as item_names\n    end\n    action activate\n        create a Item using the current values as item_id\n        count all Item records as item_count\n        collect every Item name as item_names\n        set status to \"Saved item:\" joined with item_id\n        open view \"main\"\n    end\n    when application starts\n        run action initialize\n        show view \"main\"\n    end\nend\n");
    else if (!strcmp(target, "game")) snprintf(source, sizeof(source),
        "controller Items\n    action initialize\n        set player_x to 100\n        set player_y to 100\n        set player_velocity_x to 0\n        set player_velocity_y to 0\n    end\n    action \"move right\"\n        set player_velocity_x to 200\n    end\n    when application starts\n        run action initialize\n        show view \"main\"\n    end\n    when player presses right\n        run action \"move right\"\n    end\n    when game updates\n        apply gravity 300 to player_velocity_y\n        move position player_x player_y using velocity player_velocity_x player_velocity_y\n        keep position player_x player_y inside 960 by 540 sized 100 by 100\n        check collision between player_x player_y sized 100 by 100 and 400 260 sized 120 by 120 as player_hit\n    end\nend\n");
    else snprintf(source, sizeof(source),
        "controller Items\n    when application starts\n        find all Item as items\n        show view \"main\" with items\n    end\nend\n");
    snprintf(path, sizeof(path), "%s/controllers/items.hyp", name); if (!write_project_file(path, source)) return 1;
    if (strcmp(target, "api")) {
        snprintf(path, sizeof(path), "%s/views/main.hyp", name);
        const char *view = (!strcmp(target, "desktop") || !strcmp(target, "mobile")) ?
            (!strcmp(target, "mobile") ?
            "view \"main\"\n    heading \"Mobile application\"\n    input \"Your name\" as name\n    button \"Save item\" runs action activate\n    show status\n    for each item_name in item_names show\n        show item_name\n    end\nend\n" :
            "view \"main\"\n    heading \"Native desktop application\"\n    input \"Your name\" as name\n    button \"Save item\" runs action activate\n    show status\n    for each item_name in item_names show\n        show item_name\n    end\nend\n") :
            !strcmp(target, "game") ?
            "view \"main\"\n    fill background with color 18 24 38\n    draw rectangle at player_x player_y sized 100 by 100 with color 70 170 255\n    draw rectangle at 400 260 sized 120 by 120 with color 255 110 90\nend\n" :
            !strcmp(target, "pwa") ?
            "view \"main\"\n    title \"Installable Hyperian application\"\n    style \"/assets/app.css\"\n    heading \"Installable Hyperian application\"\n    text \"This English MVC application works online and can be installed.\"\nend\n" :
            "view \"main\"\n    heading \"Welcome to Hyperian\"\n    text \"Your foldered MVC application is ready.\"\nend\n";
        if (!write_project_file(path, view)) return 1;
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

static int format_source(const char *path) {
    FILE *file = fopen(path, "r"); if (!file) { fprintf(stderr, "error: cannot open %s\n", path); return 1; }
    char line[4096]; int indentation = 0;
    while (fgets(line, sizeof(line), file)) {
        char *start = line; while (*start == ' ' || *start == '\t') start++;
        start[strcspn(start, "\r\n")] = 0;
        if (!*start) { putchar('\n'); continue; }
        int closing = !strcmp(start, "end") || !strcmp(start, "otherwise") || starts_with(start, "when it fails as ");
        if (closing && indentation) indentation--;
        for (int i = 0; i < indentation * 4; i++) putchar(' ');
        puts(start);
        int opening = starts_with(start, "model ") || starts_with(start, "controller ") || starts_with(start, "view ") ||
            starts_with(start, "layout ") || starts_with(start, "component ") || starts_with(start, "action ") ||
            starts_with(start, "test ") ||
            starts_with(start, "when data changes from ") ||
            starts_with(start, "when someone ") || starts_with(start, "when player presses ") || !strcmp(start, "when game updates") ||
            !strcmp(start, "when window closes") ||
            starts_with(start, "every ") ||
            !strcmp(start, "when application starts") || starts_with(start, "form ") ||
            starts_with(start, "for each ") || starts_with(start, "if ") || starts_with(start, "repeat ") || !strcmp(start, "try") ||
            !strcmp(start, "otherwise") || starts_with(start, "when it fails as ");
        if (opening) indentation++;
    }
    int failed = ferror(file); fclose(file); return failed ? 1 : 0;
}

static void help(void) {
    puts("Hyperian " HYPERIAN_VERSION " - English-like MVC for every kind of app\n"
         "\n"
         "Usage:\n"
         "  hyperian compile app.hyp -o app.hyc   Compile source to bytecode\n"
         "  hyperian build app.hyp -o MyApp       Build one executable application\n"
         "  hyperian bundle app.hyp -o App        Bundle an executable and assets\n"
         "  hyperian export app.hyp for android to App\n"
         "                                          Export a phone deployment package\n"
         "  hyperian new MyApp [--target web]     Create a foldered MVC project\n"
         "  hyperian check app.hyp                Check source for mistakes\n"
         "  hyperian run app.hyp                  Compile and run an app\n"
         "  hyperian run app.hyc [--port 9000]    Run compiled bytecode\n"
         "  hyperian debug app.hyp                Trace its start event and state\n"
         "  hyperian debug app.hyp --event NAME   Trace a particular event\n"
         "  hyperian debug app.hyp --action NAME  Trace a particular action\n"
         "  hyperian inspect app.hyc              Show compiled instructions\n"
         "  hyperian format app.hyp               Print consistently indented source\n"
         "  hyperian test app.hyp                 Run English test blocks\n"
         "  hyperian migrate app.hyp              Apply pending data migrations\n"
         "  hyperian doctor                       Check available native backends\n"
         "  hyperian version                      Show the version\n");
}

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix); return a >= b && !strcmp(text + a - b, suffix);
}

int main(int argc, char **argv) {
    int embedded = 0, embedded_result = run_embedded_program(argc, argv, &embedded); if (embedded) return embedded_result;
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) { help(); return 0; }
    if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) { puts("Hyperian " HYPERIAN_VERSION); return 0; }
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
        if (argc != 5 || strcmp(argv[3], "-o")) { fprintf(stderr, "usage: hyperian build app.hyp -o MyApp\n"); return 2; }
        return build_executable(argv[2], argv[4], argv[0], 0);
    }
    if (!strcmp(argv[1], "bundle")) {
        if (argc != 5 || strcmp(argv[3], "-o")) { fprintf(stderr, "usage: hyperian bundle app.hyp -o App\n"); return 2; }
        return bundle_application(argv[2], argv[4], argv[0]);
    }
    if (!strcmp(argv[1], "export")) {
        if (argc != 7 || strcmp(argv[3], "for") || strcmp(argv[5], "to")) {
            fprintf(stderr, "usage: hyperian export app.hyp for android to App\n"); return 2;
        }
        return export_mobile_application(argv[2], argv[4], argv[6], argv[0]);
    }
    if (!strcmp(argv[1], "check")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian check app.hyp\n"); return 2; }
        char temporary[128]; snprintf(temporary, sizeof(temporary), "/tmp/hyperian-check-%ld.hyc", (long)getpid());
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
        char temporary[128]; snprintf(temporary, sizeof(temporary), "/tmp/hyperian-debug-%ld.hyc", (long)getpid());
        int result = compile_file(argv[2], temporary);
        if (!result) result = debug_bytecode(temporary, event, action, input);
        unlink(temporary); return result;
    }
    if (!strcmp(argv[1], "format")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian format app.hyp\n"); return 2; }
        return format_source(argv[2]);
    }
    if (!strcmp(argv[1], "test")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian test app.hyp\n"); return 2; }
        if (ends_with(argv[2], ".hyc")) return test_bytecode(argv[2]);
        char temporary[128]; snprintf(temporary, sizeof(temporary), "/tmp/hyperian-test-%ld.hyc", (long)getpid());
        int result = compile_file(argv[2], temporary);
        if (!result) result = test_bytecode(temporary);
        unlink(temporary); return result;
    }
    if (!strcmp(argv[1], "migrate")) {
        if (argc != 3) { fprintf(stderr, "usage: hyperian migrate app.hyp\n"); return 2; }
        if (ends_with(argv[2], ".hyc")) return migrate_bytecode(argv[2]);
        char temporary[128]; snprintf(temporary, sizeof(temporary), "/tmp/hyperian-migrate-%ld.hyc", (long)getpid());
        int result = compile_file(argv[2], temporary);
        if (!result) result = migrate_bytecode(temporary);
        unlink(temporary); return result;
    }
    if (!strcmp(argv[1], "run")) {
        if (argc < 3) { fprintf(stderr, "usage: hyperian run app.hyp [--port 9000]\n"); return 2; }
        int port = 0;
        if (argc == 5 && !strcmp(argv[3], "--port")) port = atoi(argv[4]);
        else if (argc != 3) { fprintf(stderr, "usage: hyperian run app.hyp [--port 9000]\n"); return 2; }
        if (port < 0 || port > 65535) { fprintf(stderr, "error: invalid port\n"); return 2; }
        if (ends_with(argv[2], ".hyp")) {
            char temporary[128]; snprintf(temporary, sizeof(temporary), "/tmp/hyperian-run-%ld.hyc", (long)getpid());
            int result = compile_file(argv[2], temporary);
            if (!result) result = run_bytecode(temporary, port);
            unlink(temporary); return result;
        }
        return run_bytecode(argv[2], port);
    }
    fprintf(stderr, "error: unknown command %s\n", argv[1]); help(); return 2;
}
