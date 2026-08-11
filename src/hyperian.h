#ifndef HYPERIAN_H
#define HYPERIAN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define HYPERIAN_VERSION "0.12.0"
#define HYC_MAGIC "HYC1"
#define HYC_MAX_ARGS 9

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
    OP_TRY, OP_CATCH, OP_END_TRY, OP_HTTP_GET
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

void bytecode_init(Bytecode *code);
void bytecode_free(Bytecode *code);
int bytecode_add(Bytecode *code, uint8_t op, int argc, char **args, uint32_t line);
int bytecode_write(const Bytecode *code, const char *path, char *error, size_t error_size);
int bytecode_read(Bytecode *code, const char *path, char *error, size_t error_size);
const char *opcode_name(uint8_t opcode);

int compile_file(const char *source_path, const char *output_path);
int run_bytecode(const char *path, int port_override);
int inspect_bytecode(const char *path);
int test_bytecode(const char *path);
int migrate_bytecode(const char *path);
int run_desktop_app(const Bytecode *code, const char *name);
int run_game_app(const Bytecode *code, const char *name);
int hyperian_http_get(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size);

#endif
