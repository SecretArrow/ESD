/* Eclipse SSH - terminal page: GTK4 widget hosting a libvterm-backed
 * terminal bound to a live SSH session.
 *
 * Rendering: GtkDrawingArea draw-func with a single reusable PangoLayout;
 * monospace metrics measured once per font change; per-cell glyph painting
 * with damage-limited redraws (only when ec_term_render reports changes).
 * Input: GtkEventControllerKey + IM context; mouse via gestures; scroll via
 * GtkEventControllerScroll + GtkAdjustment-driven scrollback viewport.
 */
#ifndef ECLIPSE_TERM_PAGE_H
#define ECLIPSE_TERM_PAGE_H

#include "eclipse/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

struct EcTermPage {
    EcApp* app;
    EcSession profile;      /* owned copy */
    EcLiveSession* live;
    EcTerm* term;
    /* widgets */
    GtkWidget* root;        /* outer box */
    GtkWidget* container;   /* notebook child hosting this pane's split group */
    GtkWidget* draw;        /* GtkDrawingArea */
    GtkWidget* scrollbar;
    GtkWidget* label;       /* tab label text */
    /* rendering state */
    PangoLayout* layout;
    PangoFontDescription* font;
    double cell_w, cell_h;
    int cols, rows;
    EcTermCell* cells;
    /* selection state */
    bool selecting;
    int sel_anchor_row, sel_anchor_col, sel_end_row, sel_end_col;
    bool has_selection;
    /* mouse mode relay */
    bool term_mouse_mode;
    bool scroll_sync;       /* guard while the scrollbar mirrors the term */
    guint resize_pending;
    int pending_cols, pending_rows;
};

GtkWidget* ui_term_page_tab_label(EcTermPage* page);
void ui_term_page_paste_from_clipboard_pub(EcTermPage* page);
const char* ui_term_page_title(EcTermPage* page);
/* live theme / font re-apply (single page or every open page) */
void ui_term_page_apply_theme(EcTermPage* page);
void ui_term_page_apply_theme_all(EcApp* app);
void ui_term_page_apply_font(EcTermPage* page);
void ui_term_page_apply_font_all(EcApp* app);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_TERM_PAGE_H */
