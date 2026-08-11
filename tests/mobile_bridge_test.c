#include "hyperian.h"

#include <stdio.h>
#include <string.h>

static int contains(const char *json, const char *wanted) {
    if (strstr(json, wanted)) return 1;
    fprintf(stderr, "missing from rendered interface: %s\n%s\n", wanted, json); return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char error[256] = {0}, json[16384];
    HyperianMobile *mobile = hyperian_mobile_open(argv[1], error, sizeof(error));
    if (!mobile) { fprintf(stderr, "%s\n", error); return 1; }
    int okay = hyperian_mobile_start(mobile, error, sizeof(error)) &&
        hyperian_mobile_render_json(mobile, json, sizeof(json), error, sizeof(error)) &&
        contains(json, "\"view\":\"home\"") && contains(json, "\"kind\":\"input\"") &&
        contains(json, "\"action\":\"add task\"");
    if (okay) okay = hyperian_mobile_set(mobile, "title", "Coffee \"and\" tea", error, sizeof(error)) &&
        hyperian_mobile_run_action(mobile, "add task", NULL, error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "status") && !strcmp(hyperian_mobile_value(mobile, "status"), "Added:Coffee \"and\" tea") &&
        hyperian_mobile_render_json(mobile, json, sizeof(json), error, sizeof(error)) &&
        contains(json, "Coffee \\\"and\\\" tea") && hyperian_mobile_run_action(mobile, "show completed", NULL, error, sizeof(error)) &&
        hyperian_mobile_render_json(mobile, json, sizeof(json), error, sizeof(error)) && contains(json, "\"view\":\"completed\"") &&
        hyperian_mobile_send_event(mobile, "TIMER:1000", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "heartbeat") && !strcmp(hyperian_mobile_value(mobile, "heartbeat"), "1");
    if (!okay && !*error) {
        fprintf(stderr, "status=%s heartbeat=%s json=%s\n", hyperian_mobile_value(mobile, "status"),
            hyperian_mobile_value(mobile, "heartbeat"), json);
        snprintf(error, sizeof(error), "a rendered mobile value was incorrect");
    }
    if (!okay) fprintf(stderr, "mobile bridge failed: %s\n", error);
    hyperian_mobile_close(mobile); return okay ? 0 : 1;
}
