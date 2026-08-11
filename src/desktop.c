#include "hyperian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HYPERIAN_HAVE_GTK3
#include <gtk/gtk.h>

typedef enum { DESKTOP_ENTRY, DESKTOP_TEXT, DESKTOP_CHECK } DesktopInputKind;
typedef struct { const char *name; GtkWidget *widget; DesktopInputKind kind; } DesktopInput;
typedef struct { const char *expression; GtkWidget *label; } DesktopOutput;
typedef struct DesktopContext DesktopContext;
typedef struct { DesktopContext *context; const char *action; } DesktopButton;
struct DesktopContext {
    const Bytecode *code; HyperianState state;
    GtkWidget *box;
    DesktopInput inputs[HYPERIAN_STATE_MAX]; int input_count;
    DesktopOutput outputs[HYPERIAN_STATE_MAX]; int output_count;
    DesktopButton buttons[HYPERIAN_STATE_MAX]; int button_count;
};

static int show_interface_view(DesktopContext *context, const char *name);

static size_t desktop_end(const Bytecode *code, size_t start, uint8_t close) {
    for (size_t i = start + 1; i < code->count; i++) if (code->items[i].opcode == close) return i;
    return code->count;
}

static const char *starting_view(const Bytecode *code) {
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_EVENT && !strcmp(code->items[i].args[0], "START")) {
        size_t end = desktop_end(code, i, OP_END_ROUTE);
        for (i++; i < end; i++) if (code->items[i].opcode == OP_SHOW_VIEW) return code->items[i].args[0];
    }
    return NULL;
}

static void sync_inputs(DesktopContext *context) {
    for (int i = 0; i < context->input_count; i++) {
        DesktopInput *input = &context->inputs[i]; const char *value = ""; char *allocated = NULL;
        if (input->kind == DESKTOP_ENTRY) value = gtk_entry_get_text(GTK_ENTRY(input->widget));
        else if (input->kind == DESKTOP_CHECK) value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(input->widget)) ? "true" : "false";
        else {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(input->widget)); GtkTextIter start, end;
            gtk_text_buffer_get_bounds(buffer, &start, &end); allocated = gtk_text_buffer_get_text(buffer, &start, &end, FALSE); value = allocated;
        }
        hyperian_state_set(&context->state, input->name, value); g_free(allocated);
    }
}

static void refresh_outputs(DesktopContext *context) {
    for (int i = 0; i < context->output_count; i++) {
        char value[HYPERIAN_VALUE_SIZE]; hyperian_state_evaluate(&context->state, context->outputs[i].expression, value, sizeof(value));
        gtk_label_set_text(GTK_LABEL(context->outputs[i].label), value);
    }
}

static void action_clicked(GtkButton *button, gpointer data) {
    (void)button; DesktopButton *binding = data; char error[256] = {0}; sync_inputs(binding->context);
    if (!hyperian_execute_action(binding->context->code, binding->action, NULL, &binding->context->state, error, sizeof(error)))
        hyperian_state_set(&binding->context->state, "error", error);
    const char *next = hyperian_state_get(&binding->context->state, "__hyperian_open_view");
    if (next && *next) {
        char name[256]; snprintf(name, sizeof(name), "%s", next); hyperian_state_set(&binding->context->state, "__hyperian_open_view", "");
        if (!show_interface_view(binding->context, name)) hyperian_state_set(&binding->context->state, "error", "the requested view could not be opened");
    } else refresh_outputs(binding->context);
}

static void remember_input(DesktopContext *context, const char *name, GtkWidget *widget, DesktopInputKind kind) {
    if (context->input_count < HYPERIAN_STATE_MAX) context->inputs[context->input_count++] = (DesktopInput){name, widget, kind};
}

static void add_desktop_widgets(DesktopContext *context, GtkWidget *box, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        Instruction *in = &context->code->items[i]; GtkWidget *widget = NULL;
        if (in->opcode == OP_HEADING) {
            char *safe = g_markup_escape_text(in->args[0], -1), *markup = g_strdup_printf("<span size=\"xx-large\" weight=\"bold\">%s</span>", safe);
            widget = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(widget), markup); g_free(markup); g_free(safe);
        } else if (in->opcode == OP_TEXT) widget = gtk_label_new(in->args[0]);
        else if (in->opcode == OP_SHOW_VALUE) {
            widget = gtk_label_new("");
            if (context->output_count < HYPERIAN_STATE_MAX) context->outputs[context->output_count++] = (DesktopOutput){in->args[0], widget};
        } else if (in->opcode == OP_BUTTON) widget = gtk_button_new_with_label(in->args[0]);
        else if (in->opcode == OP_BUTTON_ACTION) {
            widget = gtk_button_new_with_label(in->args[0]);
            if (context->button_count < HYPERIAN_STATE_MAX) {
                DesktopButton *binding = &context->buttons[context->button_count++]; *binding = (DesktopButton){context, in->args[1]};
                g_signal_connect(widget, "clicked", G_CALLBACK(action_clicked), binding);
            }
        } else if (in->opcode == OP_INPUT) {
            widget = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(widget), in->args[0]); remember_input(context, in->args[1], widget, DESKTOP_ENTRY);
        } else if (in->opcode == OP_TEXTAREA) {
            widget = gtk_text_view_new(); gtk_widget_set_size_request(widget, -1, 120); remember_input(context, in->args[1], widget, DESKTOP_TEXT);
        } else if (in->opcode == OP_CHECKBOX) {
            widget = gtk_check_button_new_with_label(in->args[0]); remember_input(context, in->args[1], widget, DESKTOP_CHECK);
        } else if (in->opcode == OP_LINK) widget = gtk_link_button_new_with_label(in->args[1], in->args[0]);
        else if (in->opcode == OP_IMAGE) widget = gtk_image_new_from_file(in->args[0]);
        if (widget) { gtk_widget_set_halign(widget, GTK_ALIGN_FILL); gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 4); }
    }
}

static int show_interface_view(DesktopContext *context, const char *name) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(context->box));
    for (GList *at = children; at; at = at->next) gtk_widget_destroy(GTK_WIDGET(at->data));
    g_list_free(children); context->input_count = context->output_count = context->button_count = 0;
    for (size_t i = 0; i < context->code->count; i++) if (context->code->items[i].opcode == OP_VIEW && !strcmp(context->code->items[i].args[0], name)) {
        add_desktop_widgets(context, context->box, i + 1, desktop_end(context->code, i, OP_END_VIEW));
        hyperian_state_set(&context->state, "current_view", name); refresh_outputs(context); gtk_widget_show_all(context->box); return 1;
    }
    return 0;
}

static gboolean window_closing(GtkWidget *window, GdkEvent *event, gpointer data) {
    (void)window; (void)event; DesktopContext *context = data; char error[256] = {0}; sync_inputs(context);
    if (!hyperian_execute_event(context->code, "CLOSE", &context->state, error, sizeof(error))) fprintf(stderr, "error while closing: %s\n", error);
    gtk_main_quit(); return FALSE;
}

static int run_interface_app(const Bytecode *code, const char *name, int mobile) {
    if (!gtk_init_check(NULL, NULL)) { fprintf(stderr, "error: cannot open a desktop display\n"); return 1; }
    const char *view_name = starting_view(code); if (!view_name) { fprintf(stderr, "error: desktop application needs a starting view\n"); return 1; }
    DesktopContext context = {.code = code}; hyperian_state_init(&context.state); char error[256] = {0};
    if (!hyperian_execute_event(code, "START", &context.state, error, sizeof(error))) { fprintf(stderr, "error: %s\n", error); return 1; }
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL), *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); context.box = box;
    char title[256]; snprintf(title, sizeof(title), "%s%s", name, mobile ? " — mobile preview" : "");
    gtk_window_set_title(GTK_WINDOW(window), title); gtk_window_set_default_size(GTK_WINDOW(window), mobile ? 390 : 800, mobile ? 780 : 600);
    gtk_container_set_border_width(GTK_CONTAINER(window), mobile ? 16 : 20); gtk_box_set_spacing(GTK_BOX(box), mobile ? 12 : 8); gtk_container_add(GTK_CONTAINER(window), box);
    g_signal_connect(window, "delete-event", G_CALLBACK(window_closing), &context);
    if (!show_interface_view(&context, view_name)) { fprintf(stderr, "error: starting view does not exist\n"); gtk_widget_destroy(window); return 1; }
    gtk_widget_show_all(window);
    if (getenv("HYPERIAN_VISUAL_TEST")) {
        const char *wanted_action = getenv("HYPERIAN_VISUAL_TEST_ACTION"); DesktopButton *chosen = context.button_count ? &context.buttons[0] : NULL;
        for (int i = 0; wanted_action && i < context.button_count; i++) if (!strcmp(context.buttons[i].action, wanted_action)) chosen = &context.buttons[i];
        if (chosen) action_clicked(NULL, chosen);
        const char *test_name = getenv("HYPERIAN_VISUAL_TEST_STATE");
        if (test_name) printf("%s=%s\n", test_name, hyperian_state_get(&context.state, test_name) ? hyperian_state_get(&context.state, test_name) : "");
        gtk_main_iteration_do(FALSE);
    } else gtk_main();
    return 0;
}
int run_desktop_app(const Bytecode *code, const char *name) { return run_interface_app(code, name, 0); }
int run_mobile_app(const Bytecode *code, const char *name) { return run_interface_app(code, name, 1); }
#else
int run_desktop_app(const Bytecode *code, const char *name) {
    (void)code; (void)name; fprintf(stderr, "error: this Hyperian build does not include the GTK desktop backend\n"); return 1;
}
int run_mobile_app(const Bytecode *code, const char *name) {
    (void)code; (void)name; fprintf(stderr, "error: this Hyperian build does not include the GTK mobile preview backend\n"); return 1;
}
#endif
