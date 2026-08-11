#include "hyperian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HYPERIAN_HAVE_SDL2
#include <SDL.h>

static int channel(const char *text) { int value = atoi(text); return value < 0 ? 0 : value > 255 ? 255 : value; }

static size_t game_end(const Bytecode *code, size_t start, uint8_t close) {
    for (size_t i = start + 1; i < code->count; i++) if (code->items[i].opcode == close) return i;
    return code->count;
}

static int game_view_range(const Bytecode *code, size_t *from, size_t *to) {
    const char *name = NULL;
    for (size_t i = 0; i < code->count && !name; i++) if (code->items[i].opcode == OP_EVENT && !strcmp(code->items[i].args[0], "START")) {
        size_t end = game_end(code, i, OP_END_ROUTE);
        for (i++; i < end; i++) if (code->items[i].opcode == OP_SHOW_VIEW) { name = code->items[i].args[0]; break; }
    }
    if (!name) return 0;
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_VIEW && !strcmp(code->items[i].args[0], name)) {
        *from = i + 1; *to = game_end(code, i, OP_END_VIEW); return 1;
    }
    return 0;
}

static const char *key_name(SDL_Keycode key) {
    if (key == SDLK_LEFT) return "left";
    if (key == SDLK_RIGHT) return "right";
    if (key == SDLK_UP) return "up";
    if (key == SDLK_DOWN) return "down";
    if (key == SDLK_SPACE) return "space";
    if (key == SDLK_RETURN) return "enter";
    return NULL;
}

static int run_game_event(const Bytecode *code, const char *event, HyperianState *state) {
    char error[256] = {0};
    if (hyperian_execute_event(code, event, state, error, sizeof(error))) return 1;
    fprintf(stderr, "error in %s: %s\n", event, error); return 0;
}

static int evaluated_number(HyperianState *state, const char *expression) {
    char value[128]; hyperian_state_evaluate(state, expression, value, sizeof(value)); return atoi(value);
}

static int evaluated_channel(HyperianState *state, const char *expression) {
    char value[128]; hyperian_state_evaluate(state, expression, value, sizeof(value)); return channel(value);
}

int run_game_app(const Bytecode *code, const char *name) {
    size_t view_from, view_to;
    if (!game_view_range(code, &view_from, &view_to)) { fprintf(stderr, "error: game application needs a starting view\n"); return 1; }
    HyperianState state; hyperian_state_init(&state); if (!run_game_event(code, "START", &state)) return 1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "error: cannot start SDL2: %s\n", SDL_GetError()); return 1; }
    SDL_Window *window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) { fprintf(stderr, "error: cannot create game window: %s\n", SDL_GetError()); if (window) SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    const char *injected_key = getenv("HYPERIAN_GAME_TEST_KEY"); int running = 1, frames = 0;
    if (injected_key) { char event[128]; snprintf(event, sizeof(event), "KEY:%s", injected_key); running = run_game_event(code, event, &state); }
    while (running) {
        SDL_Event input; while (SDL_PollEvent(&input)) {
            if (input.type == SDL_QUIT || (input.type == SDL_KEYDOWN && input.key.keysym.sym == SDLK_ESCAPE)) running = 0;
            else if (input.type == SDL_KEYDOWN) {
                const char *key = key_name(input.key.keysym.sym); if (key) { char event[128]; snprintf(event, sizeof(event), "KEY:%s", key); running = run_game_event(code, event, &state); }
            }
        }
        if (!running || !run_game_event(code, "FRAME", &state)) break;
        int red = 20, green = 24, blue = 35;
        for (size_t i = view_from; i < view_to; i++) if (code->items[i].opcode == OP_BACKGROUND) {
            char value[32];
            hyperian_state_evaluate(&state, code->items[i].args[0], value, sizeof(value)); red = channel(value);
            hyperian_state_evaluate(&state, code->items[i].args[1], value, sizeof(value)); green = channel(value);
            hyperian_state_evaluate(&state, code->items[i].args[2], value, sizeof(value)); blue = channel(value); break;
        }
        SDL_SetRenderDrawColor(renderer, (Uint8)red, (Uint8)green, (Uint8)blue, 255); SDL_RenderClear(renderer);
        for (size_t i = view_from; i < view_to; i++) if (code->items[i].opcode == OP_RECTANGLE) {
            SDL_Rect rectangle = {evaluated_number(&state, code->items[i].args[0]), evaluated_number(&state, code->items[i].args[1]),
                evaluated_number(&state, code->items[i].args[2]), evaluated_number(&state, code->items[i].args[3])};
            SDL_SetRenderDrawColor(renderer, (Uint8)evaluated_channel(&state, code->items[i].args[4]), (Uint8)evaluated_channel(&state, code->items[i].args[5]),
                (Uint8)evaluated_channel(&state, code->items[i].args[6]), 255); SDL_RenderFillRect(renderer, &rectangle);
        }
        SDL_RenderPresent(renderer); frames++;
        if (getenv("HYPERIAN_VISUAL_TEST") && frames >= 1) running = 0;
    }
    const char *test_name = getenv("HYPERIAN_VISUAL_TEST_STATE");
    if (test_name) printf("%s=%s\n", test_name, hyperian_state_get(&state, test_name) ? hyperian_state_get(&state, test_name) : "");
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
#else
int run_game_app(const Bytecode *code, const char *name) {
    (void)code; (void)name; fprintf(stderr, "error: this Hyperian build does not include the SDL2 game backend\n"); return 1;
}
#endif
