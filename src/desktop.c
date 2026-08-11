#include "hyperian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HYPERIAN_HAVE_GTK3
#include <gtk/gtk.h>

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

static void add_desktop_widgets(const Bytecode *code, GtkWidget *box, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        Instruction *in = &code->items[i]; GtkWidget *widget = NULL;
        if (in->opcode == OP_HEADING) {
            char *safe = g_markup_escape_text(in->args[0], -1), *markup = g_strdup_printf("<span size=\"xx-large\" weight=\"bold\">%s</span>", safe);
            widget = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(widget), markup); g_free(markup); g_free(safe);
        } else if (in->opcode == OP_TEXT) widget = gtk_label_new(in->args[0]);
        else if (in->opcode == OP_BUTTON) widget = gtk_button_new_with_label(in->args[0]);
        else if (in->opcode == OP_INPUT) { widget = gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(widget), in->args[0]); }
        else if (in->opcode == OP_TEXTAREA) { widget = gtk_text_view_new(); gtk_widget_set_size_request(widget, -1, 120); }
        else if (in->opcode == OP_CHECKBOX) widget = gtk_check_button_new_with_label(in->args[0]);
        else if (in->opcode == OP_LINK) widget = gtk_link_button_new_with_label(in->args[1], in->args[0]);
        else if (in->opcode == OP_IMAGE) widget = gtk_image_new_from_file(in->args[0]);
        if (widget) { gtk_widget_set_halign(widget, GTK_ALIGN_FILL); gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 4); }
    }
}

int run_desktop_app(const Bytecode *code, const char *name) {
    if (!gtk_init_check(NULL, NULL)) { fprintf(stderr, "error: cannot open a desktop display\n"); return 1; }
    const char *view_name = starting_view(code); if (!view_name) { fprintf(stderr, "error: desktop application needs a starting view\n"); return 1; }
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL), *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_window_set_title(GTK_WINDOW(window), name); gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(window), 20); gtk_container_add(GTK_CONTAINER(window), box);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    for (size_t i = 0; i < code->count; i++) if (code->items[i].opcode == OP_VIEW && !strcmp(code->items[i].args[0], view_name)) {
        add_desktop_widgets(code, box, i + 1, desktop_end(code, i, OP_END_VIEW)); break;
    }
    gtk_widget_show_all(window);
    if (getenv("HYPERIAN_VISUAL_TEST")) gtk_main_iteration_do(FALSE);
    else gtk_main();
    return 0;
}
#else
int run_desktop_app(const Bytecode *code, const char *name) {
    (void)code; (void)name; fprintf(stderr, "error: this Hyperian build does not include the GTK desktop backend\n"); return 1;
}
#endif
