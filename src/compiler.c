#include "hyperian.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 4096
#define MAX_WORDS 32
#define MAX_DEPTH 64

typedef enum { BLOCK_ROOT, BLOCK_MODEL, BLOCK_CONTROLLER, BLOCK_ROUTE, BLOCK_VIEW, BLOCK_FORM, BLOCK_EACH, BLOCK_IF,
    BLOCK_LAYOUT, BLOCK_COMPONENT, BLOCK_ACTION, BLOCK_LOGIC_IF, BLOCK_REPEAT, BLOCK_TEST, BLOCK_MIGRATION } Block;

static void source_error(const char *path, unsigned line, const char *message) {
    fprintf(stderr, "%s:%u: error: %s\n", path, line, message);
}

static int words(char *line, char **out, int maximum, char *error, size_t error_size) {
    int count = 0; char *read = line, *write = line;
    while (*read) {
        while (isspace((unsigned char)*read)) read++;
        if (!*read || *read == '#') break;
        if (count == maximum) { snprintf(error, error_size, "too many words on one line"); return -1; }
        out[count++] = write;
        if (*read == '"') {
            read++;
            while (*read && *read != '"') {
                if (*read == '\\' && (read[1] == '"' || read[1] == '\\')) read++;
                *write++ = *read++;
            }
            if (*read != '"') { snprintf(error, error_size, "this quote needs a closing quote"); return -1; }
            read++;
        } else {
            while (*read && !isspace((unsigned char)*read) && *read != '#') *write++ = *read++;
        }
        int comment = *read == '#';
        while (isspace((unsigned char)*read)) read++;
        *write++ = '\0';
        if (comment || *read == '#') break;
    }
    return count;
}

static int emit(Bytecode *code, int op, int argc, char **args, unsigned line) {
    if (!bytecode_add(code, (uint8_t)op, argc, args, line)) {
        fprintf(stderr, "error: the compiler ran out of memory\n"); return 0;
    }
    return 1;
}

static void join_words(char *output, size_t size, char **words, int from, int count) {
    size_t used = 0; output[0] = 0;
    for (int i = from; i < count && used + 1 < size; i++) {
        if (i > from) output[used++] = ' ';
        size_t length = strlen(words[i]); if (length > size - used - 1) length = size - used - 1;
        memcpy(output + used, words[i], length); used += length; output[used] = 0;
    }
}

static int is_name(const char *value) {
    if (!isalpha((unsigned char)*value)) return 0;
    for (value++; *value; value++) if (!isalnum((unsigned char)*value) && *value != '_') return 0;
    return 1;
}

static int known_name(const Bytecode *code, uint8_t op, const char *name) {
    for (size_t i = 0; i < code->count; i++)
        if (code->items[i].opcode == op && !strcmp(code->items[i].args[0], name)) return 1;
    return 0;
}

static int duplicate_name(const Bytecode *code, uint8_t op, const char *name) {
    int count = 0;
    for (size_t i = 0; i < code->count; i++)
        if (code->items[i].opcode == op && !strcmp(code->items[i].args[0], name)) count++;
    return count > 1;
}

static int model_has_field(const Bytecode *code, const char *model, const char *field) {
    if (!strcmp(field, "id")) return 1;
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_MODEL && !strcmp(code->items[i].args[0], model))
        for (i++; i < code->count && code->items[i].opcode != OP_END_MODEL; i++)
            if (code->items[i].opcode == OP_FIELD && !strcmp(code->items[i].args[0], field)) return 1;
    return 0;
}

static const char *model_field_kind(const Bytecode *code, const char *model, const char *field) {
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_MODEL && !strcmp(code->items[i].args[0], model))
        for (i++; i < code->count && code->items[i].opcode != OP_END_MODEL; i++)
            if (code->items[i].opcode == OP_FIELD && !strcmp(code->items[i].args[0], field)) return code->items[i].args[1];
    return NULL;
}

static int validate(Bytecode *code, const char *path) {
    int models = 0, controllers = 0, views = 0;
    int data_version = 1, version_declarations = 0;
    const char *target = "web";
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_DATA_VERSION) {
        data_version = atoi(code->items[i].args[0]); version_declarations++;
        if (version_declarations > 1) { source_error(path, code->items[i].line, "data version can only be declared once"); return 0; }
    }
    for (size_t i = 0; i < code->count; i++) {
        Instruction *in = &code->items[i];
        if (in->opcode == OP_TARGET) target = in->args[0];
        if (in->opcode == OP_MODEL) models++;
        if (in->opcode == OP_CONTROLLER) controllers++;
        if (in->opcode == OP_VIEW) views++;
        if ((in->opcode == OP_MODEL || in->opcode == OP_CONTROLLER) && duplicate_name(code, in->opcode, in->args[0])) {
            source_error(path, in->line, "this name is declared twice"); return 0;
        }
        if (in->opcode == OP_VIEW && duplicate_name(code, OP_VIEW, in->args[0])) {
            source_error(path, in->line, "this view is declared twice"); return 0;
        }
        if (in->opcode == OP_LAYOUT && duplicate_name(code, OP_LAYOUT, in->args[0])) {
            source_error(path, in->line, "this layout is declared twice"); return 0;
        }
        if (in->opcode == OP_COMPONENT && duplicate_name(code, OP_COMPONENT, in->args[0])) {
            source_error(path, in->line, "this component is declared twice"); return 0;
        }
        if (in->opcode == OP_ACTION && duplicate_name(code, OP_ACTION, in->args[0])) {
            source_error(path, in->line, "this controller action is declared twice"); return 0;
        }
        if (in->opcode == OP_RUN_ACTION && !known_name(code, OP_ACTION, in->args[0])) {
            source_error(path, in->line, "this controller action does not exist"); return 0;
        }
        if (in->opcode == OP_BEFORE_ACTION && !known_name(code, OP_ACTION, in->args[0])) {
            source_error(path, in->line, "the before-route action does not exist"); return 0;
        }
        if (in->opcode == OP_TEST && duplicate_name(code, OP_TEST, in->args[0])) {
            source_error(path, in->line, "this test name is used twice"); return 0;
        }
        if (in->opcode == OP_ERROR_VIEW && !known_name(code, OP_VIEW, in->args[1])) {
            source_error(path, in->line, "the error view does not exist"); return 0;
        }
        if (in->opcode == OP_USE_LAYOUT && !known_name(code, OP_LAYOUT, in->args[0])) {
            source_error(path, in->line, "this layout does not exist"); return 0;
        }
        if (in->opcode == OP_USE_COMPONENT && !known_name(code, OP_COMPONENT, in->args[0])) {
            source_error(path, in->line, "this component does not exist"); return 0;
        }
        if (in->opcode == OP_MIGRATION) {
            int from = atoi(in->args[0]), to = atoi(in->args[1]);
            if (to != from + 1) { source_error(path, in->line, "a data migration must move forward by one version"); return 0; }
            if (to > data_version) { source_error(path, in->line, "this migration goes beyond the declared data version"); return 0; }
            for (size_t other = 0; other < i; other++) if (code->items[other].opcode == OP_MIGRATION && atoi(code->items[other].args[0]) == from) {
                source_error(path, in->line, "this starting data version already has a migration"); return 0;
            }
        }
        if (in->opcode == OP_RENAME_FIELD) {
            if (!known_name(code, OP_MODEL, in->args[0])) { source_error(path, in->line, "the migration model does not exist"); return 0; }
            if (!strcmp(in->args[1], "id") || !strcmp(in->args[2], "id")) { source_error(path, in->line, "automatic id fields cannot be renamed"); return 0; }
            if (!model_has_field(code, in->args[0], in->args[2])) { source_error(path, in->line, "the renamed field must exist in the current model"); return 0; }
        }
        if ((in->opcode == OP_FIND_ALL || in->opcode == OP_FIND_ONE || in->opcode == OP_FIND_WHERE || in->opcode == OP_FIND_ORDERED || in->opcode == OP_FIND_SIGNED_IN || in->opcode == OP_SIGN_IN || in->opcode == OP_CREATE ||
            in->opcode == OP_UPDATE || in->opcode == OP_DELETE) && !known_name(code, OP_MODEL, in->args[0])) {
            char message[256]; snprintf(message, sizeof(message), "there is no model named %s", in->args[0]);
            source_error(path, in->line, message); return 0;
        }
        if (in->opcode == OP_FIELD && !strcmp(in->args[1], "reference") && !known_name(code, OP_MODEL, in->args[7])) {
            char message[256]; snprintf(message, sizeof(message), "there is no referenced model named %s", in->args[7]);
            source_error(path, in->line, message); return 0;
        }
        if (in->opcode == OP_FIND_ORDERED && !model_has_field(code, in->args[0], in->args[1])) {
            source_error(path, in->line, "the ordered field does not exist on this model"); return 0;
        }
        if (in->opcode == OP_FIND_WHERE && !model_has_field(code, in->args[0], in->args[1])) {
            source_error(path, in->line, "the filtered field does not exist on this model"); return 0;
        }
        if (in->opcode == OP_SIGN_IN) {
            const char *identity = model_field_kind(code, in->args[0], in->args[1]);
            const char *password = model_field_kind(code, in->args[0], in->args[2]);
            if (!identity) { source_error(path, in->line, "the sign-in identity field does not exist"); return 0; }
            if (!password || strcmp(password, "secret")) { source_error(path, in->line, "the sign-in password field must be secret"); return 0; }
        }
        if (in->opcode == OP_LAYOUT) {
            int contents = 0;
            for (size_t at = i + 1; at < code->count && code->items[at].opcode != OP_END_LAYOUT; at++) contents += code->items[at].opcode == OP_CONTENT;
            if (contents != 1) { source_error(path, in->line, "a layout must contain exactly one content instruction"); return 0; }
        }
        if ((in->opcode == OP_FIND_ONE || in->opcode == OP_FIND_WHERE || in->opcode == OP_UPDATE || in->opcode == OP_DELETE)) {
            size_t route = i;
            while (route && code->items[route].opcode != OP_ROUTE) route--;
            if (code->items[route].opcode != OP_ROUTE || !strstr(code->items[route].args[1], "{id}")) {
                source_error(path, in->line, "this instruction needs {id} in its route"); return 0;
            }
        }
        if (in->opcode == OP_SHOW_VIEW && !known_name(code, OP_VIEW, in->args[0])) {
            char message[256]; snprintf(message, sizeof(message), "there is no view named %s", in->args[0]);
            source_error(path, in->line, message); return 0;
        }
        if (in->opcode == OP_ROUTE) {
            for (size_t other = 0; other < i; other++) if (code->items[other].opcode == OP_ROUTE &&
                !strcmp(code->items[other].args[0], in->args[0]) && !strcmp(code->items[other].args[1], in->args[1])) {
                source_error(path, in->line, "this route is declared twice"); return 0;
            }
            size_t last = i;
            while (++last < code->count && code->items[last].opcode != OP_END_ROUTE) {}
            if (last == i + 1 || (code->items[last - 1].opcode != OP_SHOW_VIEW && code->items[last - 1].opcode != OP_SHOW_JSON && code->items[last - 1].opcode != OP_REDIRECT)) {
                source_error(path, in->line, "a route must finish by showing a view, showing JSON, or redirecting"); return 0;
            }
        }
        if (in->opcode == OP_EVENT) {
            size_t last = i;
            while (++last < code->count && code->items[last].opcode != OP_END_ROUTE) {}
            if (last == i + 1 || code->items[last - 1].opcode != OP_SHOW_VIEW) {
                source_error(path, in->line, "a start event must finish by showing a view"); return 0;
            }
        }
    }
    for (int version = 1; version < data_version; version++) {
        int found = 0;
        for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_MIGRATION && atoi(code->items[i].args[0]) == version) found = 1;
        if (!found) { source_error(path, 1, "every data version needs a migration to the next version"); return 0; }
    }
    if (!models || !controllers || (!views && strcmp(target, "api"))) {
        source_error(path, 1, "an MVC program needs at least one model, controller, and view"); return 0;
    }
    if (!strcmp(target, "web")) {
        int routes = 0;
        for (size_t i = 0; i < code->count; i++) routes += code->items[i].opcode == OP_ROUTE;
        if (!routes) { source_error(path, 1, "a web application needs at least one route"); return 0; }
    } else if (!strcmp(target, "console")) {
        int starts = 0;
        for (size_t i = 0; i < code->count; i++) starts += code->items[i].opcode == OP_EVENT;
        if (!starts) { source_error(path, 1, "a console application needs: when application starts"); return 0; }
    }
    return 1;
}

static int expand_source(const char *path, FILE *output, int depth) {
    if (depth > 32) { fprintf(stderr, "error: includes are nested too deeply near %s\n", path); return 0; }
    FILE *input = fopen(path, "r");
    if (!input) { fprintf(stderr, "error: cannot open included source %s: %s\n", path, strerror(errno)); return 0; }
    char line[MAX_LINE]; unsigned number = 0; int okay = 1;
    while (okay && fgets(line, sizeof(line), input)) {
        number++; char parsed[MAX_LINE]; snprintf(parsed, sizeof(parsed), "%s", line);
        char *parts[MAX_WORDS], error[128]; int count = words(parsed, parts, MAX_WORDS, error, sizeof(error));
        if (count < 0) { source_error(path, number, error); okay = 0; break; }
        if (count && !strcmp(parts[0], "include")) {
            if (count != 2) { source_error(path, number, "say: include \"models.hyp\""); okay = 0; break; }
            char included[PATH_MAX]; const char *slash = strrchr(path, '/');
            size_t directory = slash ? (size_t)(slash - path + 1) : 0;
            if (directory + strlen(parts[1]) + 1 > sizeof(included)) { source_error(path, number, "the included path is too long"); okay = 0; break; }
            if (directory) memcpy(included, path, directory);
            strcpy(included + directory, parts[1]);
            okay = expand_source(included, output, depth + 1);
        } else if (fputs(line, output) == EOF) okay = 0;
    }
    if (ferror(input)) okay = 0;
    fclose(input); return okay;
}

int compile_file(const char *source_path, const char *output_path) {
    FILE *file = tmpfile();
    if (!file) { fprintf(stderr, "error: cannot create compiler workspace: %s\n", strerror(errno)); return 1; }
    if (!expand_source(source_path, file, 0)) { fclose(file); return 1; }
    rewind(file);
    Bytecode code; bytecode_init(&code);
    Block stack[MAX_DEPTH] = {BLOCK_ROOT}; int depth = 1;
    char line[MAX_LINE], error[256]; unsigned number = 0; int okay = 1;
    int saw_application = 0;
    while (okay && fgets(line, sizeof(line), file)) {
        number++;
        if (!strchr(line, '\n') && !feof(file)) { source_error(source_path, number, "line is too long"); okay = 0; break; }
        char *w[MAX_WORDS]; int n = words(line, w, MAX_WORDS, error, sizeof(error));
        if (n < 0) { source_error(source_path, number, error); okay = 0; break; }
        if (!n) continue;
        Block current = stack[depth - 1];
        if (n == 1 && !strcmp(w[0], "end")) {
            if (depth == 1) { source_error(source_path, number, "this end does not close anything"); okay = 0; break; }
            int op = current == BLOCK_MODEL ? OP_END_MODEL : current == BLOCK_CONTROLLER ? OP_END_CONTROLLER :
                current == BLOCK_ROUTE ? OP_END_ROUTE : current == BLOCK_VIEW ? OP_END_VIEW :
                current == BLOCK_FORM ? OP_END_FORM : current == BLOCK_EACH ? OP_END_EACH : current == BLOCK_IF ? OP_END_IF :
                current == BLOCK_LAYOUT ? OP_END_LAYOUT : current == BLOCK_COMPONENT ? OP_END_COMPONENT :
                current == BLOCK_ACTION ? OP_END_ACTION : current == BLOCK_LOGIC_IF ? OP_END_LOGIC_IF :
                current == BLOCK_REPEAT ? OP_END_REPEAT : current == BLOCK_MIGRATION ? OP_END_MIGRATION : OP_END_TEST;
            okay = emit(&code, op, 0, NULL, number); depth--; continue;
        }
        if (current == BLOCK_ROOT) {
            if ((n == 2 || n == 4) && !strcmp(w[0], "application") &&
                (n == 2 || (!strcmp(w[2], "is") && (!strcmp(w[3], "web") || !strcmp(w[3], "console") ||
                !strcmp(w[3], "desktop") || !strcmp(w[3], "api") || !strcmp(w[3], "service") || !strcmp(w[3], "game"))))) {
                okay = emit(&code, OP_APPLICATION, 1, &w[1], number);
                if (okay) { char *target = n == 4 ? w[3] : "web"; okay = emit(&code, OP_TARGET, 1, &target, number); }
                saw_application = 1;
            }
            else if (n == 3 && !strcmp(w[0], "listen") && !strcmp(w[1], "on")) {
                char *end; long port = strtol(w[2], &end, 10);
                if (*end || port < 1 || port > 65535) { source_error(source_path, number, "the port must be between 1 and 65535"); okay = 0; }
                else okay = emit(&code, OP_PORT, 1, &w[2], number);
            } else if (n == 3 && !strcmp(w[0], "data") && !strcmp(w[1], "version")) {
                char *end; long version = strtol(w[2], &end, 10);
                if (*end || version < 1 || version > 1000000) { source_error(source_path, number, "data version must be a positive whole number"); okay = 0; }
                else okay = emit(&code, OP_DATA_VERSION, 1, &w[2], number);
            } else if (n == 7 && !strcmp(w[0], "when") && !strcmp(w[1], "data") && !strcmp(w[2], "changes") &&
                !strcmp(w[3], "from") && !strcmp(w[5], "to")) {
                char *from_end, *to_end; long from = strtol(w[4], &from_end, 10), to = strtol(w[6], &to_end, 10);
                if (*from_end || *to_end || from < 1 || to < 2) { source_error(source_path, number, "data migration versions must be positive whole numbers"); okay = 0; }
                else { char *args[2] = {w[4], w[6]}; okay = emit(&code, OP_MIGRATION, 2, args, number); stack[depth++] = BLOCK_MIGRATION; }
            } else if (n == 2 && !strcmp(w[0], "model") && is_name(w[1])) {
                okay = emit(&code, OP_MODEL, 1, &w[1], number); stack[depth++] = BLOCK_MODEL;
            } else if (n == 2 && !strcmp(w[0], "controller") && is_name(w[1])) {
                okay = emit(&code, OP_CONTROLLER, 1, &w[1], number); stack[depth++] = BLOCK_CONTROLLER;
            } else if (n == 2 && !strcmp(w[0], "view")) {
                okay = emit(&code, OP_VIEW, 1, &w[1], number); stack[depth++] = BLOCK_VIEW;
            } else if (n == 2 && !strcmp(w[0], "layout")) {
                okay = emit(&code, OP_LAYOUT, 1, &w[1], number); stack[depth++] = BLOCK_LAYOUT;
            } else if (n == 2 && !strcmp(w[0], "component")) {
                okay = emit(&code, OP_COMPONENT, 1, &w[1], number); stack[depth++] = BLOCK_COMPONENT;
            } else if (n == 6 && !strcmp(w[0], "when") && !strcmp(w[1], "error") && !strcmp(w[3], "show") && !strcmp(w[4], "view")) {
                char *args[2] = {w[2], w[5]}; okay = emit(&code, OP_ERROR_VIEW, 2, args, number);
            } else if (n == 6 && !strcmp(w[0], "serve") && !strcmp(w[1], "files") && !strcmp(w[2], "from") && !strcmp(w[4], "at")) {
                if (w[5][0] != '/') { source_error(source_path, number, "the public file address must start with /"); okay = 0; }
                else { char *args[2] = {w[3], w[5]}; okay = emit(&code, OP_STATIC_FILES, 2, args, number); }
            } else { source_error(source_path, number, "expected application, data version, data migration, listen on, model, controller, view, layout, or component"); okay = 0; }
        } else if (current == BLOCK_MIGRATION) {
            if (n == 8 && !strcmp(w[0], "rename") && !strcmp(w[1], "field") && !strcmp(w[3], "to") &&
                !strcmp(w[5], "in") && !strcmp(w[6], "model") && is_name(w[2]) && is_name(w[4]) && is_name(w[7])) {
                char *args[3] = {w[7], w[2], w[4]}; okay = emit(&code, OP_RENAME_FIELD, 3, args, number);
            } else { source_error(source_path, number, "say: rename field old_name to new_name in model ModelName"); okay = 0; }
        } else if (current == BLOCK_MODEL) {
            if (n >= 4 && n <= 13 && !strcmp(w[0], "field") && !strcmp(w[2], "is") &&
                (!strcmp(w[3], "text") || !strcmp(w[3], "number") || !strcmp(w[3], "boolean") || !strcmp(w[3], "secret") || !strcmp(w[3], "reference")) && is_name(w[1])) {
                char *args[9] = {w[1], w[3], "false", "", "false", "", "", "", "false"};
                int at = 4;
                if (!strcmp(w[3], "reference")) {
                    if (at >= n || !is_name(w[at])) { source_error(source_path, number, "say: field author is reference User"); okay = 0; }
                    else args[7] = w[at++];
                }
                while (at < n) {
                    if (!strcmp(w[at], "required")) { args[2] = "true"; at++; }
                    else if (!strcmp(w[at], "unique")) { args[4] = "true"; at++; }
                    else if (!strcmp(w[at], "default") && at + 1 < n) { args[3] = w[at + 1]; at += 2; }
                    else if (!strcmp(w[at], "minimum") && at + 1 < n) { args[5] = w[at + 1]; at += 2; }
                    else if (!strcmp(w[at], "maximum") && at + 1 < n) { args[6] = w[at + 1]; at += 2; }
                    else if (!strcmp(w[at], "protected")) { args[8] = "true"; at++; }
                    else { source_error(source_path, number, "unknown field rule; use required, unique, protected, default, minimum, or maximum"); okay = 0; break; }
                }
                if (okay && !strcmp(w[1], "id")) { source_error(source_path, number, "id is automatic; choose a different field name"); okay = 0; }
                int field_count = 0;
                for (size_t i = code.count; okay && i > 0 && code.items[i - 1].opcode != OP_MODEL; i--)
                    if (code.items[i - 1].opcode == OP_FIELD) {
                        field_count++;
                        if (!strcmp(code.items[i - 1].args[0], w[1])) { source_error(source_path, number, "this field is declared twice"); okay = 0; }
                    }
                if (okay && field_count >= 31) { source_error(source_path, number, "a model can have at most 31 fields plus its automatic id"); okay = 0; }
                if (okay && !strcmp(w[3], "number") && *args[3]) { char *end; strtod(args[3], &end); if (*end) { source_error(source_path, number, "a number field needs a numeric default"); okay = 0; } }
                if (okay && !strcmp(w[3], "boolean") && *args[3] && strcmp(args[3], "true") && strcmp(args[3], "false")) {
                    source_error(source_path, number, "a boolean default must be true or false"); okay = 0;
                }
                if (okay && !strcmp(w[3], "secret") && *args[3]) { source_error(source_path, number, "secret fields cannot have default values"); okay = 0; }
                if (okay && *args[5]) { char *end; strtod(args[5], &end); if (*end) { source_error(source_path, number, "minimum must be a number"); okay = 0; } }
                if (okay && *args[6]) { char *end; strtod(args[6], &end); if (*end) { source_error(source_path, number, "maximum must be a number"); okay = 0; } }
                if (okay && *args[5] && *args[6] && strtod(args[5], NULL) > strtod(args[6], NULL)) {
                    source_error(source_path, number, "minimum cannot be greater than maximum"); okay = 0;
                }
                if (okay) okay = emit(&code, OP_FIELD, 9, args, number);
            } else { source_error(source_path, number, "say: field name is text [required] [unique] [default value] [minimum value] [maximum value]"); okay = 0; }
        } else if (current == BLOCK_CONTROLLER) {
            const char *method = NULL;
            if (n == 2 && !strcmp(w[0], "action") && *w[1]) {
                okay = emit(&code, OP_ACTION, 1, &w[1], number); stack[depth++] = BLOCK_ACTION; continue;
            }
            if (n == 2 && !strcmp(w[0], "test") && *w[1]) {
                okay = emit(&code, OP_TEST, 1, &w[1], number); stack[depth++] = BLOCK_TEST; continue;
            }
            if (n == 6 && !strcmp(w[0], "before") && !strcmp(w[1], "every") && !strcmp(w[2], "route") &&
                !strcmp(w[3], "run") && !strcmp(w[4], "action")) {
                okay = emit(&code, OP_BEFORE_ACTION, 1, &w[5], number); continue;
            }
            if (n == 3 && !strcmp(w[0], "when") && !strcmp(w[1], "application") && !strcmp(w[2], "starts")) {
                char *event = "START"; okay = emit(&code, OP_EVENT, 1, &event, number); stack[depth++] = BLOCK_ROUTE; continue;
            }
            if (n == 4 && !strcmp(w[0], "when") && !strcmp(w[1], "someone")) {
                if (!strcmp(w[2], "visits")) method = "GET";
                else if (!strcmp(w[2], "submits")) method = "POST";
            }
            if (!method || w[3][0] != '/') { source_error(source_path, number, "say: when someone visits \"/path\""); okay = 0; }
            else { char *args[2] = {(char *)method, w[3]}; okay = emit(&code, OP_ROUTE, 2, args, number); stack[depth++] = BLOCK_ROUTE; }
        } else if (current == BLOCK_ROUTE || current == BLOCK_ACTION || current == BLOCK_LOGIC_IF || current == BLOCK_REPEAT || current == BLOCK_TEST) {
            if (n == 5 && !strcmp(w[0], "find") && !strcmp(w[1], "all") && !strcmp(w[3], "as")) {
                char *args[2] = {w[2], w[4]}; okay = emit(&code, OP_FIND_ALL, 2, args, number);
            } else if (n == 8 && !strcmp(w[0], "find") && !strcmp(w[1], "all") && !strcmp(w[3], "ordered") &&
                !strcmp(w[4], "by") && !strcmp(w[6], "as")) {
                char *args[3] = {w[2], w[5], w[7]}; okay = emit(&code, OP_FIND_ORDERED, 3, args, number);
            } else if (n == 9 && !strcmp(w[0], "find") && !strcmp(w[2], "where") && !strcmp(w[4], "is") &&
                !strcmp(w[5], "route") && !strcmp(w[6], "id") && !strcmp(w[7], "as")) {
                char *args[4] = {w[1], w[3], "id", w[8]}; okay = emit(&code, OP_FIND_WHERE, 4, args, number);
            } else if (n == 7 && !strcmp(w[0], "find") && !strcmp(w[2], "by") && !strcmp(w[3], "route") &&
                !strcmp(w[4], "id") && !strcmp(w[5], "as")) {
                char *args[2] = {w[1], w[6]}; okay = emit(&code, OP_FIND_ONE, 2, args, number);
            } else if (n == 4 && !strcmp(w[0], "create") && !strcmp(w[2], "from") && !strcmp(w[3], "form")) {
                okay = emit(&code, OP_CREATE, 1, &w[1], number);
            } else if (n == 7 && !strcmp(w[0], "update") && !strcmp(w[2], "using") && !strcmp(w[3], "route") &&
                !strcmp(w[4], "id") && !strcmp(w[5], "from") && !strcmp(w[6], "form")) {
                okay = emit(&code, OP_UPDATE, 1, &w[1], number);
            } else if (n == 5 && !strcmp(w[0], "delete") && !strcmp(w[2], "using") && !strcmp(w[3], "route") && !strcmp(w[4], "id")) {
                okay = emit(&code, OP_DELETE, 1, &w[1], number);
            } else if (n == 4 && !strcmp(w[0], "ask") && !strcmp(w[2], "as") && is_name(w[3])) {
                char *args[2] = {w[1], w[3]}; okay = emit(&code, OP_ASK, 2, args, number);
            } else if (n == 7 && !strcmp(w[0], "sign") && !strcmp(w[1], "in") && !strcmp(w[3], "using") && !strcmp(w[5], "and")) {
                char *args[3] = {w[2], w[4], w[6]}; okay = emit(&code, OP_SIGN_IN, 3, args, number);
            } else if (n == 2 && !strcmp(w[0], "sign") && !strcmp(w[1], "out")) {
                okay = emit(&code, OP_SIGN_OUT, 0, NULL, number);
            } else if (n == 7 && !strcmp(w[0], "require") && !strcmp(w[1], "sign") && !strcmp(w[2], "in") &&
                !strcmp(w[3], "or") && !strcmp(w[4], "redirect") && !strcmp(w[5], "to")) {
                okay = emit(&code, OP_REQUIRE_SIGN_IN, 1, &w[6], number);
            } else if (n == 8 && !strcmp(w[0], "require") && !strcmp(w[2], "is") && !strcmp(w[4], "or") &&
                !strcmp(w[5], "redirect") && !strcmp(w[6], "to")) {
                char *args[3] = {w[1], w[3], w[7]}; okay = emit(&code, OP_REQUIRE_VALUE, 3, args, number);
            } else if (n >= 4 && !strcmp(w[0], "set") && !strcmp(w[2], "to") && is_name(w[1])) {
                char expression[MAX_LINE]; join_words(expression, sizeof(expression), w, 3, n);
                char *args[2] = {w[1], expression}; okay = emit(&code, OP_SET_VALUE, 2, args, number);
            } else if (n == 6 && !strcmp(w[0], "read") && !strcmp(w[2], "from") && !strcmp(w[3], "form") &&
                !strcmp(w[4], "as") && is_name(w[5])) {
                char *args[2] = {w[1], w[5]}; okay = emit(&code, OP_READ_FORM, 2, args, number);
            } else if (n >= 2 && !strcmp(w[0], "if")) {
                char expression[MAX_LINE]; join_words(expression, sizeof(expression), w, 1, n);
                char *arg = expression; okay = emit(&code, OP_LOGIC_IF, 1, &arg, number); stack[depth++] = BLOCK_LOGIC_IF;
            } else if (n == 1 && !strcmp(w[0], "otherwise") && current == BLOCK_LOGIC_IF) {
                okay = emit(&code, OP_OTHERWISE, 0, NULL, number);
            } else if (n >= 3 && !strcmp(w[0], "repeat") && !strcmp(w[n - 1], "times")) {
                char expression[MAX_LINE]; join_words(expression, sizeof(expression), w, 1, n - 1);
                char *arg = expression; okay = emit(&code, OP_REPEAT, 1, &arg, number); stack[depth++] = BLOCK_REPEAT;
            } else if (n == 3 && !strcmp(w[0], "run") && !strcmp(w[1], "action")) {
                okay = emit(&code, OP_RUN_ACTION, 1, &w[2], number);
            } else if (n >= 5 && !strcmp(w[0], "expect") && !strcmp(w[2], "to") && !strcmp(w[3], "be") && current == BLOCK_TEST) {
                char expected[MAX_LINE], condition[MAX_LINE]; join_words(expected, sizeof(expected), w, 4, n);
                if (strlen(w[1]) + strlen(expected) + 5 >= sizeof(condition)) {
                    source_error(source_path, number, "this expectation is too long"); okay = 0; continue;
                }
                size_t name_length = strlen(w[1]), expected_length = strlen(expected);
                memcpy(condition, w[1], name_length); memcpy(condition + name_length, " is ", 4);
                memcpy(condition + name_length + 4, expected, expected_length + 1);
                char *arg = condition; okay = emit(&code, OP_EXPECT, 1, &arg, number);
            } else if (n == 6 && !strcmp(w[0], "find") && !strcmp(w[1], "signed") && !strcmp(w[2], "in") && !strcmp(w[4], "as")) {
                char *args[2] = {w[3], w[5]}; okay = emit(&code, OP_FIND_SIGNED_IN, 2, args, number);
            } else if ((n == 3 || n == 5) && !strcmp(w[0], "show") && !strcmp(w[1], "view") &&
                (n == 3 || !strcmp(w[3], "with"))) {
                char *args[2] = {w[2], n == 5 ? w[4] : ""}; okay = emit(&code, OP_SHOW_VIEW, 2, args, number);
            } else if (n == 3 && !strcmp(w[0], "show") && !strcmp(w[1], "json")) {
                okay = emit(&code, OP_SHOW_JSON, 1, &w[2], number);
            } else if (n == 3 && !strcmp(w[0], "redirect") && !strcmp(w[1], "to")) {
                okay = emit(&code, OP_REDIRECT, 1, &w[2], number);
            } else { source_error(source_path, number, "unknown controller instruction"); okay = 0; }
        } else {
            if ((current == BLOCK_VIEW || current == BLOCK_LAYOUT || current == BLOCK_COMPONENT || current == BLOCK_FORM || current == BLOCK_EACH || current == BLOCK_IF) &&
                n == 2 && !strcmp(w[0], "title")) okay = emit(&code, OP_TITLE, 1, &w[1], number);
            else if (n == 2 && !strcmp(w[0], "heading")) okay = emit(&code, OP_HEADING, 1, &w[1], number);
            else if (n == 2 && !strcmp(w[0], "text")) okay = emit(&code, OP_TEXT, 1, &w[1], number);
            else if (n == 2 && !strcmp(w[0], "say")) okay = emit(&code, OP_SAY, 1, &w[1], number);
            else if (n == 2 && !strcmp(w[0], "style")) okay = emit(&code, OP_STYLE, 1, &w[1], number);
            else if (n == 2 && !strcmp(w[0], "script")) okay = emit(&code, OP_SCRIPT, 1, &w[1], number);
            else if (n == 5 && !strcmp(w[0], "image") && !strcmp(w[2], "described") && !strcmp(w[3], "as")) {
                char *args[2] = {w[1], w[4]}; okay = emit(&code, OP_IMAGE, 2, args, number);
            }
            else if (n == 3 && !strcmp(w[0], "use") && !strcmp(w[1], "layout") && current == BLOCK_VIEW)
                okay = emit(&code, OP_USE_LAYOUT, 1, &w[2], number);
            else if (n == 3 && !strcmp(w[0], "use") && !strcmp(w[1], "component"))
                okay = emit(&code, OP_USE_COMPONENT, 1, &w[2], number);
            else if (n == 1 && !strcmp(w[0], "content") && current == BLOCK_LAYOUT) okay = emit(&code, OP_CONTENT, 0, NULL, number);
            else if (n == 2 && !strcmp(w[0], "show")) okay = emit(&code, OP_SHOW_VALUE, 1, &w[1], number);
            else if (n == 4 && !strcmp(w[0], "link") && !strcmp(w[2], "to")) { char *a[2] = {w[1], w[3]}; okay = emit(&code, OP_LINK, 2, a, number); }
            else if (n == 4 && !strcmp(w[0], "form") && (!strcmp(w[1], "posts") || !strcmp(w[1], "gets")) && !strcmp(w[2], "to")) {
                char *a[2] = {w[1], w[3]}; okay = emit(&code, OP_FORM, 2, a, number); stack[depth++] = BLOCK_FORM;
            } else if ((n == 4 || n == 5) && !strcmp(w[0], "input") && !strcmp(w[2], "as") && (n == 4 || !strcmp(w[4], "required"))) {
                char *a[4] = {w[1], w[3], n == 5 ? "true" : "false", "text"}; okay = emit(&code, OP_INPUT, 4, a, number);
            } else if ((n == 5 || n == 6) && !strcmp(w[0], "secret") && !strcmp(w[1], "input") && !strcmp(w[3], "as") &&
                (n == 5 || !strcmp(w[5], "required"))) {
                char *a[4] = {w[2], w[4], n == 6 ? "true" : "false", "secret"}; okay = emit(&code, OP_INPUT, 4, a, number);
            } else if ((n == 4 || n == 5) && !strcmp(w[0], "textarea") && !strcmp(w[2], "as") && (n == 4 || !strcmp(w[4], "required"))) {
                char *a[3] = {w[1], w[3], n == 5 ? "true" : "false"}; okay = emit(&code, OP_TEXTAREA, 3, a, number);
            } else if (n == 4 && !strcmp(w[0], "checkbox") && !strcmp(w[2], "as")) {
                char *a[2] = {w[1], w[3]}; okay = emit(&code, OP_CHECKBOX, 2, a, number);
            } else if (n == 2 && !strcmp(w[0], "button")) okay = emit(&code, OP_BUTTON, 1, &w[1], number);
            else if (n == 6 && !strcmp(w[0], "for") && !strcmp(w[1], "each") && !strcmp(w[3], "in") && !strcmp(w[5], "show")) {
                char *a[2] = {w[2], w[4]}; okay = emit(&code, OP_EACH, 2, a, number); stack[depth++] = BLOCK_EACH;
            } else if (n == 2 && !strcmp(w[0], "if")) { okay = emit(&code, OP_IF, 1, &w[1], number); stack[depth++] = BLOCK_IF; }
            else { source_error(source_path, number, "unknown view instruction"); okay = 0; }
        }
        if (depth >= MAX_DEPTH) { source_error(source_path, number, "blocks are nested too deeply"); okay = 0; }
    }
    if (ferror(file)) { fprintf(stderr, "error: could not read %s\n", source_path); okay = 0; }
    fclose(file);
    if (okay && depth != 1) { source_error(source_path, number, "a block is missing its end"); okay = 0; }
    if (okay && !saw_application) { source_error(source_path, 1, "start the program with application \"Name\""); okay = 0; }
    if (okay) okay = validate(&code, source_path);
    if (okay) {
        if (!bytecode_write(&code, output_path, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); okay = 0; }
        else printf("Compiled %s -> %s (%zu instructions)\n", source_path, output_path, code.count);
    }
    bytecode_free(&code);
    return okay ? 0 : 1;
}
