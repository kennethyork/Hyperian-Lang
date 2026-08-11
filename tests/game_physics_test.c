#include "hyperian.h"

#include <stdio.h>
#include <string.h>

static int expected(HyperianState *state, const char *name, const char *value) {
    const char *actual = hyperian_state_get(state, name);
    if (actual && !strcmp(actual, value)) return 1;
    fprintf(stderr, "%s should be %s but is %s\n", name, value, actual ? actual : "missing"); return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    Bytecode code; bytecode_init(&code); char error[256] = {0};
    if (!bytecode_read(&code, argv[1], error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 1; }
    HyperianState state; hyperian_state_init(&state);
    int okay = hyperian_execute_event(&code, "START", &state, error, sizeof(error));
    hyperian_state_set(&state, "seconds_since_last_frame", "0.25");
    if (okay) okay = hyperian_execute_event(&code, "FRAME", &state, error, sizeof(error));
    if (okay) okay = expected(&state, "player_velocity_y", "50") && expected(&state, "player_x", "30") &&
        expected(&state, "player_y", "22.5") && expected(&state, "player_hit", "true");
    if (!okay) fprintf(stderr, "physics test failed: %s\n", error);
    bytecode_free(&code); return okay ? 0 : 1;
}
