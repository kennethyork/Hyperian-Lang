#include "hyperian.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int starts_with(const char *text, const char *prefix) { return !strncmp(text, prefix, strlen(prefix)); }

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

static int create_project(const char *name, const char *target) {
    static const char *targets[] = {"web", "console", "api", "service", "desktop", "game"}; int known = 0;
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) if (!strcmp(target, targets[i])) known = 1;
    if (!project_name_is_safe(name)) { fprintf(stderr, "error: use only letters, numbers, hyphens, or underscores in a project name\n"); return 1; }
    if (!known) { fprintf(stderr, "error: target must be web, console, api, service, desktop, or game\n"); return 1; }
    char path[1024], source[4096];
    if (!make_project_directory(name)) return 1;
    const char *folders[] = {"models", "controllers", "views", "public"};
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", name, folders[i]); if (!make_project_directory(path)) return 1;
    }
    snprintf(source, sizeof(source), "application \"%s\" is %s\n%s\ninclude \"models/item.hyp\"\ninclude \"controllers/items.hyp\"\n%s",
        name, target, (!strcmp(target, "web") || !strcmp(target, "api")) ? "listen on 8000\nserve files from \"public\" at \"/assets\"" : "",
        strcmp(target, "api") ? "include \"views/main.hyp\"\n" : "");
    snprintf(path, sizeof(path), "%s/app.hyp", name); if (!write_project_file(path, source)) return 1;
    snprintf(path, sizeof(path), "%s/models/item.hyp", name);
    if (!write_project_file(path, "model Item\n    field name is text required\nend\n")) return 1;
    if (!strcmp(target, "web")) snprintf(source, sizeof(source),
        "controller Items\n    when someone visits \"/\"\n        find all Item as items\n        show view \"main\" with items\n    end\nend\n");
    else if (!strcmp(target, "api")) snprintf(source, sizeof(source),
        "controller Items\n    when someone visits \"/items\"\n        find all Item as items\n        show json items\n    end\nend\n");
    else snprintf(source, sizeof(source),
        "controller Items\n    when application starts\n        find all Item as items\n        show view \"main\" with items\n    end\nend\n");
    snprintf(path, sizeof(path), "%s/controllers/items.hyp", name); if (!write_project_file(path, source)) return 1;
    if (strcmp(target, "api")) {
        snprintf(path, sizeof(path), "%s/views/main.hyp", name);
        if (!write_project_file(path, "view \"main\"\n    heading \"Welcome to Hyperian\"\n    text \"Your foldered MVC application is ready.\"\nend\n")) return 1;
    }
    snprintf(path, sizeof(path), "%s/public/app.css", name);
    if (!write_project_file(path, "body { font-family: system-ui, sans-serif; margin: 3rem auto; max-width: 48rem; }\n")) return 1;
    snprintf(path, sizeof(path), "%s/README.md", name);
    snprintf(source, sizeof(source), "# %s\n\nRun this %s application with:\n\n    hyperian run app.hyp\n", name, target);
    if (!write_project_file(path, source)) return 1;
    printf("Created %s as a foldered Hyperian %s application.\n", name, target); return 0;
}

static int format_source(const char *path) {
    FILE *file = fopen(path, "r"); if (!file) { fprintf(stderr, "error: cannot open %s\n", path); return 1; }
    char line[4096]; int indentation = 0;
    while (fgets(line, sizeof(line), file)) {
        char *start = line; while (*start == ' ' || *start == '\t') start++;
        start[strcspn(start, "\r\n")] = 0;
        if (!*start) { putchar('\n'); continue; }
        int closing = !strcmp(start, "end") || !strcmp(start, "otherwise");
        if (closing && indentation) indentation--;
        for (int i = 0; i < indentation * 4; i++) putchar(' ');
        puts(start);
        int opening = starts_with(start, "model ") || starts_with(start, "controller ") || starts_with(start, "view ") ||
            starts_with(start, "layout ") || starts_with(start, "component ") || starts_with(start, "action ") ||
            starts_with(start, "when data changes from ") ||
            starts_with(start, "when someone ") || !strcmp(start, "when application starts") || starts_with(start, "form ") ||
            starts_with(start, "for each ") || starts_with(start, "if ") || starts_with(start, "repeat ") || !strcmp(start, "otherwise");
        if (opening) indentation++;
    }
    int failed = ferror(file); fclose(file); return failed ? 1 : 0;
}

static void help(void) {
    puts("Hyperian " HYPERIAN_VERSION " - English-like MVC for every kind of app\n"
         "\n"
         "Usage:\n"
         "  hyperian compile app.hyp -o app.hyc   Compile source to bytecode\n"
         "  hyperian new MyApp [--target web]     Create a foldered MVC project\n"
         "  hyperian check app.hyp                Check source for mistakes\n"
         "  hyperian run app.hyp                  Compile and run an app\n"
         "  hyperian run app.hyc [--port 9000]    Run compiled bytecode\n"
         "  hyperian inspect app.hyc              Show compiled instructions\n"
         "  hyperian format app.hyp               Print consistently indented source\n"
         "  hyperian test app.hyp                 Run English test blocks\n"
         "  hyperian migrate app.hyp              Apply pending data migrations\n"
         "  hyperian version                      Show the version\n");
}

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix); return a >= b && !strcmp(text + a - b, suffix);
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) { help(); return 0; }
    if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) { puts("Hyperian " HYPERIAN_VERSION); return 0; }
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
