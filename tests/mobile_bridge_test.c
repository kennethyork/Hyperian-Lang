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
        contains(json, "\"action\":\"add task\"") && contains(json, "\"changeEvent\":\"CHANGE:title\"") &&
        contains(json, "\"submitEvent\":\"SUBMIT:title\"") && contains(json, "\"changeEvent\":\"CHANGE:details\"") &&
        contains(json, "\"changeEvent\":\"CHANGE:urgent\"");
    if (okay) okay = hyperian_mobile_send_event(mobile, "RESUME", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "lifecycle_status") && !strcmp(hyperian_mobile_value(mobile, "lifecycle_status"), "Application resumed") &&
        hyperian_mobile_send_event(mobile, "FOCUS", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "lifecycle_status"), "Window focused");
    if (okay) okay = hyperian_mobile_send_event(mobile, "SWIPE:left", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "gesture_status") && !strcmp(hyperian_mobile_value(mobile, "gesture_status"), "Swiped left") &&
        hyperian_mobile_send_event(mobile, "SWIPE:right", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "gesture_status"), "Swiped right") &&
        hyperian_mobile_send_event(mobile, "SWIPE:up", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "gesture_status"), "Swiped up") &&
        hyperian_mobile_send_event(mobile, "SWIPE:down", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "gesture_status"), "Swiped down") &&
        hyperian_mobile_send_event(mobile, "TAP", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "gesture_status"), "Tapped") &&
        hyperian_mobile_send_event(mobile, "LONG_PRESS", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "gesture_status"), "Pressed and held");
    if (okay) okay = hyperian_mobile_set(mobile, "title", "Coffee \"and\" tea", error, sizeof(error)) &&
        hyperian_mobile_send_event(mobile, "CHANGE:title", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "live_preview") && !strcmp(hyperian_mobile_value(mobile, "live_preview"), "Typing:Coffee \"and\" tea") &&
        hyperian_mobile_set(mobile, "details", "Bring a cup", error, sizeof(error)) &&
        hyperian_mobile_send_event(mobile, "CHANGE:details", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "details_preview") && !strcmp(hyperian_mobile_value(mobile, "details_preview"), "Details:Bring a cup") &&
        hyperian_mobile_set(mobile, "urgent", "true", error, sizeof(error)) &&
        hyperian_mobile_send_event(mobile, "CHANGE:urgent", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "urgency_preview") && !strcmp(hyperian_mobile_value(mobile, "urgency_preview"), "Urgent:true") &&
        hyperian_mobile_send_event(mobile, "SUBMIT:title", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "status") && !strcmp(hyperian_mobile_value(mobile, "status"), "Added: Coffee \"and\" tea") &&
        hyperian_mobile_render_json(mobile, json, sizeof(json), error, sizeof(error)) &&
        contains(json, "Coffee \\\"and\\\" tea") && contains(json, "\"timers\":[1000]") &&
        hyperian_mobile_run_action(mobile, "show completed", NULL, error, sizeof(error)) &&
        hyperian_mobile_render_json(mobile, json, sizeof(json), error, sizeof(error)) && contains(json, "\"view\":\"completed\"") &&
        hyperian_mobile_send_event(mobile, "TIMER:1000", error, sizeof(error)) &&
        hyperian_mobile_value(mobile, "heartbeat") && !strcmp(hyperian_mobile_value(mobile, "heartbeat"), "1") &&
        hyperian_mobile_send_event(mobile, "BLUR", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "lifecycle_status"), "Window unfocused") &&
        hyperian_mobile_send_event(mobile, "PAUSE", error, sizeof(error)) &&
        !strcmp(hyperian_mobile_value(mobile, "lifecycle_status"), "Application paused");
    if (!okay && !*error) {
        fprintf(stderr, "status=%s lifecycle=%s gesture=%s preview=%s details=%s urgency=%s heartbeat=%s json=%s\n", hyperian_mobile_value(mobile, "status"),
            hyperian_mobile_value(mobile, "lifecycle_status"),
            hyperian_mobile_value(mobile, "gesture_status"),
            hyperian_mobile_value(mobile, "live_preview"), hyperian_mobile_value(mobile, "details_preview"),
            hyperian_mobile_value(mobile, "urgency_preview"), hyperian_mobile_value(mobile, "heartbeat"), json);
        snprintf(error, sizeof(error), "a rendered mobile value was incorrect");
    }
    if (!okay) fprintf(stderr, "mobile bridge failed: %s\n", error);
    hyperian_mobile_close(mobile); return okay ? 0 : 1;
}
