/* Eclipse SSH - split-pane terminal groups.
 *
 * Each notebook tab hosts a pane-group container (a plain GtkBox). A group
 * starts with a single EcTermPage; splitting the focused pane replaces that
 * pane's slot in the widget tree with a GtkPaned holding the old pane and a
 * freshly connected one, so pane layouts recurse to arbitrary trees.
 *
 * Bookkeeping lives on the container widget ("ec-pane-group" data), so a
 * notebook page removal frees every pane in the tab through one call.
 * Each pane owns its own EcLiveSession (a split opens a second connection
 * with the same profile), keeping the live<->term 1:1 binding intact.
 */
#include "eclipse/ui.h"
#include "eclipse/term_page.h"

typedef struct {
    EcApp* app;
    EcVec pages;             /* EcTermPage* (borrowed; owned via app->live) */
    EcTermPage* focused;     /* last pane that received a click */
} PaneGroup;

static PaneGroup* group_of(EcTermPage* page)
{
    if (!page || !page->container) return NULL;
    return g_object_get_data(G_OBJECT(page->container), "ec-pane-group");
}

static void group_free(void* p)
{
    PaneGroup* g = p;
    ec_vec_free(&g->pages);
    g_free(g);
}

static void group_remove_page(PaneGroup* g, EcTermPage* page)
{
    for (size_t i = 0; i < g->pages.len; i++) {
        if (g->pages.items[i] == page) {
            ec_vec_remove_at(&g->pages, i);
            return;
        }
    }
}

void ui_pane_group_attach(EcApp* app, GtkWidget* container, EcTermPage* page)
{
    PaneGroup* g = g_new0(PaneGroup, 1);
    g->app = app;
    ec_vec_init(&g->pages);
    ec_vec_push(&g->pages, page);
    page->container = container;
    g_object_set_data_full(G_OBJECT(container), "ec-pane-group", g, group_free);
}

void ui_pane_group_dispose(EcApp* app, GtkWidget* container)
{
    PaneGroup* g = g_object_get_data(G_OBJECT(container), "ec-pane-group");
    if (!g) return;
    for (size_t i = 0; i < g->pages.len; i++) {
        EcTermPage* p = g->pages.items[i];
        for (size_t j = 0; j < app->live.len; j++) {
            if (app->live.items[j] == p) {
                ec_vec_remove_at(&app->live, j);
                break;
            }
        }
        ui_term_page_free(p);
    }
    g->pages.len = 0;
    g->focused = NULL;
    g_object_set_data(G_OBJECT(container), "ec-pane-group", NULL);
}

/* Set a 50/50 divider once the paned has been allocated. */
static gboolean split_position_idle(gpointer user)
{
    GtkPaned* paned = GTK_PANED(user);
    bool vertical = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(paned), "ec-vertical")) != 0;
    int len = vertical ? gtk_widget_get_height(GTK_WIDGET(paned))
                       : gtk_widget_get_width(GTK_WIDGET(paned));
    if (len > 1)
        gtk_paned_set_position(paned, len / 2);
    return G_SOURCE_REMOVE;
}

void ui_split_pane(EcTermPage* page, bool vertical)
{
    if (!page || !page->container) return;
    EcApp* app = page->app;
    PaneGroup* grp = group_of(page);
    if (!grp) return;

    /* new pane = new live session with the same profile */
    EcTermPage* np = ui_term_page_new(app, &page->profile);
    if (!np) {
        ui_show_error(app, "Split", "Failed to open the split pane.");
        return;
    }
    np->container = page->container;
    ec_vec_push(&app->live, np);
    ec_vec_push(&grp->pages, np);

    GtkWidget* paned = gtk_paned_new(vertical ? GTK_ORIENTATION_VERTICAL
                                              : GTK_ORIENTATION_HORIZONTAL);
    g_object_set_data(G_OBJECT(paned), "ec-vertical", GINT_TO_POINTER(vertical ? 1 : 0));
    g_object_ref(page->root);
    GtkWidget* parent = gtk_widget_get_parent(page->root);
    if (GTK_IS_PANED(parent)) {
        GtkPaned* pp = GTK_PANED(parent);
        gboolean start_side = gtk_paned_get_start_child(pp) == page->root;
        if (start_side)
            gtk_paned_set_start_child(pp, NULL);
        else
            gtk_paned_set_end_child(pp, NULL);
        gtk_paned_set_start_child(GTK_PANED(paned), page->root);
        gtk_paned_set_end_child(GTK_PANED(paned), np->root);
        if (start_side)
            gtk_paned_set_start_child(pp, paned);
        else
            gtk_paned_set_end_child(pp, paned);
    } else {
        gtk_box_remove(GTK_BOX(parent), page->root);
        gtk_paned_set_start_child(GTK_PANED(paned), page->root);
        gtk_paned_set_end_child(GTK_PANED(paned), np->root);
        gtk_box_append(GTK_BOX(parent), paned);
    }
    g_object_unref(page->root);

    g_idle_add(split_position_idle, paned);
    grp->focused = np;
    ui_term_page_focus(np);
}

void ui_close_pane(EcTermPage* page)
{
    if (!page || !page->container) return;
    EcApp* app = page->app;
    PaneGroup* grp = group_of(page);
    if (!grp) return;

    GtkWidget* container = page->container;
    GtkWidget* root = page->root;

    g_object_ref(root);
    GtkWidget* parent = gtk_widget_get_parent(root);
    if (GTK_IS_PANED(parent)) {
        GtkPaned* pp = GTK_PANED(parent);
        if (gtk_paned_get_start_child(pp) == root)
            gtk_paned_set_start_child(pp, NULL);
        else
            gtk_paned_set_end_child(pp, NULL);
    } else if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), root);
    }

    group_remove_page(grp, page);
    for (size_t j = 0; j < app->live.len; j++) {
        if (app->live.items[j] == page) {
            ec_vec_remove_at(&app->live, j);
            break;
        }
    }

    bool was_last = grp->pages.len == 0;
    ui_term_page_free(page);
    g_object_unref(root); /* unparented above; finalizes with no other refs */

    if (was_last) {
        /* last pane gone -> close the whole tab (page-removed frees the group) */
        int idx = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), container);
        if (idx >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), idx);
        return;
    }
    grp->focused = grp->pages.items[0];
    ui_term_page_focus(grp->focused);
}

void ui_note_focus(EcTermPage* page)
{
    PaneGroup* grp = group_of(page);
    if (grp) grp->focused = page;
}

void ui_close_tab_for_page(EcTermPage* page)
{
    if (!page || !page->container) return;
    PaneGroup* grp = group_of(page);
    if (!grp) return;
    /* snapshot first: closing panes mutates grp->pages; the anchor page goes
     * last because closing the final pane removes the whole notebook tab */
    EcVec snapshot;
    ec_vec_init(&snapshot);
    for (size_t i = 0; i < grp->pages.len; i++)
        ec_vec_push(&snapshot, grp->pages.items[i]);
    for (size_t i = 0; i < snapshot.len; i++) {
        EcTermPage* p = snapshot.items[i];
        if (p != page) ui_close_pane(p);
    }
    ui_close_pane(page);
    ec_vec_free(&snapshot);
}

EcTermPage* ui_current_term_page(EcApp* app)
{
    int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (cur < 0) return NULL;
    GtkWidget* child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), cur);
    PaneGroup* grp = g_object_get_data(G_OBJECT(child), "ec-pane-group");
    if (!grp) return NULL;
    if (grp->focused) return grp->focused;
    return grp->pages.len ? grp->pages.items[0] : NULL;
}
