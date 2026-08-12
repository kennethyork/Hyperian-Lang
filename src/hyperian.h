#ifndef HYPERIAN_H
#define HYPERIAN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define HYPERIAN_VERSION "0.48.0"
#define HYC_MAGIC "HYC1"
#define HYC_MAX_ARGS 9
#define HYPERIAN_STATE_MAX 64
#define HYPERIAN_VALUE_SIZE 2048

typedef enum {
    OP_APPLICATION = 1, OP_PORT,
    OP_MODEL, OP_FIELD, OP_END_MODEL,
    OP_CONTROLLER, OP_ROUTE, OP_FIND_ALL, OP_CREATE, OP_SHOW_VIEW,
    OP_REDIRECT, OP_END_ROUTE, OP_END_CONTROLLER,
    OP_VIEW, OP_TITLE, OP_HEADING, OP_TEXT, OP_SHOW_VALUE, OP_LINK,
    OP_FORM, OP_INPUT, OP_BUTTON, OP_EACH, OP_IF,
    OP_END_FORM, OP_END_EACH, OP_END_IF, OP_END_VIEW,
    OP_TARGET, OP_EVENT, OP_ASK, OP_SAY,
    OP_FIND_ONE, OP_UPDATE, OP_DELETE,
    OP_FIND_WHERE, OP_FIND_ORDERED, OP_SHOW_JSON,
    OP_LAYOUT, OP_COMPONENT, OP_USE_LAYOUT, OP_USE_COMPONENT, OP_CONTENT,
    OP_END_LAYOUT, OP_END_COMPONENT,
    OP_SIGN_IN, OP_SIGN_OUT, OP_REQUIRE_SIGN_IN, OP_FIND_SIGNED_IN, OP_REQUIRE_VALUE,
    OP_ACTION, OP_END_ACTION, OP_SET_VALUE, OP_LOGIC_IF, OP_OTHERWISE,
    OP_END_LOGIC_IF, OP_REPEAT, OP_END_REPEAT, OP_RUN_ACTION, OP_READ_FORM,
    OP_TEST, OP_EXPECT, OP_END_TEST, OP_BEFORE_ACTION, OP_ERROR_VIEW,
    OP_STATIC_FILES, OP_STYLE, OP_SCRIPT, OP_IMAGE, OP_TEXTAREA, OP_CHECKBOX,
    OP_DATA_VERSION, OP_MIGRATION, OP_RENAME_FIELD, OP_END_MIGRATION,
    OP_RETURN_VALUE, OP_READ_FILE, OP_WRITE_FILE, OP_BACKGROUND, OP_RECTANGLE,
    OP_MAKE_LIST, OP_LIST_ADD, OP_LIST_REMOVE, OP_LIST_COUNT, OP_LIST_ITEM,
    OP_TRY, OP_CATCH, OP_END_TRY, OP_HTTP_GET,
    OP_MAKE_MAP, OP_MAP_PUT, OP_MAP_GET, OP_MAP_REMOVE, OP_MAP_COUNT, OP_STORAGE,
    OP_BUTTON_ACTION, OP_SPRITE, OP_PLAY_SOUND, OP_OPEN_VIEW,
    OP_CREATE_STATE, OP_FIND_STATE, OP_UPDATE_STATE, OP_DELETE_STATE, OP_COUNT_RECORDS, OP_COLLECT_FIELD,
    OP_MOVE_POSITION, OP_APPLY_GRAVITY, OP_KEEP_INSIDE, OP_CHECK_COLLISION, OP_COLLECT_QUERY,
    OP_MOVE_VALUE_TOWARD, OP_ADVANCE_ANIMATION,
    OP_CIRCLE, OP_CHECK_CIRCLE_COLLISION, OP_CHECK_CIRCLE_RECTANGLE_COLLISION,
    OP_LINE, OP_CHECK_LINE_COLLISION, OP_CHECK_LINE_CIRCLE_COLLISION, OP_CHECK_LINE_RECTANGLE_COLLISION,
    OP_POLYGON, OP_CHECK_POLYGON_COLLISION, OP_CHECK_POLYGON_LINE_COLLISION,
    OP_CHECK_POLYGON_CIRCLE_COLLISION, OP_CHECK_POLYGON_RECTANGLE_COLLISION,
    OP_PLAY_MUSIC, OP_PAUSE_MUSIC, OP_RESUME_MUSIC, OP_STOP_MUSIC, OP_SET_MUSIC_VOLUME
} OpCode;

typedef struct {
    uint8_t opcode;
    uint8_t argc;
    char *args[HYC_MAX_ARGS];
    uint32_t line;
} Instruction;

typedef struct {
    Instruction *items;
    size_t count;
    size_t capacity;
} Bytecode;

typedef struct {
    char names[HYPERIAN_STATE_MAX][64];
    char values[HYPERIAN_STATE_MAX][HYPERIAN_VALUE_SIZE];
    int count;
} HyperianState;

typedef int (*HyperianSoundHandler)(const char *path, char *error, size_t error_size);
typedef enum { HYPERIAN_MUSIC_PLAY_ONCE, HYPERIAN_MUSIC_PLAY_REPEATEDLY, HYPERIAN_MUSIC_PAUSE,
    HYPERIAN_MUSIC_RESUME, HYPERIAN_MUSIC_STOP, HYPERIAN_MUSIC_VOLUME } HyperianMusicCommand;
typedef int (*HyperianMusicHandler)(HyperianMusicCommand command, const char *path, int value, char *error, size_t error_size);
typedef int (*HyperianHttpHandler)(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size);
typedef struct HyperianData HyperianData;
typedef struct HyperianMobile HyperianMobile;

void bytecode_init(Bytecode *code);
void bytecode_free(Bytecode *code);
int bytecode_add(Bytecode *code, uint8_t op, int argc, char **args, uint32_t line);
int bytecode_write(const Bytecode *code, const char *path, char *error, size_t error_size);
int bytecode_read(Bytecode *code, const char *path, char *error, size_t error_size);
const char *opcode_name(uint8_t opcode);

int compile_file(const char *source_path, const char *output_path);
int run_bytecode(const char *path, int port_override);
int inspect_bytecode(const char *path);
int debug_bytecode(const char *path, const char *event, const char *action, const char *input);
int test_bytecode(const char *path);
int migrate_bytecode(const char *path);
int run_desktop_app(const Bytecode *code, const char *name);
int run_mobile_app(const Bytecode *code, const char *name);
int run_game_app(const Bytecode *code, const char *name);
int hyperian_http_get(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size);
void hyperian_set_http_handler(HyperianHttpHandler handler);
void hyperian_state_init(HyperianState *state);
const char *hyperian_state_get(const HyperianState *state, const char *name);
void hyperian_state_set(HyperianState *state, const char *name, const char *value);
void hyperian_state_evaluate(HyperianState *state, const char *expression, char *output, size_t output_size);
void hyperian_set_sound_handler(HyperianSoundHandler handler);
void hyperian_set_music_handler(HyperianMusicHandler handler);
int hyperian_game_mixer_available(void);
int hyperian_execute_action(const Bytecode *code, const char *name, const char *input, HyperianState *state, char *error, size_t error_size);
int hyperian_execute_event(const Bytecode *code, const char *event, HyperianState *state, char *error, size_t error_size);
HyperianData *hyperian_data_open(const Bytecode *code, char *error, size_t error_size);
void hyperian_data_close(HyperianData *data);
int hyperian_execute_data_action(HyperianData *data, const char *name, const char *input, HyperianState *state, char *error, size_t error_size);
int hyperian_execute_data_event(HyperianData *data, const char *event, HyperianState *state, char *error, size_t error_size);
HyperianMobile *hyperian_mobile_open(const char *bytecode_path, char *error, size_t error_size);
void hyperian_mobile_close(HyperianMobile *mobile);
int hyperian_mobile_start(HyperianMobile *mobile, char *error, size_t error_size);
int hyperian_mobile_set(HyperianMobile *mobile, const char *name, const char *value, char *error, size_t error_size);
int hyperian_mobile_run_action(HyperianMobile *mobile, const char *action, const char *input, char *error, size_t error_size);
int hyperian_mobile_send_event(HyperianMobile *mobile, const char *event, char *error, size_t error_size);
int hyperian_mobile_render_json(HyperianMobile *mobile, char *output, size_t output_size, char *error, size_t error_size);
const char *hyperian_mobile_value(const HyperianMobile *mobile, const char *name);

#endif
