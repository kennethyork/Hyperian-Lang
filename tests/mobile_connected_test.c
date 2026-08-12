#include "hyperian.h"

#include <stdio.h>
#include <string.h>

static int fake_https(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size) {
    if (strcmp(url, "https://example.com/hyperian-status")) {
        snprintf(error, error_size, "unexpected mobile URL: %s", url); return 0;
    }
    const char *response = "ready";
    if (strlen(response) >= body_size) { snprintf(error, error_size, "response buffer is too small"); return 0; }
    snprintf(body, body_size, "%s", response); *status = 200; return 1;
}

static int sqlite_file(const char *path) {
    FILE *file = fopen(path, "rb"); char header[16];
    int okay = file && fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header, "SQLite format 3\0", sizeof(header));
    if (file) fclose(file);
    return okay;
}

static int rendered_contains(HyperianMobile *mobile, const char *wanted, char *error, size_t error_size) {
    char json[16384];
    if (!hyperian_mobile_render_json(mobile, json, sizeof(json), error, error_size)) return 0;
    if (strstr(json, wanted)) return 1;
    snprintf(error, error_size, "rendered mobile view does not contain %s", wanted); return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    hyperian_set_http_handler(fake_https); char error[512] = {0};
    HyperianMobile *mobile = hyperian_mobile_open(argv[1], error, sizeof(error));
    int okay = mobile && hyperian_mobile_start(mobile, error, sizeof(error)) &&
        hyperian_mobile_run_action(mobile, "refresh status", NULL, error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "internet message") &&
        !strcmp(hyperian_mobile_value(mobile, "internet message"), "Internet status:200:ready") &&
        hyperian_mobile_set(mobile, "task title", "Draft phone task", error, sizeof(error)) &&
        hyperian_mobile_send_event(mobile, "CHANGE:task title", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "saved message") &&
        !strcmp(hyperian_mobile_value(mobile, "saved message"), "Typing:Draft phone task") &&
        hyperian_mobile_send_event(mobile, "SUBMIT:task title", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "saved message"), "Submitted:Draft phone task") &&
        hyperian_mobile_set(mobile, "task title", "Persistent phone task", error, sizeof(error)) &&
        hyperian_mobile_run_action(mobile, "save task", NULL, error, sizeof(error)) &&
        rendered_contains(mobile, "Persistent phone task", error, sizeof(error));
    hyperian_mobile_close(mobile); mobile = NULL;
    if (okay) okay = sqlite_file(argv[2]);
    if (okay) {
        mobile = hyperian_mobile_open(argv[1], error, sizeof(error));
        okay = mobile && hyperian_mobile_start(mobile, error, sizeof(error)) &&
            rendered_contains(mobile, "Persistent phone task", error, sizeof(error));
    }
    if (!okay) fprintf(stderr, "connected mobile test failed: %s\n", *error ? error : "unexpected value");
    hyperian_mobile_close(mobile); hyperian_set_http_handler(NULL); return okay ? 0 : 1;
}
