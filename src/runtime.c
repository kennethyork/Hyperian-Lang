#include "hyperian.h"
#include "security.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef HYPERIAN_HAVE_SQLITE3
#include <sqlite3.h>
#endif

#define REQUEST_MAX 65536
#define FIELD_MAX 32

typedef struct { char *data; size_t length, capacity; } Buffer;
typedef struct { char *key, *value; } Pair;
typedef struct Record { char *model; Pair fields[FIELD_MAX]; int field_count; struct Record *next; } Record;
struct HyperianData { const Bytecode *code; Record *records; };
typedef struct Session { char token[65]; char *model, *record_id; struct Session *next; } Session;
typedef struct { Pair pairs[FIELD_MAX]; int count; } Form;
typedef HyperianState VariableSet;
typedef struct {
    const char *collection_alias, *model, *item_alias;
    Record *item;
    Pair *variables;
    int variable_count;
    const char *filter_field, *filter_value, *order_field;
    const char *raw_content;
    VariableSet *locals;
    HyperianData *data;
} Scope;

static const char *resolve(Scope *scope, const char *expression);
static const char *field_kind(const Bytecode *code, const char *model, const char *field);
static int field_protected(const Bytecode *code, const char *model, const char *field);
static int create_record(const Bytecode *code, const char *model, Form *form, Record **records, char *error, size_t error_size);
static int update_record(const Bytecode *code, const char *model, const char *id, Form *form, Record *records, char *error, size_t error_size);
static int delete_record(Record **records, const char *model, const char *id);
static int save_records(Record *records, const Bytecode *code);
static Record *find_record(Record *records, const char *model, const char *id);

static volatile sig_atomic_t keep_running = 1;
static void stop_server(int signal_number) { (void)signal_number; keep_running = 0; }

static char *copy_string(const char *s) { size_t n = strlen(s) + 1; char *r = malloc(n); if (r) memcpy(r, s, n); return r; }

static void buffer_init(Buffer *b) { b->data = NULL; b->length = b->capacity = 0; }
static int buffer_grow(Buffer *b, size_t extra) {
    if (b->length + extra + 1 <= b->capacity) return 1;
    size_t size = b->capacity ? b->capacity * 2 : 1024;
    while (size < b->length + extra + 1) size *= 2;
    char *data = realloc(b->data, size); if (!data) return 0;
    b->data = data; b->capacity = size; return 1;
}
static int buffer_addn(Buffer *b, const char *s, size_t n) {
    if (!buffer_grow(b, n)) return 0;
    memcpy(b->data + b->length, s, n);
    b->length += n;
    b->data[b->length] = 0;
    return 1;
}
static int buffer_add(Buffer *b, const char *s) { return buffer_addn(b, s, strlen(s)); }
static int buffer_format(Buffer *b, const char *format, ...) {
    va_list args, copy; va_start(args, format); va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy); va_end(copy);
    if (n < 0 || !buffer_grow(b, (size_t)n)) { va_end(args); return 0; }
    vsnprintf(b->data + b->length, b->capacity - b->length, format, args); va_end(args); b->length += (size_t)n; return 1;
}
static void buffer_htmln(Buffer *b, const char *s, size_t length) {
    for (size_t i = 0; i < length; i++, s++) {
        if (*s == '&') buffer_add(b, "&amp;"); else if (*s == '<') buffer_add(b, "&lt;");
        else if (*s == '>') buffer_add(b, "&gt;"); else if (*s == '"') buffer_add(b, "&quot;");
        else buffer_addn(b, s, 1);
    }
}
static void buffer_html(Buffer *b, const char *s) { buffer_htmln(b, s, strlen(s)); }

static void buffer_dynamic_html(Buffer *b, const char *text, Scope *scope) {
    while (*text) {
        const char *open = strchr(text, '{');
        if (!open) { buffer_html(b, text); return; }
        buffer_htmln(b, text, (size_t)(open - text));
        const char *close = strchr(open + 1, '}');
        if (!close) { buffer_html(b, open); return; }
        char expression[256]; size_t length = (size_t)(close - open - 1);
        if (length >= sizeof(expression)) length = sizeof(expression) - 1;
        memcpy(expression, open + 1, length); expression[length] = 0;
        buffer_html(b, resolve(scope, expression)); text = close + 1;
    }
}

static const char *pair_value(const Pair *pairs, int count, const char *key) {
    for (int i = 0; i < count; i++) if (!strcmp(pairs[i].key, key)) return pairs[i].value;
    return NULL;
}

static const char *local_value(VariableSet *locals, const char *name) {
    if (!locals) return NULL;
    for (int i = 0; i < locals->count; i++) if (!strcmp(locals->names[i], name)) return locals->values[i];
    return NULL;
}

static void local_set(VariableSet *locals, const char *name, const char *value) {
    if (!locals) return;
    int at = locals->count;
    for (int i = 0; i < locals->count; i++) if (!strcmp(locals->names[i], name)) { at = i; break; }
    if (at == HYPERIAN_STATE_MAX) return;
    if (at == locals->count) { snprintf(locals->names[at], sizeof(locals->names[at]), "%s", name); locals->count++; }
    snprintf(locals->values[at], sizeof(locals->values[at]), "%s", value);
}

void hyperian_state_init(HyperianState *state) { memset(state, 0, sizeof(*state)); }

const char *hyperian_state_get(const HyperianState *state, const char *name) {
    return local_value((VariableSet *)state, name);
}

void hyperian_state_set(HyperianState *state, const char *name, const char *value) { local_set(state, name, value); }

static int list_next(const char **cursor, char *value, size_t value_size) {
    if (!**cursor) return 0;
    char *end; unsigned long length = strtoul(*cursor, &end, 10);
    if (end == *cursor || *end != ':' || length >= value_size || strlen(end + 1) < length) return -1;
    memcpy(value, end + 1, length); value[length] = 0; *cursor = end + 1 + length; return 1;
}

static int list_add_value(const char *encoded, const char *value, char *output, size_t size) {
    int written = snprintf(output, size, "%s%zu:%s", encoded ? encoded : "", strlen(value), value);
    return written >= 0 && (size_t)written < size;
}

static int list_count_values(const char *encoded) {
    const char *cursor = encoded ? encoded : ""; char value[2048]; int count = 0, result;
    while ((result = list_next(&cursor, value, sizeof(value))) > 0) count++;
    return result < 0 ? -1 : count;
}

static int list_item_value(const char *encoded, int wanted, char *output, size_t size) {
    const char *cursor = encoded ? encoded : ""; char value[2048]; int at = 1, result;
    while ((result = list_next(&cursor, value, sizeof(value))) > 0) {
        if (at++ == wanted) { snprintf(output, size, "%s", value); return 1; }
    }
    return 0;
}

static int list_remove_value(const char *encoded, const char *removed, char *output, size_t size) {
    const char *cursor = encoded ? encoded : ""; char value[2048]; int result, found = 0; output[0] = 0;
    while ((result = list_next(&cursor, value, sizeof(value))) > 0) {
        if (!found && !strcmp(value, removed)) { found = 1; continue; }
        char combined[2048]; if (!list_add_value(output, value, combined, sizeof(combined))) return 0;
        snprintf(output, size, "%s", combined);
    }
    return result >= 0;
}

static int map_put_value(const char *encoded, const char *wanted_key, const char *wanted_value, char *output, size_t size) {
    const char *cursor = encoded ? encoded : ""; char key[2048], value[2048]; int result, replaced = 0; output[0] = 0;
    while ((result = list_next(&cursor, key, sizeof(key))) > 0) {
        if (list_next(&cursor, value, sizeof(value)) <= 0) return 0;
        if (!strcmp(key, wanted_key)) { snprintf(value, sizeof(value), "%s", wanted_value); replaced = 1; }
        char with_key[2048], with_value[2048];
        if (!list_add_value(output, key, with_key, sizeof(with_key)) || !list_add_value(with_key, value, with_value, sizeof(with_value))) return 0;
        snprintf(output, size, "%s", with_value);
    }
    if (result < 0) return 0;
    if (!replaced) {
        char with_key[2048], with_value[2048];
        if (!list_add_value(output, wanted_key, with_key, sizeof(with_key)) || !list_add_value(with_key, wanted_value, with_value, sizeof(with_value))) return 0;
        snprintf(output, size, "%s", with_value);
    }
    return 1;
}

static int map_get_value(const char *encoded, const char *wanted_key, char *output, size_t size) {
    const char *cursor = encoded ? encoded : ""; char key[2048], value[2048]; int result;
    while ((result = list_next(&cursor, key, sizeof(key))) > 0) {
        if (list_next(&cursor, value, sizeof(value)) <= 0) return 0;
        if (!strcmp(key, wanted_key)) { snprintf(output, size, "%s", value); return 1; }
    }
    return 0;
}

static int map_remove_value(const char *encoded, const char *wanted_key, char *output, size_t size) {
    const char *cursor = encoded ? encoded : ""; char key[2048], value[2048]; int result; output[0] = 0;
    while ((result = list_next(&cursor, key, sizeof(key))) > 0) {
        if (list_next(&cursor, value, sizeof(value)) <= 0) return 0;
        if (!strcmp(key, wanted_key)) continue;
        char with_key[2048], with_value[2048];
        if (!list_add_value(output, key, with_key, sizeof(with_key)) || !list_add_value(with_key, value, with_value, sizeof(with_value))) return 0;
        snprintf(output, size, "%s", with_value);
    }
    return result >= 0;
}

static int map_count_values(const char *encoded) {
    int values = list_count_values(encoded); return values < 0 || values % 2 ? -1 : values / 2;
}

static void url_decode(char *text) {
    char *read = text, *write = text;
    while (*read) {
        if (*read == '+') { *write++ = ' '; read++; }
        else if (*read == '%' && isxdigit((unsigned char)read[1]) && isxdigit((unsigned char)read[2])) {
            char hex[3] = {read[1], read[2], 0}; *write++ = (char)strtol(hex, NULL, 16); read += 3;
        } else *write++ = *read++;
    }
    *write = 0;
}

static void parse_form(Form *form, char *body) {
    form->count = 0;
    for (char *part = strtok(body, "&"); part && form->count < FIELD_MAX; part = strtok(NULL, "&")) {
        char *equal = strchr(part, '='); if (!equal) continue; *equal = 0;
        url_decode(part); url_decode(equal + 1);
        form->pairs[form->count++] = (Pair){part, equal + 1};
    }
}

static size_t find_end(const Bytecode *code, size_t start, uint8_t open, uint8_t close) {
    int depth = 1;
    for (size_t i = start + 1; i < code->count; i++) {
        if (code->items[i].opcode == open) depth++;
        if (code->items[i].opcode == close && --depth == 0) return i;
    }
    return code->count;
}

static const char *record_value(Record *record, const char *field) {
    return record ? pair_value(record->fields, record->field_count, field) : NULL;
}

static Session *session_find(Session *sessions, const char *token) {
    if (!token || !*token) return NULL;
    for (Session *session = sessions; session; session = session->next) if (!strcmp(session->token, token)) return session;
    return NULL;
}

static void sessions_free(Session *session) {
    while (session) { Session *next = session->next; free(session->model); free(session->record_id); free(session); session = next; }
}

static const char *resolve(Scope *scope, const char *expression) {
    const char *local = local_value(scope->locals, expression); if (local) return local;
    const char *dot = strchr(expression, '.');
    if (!dot) {
        const char *value = pair_value(scope->variables, scope->variable_count, expression);
        return value ? value : "";
    }
    if (!scope->item_alias || strncmp(expression, scope->item_alias, (size_t)(dot - expression)) ||
        strlen(scope->item_alias) != (size_t)(dot - expression)) return "";
    const char *value = record_value(scope->item, dot + 1);
    return value ? value : "";
}

static int truthy(const char *value) {
    return *value && strcmp(value, "false") && strcmp(value, "0") && strcmp(value, "no");
}

static void trimmed(const char *source, char *output, size_t size) {
    while (isspace((unsigned char)*source)) source++;
    size_t length = strlen(source); while (length && isspace((unsigned char)source[length - 1])) length--;
    if (length >= size) length = size - 1;
    memcpy(output, source, length); output[length] = 0;
}

static int number_value(const char *text, double *value) {
    if (!*text) return 0;
    char *end; *value = strtod(text, &end); return !*end;
}

static void evaluate(Scope *scope, const char *expression, char *output, size_t size) {
    char clean[2048]; trimmed(expression, clean, sizeof(clean));
    static const struct { const char *words; int operation; } operators[] = {
        {" plus ", 1}, {" minus ", 2}, {" joined with ", 3}, {" times ", 4}, {" divided by ", 5}
    };
    for (size_t op = 0; op < sizeof(operators) / sizeof(operators[0]); op++) {
        char *middle = strstr(clean, operators[op].words); if (!middle) continue;
        char left_expression[1024], right_expression[1024], left[2048], right[2048];
        size_t left_length = (size_t)(middle - clean); if (left_length >= sizeof(left_expression)) left_length = sizeof(left_expression) - 1;
        memcpy(left_expression, clean, left_length); left_expression[left_length] = 0;
        snprintf(right_expression, sizeof(right_expression), "%s", middle + strlen(operators[op].words));
        evaluate(scope, left_expression, left, sizeof(left)); evaluate(scope, right_expression, right, sizeof(right));
        double a, b; int left_number = number_value(left, &a), right_number = number_value(right, &b);
        if (operators[op].operation == 1 && left_number && right_number) snprintf(output, size, "%.15g", a + b);
        else if (operators[op].operation == 2 && left_number && right_number) snprintf(output, size, "%.15g", a - b);
        else if (operators[op].operation == 4 && left_number && right_number) snprintf(output, size, "%.15g", a * b);
        else if (operators[op].operation == 5 && left_number && right_number && b != 0) snprintf(output, size, "%.15g", a / b);
        else snprintf(output, size, "%s%s", left, right);
        return;
    }
    const char *resolved = resolve(scope, clean);
    if (*resolved || local_value(scope->locals, clean)) snprintf(output, size, "%s", resolved);
    else snprintf(output, size, "%s", clean);
}

void hyperian_state_evaluate(HyperianState *state, const char *expression, char *output, size_t output_size) {
    Scope scope = {.locals = state}; evaluate(&scope, expression, output, output_size);
}

static int condition_is_true(Scope *scope, const char *expression) {
    char clean[2048]; trimmed(expression, clean, sizeof(clean));
    static const struct { const char *words; int comparison; } comparisons[] = {
        {" is not ", 1}, {" is greater than ", 2}, {" is less than ", 3}, {" contains ", 4}, {" is ", 5}
    };
    for (size_t comparison = 0; comparison < sizeof(comparisons) / sizeof(comparisons[0]); comparison++) {
        char *middle = strstr(clean, comparisons[comparison].words); if (!middle) continue;
        char left_expression[1024], right_expression[1024], left[2048], right[2048];
        size_t length = (size_t)(middle - clean); if (length >= sizeof(left_expression)) length = sizeof(left_expression) - 1;
        memcpy(left_expression, clean, length); left_expression[length] = 0;
        snprintf(right_expression, sizeof(right_expression), "%s", middle + strlen(comparisons[comparison].words));
        evaluate(scope, left_expression, left, sizeof(left)); evaluate(scope, right_expression, right, sizeof(right));
        double a, b; int numeric = number_value(left, &a) && number_value(right, &b);
        if (comparisons[comparison].comparison == 1) return strcmp(left, right) != 0;
        if (comparisons[comparison].comparison == 2) return numeric ? a > b : strcmp(left, right) > 0;
        if (comparisons[comparison].comparison == 3) return numeric ? a < b : strcmp(left, right) < 0;
        if (comparisons[comparison].comparison == 4) return strstr(left, right) != NULL;
        return strcmp(left, right) == 0;
    }
    char value[2048]; evaluate(scope, clean, value, sizeof(value)); return truthy(value);
}

static size_t logic_otherwise(const Bytecode *code, size_t start, size_t end) {
    int depth = 0;
    for (size_t i = start + 1; i < end; i++) {
        if (code->items[i].opcode == OP_LOGIC_IF) depth++;
        else if (code->items[i].opcode == OP_END_LOGIC_IF) depth--;
        else if (code->items[i].opcode == OP_OTHERWISE && depth == 0) return i;
    }
    return end;
}

static size_t logic_catch(const Bytecode *code, size_t start, size_t end) {
    int depth = 0;
    for (size_t i = start + 1; i < end; i++) {
        if (code->items[i].opcode == OP_TRY) depth++;
        else if (code->items[i].opcode == OP_END_TRY) depth--;
        else if (code->items[i].opcode == OP_CATCH && depth == 0) return i;
    }
    return end;
}

static int is_logic_instruction(uint8_t opcode) {
    return opcode == OP_SET_VALUE || opcode == OP_LOGIC_IF || opcode == OP_REPEAT || opcode == OP_RUN_ACTION ||
        opcode == OP_RETURN_VALUE || opcode == OP_READ_FILE || opcode == OP_WRITE_FILE || opcode == OP_TRY ||
        opcode == OP_MAKE_LIST || opcode == OP_LIST_ADD || opcode == OP_LIST_REMOVE || opcode == OP_LIST_COUNT ||
        opcode == OP_LIST_ITEM || opcode == OP_HTTP_GET || opcode == OP_MAKE_MAP || opcode == OP_MAP_PUT ||
        opcode == OP_MAP_GET || opcode == OP_MAP_REMOVE || opcode == OP_MAP_COUNT || opcode == OP_PLAY_SOUND || opcode == OP_OPEN_VIEW ||
        opcode == OP_CREATE_STATE || opcode == OP_FIND_STATE || opcode == OP_UPDATE_STATE || opcode == OP_DELETE_STATE || opcode == OP_COUNT_RECORDS || opcode == OP_COLLECT_FIELD ||
        opcode == OP_MOVE_POSITION || opcode == OP_APPLY_GRAVITY || opcode == OP_KEEP_INSIDE || opcode == OP_CHECK_COLLISION || opcode == OP_COLLECT_QUERY ||
        opcode == OP_MOVE_VALUE_TOWARD || opcode == OP_ADVANCE_ANIMATION;
}

static HyperianSoundHandler sound_handler = NULL;
void hyperian_set_sound_handler(HyperianSoundHandler handler) { sound_handler = handler; }

static int execute_logic_range(const Bytecode *code, size_t from, size_t to, Scope *scope, int depth, char *error, size_t error_size);

static int debugger_active = 0;

static void debug_instruction(const Instruction *in, int depth) {
    printf("%*sLine %u: ", depth * 2, "", in->line);
    switch (in->opcode) {
        case OP_SET_VALUE: printf("set %s to %s", in->args[0], in->args[1]); break;
        case OP_LOGIC_IF: printf("check whether %s", in->args[0]); break;
        case OP_REPEAT: printf("repeat %s times", in->args[0]); break;
        case OP_RUN_ACTION: printf("run action %s", in->args[0]); break;
        case OP_RETURN_VALUE: printf("return %s", in->args[0]); break;
        case OP_READ_FILE: printf("read file %s into %s", in->args[0], in->args[1]); break;
        case OP_WRITE_FILE: printf("write %s to file %s", in->args[0], in->args[1]); break;
        case OP_MAKE_LIST: printf("make list %s", in->args[0]); break;
        case OP_LIST_ADD: printf("add %s to list %s", in->args[0], in->args[1]); break;
        case OP_LIST_REMOVE: printf("remove %s from list %s", in->args[0], in->args[1]); break;
        case OP_LIST_COUNT: printf("count list %s as %s", in->args[0], in->args[1]); break;
        case OP_LIST_ITEM: printf("read item %s from list %s as %s", in->args[0], in->args[1], in->args[2]); break;
        case OP_TRY: printf("try the following work"); break;
        case OP_HTTP_GET: printf("get %s from the internet", in->args[0]); break;
        case OP_MAKE_MAP: printf("make map %s", in->args[0]); break;
        case OP_MAP_PUT: printf("put %s at %s in map %s", in->args[0], in->args[1], in->args[2]); break;
        case OP_MAP_GET: printf("read %s from map %s as %s", in->args[0], in->args[1], in->args[2]); break;
        case OP_MAP_REMOVE: printf("remove %s from map %s", in->args[0], in->args[1]); break;
        case OP_MAP_COUNT: printf("count map %s as %s", in->args[0], in->args[1]); break;
        case OP_PLAY_SOUND: printf("play sound %s", in->args[0]); break;
        case OP_OPEN_VIEW: printf("open view %s", in->args[0]); break;
        case OP_CREATE_STATE: printf("create a %s using the current values as %s", in->args[0], in->args[1]); break;
        case OP_FIND_STATE: printf("find the %s numbered %s as %s", in->args[0], in->args[1], in->args[2]); break;
        case OP_UPDATE_STATE: printf("update the %s numbered %s using the current values", in->args[0], in->args[1]); break;
        case OP_DELETE_STATE: printf("delete the %s numbered %s", in->args[0], in->args[1]); break;
        case OP_COUNT_RECORDS: printf("count all %s records as %s", in->args[0], in->args[1]); break;
        case OP_COLLECT_FIELD: printf("collect every %s %s as %s", in->args[0], in->args[1], in->args[2]); break;
        case OP_MOVE_POSITION: printf("move position %s %s using velocity %s %s", in->args[0], in->args[1], in->args[2], in->args[3]); break;
        case OP_APPLY_GRAVITY: printf("apply gravity %s to %s", in->args[0], in->args[1]); break;
        case OP_KEEP_INSIDE: printf("keep position %s %s inside %s by %s sized %s by %s", in->args[0], in->args[1], in->args[2], in->args[3], in->args[4], in->args[5]); break;
        case OP_CHECK_COLLISION: printf("check collision and store it as %s", in->args[8]); break;
        case OP_COLLECT_QUERY: printf("collect every %s %s", in->args[0], in->args[1]);
            if (*in->args[2]) printf(" where %s is %s", in->args[2], in->args[3]);
            if (*in->args[4]) printf(" ordered by %s%s", in->args[4], !strcmp(in->args[5], "descending") ? " descending" : "");
            printf(" as %s", in->args[6]); break;
        case OP_MOVE_VALUE_TOWARD: printf("move value %s toward %s at %s per second", in->args[0], in->args[1], in->args[2]); break;
        case OP_ADVANCE_ANIMATION: printf("advance animation %s from %s through %s every %s milliseconds", in->args[0], in->args[1], in->args[2], in->args[3]); break;
        default: printf("execute %s", opcode_name(in->opcode)); break;
    }
    putchar('\n');
}

static void debug_changes(const HyperianState *before, const HyperianState *after, int depth) {
    for (int i = 0; i < after->count; i++) {
        if (!strncmp(after->names[i], "__hyperian_animation_", sizeof("__hyperian_animation_") - 1)) continue;
        const char *old = hyperian_state_get(before, after->names[i]);
        if (!old || strcmp(old, after->values[i]))
            printf("%*s%s is now %s\n", depth * 2 + 2, "", after->names[i], *after->values[i] ? after->values[i] : "empty");
    }
}

static void form_from_current_values(const Bytecode *code, const char *model, Scope *scope, Form *form) {
    memset(form, 0, sizeof(*form)); int inside = 0;
    for (size_t i = 0; i < code->count; i++) {
        Instruction *in = &code->items[i];
        if (in->opcode == OP_MODEL) inside = !strcmp(in->args[0], model);
        else if (inside && in->opcode == OP_END_MODEL) break;
        else if (inside && in->opcode == OP_FIELD && form->count < FIELD_MAX) {
            const char *value = local_value(scope->locals, in->args[0]);
            if (value) form->pairs[form->count++] = (Pair){in->args[0], (char *)value};
        }
    }
}

static int physics_number(Scope *scope, const char *expression, double *number, char *error, size_t error_size) {
    char value[128], *end; evaluate(scope, expression, value, sizeof(value)); *number = strtod(value, &end);
    if (*value && !*end && *number == *number && *number >= -DBL_MAX && *number <= DBL_MAX) return 1;
    snprintf(error, error_size, "this game instruction needs a number for %s", expression); return 0;
}

static void set_physics_number(HyperianState *state, const char *name, double number) {
    char value[64]; snprintf(value, sizeof(value), "%.15g", number); local_set(state, name, value);
}

static int compare_query_values(const char *left, const char *right) {
    double left_number, right_number;
    if (number_value(left, &left_number) && number_value(right, &right_number))
        return left_number < right_number ? -1 : left_number > right_number ? 1 : 0;
    return strcmp(left, right);
}

static void expose_record_values(const Bytecode *code, Scope *scope, Record *record, const char *name) {
    char key[128];
    for (int i = 0; i < record->field_count; i++) {
        if (!strcmp(field_kind(code, record->model, record->fields[i].key), "secret")) continue;
        if (snprintf(key, sizeof(key), "%s_%s", name, record->fields[i].key) < (int)sizeof(key))
            local_set(scope->locals, key, record->fields[i].value);
    }
}

static int execute_logic_at(const Bytecode *code, size_t *position, Scope *scope, int depth, char *error, size_t error_size) {
    if (depth > 64) { snprintf(error, error_size, "actions are calling each other too deeply"); return 0; }
    Instruction *in = &code->items[*position];
    HyperianState before;
    if (debugger_active) { before = *scope->locals; debug_instruction(in, depth); }
    if (in->opcode == OP_SET_VALUE) {
        char value[2048]; evaluate(scope, in->args[1], value, sizeof(value)); local_set(scope->locals, in->args[0], value);
    } else if (in->opcode == OP_MOVE_VALUE_TOWARD) {
        double current, target, speed, seconds;
        if (!physics_number(scope, in->args[0], &current, error, error_size) || !physics_number(scope, in->args[1], &target, error, error_size) ||
            !physics_number(scope, in->args[2], &speed, error, error_size) ||
            !physics_number(scope, "seconds_since_last_frame", &seconds, error, error_size)) return 0;
        if (speed < 0) { snprintf(error, error_size, "animation speed cannot be negative"); return 0; }
        if (seconds < 0) { snprintf(error, error_size, "time since the last frame cannot be negative"); return 0; }
        double distance = target - current, change = speed * seconds;
        if (distance > change) current += change;
        else if (distance < -change) current -= change;
        else current = target;
        set_physics_number(scope->locals, in->args[0], current);
    } else if (in->opcode == OP_ADVANCE_ANIMATION) {
        double first_number, last_number, seconds;
        if (!physics_number(scope, in->args[1], &first_number, error, error_size) || !physics_number(scope, in->args[2], &last_number, error, error_size) ||
            !physics_number(scope, "seconds_since_last_frame", &seconds, error, error_size)) return 0;
        if (first_number < -1000000000 || first_number > 1000000000 || last_number < -1000000000 || last_number > 1000000000) {
            snprintf(error, error_size, "animation frame numbers must be between -1000000000 and 1000000000"); return 0;
        }
        long first = (long)first_number, last = (long)last_number;
        if (first_number != (double)first || last_number != (double)last || last < first || last - first > 1000000) {
            snprintf(error, error_size, "animation frames must be whole numbers in increasing order"); return 0;
        }
        if (seconds < 0 || seconds > 86400) { snprintf(error, error_size, "time since the last frame must be between zero and one day"); return 0; }
        double interval = strtod(in->args[3], NULL) / 1000.0;
        if (interval < 0.001 || interval > 86400) { snprintf(error, error_size, "animation interval must be between one millisecond and one day"); return 0; }
        char elapsed_name[64]; snprintf(elapsed_name, sizeof(elapsed_name), "__hyperian_animation_%u", in->line);
        const char *saved_elapsed = local_value(scope->locals, elapsed_name); double elapsed = 0;
        if (saved_elapsed && *saved_elapsed) {
            char *end; elapsed = strtod(saved_elapsed, &end);
            if (*end || elapsed != elapsed || elapsed < 0 || elapsed > 86400) elapsed = 0;
        }
        elapsed += seconds;
        unsigned long steps = (unsigned long)(elapsed / interval);
        elapsed -= (double)steps * interval;
        const char *saved_frame = local_value(scope->locals, in->args[0]); double frame_number = first_number;
        if (saved_frame && *saved_frame) {
            char *end; double parsed = strtod(saved_frame, &end);
            if (!*end && parsed == parsed && parsed >= first_number && parsed <= last_number && parsed == (double)(long)parsed) frame_number = parsed;
        }
        unsigned long frame_count = (unsigned long)(last - first + 1);
        long frame = first + (long)(((unsigned long)((long)frame_number - first) + steps) % frame_count);
        set_physics_number(scope->locals, in->args[0], (double)frame);
        set_physics_number(scope->locals, elapsed_name, elapsed);
    } else if (in->opcode == OP_MOVE_POSITION) {
        double x, y, velocity_x, velocity_y, seconds;
        if (!physics_number(scope, in->args[0], &x, error, error_size) || !physics_number(scope, in->args[1], &y, error, error_size) ||
            !physics_number(scope, in->args[2], &velocity_x, error, error_size) || !physics_number(scope, in->args[3], &velocity_y, error, error_size) ||
            !physics_number(scope, "seconds_since_last_frame", &seconds, error, error_size)) return 0;
        set_physics_number(scope->locals, in->args[0], x + velocity_x * seconds);
        set_physics_number(scope->locals, in->args[1], y + velocity_y * seconds);
    } else if (in->opcode == OP_APPLY_GRAVITY) {
        double gravity, velocity, seconds;
        if (!physics_number(scope, in->args[0], &gravity, error, error_size) || !physics_number(scope, in->args[1], &velocity, error, error_size) ||
            !physics_number(scope, "seconds_since_last_frame", &seconds, error, error_size)) return 0;
        set_physics_number(scope->locals, in->args[1], velocity + gravity * seconds);
    } else if (in->opcode == OP_KEEP_INSIDE) {
        double x, y, width, height, item_width, item_height;
        if (!physics_number(scope, in->args[0], &x, error, error_size) || !physics_number(scope, in->args[1], &y, error, error_size) ||
            !physics_number(scope, in->args[2], &width, error, error_size) || !physics_number(scope, in->args[3], &height, error, error_size) ||
            !physics_number(scope, in->args[4], &item_width, error, error_size) || !physics_number(scope, in->args[5], &item_height, error, error_size)) return 0;
        if (width < 0 || height < 0 || item_width < 0 || item_height < 0) { snprintf(error, error_size, "game area and object sizes cannot be negative"); return 0; }
        double maximum_x = width > item_width ? width - item_width : 0, maximum_y = height > item_height ? height - item_height : 0;
        if (x < 0) x = 0; else if (x > maximum_x) x = maximum_x;
        if (y < 0) y = 0; else if (y > maximum_y) y = maximum_y;
        set_physics_number(scope->locals, in->args[0], x); set_physics_number(scope->locals, in->args[1], y);
    } else if (in->opcode == OP_CHECK_COLLISION) {
        double x1, y1, width1, height1, x2, y2, width2, height2;
        if (!physics_number(scope, in->args[0], &x1, error, error_size) || !physics_number(scope, in->args[1], &y1, error, error_size) ||
            !physics_number(scope, in->args[2], &width1, error, error_size) || !physics_number(scope, in->args[3], &height1, error, error_size) ||
            !physics_number(scope, in->args[4], &x2, error, error_size) || !physics_number(scope, in->args[5], &y2, error, error_size) ||
            !physics_number(scope, in->args[6], &width2, error, error_size) || !physics_number(scope, in->args[7], &height2, error, error_size)) return 0;
        if (width1 < 0 || height1 < 0 || width2 < 0 || height2 < 0) { snprintf(error, error_size, "collision sizes cannot be negative"); return 0; }
        local_set(scope->locals, in->args[8], x1 < x2 + width2 && x1 + width1 > x2 && y1 < y2 + height2 && y1 + height1 > y2 ? "true" : "false");
    } else if (in->opcode == OP_LOGIC_IF) {
        size_t end = find_end(code, *position, OP_LOGIC_IF, OP_END_LOGIC_IF), otherwise = logic_otherwise(code, *position, end);
        if (end == code->count) { snprintf(error, error_size, "an if instruction is missing its end"); return 0; }
        if (condition_is_true(scope, in->args[0])) {
            if (!execute_logic_range(code, *position + 1, otherwise, scope, depth + 1, error, error_size)) return 0;
        } else if (otherwise < end && !execute_logic_range(code, otherwise + 1, end, scope, depth + 1, error, error_size)) return 0;
        *position = end;
    } else if (in->opcode == OP_REPEAT) {
        size_t end = find_end(code, *position, OP_REPEAT, OP_END_REPEAT); char value[128]; evaluate(scope, in->args[0], value, sizeof(value));
        long times = strtol(value, NULL, 10); if (times < 0 || times > 100000) { snprintf(error, error_size, "repeat must be between 0 and 100000 times"); return 0; }
        for (long count = 0; count < times; count++) if (!execute_logic_range(code, *position + 1, end, scope, depth + 1, error, error_size)) return 0;
        *position = end;
    } else if (in->opcode == OP_TRY) {
        size_t end = find_end(code, *position, OP_TRY, OP_END_TRY), caught_at = logic_catch(code, *position, end);
        char caught[256] = {0};
        if (!execute_logic_range(code, *position + 1, caught_at, scope, depth + 1, caught, sizeof(caught))) {
            if (caught_at == end) { snprintf(error, error_size, "%s", caught); return 0; }
            local_set(scope->locals, code->items[caught_at].args[0], caught);
            if (!execute_logic_range(code, caught_at + 1, end, scope, depth + 1, error, error_size)) return 0;
        }
        *position = end;
    } else if (in->opcode == OP_RUN_ACTION) {
        for (size_t action = 0; action < code->count; action++) if (code->items[action].opcode == OP_ACTION && !strcmp(code->items[action].args[0], in->args[0])) {
            if (in->argc >= 3) {
                char input[2048]; evaluate(scope, in->args[1], input, sizeof(input));
                if (code->items[action].argc < 2 || !*code->items[action].args[1]) {
                    snprintf(error, error_size, "action %s does not accept an input", in->args[0]); return 0;
                }
                local_set(scope->locals, code->items[action].args[1], input);
                local_set(scope->locals, "__hyperian_return", "");
            }
            size_t end = find_end(code, action, OP_ACTION, OP_END_ACTION);
            if (!execute_logic_range(code, action + 1, end, scope, depth + 1, error, error_size)) return 0;
            if (in->argc >= 3) {
                const char *returned = local_value(scope->locals, "__hyperian_return");
                local_set(scope->locals, in->args[2], returned ? returned : "");
            }
            return 1;
        }
        snprintf(error, error_size, "the requested action does not exist"); return 0;
    } else if (in->opcode == OP_RETURN_VALUE) {
        char value[2048]; evaluate(scope, in->args[0], value, sizeof(value)); local_set(scope->locals, "__hyperian_return", value);
    } else if (in->opcode == OP_READ_FILE) {
        char path[2048]; evaluate(scope, in->args[0], path, sizeof(path)); FILE *file = fopen(path, "rb");
        if (!file) { snprintf(error, error_size, "could not read file %s: %s", path, strerror(errno)); return 0; }
        char contents[2048]; size_t length = fread(contents, 1, sizeof(contents) - 1, file);
        int failed = ferror(file), finished = feof(file); fclose(file);
        if (failed) { snprintf(error, error_size, "could not read file %s", path); return 0; }
        if (!finished) { snprintf(error, error_size, "file %s is larger than 2047 characters", path); return 0; }
        contents[length] = 0; local_set(scope->locals, in->args[1], contents);
    } else if (in->opcode == OP_WRITE_FILE) {
        char value[2048], path[2048], temporary[2100];
        evaluate(scope, in->args[0], value, sizeof(value)); evaluate(scope, in->args[1], path, sizeof(path));
        if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) {
            snprintf(error, error_size, "the file path is too long"); return 0;
        }
        FILE *file = fopen(temporary, "wb");
        if (!file) { snprintf(error, error_size, "could not write file %s: %s", path, strerror(errno)); return 0; }
        size_t length = strlen(value); int okay = fwrite(value, 1, length, file) == length;
        if (fclose(file)) okay = 0;
        if (okay) okay = rename(temporary, path) == 0;
        if (!okay) { remove(temporary); snprintf(error, error_size, "could not finish writing file %s", path); return 0; }
    } else if (in->opcode == OP_MAKE_LIST) {
        local_set(scope->locals, in->args[0], "");
    } else if (in->opcode == OP_LIST_ADD) {
        char value[2048], encoded[2048]; evaluate(scope, in->args[0], value, sizeof(value));
        const char *current = local_value(scope->locals, in->args[1]);
        if (!current) { snprintf(error, error_size, "list %s does not exist", in->args[1]); return 0; }
        if (!list_add_value(current, value, encoded, sizeof(encoded))) { snprintf(error, error_size, "list %s is full", in->args[1]); return 0; }
        local_set(scope->locals, in->args[1], encoded);
    } else if (in->opcode == OP_LIST_REMOVE) {
        char value[2048], encoded[2048]; evaluate(scope, in->args[0], value, sizeof(value));
        const char *current = local_value(scope->locals, in->args[1]);
        if (!current || !list_remove_value(current, value, encoded, sizeof(encoded))) { snprintf(error, error_size, "list %s is not valid", in->args[1]); return 0; }
        local_set(scope->locals, in->args[1], encoded);
    } else if (in->opcode == OP_LIST_COUNT) {
        const char *current = local_value(scope->locals, in->args[0]);
        if (!current) { snprintf(error, error_size, "list %s does not exist", in->args[0]); return 0; }
        int count = list_count_values(current); char value[32];
        if (count < 0) { snprintf(error, error_size, "list %s is not valid", in->args[0]); return 0; }
        snprintf(value, sizeof(value), "%d", count); local_set(scope->locals, in->args[1], value);
    } else if (in->opcode == OP_LIST_ITEM) {
        char index[64], value[2048]; evaluate(scope, in->args[0], index, sizeof(index)); int wanted = atoi(index);
        const char *current = local_value(scope->locals, in->args[1]);
        if (!current) { snprintf(error, error_size, "list %s does not exist", in->args[1]); return 0; }
        if (wanted < 1 || !list_item_value(current, wanted, value, sizeof(value))) {
            snprintf(error, error_size, "list %s has no item %d", in->args[1], wanted); return 0;
        }
        local_set(scope->locals, in->args[2], value);
    } else if (in->opcode == OP_HTTP_GET) {
        char url[2048], body[2048], status_text[32]; long status = 0; evaluate(scope, in->args[0], url, sizeof(url));
        if (!hyperian_http_get(url, body, sizeof(body), &status, error, error_size)) return 0;
        snprintf(status_text, sizeof(status_text), "%ld", status); local_set(scope->locals, in->args[1], body); local_set(scope->locals, in->args[2], status_text);
    } else if (in->opcode == OP_MAKE_MAP) {
        local_set(scope->locals, in->args[0], "");
    } else if (in->opcode == OP_MAP_PUT) {
        char value[2048], key[2048], encoded[2048]; evaluate(scope, in->args[0], value, sizeof(value)); evaluate(scope, in->args[1], key, sizeof(key));
        const char *current = local_value(scope->locals, in->args[2]);
        if (!current) { snprintf(error, error_size, "map %s does not exist", in->args[2]); return 0; }
        if (!map_put_value(current, key, value, encoded, sizeof(encoded))) { snprintf(error, error_size, "map %s is full or damaged", in->args[2]); return 0; }
        local_set(scope->locals, in->args[2], encoded);
    } else if (in->opcode == OP_MAP_GET) {
        char key[2048], value[2048]; evaluate(scope, in->args[0], key, sizeof(key)); const char *current = local_value(scope->locals, in->args[1]);
        if (!current) { snprintf(error, error_size, "map %s does not exist", in->args[1]); return 0; }
        if (!map_get_value(current, key, value, sizeof(value))) { snprintf(error, error_size, "map %s has no key %s", in->args[1], key); return 0; }
        local_set(scope->locals, in->args[2], value);
    } else if (in->opcode == OP_MAP_REMOVE) {
        char key[2048], encoded[2048]; evaluate(scope, in->args[0], key, sizeof(key)); const char *current = local_value(scope->locals, in->args[1]);
        if (!current || !map_remove_value(current, key, encoded, sizeof(encoded))) { snprintf(error, error_size, "map %s does not exist or is damaged", in->args[1]); return 0; }
        local_set(scope->locals, in->args[1], encoded);
    } else if (in->opcode == OP_MAP_COUNT) {
        const char *current = local_value(scope->locals, in->args[0]); int count = current ? map_count_values(current) : -1; char value[32];
        if (count < 0) { snprintf(error, error_size, "map %s does not exist or is damaged", in->args[0]); return 0; }
        snprintf(value, sizeof(value), "%d", count); local_set(scope->locals, in->args[1], value);
    } else if (in->opcode == OP_PLAY_SOUND) {
        char path[2048]; evaluate(scope, in->args[0], path, sizeof(path));
        if (!sound_handler) { snprintf(error, error_size, "this application target cannot play sounds"); return 0; }
        if (!sound_handler(path, error, error_size)) return 0;
    } else if (in->opcode == OP_OPEN_VIEW) {
        local_set(scope->locals, "__hyperian_open_view", in->args[0]);
    } else if (in->opcode == OP_CREATE_STATE) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        Form form; form_from_current_values(code, in->args[0], scope, &form);
        if (!create_record(code, in->args[0], &form, &scope->data->records, error, error_size)) {
            if (!*error) snprintf(error, error_size, "could not create the %s", in->args[0]);
            return 0;
        }
        if (!save_records(scope->data->records, code)) { snprintf(error, error_size, "could not save the data"); return 0; }
        local_set(scope->locals, in->args[1], record_value(scope->data->records, "id"));
    } else if (in->opcode == OP_FIND_STATE) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        char id[128]; evaluate(scope, in->args[1], id, sizeof(id)); Record *record = find_record(scope->data->records, in->args[0], id);
        char found[128]; snprintf(found, sizeof(found), "%s_found", in->args[2]); local_set(scope->locals, found, record ? "true" : "false");
        if (record) expose_record_values(code, scope, record, in->args[2]);
    } else if (in->opcode == OP_UPDATE_STATE) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        char id[128]; evaluate(scope, in->args[1], id, sizeof(id)); Form form; form_from_current_values(code, in->args[0], scope, &form);
        if (!update_record(code, in->args[0], id, &form, scope->data->records, error, error_size)) return 0;
        if (!save_records(scope->data->records, code)) { snprintf(error, error_size, "could not save the data"); return 0; }
    } else if (in->opcode == OP_DELETE_STATE) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        char id[128]; evaluate(scope, in->args[1], id, sizeof(id));
        if (!delete_record(&scope->data->records, in->args[0], id)) { snprintf(error, error_size, "%s %s was not found", in->args[0], id); return 0; }
        if (!save_records(scope->data->records, code)) { snprintf(error, error_size, "could not save the data"); return 0; }
    } else if (in->opcode == OP_COUNT_RECORDS) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        int count = 0; for (Record *record = scope->data->records; record; record = record->next) if (!strcmp(record->model, in->args[0])) count++;
        char value[32]; snprintf(value, sizeof(value), "%d", count); local_set(scope->locals, in->args[1], value);
    } else if (in->opcode == OP_COLLECT_FIELD) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        char collection[HYPERIAN_VALUE_SIZE] = "";
        for (Record *record = scope->data->records; record; record = record->next) if (!strcmp(record->model, in->args[0])) {
            const char *value = record_value(record, in->args[1]); char combined[HYPERIAN_VALUE_SIZE];
            if (!list_add_value(collection, value ? value : "", combined, sizeof(combined))) {
                snprintf(error, error_size, "the collected values are too large to fit in %s", in->args[2]); return 0;
            }
            snprintf(collection, sizeof(collection), "%s", combined);
        }
        local_set(scope->locals, in->args[2], collection);
    } else if (in->opcode == OP_COLLECT_QUERY) {
        if (!scope->data) { snprintf(error, error_size, "persistent model work is not available in this context"); return 0; }
        char filter_value[HYPERIAN_VALUE_SIZE] = "";
        if (*in->args[2]) evaluate(scope, in->args[3], filter_value, sizeof(filter_value));
        size_t count = 0;
        for (Record *record = scope->data->records; record; record = record->next)
            if (!strcmp(record->model, in->args[0]) && (!*in->args[2] ||
                (record_value(record, in->args[2]) && !strcmp(record_value(record, in->args[2]), filter_value)))) count++;
        Record **matches = count ? malloc(count * sizeof(*matches)) : NULL; size_t at = 0;
        if (count && !matches) { snprintf(error, error_size, "there is not enough memory to collect the records"); return 0; }
        for (Record *record = scope->data->records; record; record = record->next)
            if (!strcmp(record->model, in->args[0]) && (!*in->args[2] ||
                (record_value(record, in->args[2]) && !strcmp(record_value(record, in->args[2]), filter_value)))) matches[at++] = record;
        if (*in->args[4]) for (size_t a = 1; a < count; a++) {
            Record *chosen = matches[a]; size_t b = a;
            const char *chosen_value = record_value(chosen, in->args[4]); if (!chosen_value) chosen_value = "";
            while (b) {
                const char *previous = record_value(matches[b - 1], in->args[4]); if (!previous) previous = "";
                int order = compare_query_values(previous, chosen_value);
                if (!strcmp(in->args[5], "descending")) order = -order;
                if (order <= 0) break;
                matches[b] = matches[b - 1]; b--;
            }
            matches[b] = chosen;
        }
        char collection[HYPERIAN_VALUE_SIZE] = "";
        for (size_t i = 0; i < count; i++) {
            const char *value = record_value(matches[i], in->args[1]); char combined[HYPERIAN_VALUE_SIZE];
            if (!list_add_value(collection, value ? value : "", combined, sizeof(combined))) {
                free(matches); snprintf(error, error_size, "the collected values are too large to fit in %s", in->args[6]); return 0;
            }
            snprintf(collection, sizeof(collection), "%s", combined);
        }
        free(matches); local_set(scope->locals, in->args[6], collection);
    }
    if (debugger_active) debug_changes(&before, scope->locals, depth);
    return 1;
}

static int execute_logic_range(const Bytecode *code, size_t from, size_t to, Scope *scope, int depth, char *error, size_t error_size) {
    for (size_t i = from; i < to; i++)
        if (is_logic_instruction(code->items[i].opcode))
            if (!execute_logic_at(code, &i, scope, depth, error, error_size)) return 0;
    return 1;
}

static int execute_action_with_data(const Bytecode *code, HyperianData *data, const char *name, const char *input, HyperianState *state, char *error, size_t error_size) {
    for (size_t action = 0; action < code->count; action++) if (code->items[action].opcode == OP_ACTION && !strcmp(code->items[action].args[0], name)) {
        int accepts_input = code->items[action].argc > 1 && *code->items[action].args[1];
        if (accepts_input && !input) { snprintf(error, error_size, "action %s needs an input", name); return 0; }
        if (!accepts_input && input) { snprintf(error, error_size, "action %s does not accept an input", name); return 0; }
        if (accepts_input) local_set(state, code->items[action].args[1], input);
        Scope scope = {.locals = state, .data = data}; size_t end = find_end(code, action, OP_ACTION, OP_END_ACTION);
        return execute_logic_range(code, action + 1, end, &scope, 0, error, error_size);
    }
    snprintf(error, error_size, "action %s does not exist", name); return 0;
}

int hyperian_execute_action(const Bytecode *code, const char *name, const char *input, HyperianState *state, char *error, size_t error_size) {
    return execute_action_with_data(code, NULL, name, input, state, error, error_size);
}

static int execute_event_with_data(const Bytecode *code, HyperianData *data, const char *event, HyperianState *state, char *error, size_t error_size) {
    for (size_t at = 0; at < code->count; at++) if (code->items[at].opcode == OP_EVENT && !strcmp(code->items[at].args[0], event)) {
        Scope scope = {.locals = state, .data = data}; size_t end = find_end(code, at, OP_EVENT, OP_END_ROUTE);
        if (!execute_logic_range(code, at + 1, end, &scope, 0, error, error_size)) return 0;
        at = end;
    }
    return 1;
}

int hyperian_execute_event(const Bytecode *code, const char *event, HyperianState *state, char *error, size_t error_size) {
    return execute_event_with_data(code, NULL, event, state, error, error_size);
}

int hyperian_execute_data_action(HyperianData *data, const char *name, const char *input, HyperianState *state, char *error, size_t error_size) {
    if (!data) { snprintf(error, error_size, "the data session is not open"); return 0; }
    return execute_action_with_data(data->code, data, name, input, state, error, error_size);
}

int hyperian_execute_data_event(HyperianData *data, const char *event, HyperianState *state, char *error, size_t error_size) {
    if (!data) { snprintf(error, error_size, "the data session is not open"); return 0; }
    return execute_event_with_data(data->code, data, event, state, error, error_size);
}

static const char *english_debug_event(const char *event, char *translated, size_t size) {
    if (!event) return NULL;
    if (!strcmp(event, "application starts")) return "START";
    if (!strcmp(event, "application pauses")) return "PAUSE";
    if (!strcmp(event, "application resumes")) return "RESUME";
    if (!strcmp(event, "window closes")) return "CLOSE";
    if (!strcmp(event, "window gains focus")) return "FOCUS";
    if (!strcmp(event, "window loses focus")) return "BLUR";
    if (!strcmp(event, "game updates")) return "FRAME";
    char name[64], extra;
    if (sscanf(event, "player presses %63s%c", name, &extra) == 1) {
        snprintf(translated, size, "KEY:%s", name); return translated;
    }
    if (sscanf(event, "input %63s changes%c", name, &extra) == 1) {
        snprintf(translated, size, "CHANGE:%s", name); return translated;
    }
    if (sscanf(event, "input %63s is submitted%c", name, &extra) == 1) {
        snprintf(translated, size, "SUBMIT:%s", name); return translated;
    }
    return event;
}

int debug_bytecode(const char *path, const char *event, const char *action, const char *input) {
    Bytecode code; bytecode_init(&code); char error[256] = {0}; HyperianState state;
    if (!bytecode_read(&code, path, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    HyperianData *data = hyperian_data_open(&code, error, sizeof(error));
    if (!data) { fprintf(stderr, "Debugger could not open the application data: %s\n", error); bytecode_free(&code); return 1; }
    hyperian_state_init(&state); char translated[80]; const char *selected_event = english_debug_event(event, translated, sizeof(translated));
    if (selected_event && strcmp(selected_event, "START") && !hyperian_execute_data_event(data, "START", &state, error, sizeof(error))) {
        fprintf(stderr, "Debugger could not prepare the application: %s\n", error); hyperian_data_close(data); bytecode_free(&code); return 1;
    }
    if (selected_event && !strcmp(selected_event, "FRAME")) hyperian_state_set(&state, "seconds_since_last_frame", "0.0166666666666667");
    debugger_active = 1;
    printf("Debugging %s %s\n", action ? "action" : "event", action ? action : event);
    int okay = action ? hyperian_execute_data_action(data, action, input, &state, error, sizeof(error))
                      : hyperian_execute_data_event(data, selected_event, &state, error, sizeof(error));
    debugger_active = 0;
    if (!okay) fprintf(stderr, "Debugger stopped: %s\n", error);
    printf("Final state:\n");
    int visible = 0;
    for (int i = 0; i < state.count; i++) if (strncmp(state.names[i], "__hyperian_animation_", sizeof("__hyperian_animation_") - 1)) {
        printf("  %s = %s\n", state.names[i], *state.values[i] ? state.values[i] : "empty"); visible++;
    }
    if (!visible) printf("  no values were set\n");
    hyperian_data_close(data); bytecode_free(&code); return okay ? 0 : 1;
}

static int render_range(const Bytecode *code, size_t from, size_t to, Buffer *body, Scope *scope, Record *records) {
    for (size_t i = from; i < to; i++) {
        Instruction *in = &code->items[i];
        switch (in->opcode) {
            case OP_HEADING: buffer_add(body, "<h1>"); buffer_html(body, in->args[0]); buffer_add(body, "</h1>"); break;
            case OP_TEXT: buffer_add(body, "<p>"); buffer_html(body, in->args[0]); buffer_add(body, "</p>"); break;
            case OP_SHOW_VALUE: buffer_html(body, resolve(scope, in->args[0])); break;
            case OP_LINK: buffer_add(body, "<a href=\""); buffer_dynamic_html(body, in->args[1], scope); buffer_add(body, "\">"); buffer_html(body, in->args[0]); buffer_add(body, "</a>"); break;
            case OP_IMAGE: buffer_add(body, "<img src=\""); buffer_dynamic_html(body, in->args[0], scope); buffer_add(body, "\" alt=\""); buffer_html(body, in->args[1]); buffer_add(body, "\">"); break;
            case OP_SCRIPT: buffer_add(body, "<script src=\""); buffer_dynamic_html(body, in->args[0], scope); buffer_add(body, "\" defer></script>"); break;
            case OP_FORM: {
                size_t end = find_end(code, i, OP_FORM, OP_END_FORM);
                buffer_format(body, "<form method=\"%s\" action=\"", !strcmp(in->args[0], "posts") ? "post" : "get");
                buffer_dynamic_html(body, in->args[1], scope); buffer_add(body, "\">");
                render_range(code, i + 1, end, body, scope, records); buffer_add(body, "</form>"); i = end; break;
            }
            case OP_INPUT:
                buffer_add(body, "<input type=\""); buffer_add(body, in->argc > 3 && !strcmp(in->args[3], "secret") ? "password" : "text");
                buffer_add(body, "\" name=\""); buffer_html(body, in->args[1]); buffer_add(body, "\" placeholder=\"");
                buffer_html(body, in->args[0]); buffer_add(body, "\" value=\"");
                if (!scope->item || strcmp(field_kind(code, scope->item->model, in->args[1]), "secret"))
                    buffer_html(body, record_value(scope->item, in->args[1]) ? record_value(scope->item, in->args[1]) : "");
                buffer_add(body, !strcmp(in->args[2], "true") ? "\" required>" : "\">"); break;
            case OP_BUTTON: buffer_add(body, "<button>"); buffer_html(body, in->args[0]); buffer_add(body, "</button>"); break;
            case OP_TEXTAREA: {
                buffer_add(body, "<textarea name=\""); buffer_html(body, in->args[1]); buffer_add(body, "\" placeholder=\""); buffer_html(body, in->args[0]);
                buffer_add(body, !strcmp(in->args[2], "true") ? "\" required>" : "\">");
                const char *value = record_value(scope->item, in->args[1]); if (value) buffer_html(body, value); buffer_add(body, "</textarea>"); break;
            }
            case OP_CHECKBOX: {
                const char *value = record_value(scope->item, in->args[1]);
                buffer_add(body, "<label><input type=\"checkbox\" name=\""); buffer_html(body, in->args[1]); buffer_add(body, "\" value=\"true\"");
                if (value && truthy(value)) buffer_add(body, " checked");
                buffer_add(body, ">"); buffer_html(body, in->args[0]); buffer_add(body, "</label><input type=\"hidden\" name=\"");
                buffer_html(body, in->args[1]); buffer_add(body, "\" value=\"false\">"); break;
            }
            case OP_EACH: {
                size_t end = find_end(code, i, OP_EACH, OP_END_EACH);
                buffer_add(body, "<ul>");
                if (scope->collection_alias && !strcmp(scope->collection_alias, in->args[1])) {
                    size_t count = 0;
                    for (Record *record = records; record; record = record->next)
                        if (!strcmp(record->model, scope->model) && (!scope->filter_field ||
                            (record_value(record, scope->filter_field) && !strcmp(record_value(record, scope->filter_field), scope->filter_value)))) count++;
                    Record **matches = count ? malloc(count * sizeof(*matches)) : NULL; size_t at = 0;
                    if (count && !matches) return 0;
                    for (Record *record = records; record; record = record->next)
                        if (!strcmp(record->model, scope->model) && (!scope->filter_field ||
                            (record_value(record, scope->filter_field) && !strcmp(record_value(record, scope->filter_field), scope->filter_value)))) matches[at++] = record;
                    if (scope->order_field) for (size_t a = 1; a < count; a++) {
                        Record *chosen = matches[a]; size_t b = a;
                        const char *chosen_value = record_value(chosen, scope->order_field); if (!chosen_value) chosen_value = "";
                        while (b && strcmp(record_value(matches[b - 1], scope->order_field) ? record_value(matches[b - 1], scope->order_field) : "", chosen_value) > 0) {
                            matches[b] = matches[b - 1]; b--;
                        }
                        matches[b] = chosen;
                    }
                    for (size_t record_at = 0; record_at < count; record_at++) {
                        Record *record = matches[record_at];
                        Scope child = *scope; child.item_alias = in->args[0]; child.item = record;
                        buffer_add(body, "<li>"); render_range(code, i + 1, end, body, &child, records); buffer_add(body, "</li>");
                    }
                    free(matches);
                } else {
                    const char *encoded = local_value(scope->locals, in->args[1]);
                    if (encoded) {
                        const char *cursor = encoded; char value[2048]; int next;
                        while ((next = list_next(&cursor, value, sizeof(value))) > 0) {
                            Scope child = *scope; local_set(child.locals, in->args[0], value);
                            buffer_add(body, "<li>"); render_range(code, i + 1, end, body, &child, records); buffer_add(body, "</li>");
                        }
                    }
                }
                buffer_add(body, "</ul>"); i = end; break;
            }
            case OP_IF: {
                size_t end = find_end(code, i, OP_IF, OP_END_IF);
                if (truthy(resolve(scope, in->args[0]))) render_range(code, i + 1, end, body, scope, records);
                i = end; break;
            }
            case OP_USE_COMPONENT: {
                for (size_t component = 0; component < code->count; component++) if (code->items[component].opcode == OP_COMPONENT && !strcmp(code->items[component].args[0], in->args[0])) {
                    size_t end = find_end(code, component, OP_COMPONENT, OP_END_COMPONENT);
                    render_range(code, component + 1, end, body, scope, records); break;
                }
                break;
            }
            case OP_CONTENT: if (scope->raw_content) buffer_add(body, scope->raw_content); break;
            default: break;
        }
    }
    return 1;
}

static char *render_view(const Bytecode *code, const char *view, Scope *scope, Record *records, const char *app_name) {
    size_t start = code->count, end = code->count; const char *title = app_name, *layout = NULL; int pwa = 0;
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_TARGET && !strcmp(code->items[i].args[0], "pwa")) pwa = 1;
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_VIEW && !strcmp(code->items[i].args[0], view)) {
        start = i + 1; end = find_end(code, i, OP_VIEW, OP_END_VIEW); break;
    }
    for (size_t i = start; i < end; i++) {
        if (code->items[i].opcode == OP_TITLE) title = code->items[i].args[0];
        if (code->items[i].opcode == OP_USE_LAYOUT) layout = code->items[i].args[0];
    }
    Buffer body; buffer_init(&body); render_range(code, start, end, &body, scope, records);
    if (layout) for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_LAYOUT && !strcmp(code->items[i].args[0], layout)) {
        Buffer wrapped; buffer_init(&wrapped); Scope layout_scope = *scope; layout_scope.raw_content = body.data;
        size_t layout_end = find_end(code, i, OP_LAYOUT, OP_END_LAYOUT);
        render_range(code, i + 1, layout_end, &wrapped, &layout_scope, records); free(body.data); body = wrapped; break;
    }
    Buffer result; buffer_init(&result);
    buffer_add(&result, "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>");
    buffer_html(&result, title);
    buffer_add(&result, "</title>");
    if (pwa) buffer_add(&result, "<meta name=\"theme-color\" content=\"#2563eb\"><meta name=\"apple-mobile-web-app-capable\" content=\"yes\"><link rel=\"manifest\" href=\"/assets/manifest.webmanifest\"><link rel=\"icon\" href=\"/assets/icon.svg\"><script>if('serviceWorker' in navigator){addEventListener('load',()=>navigator.serviceWorker.register('/service-worker.js'))}</script>");
    buffer_add(&result, "<style>:root{font:17px/1.5 system-ui,sans-serif;color-scheme:light dark}body{width:min(44rem,calc(100% - 2rem));margin:3rem auto}h1{line-height:1.1}form{display:flex;gap:.6rem;margin:1rem 0}input,button,textarea{font:inherit;padding:.65rem .8rem}input,textarea{flex:1}button{cursor:pointer}li{margin:.45rem 0}img{max-width:100%;height:auto}</style>");
    for (size_t i = start; i < end; i++) if (code->items[i].opcode == OP_STYLE) {
        buffer_add(&result, "<link rel=\"stylesheet\" href=\""); buffer_dynamic_html(&result, code->items[i].args[0], scope); buffer_add(&result, "\">");
    }
    buffer_add(&result, "</head><body>");
    if (body.data) buffer_add(&result, body.data);
    free(body.data);
    buffer_add(&result, "</body></html>"); return result.data;
}

static Instruction *model_instruction(const Bytecode *code, const char *name, size_t *index) {
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_MODEL && !strcmp(code->items[i].args[0], name)) {
        if (index) *index = i;
        return &code->items[i];
    }
    return NULL;
}

static Record *find_record(Record *records, const char *model, const char *id) {
    for (Record *record = records; record; record = record->next)
        if (!strcmp(record->model, model) && !strcmp(record_value(record, "id"), id)) return record;
    return NULL;
}

static int validate_value(const Bytecode *code, Instruction *field, const char *value, Record *records, Record *except,
    const char *model, char *error, size_t error_size) {
    if ((!value || !*value) && !strcmp(field->args[2], "true") && !*field->args[3]) {
        snprintf(error, error_size, "%s is required", field->args[0]); return 0;
    }
    if (!value) value = "";
    double numeric = 0;
    if (!strcmp(field->args[1], "number") && *value) {
        char *end; numeric = strtod(value, &end);
        if (*end) { snprintf(error, error_size, "%s must be a number", field->args[0]); return 0; }
    }
    if (!strcmp(field->args[1], "boolean") && *value && strcmp(value, "true") && strcmp(value, "false") &&
        strcmp(value, "yes") && strcmp(value, "no") && strcmp(value, "on") && strcmp(value, "off") && strcmp(value, "1") && strcmp(value, "0")) {
        snprintf(error, error_size, "%s must be true or false", field->args[0]); return 0;
    }
    double measured = !strcmp(field->args[1], "number") ? numeric : (double)strlen(value);
    if (*field->args[5] && measured < strtod(field->args[5], NULL)) {
        snprintf(error, error_size, "%s must be at least %s", field->args[0], field->args[5]); return 0;
    }
    if (*field->args[6] && measured > strtod(field->args[6], NULL)) {
        snprintf(error, error_size, "%s must be at most %s", field->args[0], field->args[6]); return 0;
    }
    if (!strcmp(field->args[4], "true") && *value) {
        for (Record *record = records; record; record = record->next) {
            const char *existing = record_value(record, field->args[0]);
            if (record != except && !strcmp(record->model, model) && existing && !strcmp(existing, value)) {
                snprintf(error, error_size, "%s must be unique", field->args[0]); return 0;
            }
        }
    }
    if (!strcmp(field->args[1], "reference") && *value && !find_record(records, field->args[7], value)) {
        (void)code;
        snprintf(error, error_size, "%s must refer to an existing %s", field->args[0], field->args[7]); return 0;
    }
    return 1;
}

static int create_record(const Bytecode *code, const char *model, Form *form, Record **records, char *error, size_t error_size) {
    size_t at; if (!model_instruction(code, model, &at)) return 0;
    Record *record = calloc(1, sizeof(*record)); if (!record) return 0;
    record->model = copy_string(model);
    long next_id = 1;
    for (Record *item = *records; item; item = item->next) if (!strcmp(item->model, model)) {
        const char *stored_id = record_value(item, "id");
        long existing = stored_id ? strtol(stored_id, NULL, 10) : 0; if (existing >= next_id) next_id = existing + 1;
    }
    char id[32]; snprintf(id, sizeof(id), "%ld", next_id);
    record->fields[record->field_count++] = (Pair){copy_string("id"), copy_string(id)};
    for (at++; at < code->count && code->items[at].opcode != OP_END_MODEL; at++) {
        Instruction *field = &code->items[at]; if (field->opcode != OP_FIELD) continue;
        const char *value = field->argc > 8 && !strcmp(field->args[8], "true") ? NULL : pair_value(form->pairs, form->count, field->args[0]);
        if (!value || !*value) value = field->args[3];
        if (!validate_value(code, field, value, *records, NULL, model, error, error_size)) goto failed;
        char hashed[HYPERIAN_SECRET_SIZE];
        if (!strcmp(field->args[1], "secret")) {
            if (!hyperian_hash_secret(value, hashed)) { snprintf(error, error_size, "could not securely hash %s", field->args[0]); goto failed; }
            value = hashed;
        }
        record->fields[record->field_count++] = (Pair){copy_string(field->args[0]), copy_string(value)};
    }
    record->next = *records; *records = record; return 1;
failed:
    free(record->model);
    for (int i = 0; i < record->field_count; i++) { free(record->fields[i].key); free(record->fields[i].value); }
    free(record); return 0;
}

static int update_record(const Bytecode *code, const char *model, const char *id, Form *form, Record *records, char *error, size_t error_size) {
    Record *record = find_record(records, model, id); size_t at;
    if (!record) { snprintf(error, error_size, "%s %s was not found", model, id); return 0; }
    if (!model_instruction(code, model, &at)) return 0;
    for (at++; at < code->count && code->items[at].opcode != OP_END_MODEL; at++) {
        Instruction *field = &code->items[at]; if (field->opcode != OP_FIELD) continue;
        const char *value = pair_value(form->pairs, form->count, field->args[0]);
        if (field->argc > 8 && !strcmp(field->args[8], "true")) value = record_value(record, field->args[0]);
        if (!value) value = record_value(record, field->args[0]);
        if (!value || !*value) value = field->args[3];
        if (!validate_value(code, field, value, records, record, model, error, error_size)) return 0;
    }
    for (at = 0; at < (size_t)form->count; at++) {
        for (int f = 0; f < record->field_count; f++) if (!strcmp(record->fields[f].key, form->pairs[at].key) && strcmp(form->pairs[at].key, "id")) {
            if (field_protected(code, model, form->pairs[at].key)) continue;
            const char *incoming = form->pairs[at].value; char hashed[HYPERIAN_SECRET_SIZE];
            if (!strcmp(field_kind(code, model, form->pairs[at].key), "secret")) {
                if (!*incoming) continue;
                if (!hyperian_hash_secret(incoming, hashed)) { snprintf(error, error_size, "could not securely hash %s", form->pairs[at].key); return 0; }
                incoming = hashed;
            }
            char *value = copy_string(incoming); if (!value) return 0;
            free(record->fields[f].value); record->fields[f].value = value;
        }
    }
    return 1;
}

static int delete_record(Record **records, const char *model, const char *id) {
    for (Record **at = records; *at; at = &(*at)->next) if (!strcmp((*at)->model, model) && !strcmp(record_value(*at, "id"), id)) {
        Record *record = *at; *at = record->next; free(record->model);
        for (int i = 0; i < record->field_count; i++) { free(record->fields[i].key); free(record->fields[i].value); }
        free(record); return 1;
    }
    return 0;
}

static int save_records(Record *records, const Bytecode *code);

static uint32_t program_data_version(const Bytecode *code) {
    for (size_t i = 0; i < code->count; i++)
        if (code->items[i].opcode == OP_DATA_VERSION) return (uint32_t)strtoul(code->items[i].args[0], NULL, 10);
    return 1;
}

static char *error_page(const char *message) {
    Buffer b; buffer_init(&b); buffer_add(&b, "<!doctype html><html><body><h1>Something needs attention</h1><p>");
    buffer_html(&b, message); buffer_add(&b, "</p></body></html>"); return b.data;
}

typedef struct { int status; char *body; const char *location; const char *content_type; const char *cookie; size_t body_length; } Response;

static const char *asset_type(const char *path) {
    const char *extension = strrchr(path, '.'); if (!extension) return "application/octet-stream";
    if (!strcmp(extension, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(extension, ".js")) return "text/javascript; charset=utf-8";
    if (!strcmp(extension, ".svg")) return "image/svg+xml";
    if (!strcmp(extension, ".png")) return "image/png";
    if (!strcmp(extension, ".jpg") || !strcmp(extension, ".jpeg")) return "image/jpeg";
    if (!strcmp(extension, ".gif")) return "image/gif";
    if (!strcmp(extension, ".webp")) return "image/webp";
    if (!strcmp(extension, ".ico")) return "image/x-icon";
    if (!strcmp(extension, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(extension, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(extension, ".webmanifest")) return "application/manifest+json; charset=utf-8";
    if (!strcmp(extension, ".txt")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static int load_static_response(const Bytecode *code, const char *request_path, Response *response) {
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_STATIC_FILES) {
        const char *directory = code->items[i].args[0], *address = code->items[i].args[1]; size_t prefix = strlen(address);
        const char *relative;
        if (!strcmp(address, "/")) {
            if (*request_path != '/') continue;
            relative = request_path + 1;
        } else {
            if (strncmp(request_path, address, prefix) || (request_path[prefix] && request_path[prefix] != '/')) continue;
            relative = request_path + prefix; while (*relative == '/') relative++;
        }
        if (!*relative || strstr(relative, "..") || strchr(relative, '\\')) return 0;
        char path[4096]; if (snprintf(path, sizeof(path), "%s/%s", directory, relative) >= (int)sizeof(path)) return 0;
        FILE *file = fopen(path, "rb"); if (!file) return 0;
        if (fseek(file, 0, SEEK_END) || ftell(file) < 0) { fclose(file); return 0; }
        long length = ftell(file); rewind(file); char *body = malloc((size_t)length + 1);
        if (!body || fread(body, 1, (size_t)length, file) != (size_t)length) { free(body); fclose(file); return 0; }
        fclose(file); body[length] = 0;
        free(response->body); *response = (Response){200, body, NULL, asset_type(path), NULL, (size_t)length}; return 1;
    }
    return 0;
}

static void buffer_json_string(Buffer *buffer, const char *text) {
    buffer_add(buffer, "\"");
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') { buffer_addn(buffer, "\\", 1); buffer_addn(buffer, text, 1); }
        else if (c == '\n') buffer_add(buffer, "\\n");
        else if (c == '\r') buffer_add(buffer, "\\r");
        else if (c == '\t') buffer_add(buffer, "\\t");
        else if (c < 32) buffer_format(buffer, "\\u%04x", c);
        else buffer_addn(buffer, text, 1);
    }
    buffer_add(buffer, "\"");
}

static const char *field_kind(const Bytecode *code, const char *model, const char *field) {
    if (!strcmp(field, "id")) return "number";
    size_t at; if (!model_instruction(code, model, &at)) return "text";
    for (at++; at < code->count && code->items[at].opcode != OP_END_MODEL; at++)
        if (code->items[at].opcode == OP_FIELD && !strcmp(code->items[at].args[0], field)) return code->items[at].args[1];
    return "text";
}

static int field_protected(const Bytecode *code, const char *model, const char *field) {
    size_t at; if (!model_instruction(code, model, &at)) return 0;
    for (at++; at < code->count && code->items[at].opcode != OP_END_MODEL; at++)
        if (code->items[at].opcode == OP_FIELD && !strcmp(code->items[at].args[0], field))
            return code->items[at].argc > 8 && !strcmp(code->items[at].args[8], "true");
    return 0;
}

static void record_json(const Bytecode *code, Buffer *buffer, Record *record) {
    buffer_add(buffer, "{"); int added = 0;
    for (int i = 0; i < record->field_count; i++) {
        if (!strcmp(field_kind(code, record->model, record->fields[i].key), "secret")) continue;
        if (added++) buffer_add(buffer, ",");
        buffer_json_string(buffer, record->fields[i].key); buffer_add(buffer, ":");
        const char *kind = field_kind(code, record->model, record->fields[i].key);
        if (!strcmp(kind, "number") && *record->fields[i].value) buffer_add(buffer, record->fields[i].value);
        else if (!strcmp(kind, "boolean")) buffer_add(buffer, truthy(record->fields[i].value) ? "true" : "false");
        else buffer_json_string(buffer, record->fields[i].value);
    }
    buffer_add(buffer, "}");
}

static char *render_json(const Bytecode *code, Scope *scope, Record *records, const char *alias) {
    Buffer result; buffer_init(&result);
    const char *scalar = local_value(scope->locals, alias);
    if (scalar) {
        double number;
        if (number_value(scalar, &number)) buffer_add(&result, scalar);
        else if (!strcmp(scalar, "true") || !strcmp(scalar, "false")) buffer_add(&result, scalar);
        else buffer_json_string(&result, scalar);
    } else if (scope->item_alias && !strcmp(scope->item_alias, alias)) record_json(code, &result, scope->item);
    else {
        buffer_add(&result, "["); int added = 0;
        if (scope->collection_alias && !strcmp(scope->collection_alias, alias))
            for (Record *record = records; record; record = record->next) if (!strcmp(record->model, scope->model) &&
                (!scope->filter_field || (record_value(record, scope->filter_field) && !strcmp(record_value(record, scope->filter_field), scope->filter_value)))) {
                if (added++) buffer_add(&result, ",");
                record_json(code, &result, record);
            }
        buffer_add(&result, "]");
    }
    return result.data;
}

static Response execute_route(const Bytecode *code, size_t route_at, Form *form, Record **records, const char *app_name,
    const char *route_id, const char *session_token, Session **sessions) {
    Pair route_variables[1] = {{"id", (char *)(route_id ? route_id : "")}};
    VariableSet locals = {0};
    HyperianData route_data = {.code = code, .records = *records};
    Scope scope = {.variables = route_variables, .variable_count = route_id ? 1 : 0, .locals = &locals, .data = &route_data}; char error[256] = {0};
    const char *response_cookie = NULL;
    for (size_t middleware = 0; middleware < code->count; middleware++) if (code->items[middleware].opcode == OP_BEFORE_ACTION) {
        if (code->items[middleware].argc > 1 && strcmp(code->items[middleware].args[1], "*") &&
            strcmp(code->items[middleware].args[1], code->items[route_at].args[1])) continue;
        Instruction call = {.opcode = OP_RUN_ACTION, .argc = 1, .args = {code->items[middleware].args[0]}};
        Instruction saved = code->items[middleware];
        ((Bytecode *)code)->items[middleware] = call;
        size_t position = middleware; int okay = execute_logic_at(code, &position, &scope, 0, error, sizeof(error));
        *records = route_data.records;
        ((Bytecode *)code)->items[middleware] = saved;
        if (!okay) return (Response){500, error_page(error), NULL, "text/html; charset=utf-8", NULL, 0};
    }
    for (size_t i = route_at + 1; i < code->count && code->items[i].opcode != OP_END_ROUTE; i++) {
        Instruction *in = &code->items[i];
        if (in->opcode == OP_READ_FORM) {
            const char *value = pair_value(form->pairs, form->count, in->args[0]); local_set(&locals, in->args[1], value ? value : "");
            continue;
        }
        if (is_logic_instruction(in->opcode)) {
            int okay = execute_logic_at(code, &i, &scope, 0, error, sizeof(error)); *records = route_data.records;
            if (!okay)
                return (Response){500, error_page(error), NULL, "text/html; charset=utf-8", response_cookie, 0};
            continue;
        }
        if (in->opcode == OP_FIND_ALL) { scope.model = in->args[0]; scope.collection_alias = in->args[1]; }
        else if (in->opcode == OP_FIND_ORDERED) {
            scope.model = in->args[0]; scope.order_field = in->args[1]; scope.collection_alias = in->args[2];
        } else if (in->opcode == OP_FIND_WHERE) {
            scope.model = in->args[0]; scope.filter_field = in->args[1]; scope.filter_value = resolve(&scope, in->args[2]); scope.collection_alias = in->args[3];
        }
        else if (in->opcode == OP_FIND_ONE) {
            scope.model = in->args[0]; scope.item_alias = in->args[1]; scope.item = find_record(*records, in->args[0], route_id);
            if (!scope.item) return (Response){404, error_page("Record not found"), NULL, "text/html; charset=utf-8", NULL, 0};
        }
        else if (in->opcode == OP_SIGN_IN) {
            const char *identity = pair_value(form->pairs, form->count, in->args[1]);
            const char *password = pair_value(form->pairs, form->count, in->args[2]); Record *matched = NULL;
            for (Record *record = *records; record; record = record->next) if (!strcmp(record->model, in->args[0]) && identity &&
                record_value(record, in->args[1]) && !strcmp(record_value(record, in->args[1]), identity)) { matched = record; break; }
            if (!matched || !password || !record_value(matched, in->args[2]) || !hyperian_verify_secret(password, record_value(matched, in->args[2])))
                return (Response){401, error_page("The sign-in details were not correct"), NULL, "text/html; charset=utf-8", NULL, 0};
            Session *session = calloc(1, sizeof(*session));
            if (!session || !hyperian_random_token(session->token, 32)) { free(session); return (Response){500, error_page("Could not start a secure session"), NULL, "text/html; charset=utf-8", NULL, 0}; }
            session->model = copy_string(in->args[0]); session->record_id = copy_string(record_value(matched, "id"));
            session->next = *sessions; *sessions = session; response_cookie = session->token; session_token = session->token;
        } else if (in->opcode == OP_SIGN_OUT) {
            for (Session **at = sessions; *at; at = &(*at)->next) if (session_token && !strcmp((*at)->token, session_token)) {
                Session *removed = *at; *at = removed->next; free(removed->model); free(removed->record_id); free(removed); break;
            }
            session_token = NULL; response_cookie = "";
        } else if (in->opcode == OP_REQUIRE_SIGN_IN) {
            if (!session_find(*sessions, session_token))
                return (Response){303, copy_string(""), in->args[0], "text/html; charset=utf-8", "", 0};
        } else if (in->opcode == OP_FIND_SIGNED_IN) {
            Session *session = session_find(*sessions, session_token);
            if (session && !strcmp(session->model, in->args[0])) {
                scope.model = in->args[0]; scope.item_alias = in->args[1]; scope.item = find_record(*records, session->model, session->record_id);
            }
        } else if (in->opcode == OP_REQUIRE_VALUE) {
            if (strcmp(resolve(&scope, in->args[0]), in->args[1]))
                return (Response){303, copy_string(""), in->args[2], "text/html; charset=utf-8", response_cookie, 0};
        }
        else if (in->opcode == OP_CREATE) {
            if (!create_record(code, in->args[0], form, records, error, sizeof(error)))
                return (Response){400, error_page(*error ? error : "Could not create the record"), NULL, "text/html; charset=utf-8", NULL, 0};
            if (!save_records(*records, code)) return (Response){500, error_page("Could not save the data file"), NULL, "text/html; charset=utf-8", NULL, 0};
            route_data.records = *records;
        } else if (in->opcode == OP_UPDATE) {
            if (!update_record(code, in->args[0], route_id, form, *records, error, sizeof(error)))
                return (Response){strstr(error, "not found") ? 404 : 400, error_page(error), NULL, "text/html; charset=utf-8", NULL, 0};
            if (!save_records(*records, code)) return (Response){500, error_page("Could not save the data file"), NULL, "text/html; charset=utf-8", NULL, 0};
            route_data.records = *records;
        } else if (in->opcode == OP_DELETE) {
            if (!delete_record(records, in->args[0], route_id)) return (Response){404, error_page("Record not found"), NULL, "text/html; charset=utf-8", NULL, 0};
            if (!save_records(*records, code)) return (Response){500, error_page("Could not save the data file"), NULL, "text/html; charset=utf-8", NULL, 0};
            route_data.records = *records;
        } else if (in->opcode == OP_SHOW_VIEW) {
            return (Response){200, render_view(code, in->args[0], &scope, *records, app_name), NULL, "text/html; charset=utf-8", response_cookie, 0};
        } else if (in->opcode == OP_SHOW_JSON) {
            return (Response){200, render_json(code, &scope, *records, in->args[0]), NULL, "application/json; charset=utf-8", response_cookie, 0};
        } else if (in->opcode == OP_REDIRECT) return (Response){303, copy_string(""), in->args[0], "text/html; charset=utf-8", response_cookie, 0};
    }
    return (Response){500, error_page("The route did not show a view, show JSON, or redirect"), NULL, "text/html; charset=utf-8", NULL, 0};
}

static int send_all(int socket_fd, const char *data, size_t length) {
    while (length) { ssize_t sent = send(socket_fd, data, length, 0); if (sent <= 0) return 0; data += sent; length -= (size_t)sent; }
    return 1;
}

static int match_route(const char *pattern, const char *path, char *id, size_t id_size) {
    const char *marker = strstr(pattern, "{id}");
    if (!marker) return !strcmp(pattern, path);
    size_t prefix = (size_t)(marker - pattern); const char *suffix = marker + 4;
    size_t path_length = strlen(path), suffix_length = strlen(suffix);
    if (strncmp(pattern, path, prefix) || path_length < prefix + suffix_length ||
        strcmp(path + path_length - suffix_length, suffix)) return 0;
    size_t length = path_length - prefix - suffix_length;
    if (!length || length >= id_size || memchr(path + prefix, '/', length)) return 0;
    memcpy(id, path + prefix, length); id[length] = 0; url_decode(id); return 1;
}

static void session_cookie(const char *request, const char *headers_end, char token[65]) {
    token[0] = 0; const char *at = request;
    while ((at = strstr(at, "Cookie:")) && at < headers_end) {
        const char *line_end = strstr(at, "\r\n"); const char *name = strstr(at, "hyperian_session=");
        if (name && (!line_end || name < line_end)) {
            name += 17; size_t length = strcspn(name, ";\r\n"); if (length > 64) length = 64;
            memcpy(token, name, length); token[length] = 0; return;
        }
        at += 7;
    }
}

static void serve_client(int client, const Bytecode *code, Record **records, Session **sessions, const char *app_name) {
    char request[REQUEST_MAX + 1]; size_t used = 0; ssize_t got;
    while (used < REQUEST_MAX && (got = recv(client, request + used, REQUEST_MAX - used, 0)) > 0) {
        used += (size_t)got; request[used] = 0;
        char *headers_end = strstr(request, "\r\n\r\n");
        if (headers_end) {
            size_t content_length = 0; char *length_header = strstr(request, "Content-Length:");
            if (length_header && length_header < headers_end) content_length = (size_t)strtoul(length_header + 15, NULL, 10);
            if (used >= (size_t)(headers_end + 4 - request) + content_length) break;
        }
    }
    char method[8] = {0}, path[2048] = {0}; sscanf(request, "%7s %2047s", method, path);
    char *query = strchr(path, '?'); if (query) *query = 0;
    Form form = {0}; char *body = strstr(request, "\r\n\r\n"); if (body) { body += 4; parse_form(&form, body); }
    char token[65]; char *request_headers_end = strstr(request, "\r\n\r\n");
    session_cookie(request, request_headers_end ? request_headers_end : request + used, token);
    Response response = {404, error_page("Page not found"), NULL, "text/html; charset=utf-8", NULL, 0};
    int served_static = !strcmp(method, "GET") && load_static_response(code, path, &response);
    for (size_t i = 0; !served_static && i < code->count; i++) if (code->items[i].opcode == OP_ROUTE && !strcmp(code->items[i].args[0], method)) {
        char route_id[512] = {0};
        if (match_route(code->items[i].args[1], path, route_id, sizeof(route_id))) {
            free(response.body); response = execute_route(code, i, &form, records, app_name, *route_id ? route_id : NULL, token, sessions); break;
        }
    }
    if (response.status >= 400) {
        char wanted[16]; snprintf(wanted, sizeof(wanted), "%d", response.status);
        for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_ERROR_VIEW && !strcmp(code->items[i].args[0], wanted)) {
            Pair error_variables[1] = {{"status", wanted}}; Scope error_scope = {.variables = error_variables, .variable_count = 1};
            free(response.body); response.body = render_view(code, code->items[i].args[1], &error_scope, *records, app_name);
            response.content_type = "text/html; charset=utf-8"; response.body_length = 0; break;
        }
    }
    const char *status = response.status == 200 ? "200 OK" : response.status == 303 ? "303 See Other" : response.status == 401 ? "401 Unauthorized" :
        response.status == 400 ? "400 Bad Request" : response.status == 403 ? "403 Forbidden" : response.status == 404 ? "404 Not Found" :
        response.status == 422 ? "422 Unprocessable Content" : "500 Internal Server Error";
    Buffer header; buffer_init(&header);
    size_t response_length = response.body_length ? response.body_length : strlen(response.body);
    buffer_format(&header, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n", status,
        response.content_type ? response.content_type : "text/html; charset=utf-8", response_length);
    if (response.location) buffer_format(&header, "Location: %s\r\n", response.location);
    if (response.cookie) {
        if (*response.cookie) buffer_format(&header, "Set-Cookie: hyperian_session=%s; Path=/; HttpOnly; SameSite=Lax\r\n", response.cookie);
        else buffer_add(&header, "Set-Cookie: hyperian_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax\r\n");
    }
    buffer_add(&header, "\r\n"); send_all(client, header.data, header.length); send_all(client, response.body, response_length);
    free(header.data); free(response.body);
}

static void records_free(Record *record) {
    while (record) { Record *next = record->next; free(record->model); for (int i = 0; i < record->field_count; i++) { free(record->fields[i].key); free(record->fields[i].value); } free(record); record = next; }
}

static const char *data_path(void) {
    const char *configured = getenv("HYPERIAN_DATA"); return configured && *configured ? configured : "hyperian-data.hdb";
}

static const char *sqlite_data_path(const Bytecode *code) {
    const char *declared = NULL;
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_STORAGE && !strcmp(code->items[i].args[0], "sqlite")) declared = code->items[i].args[1];
    if (!declared) return NULL;
    const char *configured = getenv("HYPERIAN_DATA"); return configured && *configured ? configured : declared;
}

static int file_write_u32(FILE *file, uint32_t value) {
    unsigned char b[4] = {(unsigned char)value, (unsigned char)(value >> 8), (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return fwrite(b, 1, 4, file) == 4;
}

static int file_read_u32(FILE *file, uint32_t *value) {
    unsigned char b[4]; if (fread(b, 1, 4, file) != 4) return 0;
    *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; return 1;
}

static int file_write_text(FILE *file, const char *text) {
    size_t length = strlen(text); return length <= UINT32_MAX && file_write_u32(file, (uint32_t)length) && fwrite(text, 1, length, file) == length;
}

static char *file_read_text(FILE *file) {
    uint32_t length; if (!file_read_u32(file, &length) || length > 1024 * 1024) return NULL;
    char *text = malloc((size_t)length + 1); if (!text || fread(text, 1, length, file) != length) { free(text); return NULL; }
    text[length] = 0; return text;
}

static int save_hdb_records(Record *records, const Bytecode *code) {
    char temporary[4096]; snprintf(temporary, sizeof(temporary), "%s.tmp", data_path());
    FILE *file = fopen(temporary, "wb"); if (!file) return 0;
    uint32_t count = 0; for (Record *record = records; record; record = record->next) count++;
    int okay = fwrite("HDB2", 1, 4, file) == 4 && file_write_u32(file, program_data_version(code)) && file_write_u32(file, count);
    for (Record *record = records; okay && record; record = record->next) {
        okay = file_write_text(file, record->model) && file_write_u32(file, (uint32_t)record->field_count);
        for (int i = 0; okay && i < record->field_count; i++)
            okay = file_write_text(file, record->fields[i].key) && file_write_text(file, record->fields[i].value);
    }
    if (fclose(file)) okay = 0;
    if (okay) okay = rename(temporary, data_path()) == 0;
    if (!okay) remove(temporary);
    return okay;
}

#ifdef HYPERIAN_HAVE_SQLITE3
static int sqlite_schema(sqlite3 *database) {
    const char *sql = "CREATE TABLE IF NOT EXISTS hyperian_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS hyperian_records (model TEXT NOT NULL, record_id TEXT NOT NULL, field_name TEXT NOT NULL, field_value TEXT NOT NULL, field_order INTEGER NOT NULL, PRIMARY KEY(model, record_id, field_name));";
    return sqlite3_exec(database, sql, NULL, NULL, NULL) == SQLITE_OK;
}

static int save_sqlite_records(Record *records, const Bytecode *code, const char *path) {
    sqlite3 *database = NULL; sqlite3_stmt *insert = NULL, *meta = NULL; int okay = sqlite3_open(path, &database) == SQLITE_OK;
    if (okay) okay = sqlite_schema(database) && sqlite3_exec(database, "BEGIN IMMEDIATE; DELETE FROM hyperian_records;", NULL, NULL, NULL) == SQLITE_OK;
    if (okay) okay = sqlite3_prepare_v2(database, "INSERT INTO hyperian_records(model,record_id,field_name,field_value,field_order) VALUES(?,?,?,?,?);", -1, &insert, NULL) == SQLITE_OK;
    for (Record *record = records; okay && record; record = record->next) {
        const char *id = record_value(record, "id"); if (!id) id = "";
        for (int field = 0; okay && field < record->field_count; field++) {
            sqlite3_bind_text(insert, 1, record->model, -1, SQLITE_TRANSIENT); sqlite3_bind_text(insert, 2, id, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insert, 3, record->fields[field].key, -1, SQLITE_TRANSIENT); sqlite3_bind_text(insert, 4, record->fields[field].value, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insert, 5, field); okay = sqlite3_step(insert) == SQLITE_DONE; sqlite3_reset(insert); sqlite3_clear_bindings(insert);
        }
    }
    if (insert) sqlite3_finalize(insert);
    if (okay) okay = sqlite3_prepare_v2(database, "INSERT OR REPLACE INTO hyperian_meta(key,value) VALUES('data_version',?);", -1, &meta, NULL) == SQLITE_OK;
    if (okay) { char version[32]; snprintf(version, sizeof(version), "%u", program_data_version(code)); sqlite3_bind_text(meta, 1, version, -1, SQLITE_TRANSIENT); okay = sqlite3_step(meta) == SQLITE_DONE; }
    if (meta) sqlite3_finalize(meta);
    if (database) {
        if (okay) okay = sqlite3_exec(database, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK;
        else sqlite3_exec(database, "ROLLBACK;", NULL, NULL, NULL);
        if (!okay) fprintf(stderr, "error: SQLite could not save %s: %s\n", path, sqlite3_errmsg(database));
        sqlite3_close(database);
    }
    return okay;
}
#endif

static int save_records(Record *records, const Bytecode *code) {
    const char *sqlite_path = sqlite_data_path(code);
    if (!sqlite_path) return save_hdb_records(records, code);
#ifdef HYPERIAN_HAVE_SQLITE3
    return save_sqlite_records(records, code, sqlite_path);
#else
    fprintf(stderr, "error: this Hyperian build does not include SQLite support\n"); return 0;
#endif
}

static int rename_record_field(Record *record, const char *old_name, const char *new_name) {
    int old_at = -1, new_at = -1;
    for (int i = 0; i < record->field_count; i++) {
        if (!strcmp(record->fields[i].key, old_name)) old_at = i;
        if (!strcmp(record->fields[i].key, new_name)) new_at = i;
    }
    if (old_at < 0) return 1;
    if (new_at >= 0) {
        free(record->fields[old_at].key); free(record->fields[old_at].value);
        for (int i = old_at; i + 1 < record->field_count; i++) record->fields[i] = record->fields[i + 1];
        record->field_count--; return 1;
    }
    char *renamed = copy_string(new_name);
    if (!renamed) return 0;
    free(record->fields[old_at].key); record->fields[old_at].key = renamed; return 1;
}

static int apply_data_migrations(const Bytecode *code, Record *records, uint32_t stored_version) {
    uint32_t wanted = program_data_version(code);
    if (stored_version > wanted) {
        fprintf(stderr, "error: data file is version %u, but this application only understands version %u\n", stored_version, wanted);
        return 0;
    }
    for (uint32_t version = stored_version; version < wanted; version++) {
        size_t migration = code->count;
        for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_MIGRATION &&
            (uint32_t)strtoul(code->items[i].args[0], NULL, 10) == version) { migration = i; break; }
        if (migration == code->count) { fprintf(stderr, "error: no data migration starts at version %u\n", version); return 0; }
        for (size_t i = migration + 1; i < code->count && code->items[i].opcode != OP_END_MIGRATION; i++)
            if (code->items[i].opcode == OP_RENAME_FIELD)
                for (Record *record = records; record; record = record->next)
                    if (!strcmp(record->model, code->items[i].args[0]) &&
                        !rename_record_field(record, code->items[i].args[1], code->items[i].args[2])) return 0;
    }
    if (stored_version < wanted) {
        if (!save_records(records, code)) { fprintf(stderr, "error: could not save migrated data\n"); return 0; }
        printf("Migrated data from version %u to version %u.\n", stored_version, wanted);
    }
    return 1;
}

#ifdef HYPERIAN_HAVE_SQLITE3
static Record *load_sqlite_records(const Bytecode *code, const char *path, int *okay) {
    sqlite3 *database = NULL; sqlite3_stmt *statement = NULL; Record *records = NULL, **tail = &records, *current = NULL;
    char *last_model = NULL, *last_id = NULL; uint32_t stored_version = program_data_version(code); int has_version = 0;
    *okay = sqlite3_open(path, &database) == SQLITE_OK && sqlite_schema(database);
    if (*okay) *okay = sqlite3_prepare_v2(database, "SELECT value FROM hyperian_meta WHERE key='data_version';", -1, &statement, NULL) == SQLITE_OK;
    if (*okay && sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char *value = sqlite3_column_text(statement, 0); stored_version = value ? (uint32_t)strtoul((const char *)value, NULL, 10) : 0; has_version = 1;
        if (!stored_version) *okay = 0;
    }
    if (statement) { sqlite3_finalize(statement); statement = NULL; }
    if (*okay && !has_version) {
        char sql[160]; snprintf(sql, sizeof(sql), "INSERT OR REPLACE INTO hyperian_meta(key,value) VALUES('data_version','%u');", stored_version);
        *okay = sqlite3_exec(database, sql, NULL, NULL, NULL) == SQLITE_OK;
    }
    if (*okay) *okay = sqlite3_prepare_v2(database,
        "SELECT model,record_id,field_name,field_value FROM hyperian_records ORDER BY model,record_id,field_order;", -1, &statement, NULL) == SQLITE_OK;
    int step_result = SQLITE_DONE;
    while (*okay && (step_result = sqlite3_step(statement)) == SQLITE_ROW) {
        const char *model = (const char *)sqlite3_column_text(statement, 0), *id = (const char *)sqlite3_column_text(statement, 1);
        const char *field = (const char *)sqlite3_column_text(statement, 2), *value = (const char *)sqlite3_column_text(statement, 3);
        if (!model || !id || !field || !value) { *okay = 0; break; }
        if (!last_model || strcmp(last_model, model) || strcmp(last_id, id)) {
            current = calloc(1, sizeof(*current)); char *model_copy = copy_string(model), *id_copy = copy_string(id);
            if (!current || !model_copy || !id_copy) { free(current); free(model_copy); free(id_copy); *okay = 0; break; }
            current->model = model_copy; *tail = current; tail = &current->next;
            free(last_model); free(last_id); last_model = copy_string(model); last_id = id_copy;
            if (!last_model) { *okay = 0; break; }
        }
        if (current->field_count >= FIELD_MAX) { *okay = 0; break; }
        char *field_copy = copy_string(field), *value_copy = copy_string(value);
        if (!field_copy || !value_copy) { free(field_copy); free(value_copy); *okay = 0; break; }
        current->fields[current->field_count++] = (Pair){field_copy, value_copy};
    }
    if (*okay && step_result != SQLITE_DONE) *okay = 0;
    free(last_model); free(last_id); if (statement) sqlite3_finalize(statement);
    if (!*okay) {
        if (database) fprintf(stderr, "error: SQLite could not load %s: %s\n", path, sqlite3_errmsg(database));
        records_free(records); records = NULL;
    }
    if (database) sqlite3_close(database);
    if (!*okay) return NULL;
    for (Record **at = &records; *at;) {
        if (model_instruction(code, (*at)->model, NULL)) { at = &(*at)->next; continue; }
        Record *removed = *at; *at = removed->next; removed->next = NULL; records_free(removed);
    }
    if (!apply_data_migrations(code, records, stored_version)) { *okay = 0; records_free(records); return NULL; }
    return records;
}
#endif

static Record *load_records(const Bytecode *code, int *okay) {
    *okay = 1;
    const char *sqlite_path = sqlite_data_path(code);
    if (sqlite_path) {
#ifdef HYPERIAN_HAVE_SQLITE3
        return load_sqlite_records(code, sqlite_path, okay);
#else
        fprintf(stderr, "error: this Hyperian build does not include SQLite support\n"); *okay = 0; return NULL;
#endif
    }
    FILE *file = fopen(data_path(), "rb"); if (!file) return NULL;
    char magic[4]; uint32_t stored_version = 1, count;
    if (fread(magic, 1, 4, file) != 4) goto damaged;
    if (!memcmp(magic, "HDB1", 4)) {
        if (!file_read_u32(file, &count)) goto damaged;
    } else if (!memcmp(magic, "HDB2", 4)) {
        if (!file_read_u32(file, &stored_version) || !file_read_u32(file, &count) || stored_version < 1) goto damaged;
    } else goto damaged;
    if (count > 100000) goto damaged;
    Record *records = NULL, **tail = &records;
    for (uint32_t r = 0; r < count; r++) {
        Record *record = calloc(1, sizeof(*record)); uint32_t fields;
        if (!record || !(record->model = file_read_text(file)) || !file_read_u32(file, &fields) || fields > FIELD_MAX) { records_free(record); goto damaged_records; }
        for (uint32_t f = 0; f < fields; f++) {
            char *key = file_read_text(file), *value = file_read_text(file);
            if (!key || !value) { free(key); free(value); records_free(record); goto damaged_records; }
            record->fields[record->field_count++] = (Pair){key, value};
        }
        if (model_instruction(code, record->model, NULL)) { *tail = record; tail = &record->next; }
        else { free(record->model); for (int f = 0; f < record->field_count; f++) { free(record->fields[f].key); free(record->fields[f].value); } free(record); }
    }
    fclose(file);
    if (!apply_data_migrations(code, records, stored_version)) { *okay = 0; records_free(records); return NULL; }
    return records;
damaged_records:
    records_free(records);
damaged:
    fclose(file); *okay = 0; fprintf(stderr, "error: data file %s is damaged\n", data_path()); return NULL;
}

HyperianData *hyperian_data_open(const Bytecode *code, char *error, size_t error_size) {
    HyperianData *data = calloc(1, sizeof(*data)); int okay = 0;
    if (!data) { snprintf(error, error_size, "there was not enough memory to open the data"); return NULL; }
    data->code = code; data->records = load_records(code, &okay);
    if (!okay) { snprintf(error, error_size, "the application data could not be opened"); free(data); return NULL; }
    return data;
}

void hyperian_data_close(HyperianData *data) {
    if (!data) return;
    records_free(data->records); free(data);
}

static int console_render_range(const Bytecode *code, size_t from, size_t to, Scope *scope, Record *records) {
    for (size_t i = from; i < to; i++) {
        Instruction *in = &code->items[i];
        switch (in->opcode) {
            case OP_HEADING: printf("\n== %s ==\n", in->args[0]); break;
            case OP_TEXT:
            case OP_SAY: puts(in->args[0]); break;
            case OP_SHOW_VALUE: puts(resolve(scope, in->args[0])); break;
            case OP_LINK: printf("%s: %s\n", in->args[0], in->args[1]); break;
            case OP_EACH: {
                size_t end = find_end(code, i, OP_EACH, OP_END_EACH);
                if (scope->collection_alias && !strcmp(scope->collection_alias, in->args[1])) {
                    for (Record *record = records; record; record = record->next) if (!strcmp(record->model, scope->model)) {
                        Scope child = *scope; child.item_alias = in->args[0]; child.item = record;
                        fputs("- ", stdout); console_render_range(code, i + 1, end, &child, records);
                    }
                } else {
                    const char *encoded = local_value(scope->locals, in->args[1]);
                    if (encoded) {
                        const char *cursor = encoded; char value[2048]; int next;
                        while ((next = list_next(&cursor, value, sizeof(value))) > 0) {
                            Scope child = *scope; local_set(child.locals, in->args[0], value);
                            fputs("- ", stdout); console_render_range(code, i + 1, end, &child, records);
                        }
                    }
                }
                i = end; break;
            }
            case OP_IF: {
                size_t end = find_end(code, i, OP_IF, OP_END_IF);
                if (truthy(resolve(scope, in->args[0]))) console_render_range(code, i + 1, end, scope, records);
                i = end; break;
            }
            default: break;
        }
    }
    return 1;
}

static uint64_t monotonic_milliseconds(void) {
    struct timespec time; clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

static int run_console(const Bytecode *code, const char *app_name, int service) {
    VariableSet locals = {0};
    int data_okay; Record *records = load_records(code, &data_okay);
    if (!data_okay) return 1;
    HyperianData data = {.code = code, .records = records}; Scope scope = {.locals = &locals, .data = &data};
    printf("%s\n", app_name);
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_EVENT && !strcmp(code->items[i].args[0], "START")) {
        for (i++; i < code->count && code->items[i].opcode != OP_END_ROUTE; i++) {
            Instruction *in = &code->items[i];
            if (in->opcode == OP_ASK) {
                char answer[2048]; printf("%s ", in->args[0]); fflush(stdout);
                if (!fgets(answer, sizeof(answer), stdin)) answer[0] = 0;
                answer[strcspn(answer, "\r\n")] = 0;
                local_set(&locals, in->args[1], answer);
            } else if (is_logic_instruction(in->opcode)) {
                char error[256];
                if (!execute_logic_at(code, &i, &scope, 0, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); records_free(data.records); return 1; }
            } else if (in->opcode == OP_FIND_ALL) {
                scope.model = in->args[0]; scope.collection_alias = in->args[1];
            } else if (in->opcode == OP_SHOW_VIEW) {
                size_t start = code->count, end = code->count;
                for (size_t v = 0; v < code->count; v++) if (code->items[v].opcode == OP_VIEW && !strcmp(code->items[v].args[0], in->args[0])) {
                    start = v + 1; end = find_end(code, v, OP_VIEW, OP_END_VIEW); break;
                }
                console_render_range(code, start, end, &scope, data.records);
            }
        }
        break;
    }
    if (service) {
        typedef struct { const char *event; unsigned interval; uint64_t next; } ServiceTimer;
        ServiceTimer timers[HYPERIAN_STATE_MAX]; int timer_count = 0;
        uint64_t now = monotonic_milliseconds();
        for (size_t i = 0; i < code->count && timer_count < HYPERIAN_STATE_MAX; i++)
            if (code->items[i].opcode == OP_EVENT && !strncmp(code->items[i].args[0], "TIMER:", 6)) {
                int known = 0; for (int at = 0; at < timer_count; at++) if (!strcmp(timers[at].event, code->items[i].args[0])) known = 1;
                if (known) continue;
                unsigned interval = (unsigned)strtoul(code->items[i].args[0] + 6, NULL, 10);
                timers[timer_count++] = (ServiceTimer){code->items[i].args[0], interval, now + interval};
            }
        if (timer_count) {
            struct sigaction action; memset(&action, 0, sizeof(action)); action.sa_handler = stop_server; sigemptyset(&action.sa_mask);
            sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL); keep_running = 1;
            const char *limit_text = getenv("HYPERIAN_SERVICE_TEST_TICKS"); int limit = limit_text ? atoi(limit_text) : 0, ticks = 0;
            while (keep_running && (!limit || ticks < limit)) {
                now = monotonic_milliseconds(); uint64_t nearest = now + 1000;
                for (int i = 0; i < timer_count; i++) {
                    if (now >= timers[i].next) {
                        char error[256] = {0};
                        if (!hyperian_execute_data_event(&data, timers[i].event, &locals, error, sizeof(error))) {
                            fprintf(stderr, "error in scheduled service work: %s\n", error); records_free(data.records); return 1;
                        }
                        do timers[i].next += timers[i].interval; while (timers[i].next <= now);
                        ticks++;
                    }
                    if (timers[i].next < nearest) nearest = timers[i].next;
                }
                if (keep_running && (!limit || ticks < limit)) {
                    now = monotonic_milliseconds(); uint64_t wait = nearest > now ? nearest - now : 1;
                    struct timespec pause = {(time_t)(wait / 1000), (long)(wait % 1000) * 1000000}; nanosleep(&pause, NULL);
                }
            }
            const char *test_name = getenv("HYPERIAN_SERVICE_TEST_STATE");
            if (test_name) printf("%s=%s\n", test_name, hyperian_state_get(&locals, test_name) ? hyperian_state_get(&locals, test_name) : "");
        }
    }
    const char *console_test_name = getenv("HYPERIAN_CONSOLE_TEST_STATE");
    if (console_test_name) printf("%s=%s\n", console_test_name,
        hyperian_state_get(&locals, console_test_name) ? hyperian_state_get(&locals, console_test_name) : "");
    records_free(data.records); return 0;
}

int run_bytecode(const char *path, int port_override) {
    Bytecode code; bytecode_init(&code); char error[256];
    if (!bytecode_read(&code, path, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    int port = port_override; const char *name = "Hyperian App", *target = "web";
    for (size_t i = 0; i < code.count; i++) {
        if (code.items[i].opcode == OP_APPLICATION) name = code.items[i].args[0];
        if (code.items[i].opcode == OP_TARGET) target = code.items[i].args[0];
        if (!port && code.items[i].opcode == OP_PORT) port = atoi(code.items[i].args[0]);
    }
    if (!strcmp(target, "console") || !strcmp(target, "service")) {
        int result = run_console(&code, name, !strcmp(target, "service")); bytecode_free(&code); return result;
    }
    if (!strcmp(target, "desktop")) { int result = run_desktop_app(&code, name); bytecode_free(&code); return result; }
    if (!strcmp(target, "mobile")) { int result = run_mobile_app(&code, name); bytecode_free(&code); return result; }
    if (!strcmp(target, "game")) { int result = run_game_app(&code, name); bytecode_free(&code); return result; }
    if (strcmp(target, "web") && strcmp(target, "pwa") && strcmp(target, "api")) {
        fprintf(stderr, "error: the %s backend is declared but is not implemented yet\n", target);
        bytecode_free(&code); return 1;
    }
    if (!port) port = 8000;
    int server = socket(AF_INET, SOCK_STREAM, 0); int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 16) < 0) {
        fprintf(stderr, "error: cannot listen on port %d: %s\n", port, strerror(errno)); if (server >= 0) close(server); bytecode_free(&code); return 1;
    }
    struct sigaction action; memset(&action, 0, sizeof(action)); action.sa_handler = stop_server; sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    printf("%s is running at http://127.0.0.1:%d\n", name, port); fflush(stdout);
    int data_okay; Record *records = load_records(&code, &data_okay); Session *sessions = NULL;
    if (!data_okay) { close(server); bytecode_free(&code); return 1; }
    while (keep_running) {
        int client = accept(server, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; break; }
        serve_client(client, &code, &records, &sessions, name); close(client);
    }
    close(server); records_free(records); sessions_free(sessions); bytecode_free(&code); return 0;
}

int inspect_bytecode(const char *path) {
    Bytecode code; bytecode_init(&code); char error[256];
    if (!bytecode_read(&code, path, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    printf("Hyperian bytecode: %zu instructions\n", code.count);
    for (size_t i = 0; i < code.count; i++) {
        Instruction *in = &code.items[i]; printf("%04zu  %-16s", i, opcode_name(in->opcode));
        for (int a = 0; a < in->argc; a++) printf(" %s", in->args[a]);
        printf("\n");
    }
    bytecode_free(&code); return 0;
}

int test_bytecode(const char *path) {
    Bytecode code; bytecode_init(&code); char error[256];
    if (!bytecode_read(&code, path, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    int tests = 0, failures = 0;
    for (size_t i = 0; i < code.count; i++) if (code.items[i].opcode == OP_TEST) {
        tests++; VariableSet locals = {0}; Scope scope = {.locals = &locals};
        size_t end = find_end(&code, i, OP_TEST, OP_END_TEST); int failed = 0;
        for (size_t at = i + 1; at < end; at++) {
            if (is_logic_instruction(code.items[at].opcode)) {
                if (!execute_logic_at(&code, &at, &scope, 0, error, sizeof(error))) {
                    printf("FAIL  %s — %s\n", code.items[i].args[0], error); failed = 1; break;
                }
            } else if (code.items[at].opcode == OP_EXPECT && !condition_is_true(&scope, code.items[at].args[0])) {
                printf("FAIL  %s — expected %s\n", code.items[i].args[0], code.items[at].args[0]); failed = 1; break;
            }
        }
        if (failed) failures++; else printf("PASS  %s\n", code.items[i].args[0]); i = end;
    }
    if (!tests) { fprintf(stderr, "error: this program has no test blocks\n"); failures = 1; }
    printf("%d test%s, %d failure%s\n", tests, tests == 1 ? "" : "s", failures, failures == 1 ? "" : "s");
    bytecode_free(&code); return failures ? 1 : 0;
}

int migrate_bytecode(const char *path) {
    Bytecode code; bytecode_init(&code); char error[256];
    if (!bytecode_read(&code, path, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    int data_okay; Record *records = load_records(&code, &data_okay);
    if (!data_okay) { bytecode_free(&code); return 1; }
    printf("Data is ready at version %u.\n", program_data_version(&code));
    records_free(records); bytecode_free(&code); return 0;
}
