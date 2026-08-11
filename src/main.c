#include "hyperian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int starts_with(const char *text, const char *prefix) { return !strncmp(text, prefix, strlen(prefix)); }

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
         "  hyperian check app.hyp                Check source for mistakes\n"
         "  hyperian run app.hyp                  Compile and run an app\n"
         "  hyperian run app.hyc [--port 9000]    Run compiled bytecode\n"
         "  hyperian inspect app.hyc              Show compiled instructions\n"
         "  hyperian format app.hyp               Print consistently indented source\n"
         "  hyperian test app.hyp                 Run English test blocks\n"
         "  hyperian version                      Show the version\n");
}

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text), b = strlen(suffix); return a >= b && !strcmp(text + a - b, suffix);
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) { help(); return 0; }
    if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) { puts("Hyperian " HYPERIAN_VERSION); return 0; }
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
