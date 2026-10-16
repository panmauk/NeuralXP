// NeuralXP - Offline AI (Linux / GTK3 port)
// Built by Panmox (panmox.org)
// Port of gui/ai-assistant.cpp: same layout, menus, chat system and XP Luna look.

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define NXP_VERSION "1.0.1"

#define WIN_WIDTH   540
#define WIN_HEIGHT  500
#define MAX_CHAT_NAME 64

// Same as the Windows build (gui/ai-assistant.cpp)
#define NXP_CONTEXT_SIZE "512"
#define NXP_MAX_TOKENS   "80"

#define MODEL_MAIN  "qwen2.5-0.5b-instruct-q2_k.gguf"
#define MODEL_SMALL "SmolLM-135M-Instruct.Q4_K_M.gguf"
#define MODEL_URL   "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/" MODEL_MAIN
#define MODEL_SIZE  415182688LL
#define MODEL_SHA256 "9ee36184e616dfc76df4f5dd66f908dbde6979524ae36e6cefb67f532f798cb8"

#define WELCOME_TEXT \
    "Welcome to NeuralXP!\n" \
    "Offline AI  -  Built by Panmox (panmox.org)\n" \
    "Type your question below and press Send.\n" \
    "____________________________________________________________"

// ---- Global state ----
static GtkWidget *g_window, *g_chat_view, *g_input_view, *g_send_button, *g_clear_button;
static GtkWidget *g_status_main, *g_status_count;
static GtkTextBuffer *g_chat_buf, *g_input_buf;

static gboolean g_generating = FALSE;
static GPid g_child_pid = 0;
static GString *g_child_stderr = NULL;
static GString *g_stdout_pending = NULL;  // incomplete UTF-8 bytes between reads
static gboolean g_first_char = TRUE;
static gboolean g_got_output = FALSE;
static int g_open_streams = 0;
static gboolean g_child_exited = FALSE;

static char *g_exe_dir = NULL;
static char *g_helper_path = NULL;
static char *g_model_path = NULL;
static char *g_data_dir = NULL;     // ~/.local/share/neuralxp
static char *g_chats_dir = NULL;
static int g_message_count = 0;
static char g_current_chat[MAX_CHAT_NAME] = {0};
static char g_chat_context[300] = {0};

// ---- Small helpers ----

static void set_status(const char *text) { gtk_label_set_text(GTK_LABEL(g_status_main), text); }

static void update_message_count(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Messages: %d", g_message_count);
    gtk_label_set_text(GTK_LABEL(g_status_count), buf);
}

static void scroll_chat_to_end(void) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g_chat_buf, &end);
    GtkTextMark *mark = gtk_text_buffer_get_mark(g_chat_buf, "end");
    gtk_text_buffer_move_mark(g_chat_buf, mark, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(g_chat_view), mark);
}

static void append_chat(const char *text) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g_chat_buf, &end);
    gtk_text_buffer_insert(g_chat_buf, &end, text, -1);
    scroll_chat_to_end();
}

static void append_chat_line(const char *text) {
    if (gtk_text_buffer_get_char_count(g_chat_buf) > 0) append_chat("\n");
    append_chat(text);
}

static char *get_chat_text(void) {
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(g_chat_buf, &s, &e);
    return gtk_text_buffer_get_text(g_chat_buf, &s, &e, FALSE);
}

static void message_box(GtkMessageType type, const char *title, const char *text) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
                                          type, GTK_BUTTONS_OK, "%s", text);
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static gboolean ask_yes_no(const char *title, const char *text) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", text);
    gtk_window_set_title(GTK_WINDOW(d), title);
    gint r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r == GTK_RESPONSE_YES;
}

// ---- Chat file helpers (same format and rules as the Windows build) ----

static void get_chat_file_path(char *out, size_t outLen, const char *chatName) {
    snprintf(out, outLen, "%s/%s.txt", g_chats_dir, chatName);
}

// Remove characters that are bad in file names (Windows set kept so chats stay portable)
static void sanitize_name(char *name) {
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) {
            name[i] = '_';
        }
    }
    // Trim spaces; a name made only of dots would be "." or ".." on disk
    char *s = name;
    while (*s == ' ') s++;
    memmove(name, s, strlen(s) + 1);
    int len = strlen(name);
    while (len > 0 && name[len-1] == ' ') name[--len] = '\0';
    if (strspn(name, ".") == strlen(name)) name[0] = '\0';
}

static gboolean is_filler_line(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    int len = strlen(line);
    if (len < 4) return TRUE;

    const char *fillers[] = {
        "hi", "hello", "hey", "ok", "okay", "thanks", "thank you",
        "bye", "goodbye", "yes", "no", "sure", "cool", "nice",
        "how are you", "how can i help", "how can i assist",
        "welcome to neuralxp", "type your question",
        "____", "========", NULL
    };

    char lower[256] = {0};
    for (int i = 0; i < 255 && line[i]; i++)
        lower[i] = (line[i] >= 'A' && line[i] <= 'Z') ? line[i] + 32 : line[i];

    for (int i = 0; fillers[i]; i++) {
        if (strcmp(lower, fillers[i]) == 0) return TRUE;
        if (strstr(lower, fillers[i]) == lower && len < (int)strlen(fillers[i]) + 5) return TRUE;
    }
    return FALSE;
}

static void auto_save_chat(void) {
    if (g_current_chat[0] == '\0') return;

    char *all = get_chat_text();
    if (!all || !all[0]) { g_free(all); return; }

    char path[4096];
    get_chat_file_path(path, sizeof(path), g_current_chat);
    // Write to a temp file and rename, so a crash never leaves a half-written chat
    char tmp[4200];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { g_free(all); set_status("Error - could not save chat"); return; }

    fprintf(f, "[NeuralXP Chat: %s]\n", g_current_chat);

    gboolean keeping = FALSE;
    char **lines = g_strsplit(all, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = lines[i];
        size_t l = strlen(line);
        if (l && line[l-1] == '\r') line[l-1] = '\0';
        if (!line[0]) continue;
        if (strncmp(line, "You:  ", 6) == 0) {
            keeping = !is_filler_line(line + 6);
            if (keeping) fprintf(f, "%s\n", line);
        } else if (strncmp(line, "NeuralXP:  ", 11) == 0) {
            keeping = !is_filler_line(line + 11);
            if (keeping) fprintf(f, "%s\n", line);
        } else if (strncmp(line, "____", 4) == 0 || strncmp(line, "NeuralXP - Chat: ", 17) == 0) {
            keeping = FALSE;
        } else if (keeping) {
            fprintf(f, "%s\n", line);  // continuation of a multi-line message
        }
    }
    g_strfreev(lines);
    g_free(all);
    if (fclose(f) == 0) g_rename(tmp, path);
    else g_unlink(tmp);
}

// Loads the whole chat for display and builds the tiny context (last 2 user messages).
// Returns the display text (g_free) or NULL.
static char *load_chat_context(const char *chatName) {
    char path[4096];
    get_chat_file_path(path, sizeof(path), chatName);
    g_chat_context[0] = '\0';

    char *data = NULL;
    if (!g_file_get_contents(path, &data, NULL, NULL)) return NULL;

    GString *full = g_string_new(NULL);
    const char *lastUser[2] = {NULL, NULL};
    char **lines = g_strsplit(data, "\n", -1);
    for (int i = 1; lines[i]; i++) {  // line 0 = header
        char *line = lines[i];
        size_t l = strlen(line);
        while (l && (line[l-1] == '\r' || line[l-1] == '\n')) line[--l] = '\0';
        if (!l) continue;
        g_string_append(full, line);
        g_string_append_c(full, '\n');
        if (strncmp(line, "You:  ", 6) == 0) {
            lastUser[1] = lastUser[0];
            lastUser[0] = line + 6;
        }
    }

    int contextLen = 0;
    for (int i = 1; i >= 0; i--) {
        if (!lastUser[i]) continue;
        // Truncate long messages to ~80 bytes without cutting a UTF-8 character
        const char *end = lastUser[i];
        while (*end && end - lastUser[i] < 80) end = g_utf8_next_char(end);
        int clen = end - lastUser[i];
        if (contextLen + clen + 4 < (int)sizeof(g_chat_context)) {
            if (contextLen > 0) { g_chat_context[contextLen++] = ';'; g_chat_context[contextLen++] = ' '; }
            memcpy(g_chat_context + contextLen, lastUser[i], clen);
            contextLen += clen;
            g_chat_context[contextLen] = '\0';
        }
    }
    g_strfreev(lines);
    g_free(data);
    return g_string_free(full, FALSE);
}

static void update_title(void) {
    char title[256];
    if (g_current_chat[0]) snprintf(title, sizeof(title), "NeuralXP - %s", g_current_chat);
    else snprintf(title, sizeof(title), "NeuralXP - Offline AI");
    gtk_window_set_title(GTK_WINDOW(g_window), title);
}

static GPtrArray *list_chats(void) {
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GDir *dir = g_dir_open(g_chats_dir, 0, NULL);
    if (!dir) return names;
    const char *fn;
    while ((fn = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(fn, ".txt")) continue;
        size_t n = strlen(fn) - 4;
        if (n == 0 || n >= MAX_CHAT_NAME) continue;
        g_ptr_array_add(names, g_strndup(fn, n));
    }
    g_dir_close(dir);
    return names;
}

// g_ptr_array_sort passes pointers to elements
static gint cmp_names(gconstpointer a, gconstpointer b) {
    return g_utf8_collate(*(char * const *)a, *(char * const *)b);
}

// ---- Dialogs ----

// "New Chat" name dialog. Returns TRUE and fills out on OK.
static gboolean show_name_dialog(char *out) {
    GtkWidget *d = gtk_dialog_new_with_buttons("New Chat", GTK_WINDOW(g_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "OK", GTK_RESPONSE_OK, "Cancel", GTK_RESPONSE_CANCEL, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    gtk_window_set_resizable(GTK_WINDOW(d), FALSE);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_box_set_spacing(GTK_BOX(box), 4);
    GtkWidget *lbl = gtk_label_new("Chat name:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), MAX_CHAT_NAME - 1);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_set_size_request(entry, 260, -1);
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(d);

    gboolean ok = FALSE;
    while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        char name[MAX_CHAT_NAME * 4];
        // Byte limit: max_length counts characters, the buffer is bytes
        g_strlcpy(name, gtk_entry_get_text(GTK_ENTRY(entry)), sizeof(name));
        if (strlen(name) >= MAX_CHAT_NAME) {
            const char *e = name;
            while (*e && g_utf8_next_char(e) - name < MAX_CHAT_NAME) e = g_utf8_next_char(e);
            name[e - name] = '\0';
        }
        sanitize_name(name);
        if (!name[0]) {
            GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(d), GTK_DIALOG_MODAL,
                GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "Please enter a chat name.");
            gtk_window_set_title(GTK_WINDOW(m), "NeuralXP");
            gtk_dialog_run(GTK_DIALOG(m));
            gtk_widget_destroy(m);
            continue;
        }
        g_strlcpy(out, name, MAX_CHAT_NAME);
        ok = TRUE;
        break;
    }
    gtk_widget_destroy(d);
    return ok;
}

static void fill_chat_list(GtkListStore *store, GtkWidget *open_btn, GtkWidget *del_btn, GtkTreeView *tv) {
    gtk_list_store_clear(store);
    GPtrArray *names = list_chats();
    g_ptr_array_sort(names, cmp_names);
    GtkTreeIter it;
    if (names->len == 0) {
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, "(no saved chats)", -1);
        gtk_widget_set_sensitive(open_btn, FALSE);
        gtk_widget_set_sensitive(del_btn, FALSE);
    } else {
        for (guint i = 0; i < names->len; i++) {
            gtk_list_store_append(store, &it);
            gtk_list_store_set(store, &it, 0, (char *)names->pdata[i], -1);
        }
        GtkTreePath *p = gtk_tree_path_new_first();
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(tv), p);
        gtk_tree_path_free(p);
        gtk_widget_set_sensitive(open_btn, TRUE);
        gtk_widget_set_sensitive(del_btn, TRUE);
    }
    g_ptr_array_free(names, TRUE);
}

static char *selected_chat(GtkTreeView *tv) {
    GtkTreeModel *m;
    GtkTreeIter it;
    char *name = NULL;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tv), &m, &it))
        gtk_tree_model_get(m, &it, 0, &name, -1);
    return name;
}

static void on_row_activated(GtkTreeView *tv, GtkTreePath *p, GtkTreeViewColumn *c, gpointer d) {
    (void)tv; (void)p; (void)c;
    gtk_dialog_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
}

// "Open Chat" dialog: list + Open / Delete / Cancel. Returns chosen name (g_free) or NULL.
static char *show_open_chat_dialog(void) {
    GtkWidget *d = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(d), "Open Chat");
    gtk_window_set_transient_for(GTK_WINDOW(d), GTK_WINDOW(g_window));
    gtk_window_set_modal(GTK_WINDOW(d), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(d), 300, 280);
    GtkWidget *open_btn = gtk_dialog_add_button(GTK_DIALOG(d), "Open", GTK_RESPONSE_OK);
    GtkWidget *del_btn = gtk_dialog_add_button(GTK_DIALOG(d), "Delete", 1);
    gtk_dialog_add_button(GTK_DIALOG(d), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);

    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkWidget *tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv),
        gtk_tree_view_column_new_with_attributes("Chat", gtk_cell_renderer_text_new(), "text", 0, NULL));
    g_signal_connect(tv, "row-activated", G_CALLBACK(on_row_activated), d);
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);
    fill_chat_list(store, open_btn, del_btn, GTK_TREE_VIEW(tv));
    gtk_widget_show_all(d);

    char *result = NULL;
    for (;;) {
        gint r = gtk_dialog_run(GTK_DIALOG(d));
        if (r == GTK_RESPONSE_OK) {
            if (gtk_widget_get_sensitive(open_btn)) result = selected_chat(GTK_TREE_VIEW(tv));
            break;
        } else if (r == 1) {
            char *name = selected_chat(GTK_TREE_VIEW(tv));
            if (name) {
                char q[256];
                snprintf(q, sizeof(q), "Delete chat \"%s\"?", name);
                GtkWidget *m = gtk_message_dialog_new(GTK_WINDOW(d), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", q);
                gtk_window_set_title(GTK_WINDOW(m), "Delete Chat");
                gboolean yes = gtk_dialog_run(GTK_DIALOG(m)) == GTK_RESPONSE_YES;
                gtk_widget_destroy(m);
                if (yes) {
                    char path[4096];
                    get_chat_file_path(path, sizeof(path), name);
                    g_unlink(path);
                    // Deleting the open chat: forget it, or auto-save would recreate the file
                    if (strcmp(name, g_current_chat) == 0) {
                        g_current_chat[0] = '\0';
                        g_chat_context[0] = '\0';
                        update_title();
                    }
                    fill_chat_list(store, open_btn, del_btn, GTK_TREE_VIEW(tv));
                }
                g_free(name);
            }
        } else {
            break;
        }
    }
    gtk_widget_destroy(d);
    g_object_unref(store);
    return result;
}

// ---- Inference ----

static void finish_inference(const char *status) {
    set_status(status);
    g_generating = FALSE;
    g_child_pid = 0;
    gtk_widget_set_sensitive(g_send_button, TRUE);
    gtk_widget_set_sensitive(g_input_view, TRUE);
    gtk_widget_grab_focus(g_input_view);
}

static void maybe_finish(void) {
    if (!g_child_exited || g_open_streams > 0) return;
    if (!g_got_output) {
        // Show the helper's message (e.g. "message too long") instead of an empty answer
        char *msg = g_strstrip(g_strdup(g_child_stderr ? g_child_stderr->str : ""));
        append_chat(msg[0] ? msg : "Sorry, I couldn't come up with an answer.");
        g_free(msg);
    }
    if (g_child_stderr) { g_string_free(g_child_stderr, TRUE); g_child_stderr = NULL; }
    g_message_count++;
    update_message_count();
    finish_inference("Ready");
}

static gboolean on_child_stdout(GIOChannel *ch, GIOCondition cond, gpointer data) {
    (void)data;
    if (cond & G_IO_IN) {
        char buf[512];
        gsize n = 0;
        GIOStatus st = g_io_channel_read_chars(ch, buf, sizeof(buf), &n, NULL);
        if (n > 0) {
            // Raw bytes: a read may end in the middle of a UTF-8 character,
            // so keep incomplete tails for the next read.
            GString *pending = g_stdout_pending;
            g_string_append_len(pending, buf, n);
            const char *valid_end;
            g_utf8_validate(pending->str, pending->len, &valid_end);
            gsize good = valid_end - pending->str;
            // If the invalid part is not a (possibly) incomplete tail, drop the bad byte
            if (pending->len - good >= 4) good++;
            char *chunk = g_strndup(pending->str, good);
            g_string_erase(pending, 0, good);
            const char *p = chunk;
            if (g_first_char) {
                while (*p == ' ' || *p == '\n' || *p == '\r') p++;
                if (*p) g_first_char = FALSE;
            }
            if (*p) {
                char *clean = g_utf8_make_valid(p, -1);
                append_chat(clean);
                g_free(clean);
                g_got_output = TRUE;
            }
            g_free(chunk);
        }
        if (st == G_IO_STATUS_NORMAL || st == G_IO_STATUS_AGAIN) return TRUE;
    }
    g_open_streams--;
    maybe_finish();
    return FALSE;
}

static gboolean on_child_stderr(GIOChannel *ch, GIOCondition cond, gpointer data) {
    (void)data;
    if (cond & G_IO_IN) {
        char buf[512];
        gsize n = 0;
        GIOStatus st = g_io_channel_read_chars(ch, buf, sizeof(buf), &n, NULL);
        if (n > 0 && g_child_stderr && g_child_stderr->len < 4096) g_string_append_len(g_child_stderr, buf, n);
        if (st == G_IO_STATUS_NORMAL || st == G_IO_STATUS_AGAIN) return TRUE;
    }
    g_open_streams--;
    maybe_finish();
    return FALSE;
}

static void on_child_exit(GPid pid, gint status, gpointer data) {
    (void)status; (void)data;
    g_spawn_close_pid(pid);
    g_child_exited = TRUE;
    maybe_finish();
}

static void watch_fd(int fd, GIOFunc fn) {
    GIOChannel *ch = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_buffered(ch, FALSE);
    g_io_channel_set_close_on_unref(ch, TRUE);
    g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, fn, NULL);
    g_io_channel_unref(ch);
}

static void start_inference(const char *prompt) {
    char *full = g_chat_context[0] ? g_strdup_printf("(Earlier: %s) %s", g_chat_context, prompt)
                                   : g_strdup(prompt);
    // Arguments go straight to argv: no shell, no quoting problems
    char *argv[] = { g_helper_path, "-m", g_model_path, "-c", NXP_CONTEXT_SIZE,
                     "-n", NXP_MAX_TOKENS, "-p", full, NULL };
    int out_fd, err_fd;
    GError *err = NULL;
    gboolean ok = g_spawn_async_with_pipes(g_exe_dir, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                                           NULL, NULL, &g_child_pid, NULL, &out_fd, &err_fd, &err);
    g_free(full);

    append_chat_line("NeuralXP:  ");
    if (!ok) {
        append_chat("Sorry, I couldn't start the inference engine.");
        gdk_display_beep(gdk_display_get_default());
        if (err) g_error_free(err);
        finish_inference("Error - inference engine not found");
        return;
    }
    g_first_char = TRUE;
    g_got_output = FALSE;
    g_child_exited = FALSE;
    g_open_streams = 2;
    g_child_stderr = g_string_new(NULL);
    if (!g_stdout_pending) g_stdout_pending = g_string_new(NULL);
    g_string_truncate(g_stdout_pending, 0);
    watch_fd(out_fd, on_child_stdout);
    watch_fd(err_fd, on_child_stderr);
    g_child_watch_add(g_child_pid, on_child_exit, NULL);
}

static void do_send(void) {
    if (g_generating) return;

    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(g_input_buf, &s, &e);
    char *input = gtk_text_buffer_get_text(g_input_buf, &s, &e, FALSE);
    char *start = g_strstrip(input);
    if (!*start) { g_free(input); return; }
    if (strlen(start) > 2000) {  // same cap as the Windows input box
        const char *end = start;
        while (*end && g_utf8_next_char(end) - start <= 2000) end = g_utf8_next_char(end);
        start[end - start] = '\0';
    }

    // First real message of an unnamed chat: ask for a name (cancel = still send)
    if (g_current_chat[0] == '\0') {
        char name[MAX_CHAT_NAME];
        if (show_name_dialog(name)) {
            g_strlcpy(g_current_chat, name, sizeof(g_current_chat));
            update_title();
            char status[128];
            snprintf(status, sizeof(status), "Chat: %s", g_current_chat);
            set_status(status);
        }
    }

    append_chat_line("You:  ");
    append_chat(start);
    gtk_text_buffer_set_text(g_input_buf, "", -1);
    g_message_count++;
    update_message_count();

    g_generating = TRUE;
    gtk_widget_set_sensitive(g_send_button, FALSE);
    gtk_widget_set_sensitive(g_input_view, FALSE);
    set_status("NeuralXP is thinking...");
    start_inference(start);
    g_free(input);
}

static void do_new_chat(void) {
    if (g_generating) { gdk_display_beep(gdk_display_get_default()); return; }
    auto_save_chat();
    g_current_chat[0] = '\0';
    g_chat_context[0] = '\0';
    gtk_text_buffer_set_text(g_chat_buf, "", -1);
    g_message_count = 0;
    update_message_count();
    append_chat(WELCOME_TEXT);
    update_title();
    set_status("Ready - New conversation started");
    gtk_widget_grab_focus(g_input_view);
}

static void do_open_chat(void) {
    if (g_generating) { gdk_display_beep(gdk_display_get_default()); return; }
    auto_save_chat();
    char *name = show_open_chat_dialog();
    if (!name) return;

    g_strlcpy(g_current_chat, name, sizeof(g_current_chat));
    g_free(name);
    char *history = load_chat_context(g_current_chat);

    gtk_text_buffer_set_text(g_chat_buf, "", -1);
    g_message_count = 0;
    update_message_count();
    append_chat("NeuralXP - Chat: ");
    append_chat(g_current_chat);
    append_chat("\n____________________________________________________________\n");
    if (history && history[0]) {
        append_chat(history);
        append_chat("____________________________________________________________");
    } else {
        append_chat("(empty chat - start typing!)");
    }
    g_free(history);

    update_title();
    char status[128];
    snprintf(status, sizeof(status), "Chat: %s - Context loaded", g_current_chat);
    set_status(status);
    gtk_widget_grab_focus(g_input_view);
}

static void do_save_chat(void) {
    GtkWidget *d = gtk_file_chooser_dialog_new("Save Chat", GTK_WINDOW(g_window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "NeuralXP_Chat.txt");
    GtkFileFilter *ft = gtk_file_filter_new();
    gtk_file_filter_set_name(ft, "Text Files (*.txt)");
    gtk_file_filter_add_pattern(ft, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), ft);
    GtkFileFilter *fa = gtk_file_filter_new();
    gtk_file_filter_set_name(fa, "All Files");
    gtk_file_filter_add_pattern(fa, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), fa);

    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
        char *fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));
        char *text = get_chat_text();
        time_t now = time(NULL);
        char date[64];
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&now));
        char *out = g_strdup_printf("NeuralXP Chat Log\nSaved: %s\n========================================\n\n%s\n", date, text);
        GError *err = NULL;
        if (fn && g_file_set_contents(fn, out, -1, &err)) {
            set_status("Chat saved successfully");
        } else {
            message_box(GTK_MESSAGE_ERROR, "NeuralXP", err ? err->message : "Could not save the chat.");
            if (err) g_error_free(err);
        }
        g_free(out);
        g_free(text);
        g_free(fn);
    }
    gtk_widget_destroy(d);
}

static void do_about(void) {
    message_box(GTK_MESSAGE_INFO, "About NeuralXP",
        "NeuralXP v" NXP_VERSION "\n"
        "Offline AI Assistant\n"
        "\n"
        "Built by Panmox\n"
        "https://panmox.org\n"
        "\n"
        "Powered by Qwen2.5-0.5B via llama.cpp\n"
        "Runs 100% offline - no internet needed\n"
        "\n"
        "System Requirements:\n"
        "  GNU/Linux (x86_64 or arm64) with GTK 3\n"
        "  512 MB RAM minimum\n"
        "  500 MB disk space");
}

static void do_tips(void) {
    message_box(GTK_MESSAGE_INFO, "NeuralXP Tips",
        "Tips for best results:\n"
        "\n"
        "- Keep questions short and specific\n"
        "- Ask for brief answers\n"
        "- For code: \"write a short [language] function that...\"\n"
        "- For facts: just ask directly\n"
        "- Press Enter to send, Shift+Enter for new line\n"
        "\n"
        "Chat System:\n"
        "- First message asks for chat name\n"
        "- Chats auto-save when you switch or exit\n"
        "- File > Open Chat to load a saved chat\n"
        "\n"
        "NeuralXP runs on your CPU, so responses\n"
        "can take a few seconds. Be patient!");
}

// ---- Model lookup and first-run download ----

static gboolean find_model(void) {
    const char *env = g_getenv("NEURALXP_MODEL");
    if (env && g_file_test(env, G_FILE_TEST_IS_REGULAR)) { g_model_path = g_strdup(env); return TRUE; }

    const char *names[] = { MODEL_MAIN, MODEL_SMALL };
    char *dirs[3];
    dirs[0] = g_build_filename(g_exe_dir, "..", "models", NULL);  // /opt/neuralxp/models, AppImage
    dirs[1] = g_build_filename(g_exe_dir, "models", NULL);        // portable layout (like Windows)
    dirs[2] = g_build_filename(g_data_dir, "models", NULL);       // downloaded by the lite build
    gboolean found = FALSE;
    for (int n = 0; n < 2 && !found; n++) {
        for (int d = 0; d < 3 && !found; d++) {
            char *p = g_build_filename(dirs[d], names[n], NULL);
            if (g_file_test(p, G_FILE_TEST_IS_REGULAR)) { g_model_path = p; found = TRUE; }
            else g_free(p);
        }
    }
    for (int d = 0; d < 3; d++) g_free(dirs[d]);
    return found;
}

typedef struct {
    GtkWidget *dialog, *bar, *label;
    GPid pid;
    char *part_path, *final_path;
    gboolean done, ok, cancelled;
    guint timer;
    GMainLoop *loop;
    char error[256];
} Download;

static gpointer verify_thread(gpointer data) {
    Download *dl = data;
    GChecksum *cs = g_checksum_new(G_CHECKSUM_SHA256);
    FILE *f = fopen(dl->part_path, "rb");
    gboolean ok = FALSE;
    if (f) {
        guchar buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) g_checksum_update(cs, buf, n);
        fclose(f);
        ok = strcmp(g_checksum_get_string(cs), MODEL_SHA256) == 0;
    }
    g_checksum_free(cs);
    return GINT_TO_POINTER(ok);
}

static gboolean dl_tick(gpointer data) {
    Download *dl = data;
    GStatBuf st;
    goffset sz = (g_stat(dl->part_path, &st) == 0) ? st.st_size : 0;
    double frac = (double)sz / (double)MODEL_SIZE;
    if (frac > 1) frac = 1;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dl->bar), frac);
    char txt[96];
    snprintf(txt, sizeof(txt), "%.0f MB of %.0f MB", sz / 1048576.0, MODEL_SIZE / 1048576.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dl->bar), txt);
    return TRUE;
}

static void dl_exit(GPid pid, gint status, gpointer data) {
    Download *dl = data;
    g_spawn_close_pid(pid);
    dl->pid = 0;
    if (dl->cancelled) { g_main_loop_quit(dl->loop); return; }
    if (!g_spawn_check_wait_status(status, NULL)) {
        snprintf(dl->error, sizeof(dl->error), "The download failed. Check your internet connection and try again.");
        g_main_loop_quit(dl->loop);
        return;
    }
    gtk_label_set_text(GTK_LABEL(dl->label), "Verifying download...");
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(dl->bar));
    GThread *t = g_thread_new("verify", verify_thread, dl);
    while (gtk_events_pending()) gtk_main_iteration();
    dl->ok = GPOINTER_TO_INT(g_thread_join(t));
    if (dl->ok) {
        dl->ok = g_rename(dl->part_path, dl->final_path) == 0;
        if (!dl->ok) snprintf(dl->error, sizeof(dl->error), "Could not save the model file.");
    } else {
        g_unlink(dl->part_path);
        snprintf(dl->error, sizeof(dl->error), "The downloaded file is damaged (checksum mismatch). Please try again.");
    }
    g_main_loop_quit(dl->loop);
}

static void dl_response(GtkDialog *d, gint r, gpointer data) {
    (void)d; (void)r;
    Download *dl = data;
    if (dl->pid) { dl->cancelled = TRUE; kill(dl->pid, SIGTERM); }
    else g_main_loop_quit(dl->loop);
}

// Lite build: the model is not bundled, offer to download it once.
static gboolean download_model(void) {
    char *mdir = g_build_filename(g_data_dir, "models", NULL);
    g_mkdir_with_parents(mdir, 0755);
    Download dl = {0};
    dl.final_path = g_build_filename(mdir, MODEL_MAIN, NULL);
    dl.part_path = g_strconcat(dl.final_path, ".part", NULL);
    g_free(mdir);

    char *curl = g_find_program_in_path("curl");
    char *wget = curl ? NULL : g_find_program_in_path("wget");
    if (!curl && !wget) {
        message_box(GTK_MESSAGE_ERROR, "NeuralXP",
            "To download the AI model, please install 'curl' or 'wget', or download\n" MODEL_URL
            "\nand save it as ~/.local/share/neuralxp/models/" MODEL_MAIN);
        g_free(dl.final_path); g_free(dl.part_path);
        return FALSE;
    }
    char *argv_curl[] = { curl, "-fL", "-sS", "--retry", "3", "-C", "-", "-o", dl.part_path, MODEL_URL, NULL };
    char *argv_wget[] = { wget, "-q", "-c", "-O", dl.part_path, MODEL_URL, NULL };

    dl.dialog = gtk_dialog_new_with_buttons("NeuralXP - Downloading model", NULL, GTK_DIALOG_MODAL,
                                            "Cancel", GTK_RESPONSE_CANCEL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dl.dialog), 380, -1);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dl.dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);
    dl.label = gtk_label_new("Downloading the NeuralXP AI model (Qwen2.5-0.5B, 396 MB).\nThis only happens once.");
    dl.bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(dl.bar), TRUE);
    gtk_box_pack_start(GTK_BOX(box), dl.label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), dl.bar, FALSE, FALSE, 0);
    g_signal_connect(dl.dialog, "response", G_CALLBACK(dl_response), &dl);
    g_signal_connect(dl.dialog, "delete-event", G_CALLBACK(gtk_true), NULL);
    gtk_widget_show_all(dl.dialog);

    GError *err = NULL;
    if (!g_spawn_async(NULL, curl ? argv_curl : argv_wget, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, &dl.pid, &err)) {
        snprintf(dl.error, sizeof(dl.error), "Could not start the download: %s", err->message);
        g_error_free(err);
    } else {
        dl.loop = g_main_loop_new(NULL, FALSE);
        g_child_watch_add(dl.pid, dl_exit, &dl);
        dl.timer = g_timeout_add(300, dl_tick, &dl);
        g_main_loop_run(dl.loop);
        g_source_remove(dl.timer);
        g_main_loop_unref(dl.loop);
    }
    gtk_widget_destroy(dl.dialog);
    if (!dl.ok && !dl.cancelled && dl.error[0]) message_box(GTK_MESSAGE_ERROR, "NeuralXP", dl.error);
    if (dl.ok) g_model_path = g_strdup(dl.final_path);
    g_free(curl); g_free(wget);
    g_free(dl.final_path); g_free(dl.part_path);
    return dl.ok;
}

// ---- Window ----

static gboolean on_header_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    int width = gtk_widget_get_allocated_width(w);
    int h = gtk_widget_get_allocated_height(w);
    // XP-style gradient (same colors as the Win32 DrawHeader)
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgb(pat, 0, 0 / 255.0, 88 / 255.0, 208 / 255.0);
    cairo_pattern_add_color_stop_rgb(pat, 1, 4 / 255.0, 48 / 255.0, 136 / 255.0);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);

    PangoLayout *lay = gtk_widget_create_pango_layout(w, NULL);
    PangoFontDescription *fd = pango_font_description_from_string("Trebuchet MS, Tahoma, DejaVu Sans, Sans Bold 13");
    pango_layout_set_font_description(lay, fd);
    pango_layout_set_text(lay, "NeuralXP", -1);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, 16, 7);
    pango_cairo_show_layout(cr, lay);
    pango_font_description_free(fd);

    fd = pango_font_description_from_string("Tahoma, Verdana, DejaVu Sans, Sans 8");
    pango_layout_set_font_description(lay, fd);
    pango_layout_set_text(lay, "Offline AI  ·  panmox.org", -1);
    cairo_set_source_rgb(cr, 200 / 255.0, 220 / 255.0, 1);
    cairo_move_to(cr, 16, 31);
    pango_cairo_show_layout(cr, lay);
    pango_font_description_free(fd);
    g_object_unref(lay);

    cairo_set_source_rgb(cr, 0, 40 / 255.0, 120 / 255.0);
    cairo_rectangle(cr, 0, h - 1, width, 1);
    cairo_fill(cr);
    return TRUE;
}

static gboolean on_input_key(GtkWidget *w, GdkEventKey *ev, gpointer data) {
    (void)w; (void)data;
    if ((ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) &&
        !(ev->state & GDK_SHIFT_MASK)) {
        do_send();
        return TRUE;
    }
    return FALSE;
}

static void kill_child(void) {
    if (g_child_pid) kill(g_child_pid, SIGTERM);
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer data) {
    (void)w; (void)e; (void)data;
    if (g_generating) {
        gdk_display_beep(gdk_display_get_default());
        if (!ask_yes_no("NeuralXP", "NeuralXP is still thinking.\nClose anyway?")) return TRUE;
        kill_child();  // don't leave the helper running after exit
    }
    auto_save_chat();
    return FALSE;
}

static void on_menu_new(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_new_chat(); }
static void on_menu_open(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_open_chat(); }
static void on_menu_save(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_save_chat(); }
static void on_menu_exit(GtkMenuItem *m, gpointer d) {
    (void)m; (void)d;
    if (!on_delete(g_window, NULL, NULL)) gtk_widget_destroy(g_window);
}
static void on_menu_copy(GtkMenuItem *m, gpointer d) {
    (void)m; (void)d;
    gtk_text_buffer_copy_clipboard(g_chat_buf, gtk_widget_get_clipboard(g_chat_view, GDK_SELECTION_CLIPBOARD));
}
static void on_menu_selectall(GtkMenuItem *m, gpointer d) {
    (void)m; (void)d;
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(g_chat_buf, &s, &e);
    gtk_text_buffer_select_range(g_chat_buf, &s, &e);
}
static void on_menu_about(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_about(); }
static void on_menu_tips(GtkMenuItem *m, gpointer d) { (void)m; (void)d; do_tips(); }
static void on_send_clicked(GtkButton *b, gpointer d) { (void)b; (void)d; do_send(); }
static void on_clear_clicked(GtkButton *b, gpointer d) { (void)b; (void)d; do_new_chat(); }

static GtkWidget *add_item(GtkWidget *menu, const char *label, GCallback cb,
                           GtkAccelGroup *ag, guint key, GdkModifierType mods) {
    GtkWidget *it = gtk_menu_item_new_with_mnemonic(label);
    g_signal_connect(it, "activate", cb, NULL);
    if (key) gtk_widget_add_accelerator(it, "activate", ag, key, mods, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    return it;
}

static GtkWidget *create_menu(GtkAccelGroup *ag) {
    GtkWidget *bar = gtk_menu_bar_new();

    GtkWidget *file = gtk_menu_new();
    add_item(file, "_New Chat", G_CALLBACK(on_menu_new), ag, GDK_KEY_n, GDK_CONTROL_MASK);
    add_item(file, "_Open Chat...", G_CALLBACK(on_menu_open), ag, GDK_KEY_o, GDK_CONTROL_MASK);
    add_item(file, "_Save Chat...", G_CALLBACK(on_menu_save), ag, GDK_KEY_s, GDK_CONTROL_MASK);
    gtk_menu_shell_append(GTK_MENU_SHELL(file), gtk_separator_menu_item_new());
    add_item(file, "E_xit", G_CALLBACK(on_menu_exit), ag, GDK_KEY_q, GDK_CONTROL_MASK);

    GtkWidget *edit = gtk_menu_new();
    // Ctrl+C / Ctrl+A are handled by the focused text view itself; shown here for discoverability
    add_item(edit, "_Copy", G_CALLBACK(on_menu_copy), ag, 0, 0);
    add_item(edit, "Select _All", G_CALLBACK(on_menu_selectall), ag, 0, 0);

    GtkWidget *help = gtk_menu_new();
    add_item(help, "_Tips for Best Results", G_CALLBACK(on_menu_tips), ag, 0, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(help), gtk_separator_menu_item_new());
    add_item(help, "_About NeuralXP...", G_CALLBACK(on_menu_about), ag, 0, 0);

    const char *titles[] = { "_File", "_Edit", "_Help" };
    GtkWidget *menus[] = { file, edit, help };
    for (int i = 0; i < 3; i++) {
        GtkWidget *top = gtk_menu_item_new_with_mnemonic(titles[i]);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), menus[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(bar), top);
    }
    return bar;
}

// XP Luna look (colors from the Win32 build), independent of the desktop theme
static const char *XP_CSS =
    "window, dialog, .nxp-bg { background-color: #ECE9D8; color: #000000; }"
    "* { font-family: Tahoma, Verdana, 'DejaVu Sans', sans-serif; font-size: 8pt; }"
    "menubar { background-color: #ECE9D8; border-bottom: 1px solid #ACA899; padding: 0; }"
    "menubar > menuitem { padding: 2px 7px; color: #000; }"
    "menubar > menuitem:hover { background-color: #316AC5; color: #FFFFFF; }"
    "menu, .menu { background-color: #FFFFFF; border: 1px solid #ACA899; padding: 2px; }"
    "menu menuitem { padding: 3px 18px; color: #000; }"
    "menu menuitem:hover { background-color: #316AC5; color: #FFFFFF; }"
    "menu menuitem accelerator { color: #707070; }"
    "menu menuitem:hover accelerator { color: #FFFFFF; }"
    "button { background-image: linear-gradient(to bottom, #FFFFFF, #ECEBE6 85%, #D6D0C5);"
    "  border: 1px solid #003C74; border-radius: 3px; padding: 2px 8px; color: #000; box-shadow: none; text-shadow: none; }"
    "button:hover { box-shadow: inset 0 0 0 2px #F8B330; }"
    "button:active { background-image: linear-gradient(to bottom, #E5E4DE, #E3E2DA); }"
    "button:disabled { color: #ACA899; border-color: #C9C7BA; }"
    "button:disabled label { color: #ACA899; }"
    "button:focus { box-shadow: inset 0 0 0 1px #98B8EA; }"
    "#send-button { font-weight: bold; }"
    "textview, textview text { background-color: #FFFFFF; color: #000000; }"
    "textview text selection, entry selection, treeview:selected { background-color: #316AC5; color: #FFFFFF; }"
    "#chat-view, #chat-view text { font-family: 'Lucida Console', 'DejaVu Sans Mono', monospace; font-size: 9pt; }"
    "#input-view, #input-view text { font-size: 9pt; }"
    "scrolledwindow.nxp-box { border: 1px solid #7F9DB9; background-color: #FFFFFF; }"
    "scrollbar { background-color: #F3F1EA; }"
    "scrollbar slider { background-color: #C1D3FB; border: 1px solid #94A6CA; border-radius: 2px; min-width: 12px; min-height: 12px; }"
    "entry { background-color: #FFFFFF; color: #000; border: 1px solid #7F9DB9; border-radius: 0; box-shadow: none; }"
    "treeview { background-color: #FFFFFF; color: #000; }"
    ".statusbar { background-color: #ECE9D8; border-top: 1px solid #ACA899; padding: 2px 4px; }"
    ".statusbar separator { background-color: #ACA899; min-width: 1px; }"
    "progressbar trough { background-color: #FFFFFF; border: 1px solid #7F9DB9; min-height: 14px; }"
    "progressbar progress { background-image: linear-gradient(to bottom, #8BE28B, #2EB82E); min-height: 12px; }";

static void apply_xp_theme(void) {
    // Force a light base theme so the XP palette is never mixed with a dark desktop theme
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", FALSE, NULL);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, XP_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

static void build_window(GtkApplication *app) {
    g_window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(g_window), WIN_WIDTH, WIN_HEIGHT);
    GdkGeometry geo = { .min_width = 420, .min_height = 380 };
    gtk_window_set_geometry_hints(GTK_WINDOW(g_window), NULL, &geo, GDK_HINT_MIN_SIZE);
    gtk_window_set_position(GTK_WINDOW(g_window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(g_window), "neuralxp");
    g_signal_connect(g_window, "delete-event", G_CALLBACK(on_delete), NULL);

    GtkAccelGroup *ag = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(g_window), ag);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_window), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), create_menu(ag), FALSE, FALSE, 0);

    GtkWidget *header = gtk_drawing_area_new();
    gtk_widget_set_size_request(header, -1, 54);
    g_signal_connect(header, "draw", G_CALLBACK(on_header_draw), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(body), 12);
    gtk_widget_set_margin_top(body, 0);
    gtk_box_pack_start(GTK_BOX(vbox), body, TRUE, TRUE, 0);

    // Chat history
    g_chat_view = gtk_text_view_new();
    gtk_widget_set_name(g_chat_view, "chat-view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_chat_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_chat_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(g_chat_view), 4);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(g_chat_view), 4);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(g_chat_view), 2);
    g_chat_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_chat_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(g_chat_buf, &end);
    gtk_text_buffer_create_mark(g_chat_buf, "end", &end, FALSE);
    GtkWidget *chat_sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(chat_sw), "nxp-box");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_sw), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(chat_sw), g_chat_view);
    gtk_widget_set_margin_top(chat_sw, 4);
    gtk_box_pack_start(GTK_BOX(body), chat_sw, TRUE, TRUE, 0);

    GtkWidget *lbl = gtk_label_new("  Type your message:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_top(lbl, 4);
    gtk_box_pack_start(GTK_BOX(body), lbl, FALSE, FALSE, 0);

    // Input row: input box + Send / New Chat buttons
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    g_input_view = gtk_text_view_new();
    gtk_widget_set_name(g_input_view, "input-view");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_input_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(g_input_view), 3);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(g_input_view), 2);
    g_input_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_input_view));
    g_signal_connect(g_input_view, "key-press-event", G_CALLBACK(on_input_key), NULL);
    GtkWidget *in_sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(in_sw), "nxp-box");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(in_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(in_sw, -1, 54);
    gtk_container_add(GTK_CONTAINER(in_sw), g_input_view);
    gtk_box_pack_start(GTK_BOX(row), in_sw, TRUE, TRUE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    g_send_button = gtk_button_new_with_label("Send");
    gtk_widget_set_name(g_send_button, "send-button");
    gtk_widget_set_size_request(g_send_button, 72, 30);
    g_signal_connect(g_send_button, "clicked", G_CALLBACK(on_send_clicked), NULL);
    g_clear_button = gtk_button_new_with_label("New Chat");
    gtk_widget_set_size_request(g_clear_button, 72, 22);
    g_signal_connect(g_clear_button, "clicked", G_CALLBACK(on_clear_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(btns), g_send_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btns), g_clear_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), btns, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), row, FALSE, FALSE, 0);

    // Status bar with two parts
    GtkWidget *sb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(sb), "statusbar");
    g_status_main = gtk_label_new("Ready");
    gtk_widget_set_halign(g_status_main, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(g_status_main), PANGO_ELLIPSIZE_END);
    g_status_count = gtk_label_new("");
    gtk_widget_set_size_request(g_status_count, 140, -1);
    gtk_widget_set_halign(g_status_count, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(sb), g_status_main, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(sb), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sb), g_status_count, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), sb, FALSE, FALSE, 0);

    append_chat(WELCOME_TEXT);
    update_title();
    set_status("Ready");
    update_message_count();
    gtk_widget_show_all(g_window);
    gtk_widget_grab_focus(g_input_view);
}

static void on_activate(GtkApplication *app, gpointer data) {
    (void)data;
    // Single instance: a second launch just raises the existing window
    if (g_window) { gtk_window_present(GTK_WINDOW(g_window)); return; }
    apply_xp_theme();  // before any dialog, so first-run dialogs get the XP look too

    if (!g_file_test(g_helper_path, G_FILE_TEST_IS_EXECUTABLE)) {
        GtkWidget *d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "NeuralXP could not find its inference engine:\n%s\n\nPlease reinstall NeuralXP.", g_helper_path);
        gtk_window_set_title(GTK_WINDOW(d), "NeuralXP");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        return;
    }

    if (!find_model()) {
        GtkWidget *d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "NeuralXP could not find the AI model.\n\n"
            "Download it now? (Qwen2.5-0.5B, 396 MB, from huggingface.co)\n"
            "This is needed only once - after that NeuralXP runs fully offline.");
        gtk_window_set_title(GTK_WINDOW(d), "NeuralXP - Model Not Found");
        gboolean yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
        gtk_widget_destroy(d);
        if (!yes || !download_model()) return;
    }
    build_window(app);
}

int main(int argc, char **argv) {
    // Directory of the real executable (works for /opt/neuralxp/bin and inside an AppImage)
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    g_exe_dir = exe ? g_path_get_dirname(exe) : g_get_current_dir();
    g_free(exe);
    const char *helper_env = g_getenv("NEURALXP_HELPER");
    g_helper_path = helper_env ? g_strdup(helper_env)
                               : g_build_filename(g_exe_dir, "llama-simple-chat-headless", NULL);

    g_data_dir = g_build_filename(g_get_user_data_dir(), "neuralxp", NULL);
    g_chats_dir = g_build_filename(g_data_dir, "chats", NULL);
    g_mkdir_with_parents(g_chats_dir, 0700);

    g_set_prgname("neuralxp");  // matches the .desktop file (taskbar icon / grouping)
    g_set_application_name("NeuralXP");
    // (GApplicationFlags)0 instead of G_APPLICATION_DEFAULT_FLAGS: that needs GLib 2.74, we target 2.72 (Ubuntu 22.04)
    GtkApplication *app = gtk_application_new("org.panmox.NeuralXP", (GApplicationFlags)0);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    kill_child();
    g_object_unref(app);
    return status;
}
