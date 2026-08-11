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
    for (size_t i = 0; i < code->count && !name; i++) if (code->items[i].opcode == OP_EVENT) {
        size_t end = game_end(code, i, OP_END_ROUTE);
        for (i++; i < end; i++) if (code->items[i].opcode == OP_SHOW_VIEW) { name = code->items[i].args[0]; break; }
    }
    if (!name) return 0;
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_VIEW && !strcmp(code->items[i].args[0], name)) {
        *from = i + 1; *to = game_end(code, i, OP_END_VIEW); return 1;
    }
    return 0;
}

int run_game_app(const Bytecode *code, const char *name) {
    size_t view_from, view_to;
    if (!game_view_range(code, &view_from, &view_to)) { fprintf(stderr, "error: game application needs a starting view\n"); return 1; }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "error: cannot start SDL2: %s\n", SDL_GetError()); return 1; }
    SDL_Window *window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) { fprintf(stderr, "error: cannot create game window: %s\n", SDL_GetError()); if (window) SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    int running = 1, frames = 0;
    while (running) {
        SDL_Event event; while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) running = 0;
        int red = 20, green = 24, blue = 35;
        for (size_t i = view_from; i < view_to; i++) if (code->items[i].opcode == OP_BACKGROUND) {
            red = channel(code->items[i].args[0]); green = channel(code->items[i].args[1]); blue = channel(code->items[i].args[2]); break;
        }
        SDL_SetRenderDrawColor(renderer, (Uint8)red, (Uint8)green, (Uint8)blue, 255); SDL_RenderClear(renderer);
        for (size_t i = view_from; i < view_to; i++) if (code->items[i].opcode == OP_RECTANGLE) {
            SDL_Rect rectangle = {atoi(code->items[i].args[0]), atoi(code->items[i].args[1]), atoi(code->items[i].args[2]), atoi(code->items[i].args[3])};
            SDL_SetRenderDrawColor(renderer, (Uint8)channel(code->items[i].args[4]), (Uint8)channel(code->items[i].args[5]), (Uint8)channel(code->items[i].args[6]), 255);
            SDL_RenderFillRect(renderer, &rectangle);
        }
        SDL_RenderPresent(renderer); frames++;
        if (getenv("HYPERIAN_VISUAL_TEST") && frames >= 1) running = 0;
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
#else
int run_game_app(const Bytecode *code, const char *name) {
    (void)code; (void)name; fprintf(stderr, "error: this Hyperian build does not include the SDL2 game backend\n"); return 1;
}
#endif
