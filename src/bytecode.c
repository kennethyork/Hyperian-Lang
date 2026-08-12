#include "hyperian.h"

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *result = malloc(length);
    if (result) memcpy(result, value, length);
    return result;
}

void bytecode_init(Bytecode *code) {
    code->items = NULL;
    code->count = 0;
    code->capacity = 0;
}

void bytecode_free(Bytecode *code) {
    for (size_t i = 0; i < code->count; i++)
        for (int j = 0; j < code->items[i].argc; j++) free(code->items[i].args[j]);
    free(code->items);
    bytecode_init(code);
}

int bytecode_add(Bytecode *code, uint8_t op, int argc, char **args, uint32_t line) {
    if (argc < 0 || argc > HYC_MAX_ARGS) return 0;
    if (code->count == code->capacity) {
        size_t capacity = code->capacity ? code->capacity * 2 : 32;
        Instruction *items = realloc(code->items, capacity * sizeof(*items));
        if (!items) return 0;
        code->items = items;
        code->capacity = capacity;
    }
    Instruction *instruction = &code->items[code->count++];
    memset(instruction, 0, sizeof(*instruction));
    instruction->opcode = op;
    instruction->argc = (uint8_t)argc;
    instruction->line = line;
    for (int i = 0; i < argc; i++) {
        instruction->args[i] = copy_string(args[i]);
        if (!instruction->args[i]) return 0;
    }
    return 1;
}

static int write_u32(FILE *file, uint32_t value) {
    unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return fwrite(bytes, 1, 4, file) == 4;
}

static int read_u32(FILE *file, uint32_t *value) {
    unsigned char bytes[4];
    if (fread(bytes, 1, 4, file) != 4) return 0;
    *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return 1;
}

int bytecode_write(const Bytecode *code, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "wb");
    if (!file) { snprintf(error, error_size, "cannot open output %s", path); return 0; }
    int okay = fwrite(HYC_MAGIC, 1, 4, file) == 4 && write_u32(file, (uint32_t)code->count);
    for (size_t i = 0; okay && i < code->count; i++) {
        const Instruction *in = &code->items[i];
        okay = fputc(in->opcode, file) != EOF && fputc(in->argc, file) != EOF && write_u32(file, in->line);
        for (int a = 0; okay && a < in->argc; a++) {
            size_t length = strlen(in->args[a]);
            if (length > UINT32_MAX) okay = 0;
            else okay = write_u32(file, (uint32_t)length) && fwrite(in->args[a], 1, length, file) == length;
        }
    }
    if (fclose(file) != 0) okay = 0;
    if (!okay) snprintf(error, error_size, "could not write bytecode to %s", path);
    return okay;
}

int bytecode_read(Bytecode *code, const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "rb");
    if (!file) { snprintf(error, error_size, "cannot open %s", path); return 0; }
    char magic[4]; uint32_t count = 0;
    if (fread(magic, 1, 4, file) != 4 || memcmp(magic, HYC_MAGIC, 4) || !read_u32(file, &count)) {
        snprintf(error, error_size, "%s is not Hyperian bytecode", path); fclose(file); return 0;
    }
    if (count > 1000000) { snprintf(error, error_size, "bytecode is too large"); fclose(file); return 0; }
    for (uint32_t i = 0; i < count; i++) {
        int op = fgetc(file), argc = fgetc(file); uint32_t line;
        if (op == EOF || argc < 0 || argc > HYC_MAX_ARGS || !read_u32(file, &line)) goto corrupt;
        int stored_argc = argc;
        char *args[HYC_MAX_ARGS] = {0};
        for (int a = 0; a < argc; a++) {
            uint32_t length;
            if (!read_u32(file, &length) || length > 1024 * 1024) goto corrupt_args;
            args[a] = malloc((size_t)length + 1);
            if (!args[a] || fread(args[a], 1, length, file) != length) goto corrupt_args;
            args[a][length] = '\0';
        }
        if (op == OP_FIELD && argc < 9) {
            static char *defaults[9] = {"", "text", "false", "", "false", "", "", "", "false"};
            for (int a = argc; a < 9; a++) args[a] = defaults[a];
            argc = 9;
        }
        if (!bytecode_add(code, (uint8_t)op, argc, args, line)) goto corrupt_args;
        for (int a = 0; a < stored_argc; a++) free(args[a]);
        continue;
corrupt_args:
        for (int a = 0; a < stored_argc; a++) free(args[a]);
        goto corrupt;
    }
    fclose(file); return 1;
corrupt:
    snprintf(error, error_size, "%s contains damaged bytecode", path);
    fclose(file); bytecode_free(code); return 0;
}

const char *opcode_name(uint8_t op) {
    static const char *names[] = {"INVALID", "APPLICATION", "PORT", "MODEL", "FIELD", "END_MODEL",
        "CONTROLLER", "ROUTE", "FIND_ALL", "CREATE", "SHOW_VIEW", "REDIRECT", "END_ROUTE",
        "END_CONTROLLER", "VIEW", "TITLE", "HEADING", "TEXT", "SHOW_VALUE", "LINK", "FORM",
        "INPUT", "BUTTON", "EACH", "IF", "END_FORM", "END_EACH", "END_IF", "END_VIEW",
        "TARGET", "EVENT", "ASK", "SAY", "FIND_ONE", "UPDATE", "DELETE",
        "FIND_WHERE", "FIND_ORDERED", "SHOW_JSON", "LAYOUT", "COMPONENT", "USE_LAYOUT",
        "USE_COMPONENT", "CONTENT", "END_LAYOUT", "END_COMPONENT", "SIGN_IN", "SIGN_OUT",
        "REQUIRE_SIGN_IN", "FIND_SIGNED_IN", "REQUIRE_VALUE", "ACTION", "END_ACTION",
        "SET_VALUE", "LOGIC_IF", "OTHERWISE", "END_LOGIC_IF", "REPEAT", "END_REPEAT", "RUN_ACTION", "READ_FORM",
        "TEST", "EXPECT", "END_TEST", "BEFORE_ACTION", "ERROR_VIEW", "STATIC_FILES",
        "STYLE", "SCRIPT", "IMAGE", "TEXTAREA", "CHECKBOX",
        "DATA_VERSION", "MIGRATION", "RENAME_FIELD", "END_MIGRATION",
        "RETURN_VALUE", "READ_FILE", "WRITE_FILE", "BACKGROUND", "RECTANGLE",
        "MAKE_LIST", "LIST_ADD", "LIST_REMOVE", "LIST_COUNT", "LIST_ITEM",
        "TRY", "CATCH", "END_TRY", "HTTP_GET",
        "MAKE_MAP", "MAP_PUT", "MAP_GET", "MAP_REMOVE", "MAP_COUNT", "STORAGE",
        "BUTTON_ACTION", "SPRITE", "PLAY_SOUND", "OPEN_VIEW",
        "CREATE_STATE", "FIND_STATE", "UPDATE_STATE", "DELETE_STATE", "COUNT_RECORDS", "COLLECT_FIELD",
        "MOVE_POSITION", "APPLY_GRAVITY", "KEEP_INSIDE", "CHECK_COLLISION", "COLLECT_QUERY",
        "MOVE_VALUE_TOWARD", "ADVANCE_ANIMATION",
        "CIRCLE", "CHECK_CIRCLE_COLLISION", "CHECK_CIRCLE_RECTANGLE_COLLISION",
        "LINE", "CHECK_LINE_COLLISION", "CHECK_LINE_CIRCLE_COLLISION", "CHECK_LINE_RECTANGLE_COLLISION"};
    return op < sizeof(names) / sizeof(names[0]) ? names[op] : "UNKNOWN";
}
