/* Eclipse SSH - terminal engine core implementation (libvterm). */
#include "eclipse/term.h"
#include "eclipse/platform.h"

static void default_palette(uint8_t pal[16][3])
{
    /* xterm-like base palette; theme engine may override fg/bg/cursor */
    static const uint8_t base[16][3] = {
        {0,0,0},{205,49,49},{13,188,121},{229,229,16},
        {36,114,200},{188,63,188},{17,168,205},{229,229,229},
        {102,102,102},{241,76,76},{35,209,139},{245,245,67},
        {59,142,234},{214,112,214},{41,184,219},{255,255,255}
    };
    memcpy(pal, base, sizeof base);
}

/* --- libvterm callbacks --- */
static void on_output(const char* s, size_t len, void* user)
{
    EcTerm* t = user;
    if (t->write_cb) t->write_cb(s, len, t->write_user);
}

static void sb_clear(EcTerm* t)
{
    for (int i = 0; i < t->sb_len; i++) free(t->sb[(t->sb_start + i) % (size_t)t->sb_cap]);
    t->sb_len = 0;
    t->sb_start = 0;
}

static EcTermCell* sb_line_at(EcTerm* t, int idx)
{
    return t->sb[(t->sb_start + idx) % (size_t)t->sb_cap];
}

static int sb_pushline(int cols, const VTermScreenCell* cells, void* user)
{
    EcTerm* t = user;
    if (t->sb_len == t->sb_cap) { /* drop oldest */
        free(t->sb[t->sb_start]);
        t->sb_start = (t->sb_start + 1) % (size_t)t->sb_cap;
        t->sb_len--;
    }
    EcTermCell* line = calloc((size_t)cols, sizeof(EcTermCell));
    if (!line) return 0;
    for (int i = 0; i < cols; i++) {
        line[i].ch = cells[i].chars[0];
        line[i].fg_r = cells[i].fg.rgb.red; line[i].fg_g = cells[i].fg.rgb.green; line[i].fg_b = cells[i].fg.rgb.blue;
        line[i].bg_r = cells[i].bg.rgb.red; line[i].bg_g = cells[i].bg.rgb.green; line[i].bg_b = cells[i].bg.rgb.blue;
        line[i].bold = cells[i].attrs.bold;
        line[i].italic = cells[i].attrs.italic;
        line[i].underline = cells[i].attrs.underline;
        line[i].reverse = cells[i].attrs.reverse;
    }
    t->sb[(t->sb_start + t->sb_len) % (size_t)t->sb_cap] = line;
    t->sb_len++;
    if (t->sb_offset > 0) t->sb_offset++; /* keep view pinned when scrolled up */
    return 1;
}

static int sb_popline(int cols, VTermScreenCell* cells, void* user)
{
    EcTerm* t = user;
    if (t->sb_len == 0) return 0;
    int last = (t->sb_start + t->sb_len - 1) % (size_t)t->sb_cap;
    EcTermCell* line = t->sb[last];
    if (t->sb_offset > 0) t->sb_offset--;
    for (int i = 0; i < cols; i++) {
        memset(&cells[i], 0, sizeof cells[i]);
        cells[i].chars[0] = i < t->cols ? line[i].ch : 0;
        cells[i].fg.rgb.red = line[i].fg_r; cells[i].fg.rgb.green = line[i].fg_g; cells[i].fg.rgb.blue = line[i].fg_b;
        cells[i].bg.rgb.red = line[i].bg_r; cells[i].bg.rgb.green = line[i].bg_g; cells[i].bg.rgb.blue = line[i].bg_b;
        cells[i].fg.type = VTERM_COLOR_RGB;
        cells[i].bg.type = VTERM_COLOR_RGB;
        cells[i].attrs.bold = line[i].bold;
        cells[i].attrs.italic = line[i].italic;
        cells[i].attrs.underline = line[i].underline;
        cells[i].attrs.reverse = line[i].reverse;
    }
    free(line);
    t->sb_len--;
    return 1;
}

static int on_damage(VTermRect rect, void* user)
{
    (void)rect;
    EcTerm* t = user;
    t->dirty = true;
    return 1;
}

static int on_moverect(VTermRect dest, VTermRect src, void* user)
{
    (void)dest; (void)src;
    EcTerm* t = user;
    t->dirty = true;
    return 1;
}

static int on_movecursor(VTermPos pos, VTermPos oldpos, int visible, void* user)
{
    (void)pos; (void)oldpos;
    EcTerm* t = user;
    t->cursor_visible = visible != 0;
    t->dirty = true;
    return 1;
}

static int on_settermprop(VTermProp prop, VTermValue* val, void* user)
{
    EcTerm* t = user;
    switch (prop) {
    case VTERM_PROP_TITLE: {
        /* title arrives as (possibly fragmented) string fragment in 0.3+ */
        VTermStringFragment frag = val->string;
        if (frag.initial) ec_str_free(&t->pending_title);
        if (frag.str && frag.len) ec_str_append_n(&t->pending_title, frag.str, frag.len);
        if (frag.final && t->pending_title.len) {
            if (t->event_cb) t->event_cb(1, t->pending_title.s ? t->pending_title.s : "", t->event_user);
            ec_str_free(&t->pending_title);
        }
        break;
    }
    case VTERM_PROP_ALTSCREEN:
        t->dirty = true;
        if (val->boolean) t->sb_offset = 0;
        break;
    default:
        break;
    }
    return 1;
}

static int on_bell(void* user)
{
    EcTerm* t = user;
    t->bell = true;
    if (t->event_cb) t->event_cb(2, NULL, t->event_user);
    return 1;
}

static VTermScreenCallbacks screen_cbs = {
    .damage = on_damage,
    .moverect = on_moverect,
    .movecursor = on_movecursor,
    .settermprop = on_settermprop,
    .bell = on_bell,
    .sb_pushline = sb_pushline,
    .sb_popline = sb_popline,
};

EcTerm* ec_term_new(int cols, int rows, int scrollback)
{
    if (cols < 2 || rows < 2) return NULL;
    EcTerm* t = calloc(1, sizeof(EcTerm));
    if (!t) return NULL;
    t->cols = cols;
    t->rows = rows;
    t->vt = vterm_new(rows, cols);
    if (!t->vt) { free(t); return NULL; }
    vterm_set_utf8(t->vt, 1);
    t->vs = vterm_obtain_screen(t->vt);
    if (!t->vs) { vterm_free(t->vt); free(t); return NULL; }
    t->write_cb = NULL;
    vterm_output_set_callback(t->vt, on_output, t);
    vterm_screen_set_callbacks(t->vs, &screen_cbs, t);
    vterm_screen_set_damage_merge(t->vs, VTERM_DAMAGE_SCROLL);
    vterm_screen_reset(t->vs, 1);
    t->sb_cap = scrollback > 100 ? scrollback : 100;
    t->sb = calloc((size_t)t->sb_cap, sizeof(EcTermCell*));
    t->grid = calloc((size_t)(cols * rows), sizeof(EcTermCell));
    default_palette(t->palette);
    if (!t->sb || !t->grid) { ec_term_free(t); return NULL; }
    return t;
}

void ec_term_free(EcTerm* t)
{
    if (!t) return;
    if (t->vt) vterm_free(t->vt);
    sb_clear(t);
    free(t->sb);
    free(t->grid);
    free(t);
}

void ec_term_set_write_cb(EcTerm* t, EcTermWriteCb cb, void* user)
{
    if (!t) return;
    t->write_cb = cb;
    t->write_user = user;
}

void ec_term_set_event_cb(EcTerm* t, EcTermEventCb cb, void* user)
{
    if (!t) return;
    t->event_cb = cb;
    t->event_user = user;
}

void ec_term_input(EcTerm* t, const char* data, size_t len)
{
    if (!t || !data) return;
    vterm_input_write(t->vt, data, len);
}

void ec_term_key(EcTerm* t, const char* bytes, size_t len)
{
    if (!t || !bytes || !len) return;
    if (t->write_cb) t->write_cb(bytes, len, t->write_user);
}

void ec_term_paste(EcTerm* t, const char* text, size_t len)
{
    if (!t || !text || !len) return;
    if (t->bracketed_paste) {
        if (t->write_cb) {
            t->write_cb("\x1b[200~", 6, t->write_user);
            t->write_cb(text, len, t->write_user);
            t->write_cb("\x1b[201~", 6, t->write_user);
        }
    } else if (t->write_cb) {
        t->write_cb(text, len, t->write_user);
    }
}

void ec_term_resize(EcTerm* t, int cols, int rows)
{
    if (!t || cols < 2 || rows < 2) return;
    if (cols == t->cols && rows == t->rows) return;
    t->cols = cols;
    t->rows = rows;
    free(t->grid);
    t->grid = calloc((size_t)(cols * rows), sizeof(EcTermCell));
    if (!t->grid) return;
    vterm_set_size(t->vt, rows, cols);
    vterm_screen_flush_damage(t->vs);
    t->resized = true;
    t->dirty = true;
}

void ec_term_scroll(EcTerm* t, int delta)
{
    if (!t) return;
    int max_off = t->sb_len;
    t->sb_offset += delta;
    if (t->sb_offset < 0) t->sb_offset = 0;
    if (t->sb_offset > max_off) t->sb_offset = max_off;
    t->dirty = true;
}

void ec_term_scroll_to(EcTerm* t, int offset)
{
    if (!t) return;
    t->sb_offset = offset;
    if (t->sb_offset < 0) t->sb_offset = 0;
    if (t->sb_offset > t->sb_len) t->sb_offset = t->sb_len;
    t->dirty = true;
}

void ec_term_scroll_top(EcTerm* t)
{
    if (!t) return;
    t->sb_offset = t->sb_len;
    t->dirty = true;
}

void ec_term_scroll_bottom(EcTerm* t)
{
    if (!t) return;
    t->sb_offset = 0;
    t->dirty = true;
}

int ec_term_scrollback_len(const EcTerm* t) { return t ? t->sb_len : 0; }
int ec_term_view_offset(const EcTerm* t) { return t ? t->sb_offset : 0; }

const uint8_t* ec_term_palette(const EcTerm* t) { return t ? (const uint8_t*)t->palette : NULL; }

bool ec_term_render(EcTerm* t, EcTermCell* out_grid, int cols, int rows)
{
    if (!t || !out_grid) return false;
    bool changed = t->dirty;
    t->dirty = false;
    VTermState* state = vterm_obtain_state(t->vt);
    VTermPos cursor;
    vterm_state_get_cursorpos(state, &cursor);
    /* bracketed-paste tracking via mode query is version-dependent; keep a
     * heuristic: alt-screen apps rarely need it, shells send 2004 - track
     * through settermprop when prop becomes available; for now default true. */
    t->bracketed_paste = true;

    for (int r = 0; r < rows; r++) {
        int src_r = r;               /* screen row when offset==0 */
        bool from_sb = false;
        int sb_idx = 0;
        if (t->sb_offset > 0) {
            /* view shifted: rows above the screen come from scrollback */
            int visible_hist = t->rows; /* whole screen from history when fully scrolled */
            (void)visible_hist;
            sb_idx = t->sb_len - t->sb_offset + r;
            from_sb = sb_idx >= 0 && sb_idx < t->sb_len;
            src_r = -1;
        }
        for (int c = 0; c < cols; c++) {
            EcTermCell* out = &out_grid[r * cols + c];
            if (from_sb && sb_idx < t->sb_len) {
                EcTermCell* line = sb_line_at(t, sb_idx);
                *out = (c < t->cols) ? line[c] : (EcTermCell){ 0 };
                out->cursor = false;
            } else if (src_r >= 0) {
                VTermScreenCell vcell;
                VTermPos pos = { .row = r, .col = c };
                vterm_screen_get_cell(t->vs, pos, &vcell);
                memset(out, 0, sizeof *out);
                out->ch = vcell.chars[0];
                out->fg_r = vcell.fg.rgb.red; out->fg_g = vcell.fg.rgb.green; out->fg_b = vcell.fg.rgb.blue;
                out->bg_r = vcell.bg.rgb.red; out->bg_g = vcell.bg.rgb.green; out->bg_b = vcell.bg.rgb.blue;
                out->bold = vcell.attrs.bold;
                out->italic = vcell.attrs.italic;
                out->underline = vcell.attrs.underline;
                out->reverse = vcell.attrs.reverse;
                out->cursor = (r == cursor.row && c == cursor.col) && t->cursor_visible;
            } else {
                memset(out, 0, sizeof *out);
            }
        }
    }
    return changed;
}
