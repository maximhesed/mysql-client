#include <gtk/gtk.h>
#include <mysql.h>
#include <termios.h>
#include <string.h>

#include "data.h"

#define BUTTON_RIGHT 0x3

/* TODO: It's a temporary solution, while I'm looking for a way to make an
 * independent size of a window, but so, that an its size isn't too large. */
#define WIN_DBS_X 650
#define WIN_DBS_Y 550
#define WIN_TBS_X 500
#define WIN_TBS_Y 600

enum {
    COLUMN,
    NUM_COLS
};

static void application_quit(GtkWidget *wdg, gpointer data);
static void con_assert(MYSQL *con);
static void con_close(GtkWidget *wdg, gpointer data);
static void con_open(GtkWidget *wdg, gpointer data);
static void con_warn(MYSQL *con);
static gboolean connect(MYSQL *con, const struct con_info *ci);
static GtkTreeModel * dbs_get(MYSQL *con);
static GtkWidget * dbs_view_create(MYSQL *con);
static void free_data(GtkWidget *wdg, gpointer data);
static void free_clist(GtkWidget *wdg, gpointer data);
static void item_info_cb(GtkWidget *wdg, gpointer data);
static void item_remove_cb(GtkWidget *wdg, gpointer data);
static void item_rename_cb(GtkWidget *wdg, gpointer data);
static void con_sel_cb(GtkWidget *wdg, gpointer data);
static GtkWidget * con_view_create(GtkListStore *store);
static gboolean smenu_cb(GtkWidget *wdg, GdkEventButton *ev, gpointer data);
static void smenu_view(gpointer data);
static void table_sel_cb(GtkWidget *wdg, gpointer data);
static void win_dbs(GtkApplication *app, MYSQL *con);
static void win_main(GtkApplication *app, gpointer data);
static void win_table(GtkApplication *app, MYSQL *con, const gchar *tb_name);

int main(int argc, char *argv[])
{
    GtkApplication *app;
    struct args_data *ad = g_malloc0(sizeof(struct args_data));
    GOptionEntry entries[] = {
        {
            "host",
            'h',
            0,
            G_OPTION_ARG_STRING,
            &ad->host,
            "Host",
            "HOST"
        },
        {
            "username",
            'u',
            0,
            G_OPTION_ARG_STRING,
            &ad->uname,
            "Username",
            "STR"
        },
        {
            "password",
            'p',
            0,
            G_OPTION_ARG_STRING,
            &ad->passw,
            "Password",
            "STR"
        },
        {
            NULL
        }
    };
    GError *error = NULL;
    GOptionContext *context;
    int status;

    /* Parse the user arguments. */
    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR, "%s", error->message);

        g_error_free(error);

        return -1;
    }

    /* Check a MySQL version. */
    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "A MySQL client version: %s.",
        mysql_get_client_info());

    /* Init the MySQL library. */
    if (mysql_library_init(0, NULL, NULL)) {
        g_log(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR,
            "Couldn't initialize the MySQL client library.");

        return -1;
    }

    /* Run the application. */
    app = gtk_application_new(NULL, G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(win_main), ad);
    g_signal_connect(app, "shutdown", G_CALLBACK(free_data), ad);
    status = g_application_run(G_APPLICATION(app), 0, NULL);

    g_object_unref(app);

    mysql_library_end();

    /* Flush an all input. */
    tcflush(STDOUT_FILENO, TCIFLUSH);

    return status;
}

static void
win_main(GtkApplication *app, gpointer data)
{
    GtkWidget *win;
    GtkWidget *label_host;
    GtkWidget *label_uname;
    GtkWidget *label_password;
    GtkWidget *btn_con_add;
    GtkWidget *btn_con_open;
    GtkWidget *grid;
    GtkWidget *view;
    GtkListStore *store;
    GtkTreeSelection *sel;
    struct wrap_data *wd = g_malloc(sizeof(struct wrap_data));
    struct con_data *cd = g_malloc0(sizeof(struct con_data));
    struct args_data *ad = data;

    cd->con = NULL;

    /* Init the window. */
    win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "Login");
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(win), 15);

    /* TODO: For a some reason, the window remains resizable. */
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_widget_set_hexpand(win, FALSE);
    gtk_widget_set_vexpand(win, FALSE);
    gtk_widget_set_halign(win, GTK_ALIGN_START);
    gtk_widget_set_valign(win, GTK_ALIGN_START);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(free_data), wd);
    g_signal_connect(win, "destroy", G_CALLBACK(free_clist), cd->clist);
    g_signal_connect(win, "destroy", G_CALLBACK(free_data), cd);
    g_signal_connect(win, "destroy", G_CALLBACK(application_quit), app);

    wd->app = G_OBJECT(app);
    wd->win = G_OBJECT(win);

    /* Init the grid. */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(win), grid);

    /* Init the labels. */
    label_host = gtk_label_new("Host ");
    label_uname = gtk_label_new("Username ");
    label_password = gtk_label_new("Password ");

    gtk_label_set_xalign(GTK_LABEL(label_host), 0);
    gtk_label_set_xalign(GTK_LABEL(label_uname), 0);
    gtk_label_set_xalign(GTK_LABEL(label_password), 0);

    /* Init the entries. */
    cd->host = gtk_entry_new();
    cd->uname = gtk_entry_new();
    cd->passw = gtk_entry_new();

    gtk_entry_set_max_length(GTK_ENTRY(cd->host), 16);
    gtk_entry_set_max_length(GTK_ENTRY(cd->uname), 32);
    gtk_entry_set_max_length(GTK_ENTRY(cd->passw), 32);

    gtk_entry_set_visibility(GTK_ENTRY(cd->passw), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(cd->passw), L'•');

    /* Init the connection list. */
    store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING);

    /* TODO: Disable a focus on the view. */
    /* Init the view. */
    view = con_view_create(store);
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    g_signal_connect(view, "button-press-event", G_CALLBACK(smenu_cb), wd);

    cd->cview = GTK_TREE_VIEW(view);
    cd->cstore = store;
    wd->data = cd;

    /* Init the "Add a connection" button. */
    btn_con_add = gtk_button_new_with_label("Add a connection");
    g_signal_connect(btn_con_add, "clicked", G_CALLBACK(con_open), wd);

    /* Work with the user arguments. */
    if (ad->uname && ad->passw) {
        const gchar *host = ad->host ? ad->host : "localhost";

        gtk_entry_set_text(GTK_ENTRY(cd->host), host);
        gtk_entry_set_text(GTK_ENTRY(cd->uname), ad->uname);
        gtk_entry_set_text(GTK_ENTRY(cd->passw), ad->passw);

        g_signal_emit_by_name(btn_con_add, "clicked");

        gtk_entry_set_text(GTK_ENTRY(cd->host), "");
        gtk_entry_set_text(GTK_ENTRY(cd->uname), "");
        gtk_entry_set_text(GTK_ENTRY(cd->passw), "");
    }

    /* Init the "Open" button. */
    btn_con_open = gtk_button_new_with_label("Open");
    g_signal_connect(btn_con_open, "clicked", G_CALLBACK(con_sel_cb), wd);

    /* Place the widgets onto the grid. */
    gtk_grid_attach(GTK_GRID(grid), label_host,     0, 0,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_uname,    0, 1,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_password, 0, 2,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), cd->host,       1, 0,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), cd->uname,      1, 1,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), cd->passw,      1, 2,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_con_add,    0, 3,  2, 1);
    gtk_grid_attach(GTK_GRID(grid), view,           2, 0, 15, 3);
    gtk_grid_attach(GTK_GRID(grid), btn_con_open,   2, 3, 15, 1);

    gtk_widget_show_all(win);

    g_object_unref(G_OBJECT(store));

    gtk_main();
}

/* The connection menu. */
static void
smenu_view(gpointer data)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *items[3] = {
        gtk_menu_item_new_with_label("rename"),
        gtk_menu_item_new_with_label("remove"),
        gtk_menu_item_new_with_label("info")
    };
    int i;

    g_signal_connect(items[0], "activate", G_CALLBACK(item_rename_cb), data);
    g_signal_connect(items[1], "activate", G_CALLBACK(item_remove_cb), data);
    g_signal_connect(items[2], "activate", G_CALLBACK(item_info_cb), data);

    for (i = 0; i < 3; i++)
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), items[i]);

    gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);

    gtk_widget_show_all(menu);
}

/* A connection menu's handler. */
static gboolean
smenu_cb(GtkWidget *wdg, GdkEventButton *ev, gpointer data)
{
    if (ev->type == GDK_BUTTON_PRESS && ev->button == BUTTON_RIGHT) {
        GtkTreeSelection *sel;
        GtkTreeModel *model;
        struct wrap_data *wd = data;
        struct con_data *cd = wd->data;
        GList *rows = NULL;

        sel = gtk_tree_view_get_selection(cd->cview);
        model = gtk_tree_view_get_model(cd->cview);

        rows = gtk_tree_selection_get_selected_rows(sel, &model);
        if (rows) {
            smenu_view(data);

            g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

            return TRUE;
        }
    }

    return FALSE;
}

/* Rename a selected item from the connection list. */
static void
item_rename_cb(GtkWidget *wdg, gpointer data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *entry;
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkWindow *win;
    int *index;
    struct wrap_data *wd = data;
    struct con_data *cd = wd->data;
    struct con_info *ci;

    win = GTK_WINDOW(wd->win);
    dialog = gtk_dialog_new_with_buttons(
        "Rename",
        win,
        GTK_DIALOG_MODAL,
        "OK",
        GTK_RESPONSE_ACCEPT,
        "Cancel",
        GTK_RESPONSE_REJECT,
        NULL);

    sel = gtk_tree_view_get_selection(cd->cview);
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
    gtk_tree_selection_get_selected(sel, &model, &iter);

    path = gtk_tree_model_get_path(model, &iter);
    index = gtk_tree_path_get_indices(path);

    ci = g_list_nth_data(cd->clist, index[0]);

    /* Init the entry. */
    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), ci->name);

    /* Init the content area. */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), entry);
    gtk_widget_show_all(dialog);

    /* Run a dialog. */
    switch (gtk_dialog_run(GTK_DIALOG(dialog))) {
        case GTK_RESPONSE_ACCEPT:
            ci->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));

            gtk_list_store_set(cd->cstore, &iter, COLUMN, ci->name, -1);

            break;

        default:
            break;
    }

    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_path_free(path);

    gtk_widget_destroy(dialog);
}

/* Remove the selected items from the connection list. */
static void
item_remove_cb(GtkWidget *wdg, gpointer data)
{
    GtkWidget *dialog;
    GtkWindow *win;
    GtkTreeIter iter;
    GtkTreePath *path;
    GtkTreeModel *model;
    GtkTreeSelection *sel;
    GtkTreeRowReference *row_ref;
    int result;
    int *index;
    struct wrap_data *wd = data;
    struct con_data *cd = wd->data;
    struct con_info *ci;
    GList *rr_list = NULL;
    GList *node;
    GList *tmp;
    GList *rows;

    win = GTK_WINDOW(wd->win);
    dialog = gtk_message_dialog_new(
        win,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        "Are you sure you want to delete this?");
    result = gtk_dialog_run(GTK_DIALOG(dialog));

    switch (result) {
        case GTK_RESPONSE_YES:
            model = gtk_tree_view_get_model(cd->cview);
            sel = gtk_tree_view_get_selection(cd->cview);

            rows = gtk_tree_selection_get_selected_rows(sel, &model);
            if (!rows)
                break;

            /* Create the rows refereces, to modify a tree model safely. */
            for (node = g_list_reverse(rows); node; node = node->next) {
                if (path = node->data) {
                    row_ref = gtk_tree_row_reference_new(model, path);
                    rr_list = g_list_append(rr_list, row_ref);
                    index = gtk_tree_path_get_indices(path);

                    tmp = g_list_nth(cd->clist, index[0]);
                    ci = tmp->data;

                    /* Free a connection data. */
                    g_free(ci->host);
                    g_free(ci->uname);
                    g_free(ci->passw);
                    g_free(ci->name);
                    g_free(ci);

                    /* Free the temp list, because it was allocated by the
                     * g_list_nth(). */
                    g_list_free(tmp);

                    cd->clist = g_list_remove_link(cd->clist, tmp);
                }
            }

            /* Remove the connection from a connection store. */
            for (node = rr_list; node; node = node->next) {
                if (path = gtk_tree_row_reference_get_path(node->data)) {
                    if (gtk_tree_model_get_iter(model, &iter, path))
                        gtk_list_store_remove(cd->cstore, &iter);
                }
            }

            g_list_free_full(rr_list, (GDestroyNotify)
                gtk_tree_row_reference_free);
            g_list_free_full(rows, (GDestroyNotify)
                gtk_tree_path_free);

            break;

        default:
            break;
    }

    gtk_widget_destroy(dialog);
}

/* Show a selected connection's info. */
static void
item_info_cb(GtkWidget *wdg, gpointer data)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *label_host;
    GtkWidget *label_uname;
    GtkWidget *box;
    GtkWindow *win;
    GtkTreeModel *model;
    GtkTreeSelection *sel;
    GtkTreeIter iter;
    GtkTreePath *path;
    int *index;
    struct wrap_data *wd = data;
    struct con_data *cd = wd->data;
    struct con_info *ci;
    gchar *host;
    gchar *uname;

    win = GTK_WINDOW(wd->win);

    sel = gtk_tree_view_get_selection(cd->cview);
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    path = gtk_tree_model_get_path(model, &iter);
    index = gtk_tree_path_get_indices(path);

    ci = g_list_nth_data(cd->clist, index[0]);
    dialog = gtk_dialog_new_with_buttons(
        "Info",
        win,
        GTK_DIALOG_MODAL,
        NULL,
        NULL);

    /* Init the content area. */
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    /* Init the box. */
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(content_area), box);

    /* Init the labels. */
    host = g_strdup_printf("Host: %s", ci->host);
    uname = g_strdup_printf("Username: %s", ci->uname);

    label_host = gtk_label_new(host);
    label_uname = gtk_label_new(uname);

    gtk_label_set_xalign(GTK_LABEL(label_host), 0.0);
    gtk_label_set_xalign(GTK_LABEL(label_uname), 0.0);

    gtk_box_pack_start(GTK_BOX(box), label_host, TRUE, TRUE, 2);
    gtk_box_pack_start(GTK_BOX(box), label_uname, TRUE, TRUE, 2);

    g_free(host);
    g_free(uname);
    gtk_tree_path_free(path);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
application_quit(GtkWidget *wdg, gpointer data)
{
    GtkApplication *app = data;
    GList *list = gtk_application_get_windows(app);
    GList *head;

    /* TODO: Use the gtk_application_remove_window()? */
    for (head = list; head; head = head->next)
        gtk_widget_destroy(GTK_WIDGET(head->data));

    g_application_quit(G_APPLICATION(app));
}

static gboolean
connect(MYSQL *con, const struct con_info *ci)
{
    g_print("Connecting to %s as %s... ", ci->host, ci->uname);

    if (!mysql_real_connect(con, ci->host, ci->uname, ci->passw, NULL, 0,
            NULL, 0)) {
        g_print("failed!\n");
        con_assert(con);

        return FALSE;
    }

    g_print("OK.\n");

    return TRUE;
}

static void
con_open(GtkWidget *wdg, gpointer data)
{
    GList *tmp;
    MYSQL *con = NULL;
    const gchar *host;
    const gchar *uname;
    const gchar *passw;
    struct wrap_data *wd = data;
    struct con_data *cd = wd->data;
    struct con_info *ci = g_malloc0(sizeof(struct con_info));
    GtkTreeIter iter;

    /* Get a user input from a data received. */
    host = gtk_entry_get_text(GTK_ENTRY(cd->host));
    uname = gtk_entry_get_text(GTK_ENTRY(cd->uname));
    passw = gtk_entry_get_text(GTK_ENTRY(cd->passw));

    /* Don't connect, if a passed data contains in the connection list. */
    for (tmp = cd->clist; tmp; tmp = tmp->next) {
        struct con_info *ci = tmp->data;

        if (!g_strcmp0(host, ci->host) && !g_strcmp0(uname, ci->uname)) {
            g_print("This connection is already added to the list.\n");

            return;
        }
    }

    if (!g_strcmp0(host, "") || !g_strcmp0(uname, ""))
        return;

    /* Create a new active connection. */
    con = mysql_init(con);
    if (!con) {
        g_print("An insufficient memory, to allocate a new MySQL handler.");

        return;
    }

    if (!connect(con, &(struct con_info) {
            (gchar *) host,
            (gchar *) uname,
            (gchar *) passw,
            NULL}))
        return;

    /* A connection has established here. So, pass a connection handle into
     * a connection data. */
    cd->con = con;

    /* Store a connection info. */
    ci->host = g_strdup(host);
    ci->uname = g_strdup(uname);
    ci->passw = g_strdup(passw);
    ci->name = g_strdup(uname);

    gtk_list_store_append(cd->cstore, &iter);
    gtk_list_store_set(cd->cstore, &iter, COLUMN, uname, -1);

    /* Append a connection to the connection list. */
    cd->clist = g_list_append(cd->clist, ci);
}

static void
con_close(GtkWidget *wdg, gpointer data)
{
    MYSQL *con = data;

    if (con->db) {
        g_free(con->db);
        con->db = NULL;
    }

    mysql_close(con);
    con = NULL;
}

static void
free_data(GtkWidget *wdg, gpointer data)
{
    g_free(data);
}

static void
free_clist(GtkWidget *wdg, gpointer data)
{
    GList *list;

    for (list = data; list; list = list->next) {
        struct con_info *ci = list->data;

        g_free(ci->host);
        g_free(ci->uname);
        g_free(ci->passw);
        g_free(ci->name);
        g_free(ci);
    }

    g_list_free(list);
}

/* I think, that a tables showing, based on the user permissions isn't a
 * correctly decision, because a user has the permission to look at a
 * table name. */
static void
win_dbs(GtkApplication *app, MYSQL *con)
{
    GtkWidget *win;
    GtkWidget *win_scr;
    GtkWidget *view;
    GtkWidget *box;
    GtkWidget *btn_disconnect;
    GtkWidget *btn_open;
    GtkTreeSelection *sel;
    gchar *title;
    struct wrap_data *wd = g_malloc(sizeof(struct wrap_data));
    struct sel_data *sld = g_malloc(sizeof(struct sel_data));

    wd->app = G_OBJECT(app);
    title = g_strdup_printf("%s -> %s", con->host, con->user);

    /* Init the window. */
    win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_resize(GTK_WINDOW(win), WIN_DBS_X, WIN_DBS_Y);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(win), 15);

    g_signal_connect(win, "destroy", G_CALLBACK(con_close), con);
    g_signal_connect(win, "destroy", G_CALLBACK(free_data), wd);
    g_signal_connect(win, "destroy", G_CALLBACK(free_data), sld);
    g_signal_connect(win, "destroy", G_CALLBACK(free_data), title);

    /* Init the scrolled window. */
    win_scr = gtk_scrolled_window_new(NULL, NULL);

    /* Init the box. */
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(win), box);

    /* Init the tree view. */
    view = dbs_view_create(con);
    gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(win_scr), view);

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);

    /* Init the "Disconnect" button. */
    btn_disconnect = gtk_button_new_with_label("Disconnect");

    g_signal_connect_swapped(btn_disconnect, "clicked",
        G_CALLBACK(gtk_widget_destroy), win);

    /* Init the "Open" button. */
    btn_open = gtk_button_new_with_label("Open");
    sld->sel = sel;
    sld->con = con;
    wd->data = sld;

    g_signal_connect(btn_open, "clicked", G_CALLBACK(table_sel_cb), wd);

    gtk_box_pack_start(GTK_BOX(box), win_scr, TRUE, TRUE, 1);
    gtk_box_pack_start(GTK_BOX(box), btn_disconnect, FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(box), btn_open, FALSE, FALSE, 1);

    gtk_widget_show_all(win);
}

/* TODO: I think, that a tables editing is impossible here. But, can
 * to try to connect a key press signal to an each entry to get
 * its position like a (0 + x), then to change a row by a
 * (0 + y) index (MYSQL_ROW is a two-dimensional array). But,
 * how to get an entry position? */
static void
win_table(GtkApplication *app, MYSQL *con, const gchar *tb_name)
{
    GtkWidget *win;
    GtkWidget *win_scr;
    GtkWidget *grid;
    GtkWidget *label;
    MYSQL_RES *vls_res;
    MYSQL_FIELD *vls_fld;
    MYSQL_ROW vls_row;
    gchar *cmd;
    gchar *title;
    int x;
    int y;
    int vls_n;

    title = g_strdup_printf("%s -> %s -> %s -> %s", con->host, con->user,
        con->db, tb_name);

    /* Init the window.. */
    win = gtk_application_window_new(app);
    gtk_window_resize(GTK_WINDOW(win), WIN_TBS_X, WIN_TBS_Y);
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);
    gtk_window_move(GTK_WINDOW(win), 50, 50);
    gtk_container_set_border_width(GTK_CONTAINER(win), 15);

    g_signal_connect(win, "destroy", G_CALLBACK(gtk_widget_destroy), NULL);
    g_signal_connect(win, "destroy", G_CALLBACK(free_data), title);

    /* Init the scrolled window. */
    win_scr = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(win), win_scr);

    /* Init the grid. */
    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(win_scr), grid);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);

    cmd = g_strdup_printf("select * from `%s`", tb_name);
    if (mysql_query(con, cmd) != 0) {
        con_warn(con);

        return;
    }

    /* Fill the column names. */
    vls_res = mysql_store_result(con);
    x = 0;
    y = 0;

    while (vls_fld = mysql_fetch_field(vls_res)) {
        label = gtk_label_new(vls_fld->name);

        gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

        x++;
    }

    /* Make a space after the columns for a vertical scroll bar. */
    label = gtk_label_new("");
    gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

    x = 0;
    y++;

    /* Fill the column values. */
    vls_n = mysql_num_fields(vls_res);

    while (vls_row = mysql_fetch_row(vls_res)) {
        int i;

        for (i = 0; i < vls_n; i++) {
            if (!g_strcmp0(vls_row[i], "") || !vls_row[i])
                label = gtk_label_new("NULL");
            else
                label = gtk_label_new(vls_row[i]);

            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

            x++;
        }

        x = 0;
        y++;
    }

    /* Make a space after the rows for a horizontal scroll bar. */
    label = gtk_label_new("");
    gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

    mysql_free_result(vls_res);
    g_free(cmd);

    gtk_widget_show_all(win);
}

static void
table_sel_cb(GtkWidget *wdg, gpointer data)
{
    GtkApplication *app;
    GtkTreeSelection *sel;
    GtkTreeModel *mdl;
    GtkTreePath *path;
    GList *rows = NULL;
    GList *tmp = NULL;
    MYSQL *con = NULL;
    struct wrap_data *wd = data;
    struct sel_data *sld = wd->data;

    app = GTK_APPLICATION(wd->app);
    sel = sld->sel;
    con = sld->con;

    rows = gtk_tree_selection_get_selected_rows(sel, &mdl);
    if (!rows) {
        g_print("No selected tables.\n");

        return;
    }

    for (tmp = rows; tmp; tmp = tmp->next) {
        GtkTreeIter iter;
        GtkTreeIter child;
        GtkTreeIter parent;
        int depth;

        if (!tmp->data)
            continue;

        path = tmp->data;

        if (!gtk_tree_model_get_iter(mdl, &iter, path))
            continue;

        depth = gtk_tree_path_get_depth(path);
        if (!gtk_tree_model_iter_children(mdl, &child, &iter) && depth > 1) {
            /* It's a table. */
            gchar *value;
            gchar *cmd;

            if (gtk_tree_model_iter_parent(mdl, &parent, &iter)) {
                gtk_tree_model_get(mdl, &parent, COLUMN, &value, -1);

                cmd = g_strdup_printf("use %s", value);
                if (mysql_query(con, cmd) != 0)
                    con_assert(con);

                con->db = g_strdup(value);

                gtk_tree_model_get(mdl, &iter, COLUMN, &value, -1);

                /* Now, the value is a table name. */
                win_table(app, con, value);

                g_free(value);
                g_free(cmd);
            }
        }
    }

    g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);
}

static GtkTreeModel *
dbs_get(MYSQL *con)
{
    GtkTreeStore *ts;
    GtkTreeIter dbs_lvl;
    GtkTreeIter tbs_lvl;
    MYSQL_RES *dbs_res;
    MYSQL_RES *tbs_res;
    MYSQL_ROW dbs_row; /* The database names. */
    MYSQL_ROW tbs_row; /* The table names. */
    int dbs_n;

    if (mysql_query(con, "show databases") != 0)
        con_assert(con);

    dbs_res = mysql_store_result(con);
    if (!dbs_res)
        con_assert(con);

    ts = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING);

    /* Get a count of the fields from the result. */
    dbs_n = mysql_num_fields(dbs_res);

    /* Get the rows. */
    while (dbs_row = mysql_fetch_row(dbs_res)) {
        int i;

        for (i = 0; i < dbs_n; i++) {
            gchar *cmd;
            int tbs_n;

            gtk_tree_store_append(ts, &dbs_lvl, NULL);
            gtk_tree_store_set(ts, &dbs_lvl, COLUMN, dbs_row[i], -1);

            cmd = g_strdup_printf("use %s", dbs_row[i]);
            if (mysql_query(con, cmd) != 0)
                con_assert(con);

            /* FIXME: What I'm doing? */
            if (mysql_query(con, "show tables") != 0)
                con_assert(con);

            tbs_res = mysql_store_result(con);
            if (!tbs_res)
                con_assert(con);

            tbs_n = mysql_num_fields(tbs_res);

            while (tbs_row = mysql_fetch_row(tbs_res)) {
                int j;

                for (j = 0; j < tbs_n; j++) {
                    gtk_tree_store_append(ts, &tbs_lvl, &dbs_lvl);
                    gtk_tree_store_set(ts, &tbs_lvl, COLUMN, tbs_row[j], -1);
                }
            }

            g_free(cmd);
        }
    }

    mysql_free_result(dbs_res);
    mysql_free_result(tbs_res);

    return GTK_TREE_MODEL(ts);
}

static GtkWidget *
con_view_create(GtkListStore *store)
{
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkWidget *view;

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    col = gtk_tree_view_column_new();
    renderer = gtk_cell_renderer_text_new();

    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

    return view;
}

static void
con_sel_cb(GtkWidget *wdg, gpointer data)
{
    GtkApplication *app;
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GtkTreePath *path;
    GList *rows = NULL;
    GList *tmp = NULL;
    MYSQL *con = NULL;
    struct wrap_data *wd = data;
    struct con_data *cd = wd->data;

    app = GTK_APPLICATION(wd->app);
    sel = gtk_tree_view_get_selection(cd->cview);

    rows = gtk_tree_selection_get_selected_rows(sel, &model);
    if (!rows) {
        g_print("No the connections selected.\n");

        return;
    }

    for (tmp = rows; tmp; tmp = tmp->next) {
        path = tmp->data;

        if (path) {
            int *index;
            struct con_info *ci;

            index = gtk_tree_path_get_indices(path);

            con = mysql_init(con);
            if (!con) {
                g_print("Failed to open a connection!\n");

                con_assert(con);
                g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

                return;
            }

            /* Get a data, placed by a connection list's index. */
            ci = g_list_nth_data(cd->clist, index[0]);

            if (!connect(con, ci))
                g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

            win_dbs(app, con);
        }
    }

    g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);
}

static GtkWidget *
dbs_view_create(MYSQL *con)
{
    GtkWidget *view;
    GtkTreeModel *model;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;

    view = gtk_tree_view_new();

    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN);

    model = dbs_get(con);
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
    g_object_unref(model);

    return view;
}

static void
con_assert(MYSQL *con)
{
    g_print("Error %u: %s.\n", mysql_errno(con), mysql_error(con));

    con_close(NULL, con);
}

static void
con_warn(MYSQL *con)
{
    g_print("Error %u: %s.\n", mysql_errno(con), mysql_error(con));
}
