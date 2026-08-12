#include "hyperian.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HYPERIAN_HAVE_SDL2
#include <SDL.h>
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
#include <SDL_image.h>
#endif

#define GAME_SPRITES_MAX 64
#define GAME_SOUNDS_MAX 64
#define GAME_POLYGON_POINTS_MAX 32
#define GAME_POLYGON_DATA_MAX 4096
typedef struct { char path[2048]; SDL_Texture *texture; } GameSprite;
typedef struct Mix_Chunk Mix_Chunk;
typedef struct Mix_Music Mix_Music;
typedef struct { char path[2048]; Mix_Chunk *chunk; } GameSound;

typedef struct {
    void *library;
    int state;
    int (*init)(int flags);
    void (*quit)(void);
    int (*open_audio)(int frequency, Uint16 format, int channels, int chunk_size);
    void (*close_audio)(void);
    Mix_Chunk *(*load_wave)(SDL_RWops *source, int free_source);
    int (*play_channel)(int channel, Mix_Chunk *chunk, int loops, int milliseconds);
    void (*free_chunk)(Mix_Chunk *chunk);
    Mix_Music *(*load_music)(const char *path);
    int (*play_music)(Mix_Music *music, int loops);
    int (*halt_music)(void);
    void (*pause_music)(void);
    void (*resume_music)(void);
    int (*music_volume)(int volume);
    void (*free_music)(Mix_Music *music);
} MixerApi;

static MixerApi mixer = {0};
static GameSound game_sounds[GAME_SOUNDS_MAX];
static int game_sound_count = 0, game_mixer_started = 0;
static Mix_Music *game_music = NULL;
static SDL_AudioDeviceID game_audio_device = 0;

static int mixer_function(const char *name, void *target, size_t target_size) {
    void *function = SDL_LoadFunction(mixer.library, name);
    if (!function || target_size != sizeof(function)) return 0;
    memcpy(target, &function, target_size); return 1;
}

static int load_mixer(void) {
    if (mixer.state) return mixer.state > 0;
    const char *disabled = getenv("HYPERIAN_DISABLE_SDL2_MIXER");
    if (disabled && *disabled && strcmp(disabled, "0")) { mixer.state = -1; return 0; }
    static const char *libraries[] = {"SDL2_mixer.dll", "libSDL2_mixer-2.0.so.0", "libSDL2_mixer.so", "libSDL2_mixer.dylib"};
    for (size_t at = 0; at < sizeof(libraries) / sizeof(libraries[0]) && !mixer.library; at++) mixer.library = SDL_LoadObject(libraries[at]);
    if (!mixer.library || !mixer_function("Mix_Init", &mixer.init, sizeof(mixer.init)) ||
        !mixer_function("Mix_Quit", &mixer.quit, sizeof(mixer.quit)) ||
        !mixer_function("Mix_OpenAudio", &mixer.open_audio, sizeof(mixer.open_audio)) ||
        !mixer_function("Mix_CloseAudio", &mixer.close_audio, sizeof(mixer.close_audio)) ||
        !mixer_function("Mix_LoadWAV_RW", &mixer.load_wave, sizeof(mixer.load_wave)) ||
        !mixer_function("Mix_PlayChannelTimed", &mixer.play_channel, sizeof(mixer.play_channel)) ||
        !mixer_function("Mix_FreeChunk", &mixer.free_chunk, sizeof(mixer.free_chunk)) ||
        !mixer_function("Mix_LoadMUS", &mixer.load_music, sizeof(mixer.load_music)) ||
        !mixer_function("Mix_PlayMusic", &mixer.play_music, sizeof(mixer.play_music)) ||
        !mixer_function("Mix_HaltMusic", &mixer.halt_music, sizeof(mixer.halt_music)) ||
        !mixer_function("Mix_PauseMusic", &mixer.pause_music, sizeof(mixer.pause_music)) ||
        !mixer_function("Mix_ResumeMusic", &mixer.resume_music, sizeof(mixer.resume_music)) ||
        !mixer_function("Mix_VolumeMusic", &mixer.music_volume, sizeof(mixer.music_volume)) ||
        !mixer_function("Mix_FreeMusic", &mixer.free_music, sizeof(mixer.free_music))) {
        if (mixer.library) SDL_UnloadObject(mixer.library);
        memset(&mixer, 0, sizeof(mixer)); mixer.state = -1; return 0;
    }
    mixer.state = 1; return 1;
}

int hyperian_game_mixer_available(void) { return load_mixer(); }

static int start_game_audio(char *error, size_t error_size) {
    if (game_mixer_started) return 1;
    if (!load_mixer()) { snprintf(error, error_size, "SDL2_mixer is not installed"); return 0; }
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        snprintf(error, error_size, "could not start game audio: %s", SDL_GetError()); return 0;
    }
    mixer.init(0x01 | 0x08 | 0x10 | 0x40);
    if (mixer.open_audio(44100, AUDIO_S16SYS, 2, 2048) < 0) {
        snprintf(error, error_size, "could not start the game audio mixer: %s", SDL_GetError()); mixer.quit(); return 0;
    }
    game_mixer_started = 1; return 1;
}

static int is_wave_extension(const char *path) {
    const char *extension = strrchr(path, '.');
    return extension && strlen(extension) == 4 && extension[0] == '.' && tolower((unsigned char)extension[1]) == 'w' &&
        tolower((unsigned char)extension[2]) == 'a' && tolower((unsigned char)extension[3]) == 'v';
}

static int play_game_sound(const char *path, char *error, size_t error_size) {
    if (load_mixer()) {
        if (!start_game_audio(error, error_size)) return 0;
        Mix_Chunk *chunk = NULL;
        for (int at = 0; at < game_sound_count; at++) if (!strcmp(game_sounds[at].path, path)) chunk = game_sounds[at].chunk;
        if (!chunk) {
            if (game_sound_count >= GAME_SOUNDS_MAX) { snprintf(error, error_size, "a game can load at most %d sounds", GAME_SOUNDS_MAX); return 0; }
            SDL_RWops *source = SDL_RWFromFile(path, "rb");
            chunk = source ? mixer.load_wave(source, 1) : NULL;
            if (!chunk) { snprintf(error, error_size, "could not load sound %s: %s", path, SDL_GetError()); return 0; }
            snprintf(game_sounds[game_sound_count].path, sizeof(game_sounds[game_sound_count].path), "%s", path);
            game_sounds[game_sound_count++].chunk = chunk;
        }
        if (mixer.play_channel(-1, chunk, 0, -1) < 0) {
            snprintf(error, error_size, "could not play sound %s: %s", path, SDL_GetError()); return 0;
        }
        return 1;
    }
    if (!is_wave_extension(path)) {
        snprintf(error, error_size, "this Hyperian installation needs SDL2_mixer to play compressed sound %s; WAV sounds are available", path); return 0;
    }
    SDL_AudioSpec format; Uint8 *data = NULL; Uint32 length = 0;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        snprintf(error, error_size, "could not start game audio: %s", SDL_GetError()); return 0;
    }
    if (!SDL_LoadWAV(path, &format, &data, &length)) { snprintf(error, error_size, "could not load sound %s: %s", path, SDL_GetError()); return 0; }
    if (game_audio_device) SDL_CloseAudioDevice(game_audio_device);
    game_audio_device = SDL_OpenAudioDevice(NULL, 0, &format, NULL, 0);
    if (!game_audio_device || SDL_QueueAudio(game_audio_device, data, length)) {
        snprintf(error, error_size, "could not play sound %s: %s", path, SDL_GetError()); SDL_FreeWAV(data); return 0;
    }
    SDL_FreeWAV(data); SDL_PauseAudioDevice(game_audio_device, 0); return 1;
}

static int control_game_music(HyperianMusicCommand command, const char *path, int value, char *error, size_t error_size) {
    if (!start_game_audio(error, error_size)) {
        if (!load_mixer()) snprintf(error, error_size, "this Hyperian installation needs SDL2_mixer to stream music");
        return 0;
    }
    if (command == HYPERIAN_MUSIC_PLAY_ONCE || command == HYPERIAN_MUSIC_PLAY_REPEATEDLY) {
        mixer.halt_music(); if (game_music) mixer.free_music(game_music);
        game_music = mixer.load_music(path);
        if (!game_music) { snprintf(error, error_size, "could not load music %s: %s", path, SDL_GetError()); return 0; }
        if (mixer.play_music(game_music, command == HYPERIAN_MUSIC_PLAY_REPEATEDLY ? -1 : 0) < 0) {
            snprintf(error, error_size, "could not play music %s: %s", path, SDL_GetError());
            mixer.free_music(game_music); game_music = NULL; return 0;
        }
    } else if (command == HYPERIAN_MUSIC_PAUSE) mixer.pause_music();
    else if (command == HYPERIAN_MUSIC_RESUME) mixer.resume_music();
    else if (command == HYPERIAN_MUSIC_STOP) { mixer.halt_music(); if (game_music) mixer.free_music(game_music); game_music = NULL; }
    else mixer.music_volume((value * 128 + 50) / 100);
    return 1;
}

static void close_game_audio(void) {
    if (game_audio_device) { SDL_CloseAudioDevice(game_audio_device); game_audio_device = 0; }
    if (game_mixer_started) {
        mixer.halt_music(); if (game_music) mixer.free_music(game_music); game_music = NULL;
        for (int at = 0; at < game_sound_count; at++) mixer.free_chunk(game_sounds[at].chunk);
        memset(game_sounds, 0, sizeof(game_sounds)); game_sound_count = 0;
        mixer.close_audio(); mixer.quit(); game_mixer_started = 0;
    }
}

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

static int run_game_event(HyperianData *data, const char *event, HyperianState *state) {
    char error[256] = {0};
    if (hyperian_execute_data_event(data, event, state, error, sizeof(error))) return 1;
    fprintf(stderr, "error in %s: %s\n", event, error); return 0;
}

static int evaluated_number(HyperianState *state, const char *expression) {
    char value[128]; hyperian_state_evaluate(state, expression, value, sizeof(value)); return atoi(value);
}

static int evaluated_channel(HyperianState *state, const char *expression) {
    char value[128]; hyperian_state_evaluate(state, expression, value, sizeof(value)); return channel(value);
}

#ifndef HYPERIAN_HAVE_SDL2_IMAGE
static int is_bitmap_extension(const char *extension) {
    return extension && strlen(extension) == 4 && extension[0] == '.' && tolower((unsigned char)extension[1]) == 'b' &&
        tolower((unsigned char)extension[2]) == 'm' && tolower((unsigned char)extension[3]) == 'p' && !extension[4];
}
#endif

static SDL_Surface *load_game_image(const char *path, char *error, size_t error_size) {
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
    SDL_Surface *surface = IMG_Load(path);
    if (!surface) snprintf(error, error_size, "could not load game image %s: %s", path, IMG_GetError());
    return surface;
#else
    const char *extension = strrchr(path, '.');
    if (!is_bitmap_extension(extension)) {
        snprintf(error, error_size, "this Hyperian build needs SDL2_image to load %s; BMP images are available", path); return NULL;
    }
    SDL_Surface *surface = SDL_LoadBMP(path);
    if (!surface) snprintf(error, error_size, "could not load game image %s: %s", path, SDL_GetError());
    return surface;
#endif
}

static void draw_filled_circle(SDL_Renderer *renderer, int center_x, int center_y, int radius) {
    if (radius < 0) return;
    int x = radius, y = 0, decision = 1 - radius;
    while (y <= x) {
        SDL_RenderDrawLine(renderer, center_x - x, center_y + y, center_x + x, center_y + y);
        SDL_RenderDrawLine(renderer, center_x - x, center_y - y, center_x + x, center_y - y);
        SDL_RenderDrawLine(renderer, center_x - y, center_y + x, center_x + y, center_y + x);
        SDL_RenderDrawLine(renderer, center_x - y, center_y - x, center_x + y, center_y - x);
        y++;
        if (decision <= 0) decision += 2 * y + 1;
        else { x--; decision += 2 * (y - x) + 1; }
    }
}

static int evaluated_polygon_points(HyperianState *state, const char *encoded, SDL_Point *points) {
    char text[GAME_POLYGON_DATA_MAX]; size_t length = strlen(encoded);
    if (length >= sizeof(text)) return 0;
    memcpy(text, encoded, length + 1); int count = 0; char *point = text;
    while (*point && count < GAME_POLYGON_POINTS_MAX) {
        char *next = strchr(point, ';'); if (next) *next = 0;
        char *middle = strchr(point, ','); if (!middle || strchr(middle + 1, ',')) return 0; *middle = 0;
        points[count].x = evaluated_number(state, point); points[count].y = evaluated_number(state, middle + 1); count++;
        if (!next) break;
        if (count == GAME_POLYGON_POINTS_MAX) return 0;
        point = next + 1;
    }
    return count >= 3 ? count : 0;
}

static void draw_filled_polygon(SDL_Renderer *renderer, const SDL_Point *points, int count) {
    int minimum_y = points[0].y, maximum_y = points[0].y;
    for (int at = 1; at < count; at++) {
        if (points[at].y < minimum_y) minimum_y = points[at].y;
        if (points[at].y > maximum_y) maximum_y = points[at].y;
    }
    if (minimum_y < 0) minimum_y = 0;
    if (maximum_y > 539) maximum_y = 539;
    for (int y = minimum_y; y <= maximum_y; y++) {
        int intersections[GAME_POLYGON_POINTS_MAX], found = 0;
        for (int current = 0, previous = count - 1; current < count; previous = current++) {
            int first_y = points[previous].y, second_y = points[current].y;
            if ((first_y <= y && second_y > y) || (second_y <= y && first_y > y)) {
                double portion = (double)(y - first_y) / (double)(second_y - first_y);
                intersections[found++] = (int)(points[previous].x + portion * (points[current].x - points[previous].x));
            }
        }
        for (int at = 1; at < found; at++) {
            int value = intersections[at], before = at - 1;
            while (before >= 0 && intersections[before] > value) { intersections[before + 1] = intersections[before]; before--; }
            intersections[before + 1] = value;
        }
        for (int at = 0; at + 1 < found; at += 2) SDL_RenderDrawLine(renderer, intersections[at], y, intersections[at + 1], y);
    }
    for (int current = 0, previous = count - 1; current < count; previous = current++)
        SDL_RenderDrawLine(renderer, points[previous].x, points[previous].y, points[current].x, points[current].y);
}

int run_game_app(const Bytecode *code, const char *name) {
    size_t view_from, view_to;
    if (!game_view_range(code, &view_from, &view_to)) { fprintf(stderr, "error: game application needs a starting view\n"); return 1; }
    HyperianState state; hyperian_state_init(&state);
    char data_error[256] = {0}; HyperianData *data = hyperian_data_open(code, data_error, sizeof(data_error));
    if (!data) { fprintf(stderr, "error: %s\n", data_error); return 1; }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "error: cannot start SDL2: %s\n", SDL_GetError()); hyperian_data_close(data); return 1; }
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
#endif
    hyperian_set_sound_handler(play_game_sound);
    hyperian_set_music_handler(control_game_music);
    if (!run_game_event(data, "START", &state)) { hyperian_set_sound_handler(NULL); hyperian_set_music_handler(NULL); close_game_audio(); hyperian_data_close(data);
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
        IMG_Quit();
#endif
        SDL_Quit(); return 1; }
    SDL_Window *window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 540, SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (window && !renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!window || !renderer) { fprintf(stderr, "error: cannot create game window: %s\n", SDL_GetError()); if (window) SDL_DestroyWindow(window); hyperian_set_sound_handler(NULL); hyperian_set_music_handler(NULL); close_game_audio(); hyperian_data_close(data);
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
        IMG_Quit();
#endif
        SDL_Quit(); return 1; }
    GameSprite sprites[GAME_SPRITES_MAX] = {0}; int sprite_count = 0;
    const char *injected_key = getenv("HYPERIAN_GAME_TEST_KEY"); int running = 1, failed = 0, frames = 0;
    if (injected_key) { char event[128]; snprintf(event, sizeof(event), "KEY:%s", injected_key); if (!run_game_event(data, event, &state)) { running = 0; failed = 1; } }
    Uint64 previous_frame = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event input; while (SDL_PollEvent(&input)) {
            if (input.type == SDL_QUIT || (input.type == SDL_KEYDOWN && input.key.keysym.sym == SDLK_ESCAPE)) running = 0;
            else if (input.type == SDL_KEYDOWN) {
                const char *key = key_name(input.key.keysym.sym); if (key) { char event[128]; snprintf(event, sizeof(event), "KEY:%s", key); if (!run_game_event(data, event, &state)) { running = 0; failed = 1; } }
            }
        }
        Uint64 current_frame = SDL_GetPerformanceCounter(); char frame_seconds[64];
        snprintf(frame_seconds, sizeof(frame_seconds), "%.6f", (double)(current_frame - previous_frame) / (double)SDL_GetPerformanceFrequency());
        previous_frame = current_frame; hyperian_state_set(&state, "seconds since last frame", frame_seconds);
        hyperian_state_set(&state, "seconds_since_last_frame", frame_seconds);
        if (!running) break;
        if (!run_game_event(data, "FRAME", &state)) { failed = 1; break; }
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
        } else if (code->items[i].opcode == OP_CIRCLE) {
            SDL_SetRenderDrawColor(renderer, (Uint8)evaluated_channel(&state, code->items[i].args[3]), (Uint8)evaluated_channel(&state, code->items[i].args[4]),
                (Uint8)evaluated_channel(&state, code->items[i].args[5]), 255);
            draw_filled_circle(renderer, evaluated_number(&state, code->items[i].args[0]), evaluated_number(&state, code->items[i].args[1]),
                evaluated_number(&state, code->items[i].args[2]));
        } else if (code->items[i].opcode == OP_LINE) {
            SDL_SetRenderDrawColor(renderer, (Uint8)evaluated_channel(&state, code->items[i].args[4]), (Uint8)evaluated_channel(&state, code->items[i].args[5]),
                (Uint8)evaluated_channel(&state, code->items[i].args[6]), 255);
            SDL_RenderDrawLine(renderer, evaluated_number(&state, code->items[i].args[0]), evaluated_number(&state, code->items[i].args[1]),
                evaluated_number(&state, code->items[i].args[2]), evaluated_number(&state, code->items[i].args[3]));
        } else if (code->items[i].opcode == OP_POLYGON) {
            SDL_Point points[GAME_POLYGON_POINTS_MAX]; int count = evaluated_polygon_points(&state, code->items[i].args[0], points);
            if (count) {
                SDL_SetRenderDrawColor(renderer, (Uint8)evaluated_channel(&state, code->items[i].args[1]), (Uint8)evaluated_channel(&state, code->items[i].args[2]),
                    (Uint8)evaluated_channel(&state, code->items[i].args[3]), 255);
                draw_filled_polygon(renderer, points, count);
            }
        } else if (code->items[i].opcode == OP_SPRITE) {
            char path[2048]; hyperian_state_evaluate(&state, code->items[i].args[0], path, sizeof(path)); SDL_Texture *texture = NULL;
            for (int at = 0; at < sprite_count; at++) if (!strcmp(sprites[at].path, path)) texture = sprites[at].texture;
            if (!texture && sprite_count < GAME_SPRITES_MAX) {
                char image_error[256] = {0}; SDL_Surface *surface = load_game_image(path, image_error, sizeof(image_error));
                if (surface) texture = SDL_CreateTextureFromSurface(renderer, surface);
                if (surface) SDL_FreeSurface(surface);
                if (!texture) {
                    if (*image_error) fprintf(stderr, "error: %s\n", image_error);
                    else fprintf(stderr, "error: could not create game texture for %s: %s\n", path, SDL_GetError());
                    running = 0; failed = 1; break;
                }
                snprintf(sprites[sprite_count].path, sizeof(sprites[sprite_count].path), "%s", path); sprites[sprite_count++].texture = texture;
            }
            SDL_Rect destination = {evaluated_number(&state, code->items[i].args[1]), evaluated_number(&state, code->items[i].args[2]),
                evaluated_number(&state, code->items[i].args[3]), evaluated_number(&state, code->items[i].args[4])};
            if (texture) SDL_RenderCopy(renderer, texture, NULL, &destination);
        }
        SDL_RenderPresent(renderer); frames++;
        if (getenv("HYPERIAN_VISUAL_TEST") && frames >= 1) running = 0;
    }
    const char *test_name = getenv("HYPERIAN_VISUAL_TEST_STATE");
    if (test_name) printf("%s=%s\n", test_name, hyperian_state_get(&state, test_name) ? hyperian_state_get(&state, test_name) : "");
    for (int i = 0; i < sprite_count; i++) SDL_DestroyTexture(sprites[i].texture);
    close_game_audio(); hyperian_set_sound_handler(NULL); hyperian_set_music_handler(NULL);
    hyperian_data_close(data); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
#ifdef HYPERIAN_HAVE_SDL2_IMAGE
    IMG_Quit();
#endif
    SDL_Quit(); return failed ? 1 : 0;
}
#else
int run_game_app(const Bytecode *code, const char *name) {
    (void)code; (void)name; fprintf(stderr, "error: this Hyperian build does not include the SDL2 game backend\n"); return 1;
}
int hyperian_game_mixer_available(void) { return 0; }
#endif
