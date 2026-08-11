#include "hyperian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct HyperianMobile {
    Bytecode code;
    HyperianData *data;
    HyperianState state;
    char view[256];
};

typedef struct { char *text; size_t size; size_t used; int okay; int first; } Json;

static void json_raw(Json *json, const char *text) {
    if (!json->okay) return;
    size_t length = strlen(text);
    if (length >= json->size - json->used) { json->okay = 0; return; }
    memcpy(json->text + json->used, text, length + 1); json->used += length;
}

static void json_string(Json *json, const char *text) {
    json_raw(json, "\"");
    for (const unsigned char *at = (const unsigned char *)(text ? text : ""); json->okay && *at; at++) {
        char escaped[8];
        if (*at == '"' || *at == '\\') { escaped[0] = '\\'; escaped[1] = (char)*at; escaped[2] = 0; json_raw(json, escaped); }
        else if (*at == '\n') json_raw(json, "\\n");
        else if (*at == '\r') json_raw(json, "\\r");
        else if (*at == '\t') json_raw(json, "\\t");
        else if (*at < 32) { snprintf(escaped, sizeof(escaped), "\\u%04x", *at); json_raw(json, escaped); }
        else { escaped[0] = (char)*at; escaped[1] = 0; json_raw(json, escaped); }
    }
    json_raw(json, "\"");
}

static void json_property(Json *json, const char *name, const char *value) {
    json_string(json, name); json_raw(json, ":"); json_string(json, value);
}

static void control_start(Json *json, const char *kind) {
    if (!json->first) json_raw(json, ",");
    json->first = 0;
    json_raw(json, "{"); json_property(json, "kind", kind);
}

static void control_value(Json *json, const char *name, const char *value) {
    json_raw(json, ","); json_property(json, name, value);
}

static size_t matching_end(const Bytecode *code, size_t start, uint8_t open, uint8_t close) {
    int depth = 1;
    for (size_t i = start + 1; i < code->count; i++) {
        if (code->items[i].opcode == open) depth++;
        else if (code->items[i].opcode == close && --depth == 0) return i;
    }
    return code->count;
}

static const char *declared_target(const Bytecode *code) {
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_TARGET) return code->items[i].args[0];
    return "web";
}

static const char *starting_view(const Bytecode *code) {
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_EVENT && !strcmp(code->items[i].args[0], "START")) {
        size_t end = matching_end(code, i, OP_EVENT, OP_END_ROUTE);
        for (i++; i < end; i++) if (code->items[i].opcode == OP_SHOW_VIEW) return code->items[i].args[0];
    }
    return NULL;
}

static int list_next(const char **cursor, char *value, size_t value_size) {
    if (!**cursor) return 0;
    char *end; unsigned long length = strtoul(*cursor, &end, 10);
    if (end == *cursor || *end != ':' || length >= value_size || strlen(end + 1) < length) return -1;
    memcpy(value, end + 1, length); value[length] = 0; *cursor = end + 1 + length; return 1;
}

static int value_is_true(const char *value) {
    return value && *value && strcmp(value, "false") && strcmp(value, "0");
}

static void render_controls(HyperianMobile *mobile, Json *json, size_t from, size_t to) {
    for (size_t i = from; json->okay && i < to; i++) {
        Instruction *in = &mobile->code.items[i]; char value[HYPERIAN_VALUE_SIZE];
        if (in->opcode == OP_EACH) {
            size_t end = matching_end(&mobile->code, i, OP_EACH, OP_END_EACH);
            const char *encoded = hyperian_state_get(&mobile->state, in->args[1]); const char *cursor = encoded ? encoded : ""; int next;
            while ((next = list_next(&cursor, value, sizeof(value))) > 0) {
                hyperian_state_set(&mobile->state, in->args[0], value); render_controls(mobile, json, i + 1, end);
            }
            if (next < 0) json->okay = 0;
            i = end; continue;
        }
        if (in->opcode == OP_IF) {
            size_t end = matching_end(&mobile->code, i, OP_IF, OP_END_IF);
            hyperian_state_evaluate(&mobile->state, in->args[0], value, sizeof(value));
            if (value_is_true(value)) render_controls(mobile, json, i + 1, end);
            i = end; continue;
        }
        const char *kind = NULL;
        if (in->opcode == OP_HEADING) kind = "heading";
        else if (in->opcode == OP_TEXT) kind = "text";
        else if (in->opcode == OP_SHOW_VALUE) kind = "value";
        else if (in->opcode == OP_INPUT) kind = "input";
        else if (in->opcode == OP_TEXTAREA) kind = "textarea";
        else if (in->opcode == OP_CHECKBOX) kind = "checkbox";
        else if (in->opcode == OP_BUTTON_ACTION || in->opcode == OP_BUTTON) kind = "button";
        else if (in->opcode == OP_LINK) kind = "link";
        else if (in->opcode == OP_IMAGE) kind = "image";
        if (!kind) continue;
        control_start(json, kind);
        if (in->opcode == OP_SHOW_VALUE) {
            hyperian_state_evaluate(&mobile->state, in->args[0], value, sizeof(value)); control_value(json, "text", value);
        } else if (in->opcode == OP_INPUT || in->opcode == OP_TEXTAREA || in->opcode == OP_CHECKBOX) {
            control_value(json, "label", in->args[0]); control_value(json, "name", in->args[1]);
            control_value(json, "value", hyperian_state_get(&mobile->state, in->args[1]) ? hyperian_state_get(&mobile->state, in->args[1]) : "");
            json_raw(json, ",\"required\":"); json_raw(json, !strcmp(in->args[2], "true") ? "true" : "false");
        } else if (in->opcode == OP_BUTTON_ACTION) {
            control_value(json, "label", in->args[0]); control_value(json, "action", in->args[1]);
        } else if (in->opcode == OP_BUTTON) { control_value(json, "label", in->args[0]); control_value(json, "action", ""); }
        else if (in->opcode == OP_LINK) { control_value(json, "label", in->args[0]); control_value(json, "destination", in->args[1]); }
        else if (in->opcode == OP_IMAGE) { control_value(json, "source", in->args[0]); control_value(json, "description", in->args[1]); }
        else control_value(json, "text", in->args[0]);
        json_raw(json, "}");
    }
}

static int apply_navigation(HyperianMobile *mobile, char *error, size_t error_size) {
    const char *next = hyperian_state_get(&mobile->state, "__hyperian_open_view");
    if (!next || !*next) return 1;
    int found = 0;
    for (size_t i = 0; i < mobile->code.count; i++) if (mobile->code.items[i].opcode == OP_VIEW && !strcmp(mobile->code.items[i].args[0], next)) found = 1;
    if (!found) { snprintf(error, error_size, "view %s does not exist", next); return 0; }
    snprintf(mobile->view, sizeof(mobile->view), "%s", next); hyperian_state_set(&mobile->state, "current_view", next);
    hyperian_state_set(&mobile->state, "__hyperian_open_view", ""); return 1;
}

HyperianMobile *hyperian_mobile_open(const char *bytecode_path, char *error, size_t error_size) {
    HyperianMobile *mobile = calloc(1, sizeof(*mobile));
    if (!mobile) { snprintf(error, error_size, "there is not enough memory for the mobile runtime"); return NULL; }
    bytecode_init(&mobile->code);
    if (!bytecode_read(&mobile->code, bytecode_path, error, error_size)) { free(mobile); return NULL; }
    if (strcmp(declared_target(&mobile->code), "mobile")) {
        snprintf(error, error_size, "the compiled application is not declared as mobile"); bytecode_free(&mobile->code); free(mobile); return NULL;
    }
    const char *view = starting_view(&mobile->code);
    if (!view) { snprintf(error, error_size, "the mobile application has no starting view"); bytecode_free(&mobile->code); free(mobile); return NULL; }
    snprintf(mobile->view, sizeof(mobile->view), "%s", view); hyperian_state_init(&mobile->state);
    mobile->data = hyperian_data_open(&mobile->code, error, error_size);
    if (!mobile->data) { bytecode_free(&mobile->code); free(mobile); return NULL; }
    return mobile;
}

void hyperian_mobile_close(HyperianMobile *mobile) {
    if (!mobile) return;
    hyperian_data_close(mobile->data); bytecode_free(&mobile->code); free(mobile);
}

int hyperian_mobile_start(HyperianMobile *mobile, char *error, size_t error_size) {
    if (!mobile) { snprintf(error, error_size, "the mobile runtime is not open"); return 0; }
    if (!hyperian_execute_data_event(mobile->data, "START", &mobile->state, error, error_size)) return 0;
    hyperian_state_set(&mobile->state, "current_view", mobile->view); return apply_navigation(mobile, error, error_size);
}

int hyperian_mobile_set(HyperianMobile *mobile, const char *name, const char *value, char *error, size_t error_size) {
    if (!mobile || !name || !*name || !value) { snprintf(error, error_size, "set a named mobile value"); return 0; }
    hyperian_state_set(&mobile->state, name, value); return 1;
}

int hyperian_mobile_run_action(HyperianMobile *mobile, const char *action, const char *input, char *error, size_t error_size) {
    if (!mobile || !action || !*action) { snprintf(error, error_size, "run a named mobile action"); return 0; }
    return hyperian_execute_data_action(mobile->data, action, input, &mobile->state, error, error_size) && apply_navigation(mobile, error, error_size);
}

int hyperian_mobile_send_event(HyperianMobile *mobile, const char *event, char *error, size_t error_size) {
    if (!mobile || !event || !*event) { snprintf(error, error_size, "send a named mobile event"); return 0; }
    return hyperian_execute_data_event(mobile->data, event, &mobile->state, error, error_size) && apply_navigation(mobile, error, error_size);
}

int hyperian_mobile_render_json(HyperianMobile *mobile, char *output, size_t output_size, char *error, size_t error_size) {
    if (!mobile || !output || output_size < 3) { snprintf(error, error_size, "provide room for the rendered mobile interface"); return 0; }
    output[0] = 0; Json json = {.text = output, .size = output_size, .okay = 1, .first = 1};
    json_raw(&json, "{"); json_property(&json, "view", mobile->view); json_raw(&json, ",\"controls\":[");
    int found = 0;
    for (size_t i = 0; i < mobile->code.count; i++) if (mobile->code.items[i].opcode == OP_VIEW && !strcmp(mobile->code.items[i].args[0], mobile->view)) {
        found = 1; render_controls(mobile, &json, i + 1, matching_end(&mobile->code, i, OP_VIEW, OP_END_VIEW)); break;
    }
    json_raw(&json, "]}");
    if (!found) { snprintf(error, error_size, "view %s does not exist", mobile->view); return 0; }
    if (!json.okay) { if (output_size) output[0] = 0; snprintf(error, error_size, "the rendered mobile interface needs a larger output buffer"); return 0; }
    return 1;
}

const char *hyperian_mobile_value(const HyperianMobile *mobile, const char *name) {
    return mobile && name ? hyperian_state_get(&mobile->state, name) : NULL;
}
