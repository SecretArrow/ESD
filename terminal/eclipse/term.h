/* Eclipse SSH - terminal engine core: libvterm state machine + host-managed
 * scrollback ring (sb_pushline/sb_popline callbacks), damage tracking and a
 * plain cell model the UI renderer reads. No GTK here - testable in isolation.
 */
#ifndef ECLIPSE_TERM_H
#define ECLIPSE_TERM_H

#include "eclipse/core.h"
#include <vterm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ch;      /* unicode codepoint (0 = blank) */
    uint8_t fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;
    bool bold, italic, underline, reverse, cursor;
} EcTermCell;

typedef struct EcTerm EcTerm;

/* Output produced by the terminal (keyboard input, responses) that must be
 * written to the SSH channel. */
typedef void (*EcTermWriteCb)(const char* data, size_t len, void* user);
/* Bell, title change, clipboard write (OSC 52 style request). */
typedef void (*EcTermEventCb)(int event, const char* text, void* user);

struct EcTerm {
    VTerm* vt;
    VTermState* st;
    VTermScreen* vs;
    int cols, rows;
    EcTermWriteCb write_cb;
    void* write_user;
    EcTermEventCb event_cb;
    void* event_user;
    /* scrollback ring (host-managed per libvterm contract) */
    EcTermCell** sb;         /* sb[start] is oldest */
    int sb_cap;
    int sb_len;
    int sb_start;
    int sb_offset;           /* view offset (0 = live) */
    /* current screen snapshot cache */
    EcTermCell* grid;
    bool resized;
    bool bell;
    bool dirty;
    bool cursor_visible;
    bool bracketed_paste;    /* last known mode */
    uint8_t palette[16][3];
    uint8_t def_fg[3];       /* theme default fg/bg fed into libvterm state */
    uint8_t def_bg[3];
    EcStr pending_title;     /* OSC title fragment accumulator */
};

EcTerm* ec_term_new(int cols, int rows, int scrollback);
void ec_term_free(EcTerm* t);
void ec_term_set_write_cb(EcTerm* t, EcTermWriteCb cb, void* user);
void ec_term_set_event_cb(EcTerm* t, EcTermEventCb cb, void* user);
/* feed remote output; returns number of bytes consumed (may queue internally) */
void ec_term_input(EcTerm* t, const char* data, size_t len);
/* user keyboard input -> bytes to send through channel */
void ec_term_key(EcTerm* t, const char* bytes, size_t len);
void ec_term_paste(EcTerm* t, const char* text, size_t len);
void ec_term_resize(EcTerm* t, int cols, int rows);
/* apply a theme palette (16 ANSI entries) to the emulator: libvterm resolves
 * indexed SGR colors through it; also mirrors into the readable snapshot */
void ec_term_set_palette(EcTerm* t, const uint8_t pal[16][3]);
/* theme default fg/bg (SGR 39/49): fed to libvterm state so default cells
 * render with theme colors even after convert_color_to_rgb */
void ec_term_set_default_colors(EcTerm* t, const uint8_t fg[3], const uint8_t bg[3]);
/* scroll the view; positive = into history */
void ec_term_scroll(EcTerm* t, int delta);
void ec_term_scroll_to(EcTerm* t, int offset);
void ec_term_scroll_top(EcTerm* t);
void ec_term_scroll_bottom(EcTerm* t);
/* render current view into cells (grid of cols*rows); returns true if changed */
bool ec_term_render(EcTerm* t, EcTermCell* out_grid, int cols, int rows);
int ec_term_scrollback_len(const EcTerm* t);
int ec_term_view_offset(const EcTerm* t);
const uint8_t* ec_term_palette(const EcTerm* t); /* 16*3 bytes */

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_TERM_H */
