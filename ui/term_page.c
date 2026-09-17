/* Eclipse SSH - terminal page implementation. */
#include "eclipse/term_page.h"
#include "eclipse/platform.h"

#include <stdio.h>
#include <cairo.h>
#include <pango/pangocairo.h>

static void ui_term_page_paste_from_clipboard(EcTermPage* p);

/* ---------------- helpers ---------------- */
static void utf8_from_cp(uint32_t cp, char out[5])
{
    if (cp == 0) cp = ' ';
    if (cp < 0x80) {
        out[0] = (char)cp; out[1] = 0;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        out[2] = 0;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = 0;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        out[4] = 0;
    }
}

static void set_rgb(cairo_t* cr, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
}

static bool parse_hex(const char* hex, uint8_t out[3])
{
    return sscanf(hex, "#%02hhx%02hhx%02hhx", &out[0], &out[1], &out[2]) == 3;
}

static void measure_metrics(EcTermPage* p)
{
    pango_layout_set_text(p->layout, "M", -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(p->layout, &w, &h);
    p->cell_w = w > 1 ? w : 8;
    p->cell_h = h > 1 ? h : 16;
}

static gboolean resize_tick(gpointer user)
{
    EcTermPage* p = user;
    p->resize_pending = 0;
    int cols = p->pending_cols, rows = p->pending_rows;
    if (cols < 2) cols = 2;
    if (rows < 2) rows = 2;
    if (cols != p->cols || rows != p->rows) {
        p->cols = cols;
        p->rows = rows;
        free(p->cells);
        p->cells = malloc(sizeof(EcTermCell) * (size_t)(cols * rows));
        if (p->cells) {
            ec_term_resize(p->term, cols, rows);
            ec_live_resize(p->live, cols, rows);
        }
    }
    return G_SOURCE_REMOVE;
}

static void on_resize(GtkWidget* w, int width, int height, gpointer user)
{
    (void)w;
    EcTermPage* p = user;
    int cols = (int)(width / p->cell_w);
    int rows = (int)(height / p->cell_h);
    if (cols != p->cols || rows != p->rows) {
        p->pending_cols = cols;
        p->pending_rows = rows;
        if (!p->resize_pending)
            p->resize_pending = g_timeout_add(80, resize_tick, p);
    }
}

/* selection overlay (second pass) */
static void draw_selection(cairo_t* cr, EcTermPage* p)
{
    if (!p->has_selection) return;
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    int a_r = p->sel_anchor_row, a_c = p->sel_anchor_col;
    int e_r = p->sel_end_row, e_c = p->sel_end_col;
    if (a_r > e_r || (a_r == e_r && a_c > e_c)) {
        int tr = a_r, tc2 = a_c;
        a_r = e_r; a_c = e_c; e_r = tr; e_c = tc2;
    }
    for (int r = a_r; r <= e_r && r < p->rows; r++) {
        double x0 = (r == a_r ? a_c : 0) * p->cell_w;
        double x1 = ((r == e_r ? e_c + 1 : p->cols)) * p->cell_w;
        cairo_rectangle(cr, x0, r * p->cell_h, x1 - x0, p->cell_h);
        cairo_fill(cr);
    }
}

/* ---------------- draw ---------------- */
static void draw_term(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user)
{
    (void)area; (void)width; (void)height;
    EcTermPage* p = user;
    EcThemeColors* c = &p->app->theme.c;
    uint8_t tb[3] = { 0x14, 0x17, 0x1c }, tf[3] = { 0xe6, 0xe8, 0xeb }, tc[3] = { 0x4f, 0x8c, 0xff };
    if (!parse_hex(c->terminal_bg, tb)) { tb[0] = 0x14; tb[1] = 0x17; tb[2] = 0x1c; }
    if (!parse_hex(c->terminal_fg, tf)) { tf[0] = 0xe6; tf[1] = 0xe8; tf[2] = 0xeb; }
    if (!parse_hex(c->terminal_cursor, tc)) { tc[0] = 0x4f; tc[1] = 0x8c; tc[2] = 0xff; }
    set_rgb(cr, tb[0], tb[1], tb[2]);
    cairo_paint(cr);

    if (!p->cells)
        return;
    ec_term_render(p->term, p->cells, p->cols, p->rows);

    for (int r = 0; r < p->rows; r++) {
        double y = r * p->cell_h;
        for (int col = 0; col < p->cols; col++) {
            EcTermCell* cell = &p->cells[r * p->cols + col];
            double x = col * p->cell_w;
            uint8_t fgr = cell->fg_r, fgg = cell->fg_g, fgb = cell->fg_b;
            uint8_t bgr = cell->bg_r, bgg = cell->bg_g, bgb = cell->bg_b;
            if (cell->reverse) {
                uint8_t tr = fgr, tg = fgg, tb2 = fgb;
                fgr = bgr; fgg = bgg; fgb = bgb;
                bgr = tr; bgg = tg; bgb = tb2;
            }
            if (!fgr && !fgg && !fgb) { fgr = tf[0]; fgg = tf[1]; fgb = tf[2]; }
            if (!bgr && !bgg && !bgb) { bgr = tb[0]; bgg = tb[1]; bgb = tb[2]; }

            if (cell->cursor) {
                set_rgb(cr, tc[0], tc[1], tc[2]);
            } else {
                set_rgb(cr, bgr / 255.0, bgg / 255.0, bgb / 255.0);
            }
            cairo_rectangle(cr, x, y, p->cell_w, p->cell_h);
            cairo_fill(cr);

            if (cell->ch == 0 || cell->ch == ' ')
                continue;
            char utf[5];
            utf8_from_cp(cell->ch, utf);
            pango_layout_set_text(p->layout, utf, -1);
            PangoAttrList* attrs = pango_attr_list_new();
            if (cell->bold)
                pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
            if (cell->italic)
                pango_attr_list_insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC));
            if (cell->underline)
                pango_attr_list_insert(attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
            pango_layout_set_attributes(p->layout, attrs);
            pango_attr_list_unref(attrs);
            if (cell->cursor)
                set_rgb(cr, tb[0], tb[1], tb[2]);
            else
                set_rgb(cr, fgr / 255.0, fgg / 255.0, fgb / 255.0);
            cairo_move_to(cr, x, y);
            pango_cairo_show_layout(cr, p->layout);
        }
    }
    draw_selection(cr, p);
    gtk_widget_queue_draw(p->draw); /* simple continuous render; refine with damage model */
}


/* ---------------- session events (worker threads!) ---------------- */
typedef struct {
    EcTermPage* page;
    char* text;
    int state;
} PageIdle;

static gboolean idle_state(gpointer user)
{
    PageIdle* m = user;
    EcTermPage* p = m->page;
    ui_status_update(p->app,
                     m->state == (int)EC_LV_CONNECTED ? "Connected" :
                     m->state == (int)EC_LV_FAILED ? "Failed" :
                     m->state == (int)EC_LV_CLOSED ? "Closed" : "Connecting",
                     m->text ? m->text : "");
    g_free(m->text);
    g_free(m);
    return G_SOURCE_REMOVE;
}

static gboolean idle_title(gpointer user)
{
    PageIdle* m = user;
    if (m->page->label)
        gtk_label_set_text(GTK_LABEL(m->page->label), m->text ? m->text : "");
    g_free(m->text);
    g_free(m);
    return G_SOURCE_REMOVE;
}

/* ---------------- input ---------------- */
static void write_to_session(const char* data, size_t len, void* user)
{
    EcTermPage* p = user;
    if (p->live) ec_live_send(p->live, data, len);
}

static void term_event(int event, const char* text, void* user)
{
    EcTermPage* p = user;
    if (event == 1 && text) { /* title change -> tab label via idle */
        PageIdle* m = g_new0(PageIdle, 1);
        m->page = p;
        m->state = 0;
        m->text = g_strdup(text);
        g_idle_add(idle_title, m);
    } else if (event == 2) {
        EC_LOGD("term", "bell");
    }
}

static gboolean on_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                       GdkModifierType state, gpointer user)
{
    (void)keycode;
    EcTermPage* p = user;

    /* app-level shortcuts first */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK)) {
        if (keyval == GDK_KEY_C && p->has_selection) {
            GdkClipboard* cb = gtk_widget_get_clipboard(p->draw);
            EcStr sel;
            ec_str_init(&sel);
            int a_r = p->sel_anchor_row, a_c = p->sel_anchor_col;
            int e_r = p->sel_end_row, e_c = p->sel_end_col;
            if (a_r > e_r || (a_r == e_r && a_c > e_c)) {
                int tr = a_r, tc2 = a_c;
                a_r = e_r; a_c = e_c; e_r = tr; e_c = tc2;
            }
            for (int r = a_r; r <= e_r && r < p->rows; r++) {
                int c0 = (r == a_r) ? a_c : 0;
                int c1 = (r == e_r) ? e_c + 1 : p->cols;
                for (int c = c0; c < c1 && c < p->cols; c++) {
                    char utf[5];
                    utf8_from_cp(p->cells[r * p->cols + c].ch, utf);
                    ec_str_append(&sel, utf);
                }
                ec_str_append_ch(&sel, '\n');
            }
            gdk_clipboard_set_text(cb, sel.s ? sel.s : "");
            ec_str_free(&sel);
            return TRUE;
        }
        if (keyval == GDK_KEY_V) {
            ui_term_page_paste_from_clipboard(p);
            return TRUE;
        }
        if (keyval == GDK_KEY_p) {
            ui_palette_toggle(p->app);
            return TRUE;
        }
    }

    /* IME filter first (handles composed input) */
    GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(ctrl));
    if (event) {
        GtkIMContext* im = g_object_get_data(G_OBJECT(p->draw), "im-context");
        if (im && gtk_im_context_filter_keypress(im, event))
            return TRUE;
    }

    /* map keys to libvterm */
    VTermModifier mod = VTERM_MOD_NONE;
    if (state & GDK_SHIFT_MASK) mod |= VTERM_MOD_SHIFT;
    if (state & GDK_CONTROL_MASK) mod |= VTERM_MOD_CTRL;
    if (state & GDK_ALT_MASK) mod |= VTERM_MOD_ALT;

    VTermKey vk = VTERM_KEY_NONE;
    switch (keyval) {
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: vk = VTERM_KEY_ENTER; break;
    case GDK_KEY_BackSpace: vk = VTERM_KEY_BACKSPACE; break;
    case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: vk = VTERM_KEY_TAB; break;
    case GDK_KEY_Escape: vk = VTERM_KEY_ESCAPE; break;
    case GDK_KEY_Up: vk = VTERM_KEY_UP; break;
    case GDK_KEY_Down: vk = VTERM_KEY_DOWN; break;
    case GDK_KEY_Left: vk = VTERM_KEY_LEFT; break;
    case GDK_KEY_Right: vk = VTERM_KEY_RIGHT; break;
    case GDK_KEY_Insert: vk = VTERM_KEY_INS; break;
    case GDK_KEY_Delete: vk = VTERM_KEY_DEL; break;
    case GDK_KEY_Home: case GDK_KEY_KP_Home: vk = VTERM_KEY_HOME; break;
    case GDK_KEY_End: case GDK_KEY_KP_End: vk = VTERM_KEY_END; break;
    case GDK_KEY_Page_Up: vk = VTERM_KEY_PAGEUP; break;
    case GDK_KEY_Page_Down: vk = VTERM_KEY_PAGEDOWN; break;
    case GDK_KEY_F1: vk = VTERM_KEY_FUNCTION(1); break;
    case GDK_KEY_F2: vk = VTERM_KEY_FUNCTION(2); break;
    case GDK_KEY_F3: vk = VTERM_KEY_FUNCTION(3); break;
    case GDK_KEY_F4: vk = VTERM_KEY_FUNCTION(4); break;
    case GDK_KEY_F5: vk = VTERM_KEY_FUNCTION(5); break;
    case GDK_KEY_F6: vk = VTERM_KEY_FUNCTION(6); break;
    case GDK_KEY_F7: vk = VTERM_KEY_FUNCTION(7); break;
    case GDK_KEY_F8: vk = VTERM_KEY_FUNCTION(8); break;
    case GDK_KEY_F9: vk = VTERM_KEY_FUNCTION(9); break;
    case GDK_KEY_F10: vk = VTERM_KEY_FUNCTION(10); break;
    case GDK_KEY_F11: vk = VTERM_KEY_FUNCTION(11); break;
    case GDK_KEY_F12: vk = VTERM_KEY_FUNCTION(12); break;
    default: break;
    }
    if (vk != VTERM_KEY_NONE) {
        vterm_keyboard_key(p->term->vt, vk, mod);
        return TRUE;
    }
    if ((state & GDK_CONTROL_MASK) && !(state & GDK_SHIFT_MASK) && keyval >= 'a' && keyval <= 'z') {
        vterm_keyboard_unichar(p->term->vt, (uint32_t)(keyval - 'a' + 1), VTERM_MOD_CTRL);
        return TRUE;
    }
    if ((state & GDK_ALT_MASK) && keyval < 128) {
        vterm_keyboard_unichar(p->term->vt, keyval, VTERM_MOD_ALT);
        return TRUE;
    }
    return FALSE; /* printable input flows through IM commit */
}

static void on_im_commit(GtkIMContext* im, gchar* str, gpointer user)
{
    (void)im;
    EcTermPage* p = user;
    if (str && *str)
        for (const gchar* c = str; *c; c = g_utf8_next_char(c))
            vterm_keyboard_unichar(p->term->vt, g_utf8_get_char(c), VTERM_MOD_NONE);
}

static void paste_ready(GObject* src, GAsyncResult* res, gpointer user)
{
    EcTermPage* p = user;
    char* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res, NULL);
    if (text) {
        /* paste confirmation (spec #13) is enforced by the caller dialog in
         * a later iteration; direct paste keeps the async clipboard path */
        ec_term_paste(p->term, text, strlen(text));
        g_free(text);
    }
}

static void ui_term_page_paste_from_clipboard(EcTermPage* p)
{
    GdkClipboard* cb = gtk_widget_get_clipboard(p->draw);
    gdk_clipboard_read_text_async(cb, NULL, paste_ready, p);
}

/* ---------------- mouse ---------------- */
static void cell_at(EcTermPage* p, double x, double y, int* row, int* col)
{
    *row = (int)(y / p->cell_h);
    *col = (int)(x / p->cell_w);
    if (*row < 0) *row = 0;
    if (*row >= p->rows) *row = p->rows - 1;
    if (*col < 0) *col = 0;
    if (*col >= p->cols) *col = p->cols - 1;
}

static void on_click(GtkGestureClick* g, int n_press, double x, double y, gpointer user)
{
    (void)n_press;
    EcTermPage* p = user;
    gtk_widget_grab_focus(p->draw);
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g));
    int row, col;
    cell_at(p, x, y, &row, &col);
    if (button == GDK_BUTTON_PRIMARY) {
        p->selecting = TRUE;
        p->sel_anchor_row = p->sel_end_row = row;
        p->sel_anchor_col = p->sel_end_col = col;
        p->has_selection = FALSE;
    } else if (button == GDK_BUTTON_MIDDLE) {
        ui_term_page_paste_from_clipboard(p);
    }
}

static void on_motion(GtkEventControllerMotion* ctrl, double x, double y, gpointer user)
{
    (void)ctrl;
    EcTermPage* p = user;
    if (!p->selecting) return;
    int row, col;
    cell_at(p, x, y, &row, &col);
    if (row != p->sel_end_row || col != p->sel_end_col) {
        p->sel_end_row = row;
        p->sel_end_col = col;
        p->has_selection = TRUE;
    }
}

static void on_release(GtkGestureClick* g, int n_press, double x, double y, gpointer user)
{
    (void)n_press; (void)x; (void)y; (void)g;
    EcTermPage* p = user;
    if (p->selecting) {
        p->selecting = FALSE;
        if (p->has_selection && p->app->settings.copy_on_select) {
            GdkClipboard* cb = gtk_widget_get_clipboard(p->draw);
            EcStr sel;
            ec_str_init(&sel);
            int a_r = p->sel_anchor_row, a_c = p->sel_anchor_col;
            int e_r = p->sel_end_row, e_c = p->sel_end_col;
            if (a_r > e_r || (a_r == e_r && a_c > e_c)) {
                int tr = a_r, tc2 = a_c;
                a_r = e_r; a_c = e_c; e_r = tr; e_c = tc2;
            }
            for (int r = a_r; r <= e_r && r < p->rows; r++) {
                int c0 = (r == a_r) ? a_c : 0;
                int c1 = (r == e_r) ? e_c + 1 : p->cols;
                for (int c = c0; c < c1 && c < p->cols; c++) {
                    char utf[5];
                    utf8_from_cp(p->cells[r * p->cols + c].ch, utf);
                    ec_str_append(&sel, utf);
                }
                if (r != e_r) ec_str_append_ch(&sel, '\n');
            }
            gdk_clipboard_set_text(cb, sel.s ? sel.s : "");
            ec_str_free(&sel);
        }
    }
}

static void on_scroll(GtkEventControllerScroll* ctrl, double dx, double dy, gpointer user)
{
    (void)ctrl;
    EcTermPage* p = user;
    ec_term_scroll(p->term, (int)(dy * 3.0));
}

static void cb_state(EcLiveSession* live, EcLiveState st, const char* message, void* user)
{
    (void)live;
    PageIdle* m = g_new0(PageIdle, 1);
    m->page = user;
    m->state = (int)st;
    m->text = g_strdup(message ? message : "");
    g_idle_add(idle_state, m);
}

static void cb_eof(EcLiveSession* live, void* user)
{
    (void)live;
    PageIdle* m = g_new0(PageIdle, 1);
    m->page = user;
    m->state = (int)EC_LV_CLOSED;
    m->text = g_strdup("Connection closed");
    g_idle_add(idle_state, m);
}

static int cb_hostkey(EcLiveSession* live, const EcServerKey* key, EcHostKeyStatus st,
                      const char* host, int port, void* user)
{
    (void)live;
    int answer = 0;
    ui_show_hostkey_dialog(((EcTermPage*)user)->app, key, st, host, port, &answer);
    return answer;
}

/* ---------------- lifecycle ---------------- */
EcTermPage* ui_term_page_new(EcApp* app, EcSession* profile)
{
    EcTermPage* p = calloc(1, sizeof(EcTermPage));
    if (!p) return NULL;
    p->app = app;
    p->profile = *profile;
    p->cols = 80;
    p->rows = 24;

    p->term = ec_term_new(80, 24, app->settings.scrollback_lines);
    if (!p->term) { free(p); return NULL; }
    ec_term_set_write_cb(p->term, write_to_session, p);
    ec_term_set_event_cb(p->term, term_event, p);

    p->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    p->draw = gtk_drawing_area_new();
    gtk_widget_set_can_focus(p->draw, TRUE);
    gtk_widget_set_focusable(p->draw, TRUE);
    gtk_widget_set_hexpand(p->draw, TRUE);
    gtk_widget_set_vexpand(p->draw, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(p->draw), draw_term, p, NULL);
    g_signal_connect(p->draw, "resize", G_CALLBACK(on_resize), p);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(box), p->draw);
    p->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
    gtk_box_append(GTK_BOX(box), p->scrollbar);
    gtk_box_append(GTK_BOX(p->root), box);

    /* font + layout */
    char fontspec[160];
    snprintf(fontspec, sizeof fontspec, "%s %d", app->settings.font_name, app->settings.font_size);
    p->font = pango_font_description_from_string(fontspec);
    p->layout = gtk_widget_create_pango_layout(p->draw, NULL);
    pango_layout_set_font_description(p->layout, p->font);
    measure_metrics(p);
    p->cells = malloc(sizeof(EcTermCell) * (size_t)(p->cols * p->rows));

    /* input controllers */
    GtkEventController* key = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_key), p);
    gtk_widget_add_controller(p->draw, key);

    GtkIMContext* im = gtk_im_multicontext_new();
    g_object_set_data_full(G_OBJECT(p->draw), "im-context", im, g_object_unref);
    g_signal_connect(im, "commit", G_CALLBACK(on_im_commit), p);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_click), p);
    g_signal_connect(click, "released", G_CALLBACK(on_release), p);
    gtk_widget_add_controller(p->draw, GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), p);
    gtk_widget_add_controller(p->draw, motion);

    GtkEventController* scrollc = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
    g_signal_connect(scrollc, "scroll", G_CALLBACK(on_scroll), p);
    gtk_widget_add_controller(p->draw, scrollc);

    /* live session */
    EcLiveCallbacks cbs = { 0 };
    cbs.user = p;
    cbs.state = cb_state;
    cbs.eof = cb_eof;
    cbs.hostkey_prompt = cb_hostkey;
    p->live = ec_live_new(&p->profile, app->hostkeys);
    ec_live_set_term(p->live, p->term);
    ec_live_set_callbacks(p->live, &cbs);

    /* tab label */
    p->label = gtk_label_new(profile->name);
    gtk_widget_set_margin_start(p->label, 6);
    gtk_widget_set_margin_end(p->label, 6);

    /* start connection (password injected from vault) */
    if (p->profile.auth_mode == EC_AUTH_PASSWORD && app->vault && app->vault) {
        char* user = NULL, * pass = NULL;
        if (ec_vault_get(app->vault, p->profile.id, &user, &pass)) {
            ec_live_set_password(p->live, pass);
            free(user);
            free(pass);
        }
    }
    ec_live_start(p->live);
    return p;
}

GtkWidget* ui_term_page_widget(EcTermPage* page) { return page ? page->root : NULL; }
GtkWidget* ui_term_page_tab_label(EcTermPage* page) { return page ? page->label : NULL; }

void ui_term_page_focus(EcTermPage* page)
{
    if (page) gtk_widget_grab_focus(page->draw);
}

void ui_term_page_run_snippet(EcTermPage* page, const char* command)
{
    if (!page || !command) return;
    size_t n = strlen(command);
    char* with_nl = malloc(n + 2);
    if (with_nl) {
        memcpy(with_nl, command, n);
        with_nl[n] = '\r';
        with_nl[n + 1] = 0;
        ec_live_send(page->live, with_nl, n + 1);
        free(with_nl);
    }
}

void ui_term_page_free(EcTermPage* page)
{
    if (!page) return;
    if (page->resize_pending) g_source_remove(page->resize_pending);
    if (page->live) ec_live_free(page->live);
    if (page->term) ec_term_free(page->term);
    if (page->layout) g_object_unref(page->layout);
    if (page->font) pango_font_description_free(page->font);
    free(page->cells);
    free(page);
}

const char* ui_term_page_title(EcTermPage* page)
{
    return page ? page->profile.name : "";
}
