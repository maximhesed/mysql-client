#include <gtk/gtk.h>
#include <mysql.h>

#include "data.h"

enum {
    COLUMN = 0,
    NUM_COLS
};

/* callbacks */
static void application_quit(GtkWidget *widget, gpointer data);
static void connection_open(GtkWidget *widget, gpointer data);
static void connection_close(GtkWidget *widget, gpointer data);
static void server_selected(GtkWidget *widget, gpointer data);
static void window_main(GtkApplication *app, gpointer data);
static void table_selected(GtkWidget *widget, gpointer data);
static void data_free_cb(GtkWidget *widget, gpointer data);
static void list_free_cb(GtkWidget *widget, gpointer data);

/* direct calls */
static void connection_terminate(MYSQL *con);
static GtkWidget * servers_view_create(GtkListStore *store);
static void server_add_to_list(GtkListStore **store, const gchar *s_name);
static void window_databases(GtkApplication *app, MYSQL *con);
static void window_table(GtkApplication *app, MYSQL *con, const gchar *tb_name);
static GtkTreeModel * databases_get(MYSQL *con);
static GtkWidget * databases_view_create(MYSQL *con);

static void application_quit(GtkWidget *widget, gpointer data)
{
    GtkApplication *app = data;

    /* close all opened windows */
    GList *list = gtk_application_get_windows(app);
    GList *head = list;
    GList *tmp;

    /* TODO: gtk_application_remove_window()? */
    while (head != NULL) {
        tmp = head, head = head->next;
        gtk_widget_destroy(GTK_WIDGET(tmp->data));
    }

    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char *argv[])
{
    GtkApplication *app;

    gint status;

    /* check MySQL version */
    /*g_print("MySQL client version: %s\n", mysql_get_client_info());*/

    /* MySQL library initialization */
    if (mysql_library_init(0, NULL, NULL)) {
        g_print("Couldn't initialize MySQL client library.\n");

        return -1;
    }

    /* launch application */
    app = gtk_application_new("org.gtk.dbviewer", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(window_main), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    mysql_library_end();

    return status;
}

/*
 * Open MySQL connection.
 *
 * @data - connection_data pointer
 */
static void connection_open(GtkWidget *widget, gpointer data)
{
    GtkApplication *app;

    MYSQL *con = NULL;

    const gchar *host;
    const gchar *username;
    const gchar *password;

    GList *list;

    struct application_data *app_data = data;
    struct server_data *serv_data = app_data->data;

    app = app_data->app;
    serv_data = app_data->data;

    list = serv_data->servers_list;

    /*
     * Server structure. It needs for appending data on the list.
     * It's data will used for create connection when user
     * want connect to the server from servers list.
     */
    struct server *serv = g_malloc0(sizeof(struct server));

    /* get user input from received data */
    host = gtk_entry_get_text(GTK_ENTRY(serv_data->host));
    username = gtk_entry_get_text(GTK_ENTRY(serv_data->username));
    password = gtk_entry_get_text(GTK_ENTRY(serv_data->password));

    /* don't connect if passed data already located in the servers list */


    if (g_strcmp0(host, "") == 0 || g_strcmp0(username, "") == 0)
        return;

    /* create new active connection */
    con = mysql_init(con);
    if (con == NULL) {
        connection_terminate(con);

        return;
    }

    g_print("Username: %s\n", username);
    g_print("Password: ******\n");
    g_print("Connecting to %s...\n", host);

    /* connecting */
    if (mysql_real_connect(con,
            host,
            username,
            password,
            NULL, 0, NULL, 0) == NULL) {
        connection_terminate(con);

        return;
    }

    g_print("Successfully connected!\n");

    /*
     * Connection established, so pass connection
     * handle to connection data.
     */
    serv_data->con = con;

    /* fill server */
    serv->host = g_strdup(host);
    serv->username = g_strdup(username);
    serv->password = g_strdup(password);
    serv->name = g_strdup_printf("server%d", g_list_length(list) + 1);

    /* name the server */
    server_add_to_list(&serv_data->servers_store, serv->name);

    /* append server to servers list */
    serv_data->servers_list = g_list_append(serv_data->servers_list, serv);

    window_databases(app, serv_data->con);
}

/*
 * Close active connection.
 *
 * @data - MySQL handler
 */
static void connection_close(GtkWidget *widget, gpointer data)
{
    MYSQL *con = data;

    if (con != NULL) {
        g_print("Close connection...\n");
        mysql_close(con);
        con = NULL;
    }
}

/*
 * Create main window.
 *
 * It's callback used by application (need for wrapper), that creates
 * basic window, where user can add servers and create connections.
 */
static void window_main(GtkApplication *app, gpointer data)
{
    GtkWidget *window;
    GtkWidget *label_host;
    GtkWidget *label_username;
    GtkWidget *label_password;
    GtkWidget *button_add_server;
    GtkWidget *button_connect;
    GtkWidget *grid;
    GtkWidget *view;
    GtkListStore *store;
    GdkPixbuf *icon;

    /* data which will passed everywhere application is needed */
    struct application_data *app_data = g_malloc(sizeof(struct application_data));

    struct server_data *serv_data = g_malloc0(sizeof(struct server_data));

    app_data->app = app;
    serv_data->con = NULL;

    /* load icon */
    GError *error = NULL;

    icon = gdk_pixbuf_new_from_file("icon.png", &error);
    if (!icon) {
        g_print("Failed to load application icon!\n");
        g_print("%s\n", error->message);
        g_error_free(error);
    }

    /* create a window */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Login");
    gtk_window_resize(GTK_WINDOW(window), 200, 100);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon(GTK_WINDOW(window), icon);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(data_free_cb), app_data);
    g_signal_connect(window, "destroy", G_CALLBACK(list_free_cb), serv_data->servers_list);
    g_signal_connect(window, "destroy", G_CALLBACK(data_free_cb), serv_data);
    g_signal_connect(window, "destroy", G_CALLBACK(application_quit), app);

    /* hang a grid */
    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);

    /* labels */
    label_host = gtk_label_new("Host: ");
    label_username = gtk_label_new("Username: ");
    label_password = gtk_label_new("Password: ");

    gtk_label_set_xalign(GTK_LABEL(label_host), 0);
    gtk_label_set_xalign(GTK_LABEL(label_username), 0);
    gtk_label_set_xalign(GTK_LABEL(label_password), 0);

    /* entries */
    serv_data->host = gtk_entry_new();
    serv_data->username = gtk_entry_new();
    serv_data->password = gtk_entry_new();

    gtk_entry_set_max_length(GTK_ENTRY(serv_data->host), 16);
    gtk_entry_set_max_length(GTK_ENTRY(serv_data->username), 32);
    gtk_entry_set_max_length(GTK_ENTRY(serv_data->password), 32);

    gtk_entry_set_visibility(GTK_ENTRY(serv_data->password), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(serv_data->password), L'•');

    /* TODO: Add servers popup menu. */
    /* create a servers list */
    store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING);

    view = servers_view_create(store);
    gtk_widget_set_can_focus(view, FALSE);

    serv_data->servers_view = GTK_TREE_VIEW(view);
    serv_data->servers_store = store;

    app_data->data = serv_data;

    /* button "Add server" */
    button_add_server = gtk_button_new_with_label("Add server");

    g_signal_connect(button_add_server, "clicked", G_CALLBACK(connection_open), app_data);

    /* button "Connect" */
    button_connect = gtk_button_new_with_label("Connect");

    g_signal_connect(button_connect, "clicked", G_CALLBACK(server_selected), app_data);

    /* place widgets onto the grid */
    gtk_grid_attach(GTK_GRID(grid), label_host, 0, 0,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_username, 0, 1,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_password, 0, 2,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), serv_data->host, 1, 0,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), serv_data->username, 1, 1,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), serv_data->password, 1, 2,  1, 1);
    gtk_grid_attach(GTK_GRID(grid), button_add_server, 0, 3,  2, 1);
    gtk_grid_attach(GTK_GRID(grid), view, 2, 0, 15, 3);
    gtk_grid_attach(GTK_GRID(grid), button_connect, 2, 3, 15, 1);

    gtk_widget_show_all(window);

    g_object_unref(G_OBJECT(icon));
    g_object_unref(G_OBJECT(store));

    gtk_main();
}

/*
 * Free data.
 *
 * @data - data which will be free
 *
 * Simple callback that allow easy and rapidly free anything little.
 */
static void data_free_cb(GtkWidget *widget, gpointer data)
{
    g_free(data);
}

static void list_free_cb(GtkWidget *widget, gpointer data)
{
    GList *list = data;
    GList *tmp = list;

    while (tmp != NULL) {
        tmp = list, list = list->next;
        g_free(tmp->data);
    }
}

/*
 * Create databases window.
 *
 * @con - MySQL handler
 *
 * Creates window which contain databases extracted from the connection handler.
 */
static void window_databases(GtkApplication *app, MYSQL *con)
{
    GtkWidget *window;
    GtkWidget *view;
    GtkTreeSelection *selection;
    GtkWidget *vbox;
    GtkWidget *button_disconnect;
    GtkWidget *button_open;

    struct application_data *app_data = g_malloc(sizeof(struct application_data));
    struct selection_data *sel_data = g_malloc(sizeof(struct selection_data));

    app_data->app = app;

    /* create a window */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Databases");
    gtk_window_resize(GTK_WINDOW(window), 500, 300);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    g_signal_connect(window, "destroy", G_CALLBACK(connection_close), con);
    g_signal_connect(window, "destroy", G_CALLBACK(data_free_cb), app_data);
    g_signal_connect(window, "destroy", G_CALLBACK(data_free_cb), sel_data);

    /* vertical oriented box */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* tree view */
    view = databases_view_create(con);

    /* tree selection */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

    /* button "Disconnect" */
    button_disconnect = gtk_button_new_with_label("Disconnect");

    g_signal_connect_swapped(button_disconnect, "clicked",
        G_CALLBACK(gtk_widget_destroy), window);

    /* button "Open" */
    button_open = gtk_button_new_with_label("Open");

    sel_data->selection = selection;
    sel_data->con = con;

    app_data->data = sel_data;

    g_signal_connect(button_open, "clicked", G_CALLBACK(table_selected), app_data);

    gtk_box_pack_start(GTK_BOX(vbox), view, TRUE, TRUE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), button_disconnect, FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), button_open, FALSE, FALSE, 1);

    gtk_widget_show_all(window);
}

/* TODO: Replace by scrolling window. */
/* TODO: Make table appearance better. */
/*
 * Show table data.
 *
 * @con - MySQL handler
 * @tb_name - name of the table which will be displayed
 *
 * Creates window which contain data from selected database's table.
 */
static void window_table(GtkApplication *app, MYSQL *con, const gchar *tb_name)
{
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *label;

    MYSQL_RES *vls_res;
    MYSQL_FIELD *vls_fld;
    MYSQL_ROW vls_row;

    gchar *cmd;

    gint x;
    gint y;
    gint vls_n;

    /* create a window */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), tb_name);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_move(GTK_WINDOW(window), 50, 50);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_widget_destroy), NULL);

    /* hang a grid */
    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);

    /* make a request */
    cmd = g_strdup_printf("select * from `%s`", tb_name);
    if (mysql_query(con, cmd))
        connection_terminate(con);

    vls_res = mysql_store_result(con);

    /* fill column names */
    x = 0;
    y = 0;

    while ((vls_fld = mysql_fetch_field(vls_res))) {
        label = gtk_label_new(vls_fld->name);

        gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

        x++;
    }

    x = 0;
    y++;

    /* fill column values */
    vls_n = mysql_num_fields(vls_res);

    while ((vls_row = mysql_fetch_row(vls_res))) {
        gint i;

        for (i = 0; i < vls_n; i++) {
            label = gtk_label_new(vls_row[i]);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

            gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

            x++;
        }

        x = 0;
        y++;
    }

    mysql_free_result(vls_res);
    g_free(cmd);

    gtk_widget_show_all(window);
}

/*
 * Table selection handler.
 *
 * @data - selection_data pointer
 *
 * Handles table selection and creates table's window.
 */
static void table_selected(GtkWidget *widget, gpointer data)
{
    GtkApplication *app;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeIter child;
    GtkTreeIter parent;

    MYSQL *con = NULL;

    struct application_data *app_data = data;
    struct selection_data *sel_data = app_data->data;

    app = app_data->app;
    selection = sel_data->selection;
    con = sel_data->con;

    /* put selected item into the 'iter' */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        /* No selected nodes, strange... */
        return;
    }

    /* TODO: If database is empty? */
    /* if 'iter' hasn't children(s) (it's table), so show table */
    if (!gtk_tree_model_iter_children(model, &child, &iter)) {
        /*
         * Maybe this table from another database,
         * so use its database just in case.
         */
        gchar *value;
        gchar *cmd;

        if (!gtk_tree_model_iter_parent(model, &parent, &iter)) {
            /* something went wrong */
            return;
        }

        gtk_tree_model_get(model, &parent, COLUMN, &value, -1);

        cmd = g_strdup_printf("use %s", value);
        if (mysql_query(con, cmd))
            connection_terminate(con);

        gtk_tree_model_get(model, &iter, COLUMN, &value, -1);
        window_table(app, con, value);

        g_free(value);
        g_free(cmd);
    } else
        g_print("No selected tables.\n");
}

/*
 * Get databases.
 *
 * @con - MySQL handler
 *
 * Gets all databases from the connection handler.
 */
static GtkTreeModel * databases_get(MYSQL *con)
{
    GtkTreeStore *ts;
    GtkTreeIter dbs_lvl;
    GtkTreeIter tbs_lvl;

    MYSQL_RES *dbs_res;
    MYSQL_RES *tbs_res;
    MYSQL_ROW dbs_row; /* database names */
    MYSQL_ROW tbs_row; /* table names */

    /*
     * It's old code. Simple user hasn't access to some databases
     * (with the exception of "information_schema"). I gave a mark such
     * databases "unnecessary", because user doesn't own them. But if root
     * is log in, then it can't see these databases, what is wrong.
     * This needs to be rewrite, but not now.
     */
    /* unnecessary database names */
    const gchar dbs_pass_row[3][32] = {
        "information_schema",
        "mysql",
        "performance_schema"
    };

    gint dbs_n;

    if (mysql_query(con, "show databases"))
        connection_terminate(con);

    dbs_res = mysql_store_result(con);
    if (dbs_res == NULL)
        connection_terminate(con);

    ts = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING);

    /* get number of fields from result */
    dbs_n = mysql_num_fields(dbs_res);

    /* write first row to 'dbs' */
    while ((dbs_row = mysql_fetch_row(dbs_res))) {
        gboolean pass;

        gint i;
        gint j;

        for (i = 0; i < dbs_n; i++) {
            pass = FALSE;

            for (j = 0; j < 3; j++) {
                if (g_strcmp0(dbs_pass_row[j], dbs_row[i]) == 0)
                    pass = TRUE;
            }

            if (!pass) {
                gchar *cmd;

                gtk_tree_store_append(ts, &dbs_lvl, NULL);
                gtk_tree_store_set(ts, &dbs_lvl, COLUMN, dbs_row[i], -1);

                cmd = g_strdup_printf("use %s", dbs_row[i]);
                if (mysql_query(con, cmd))
                    connection_terminate(con);

                if (mysql_query(con, "show tables"))
                    connection_terminate(con);

                tbs_res = mysql_store_result(con);

                if (tbs_res == NULL)
                    connection_terminate(con);

                int tbs_n = mysql_num_fields(tbs_res);

                while ((tbs_row = mysql_fetch_row(tbs_res))) {
                    int i;

                    for (i = 0; i < tbs_n; i++) {
                        gtk_tree_store_append(ts, &tbs_lvl, &dbs_lvl);
                        gtk_tree_store_set(ts, &tbs_lvl, COLUMN,
                            tbs_row[i], -1);
                    }
                }

                g_free(cmd);
            }
        }
    }

    mysql_free_result(dbs_res);
    mysql_free_result(tbs_res);

    return GTK_TREE_MODEL(ts);
}

/*
 * Add server on the servers list.
 *
 * @store - servers list
 * @s_name - displayed server name
 *
 * Just append string to end of the servers list.
 */
static void server_add_to_list(GtkListStore **store, const gchar *s_name)
{
    GtkTreeIter iter;

    gtk_list_store_append(*store, &iter);
    gtk_list_store_set(*store, &iter, COLUMN, s_name, -1);
}

static GtkWidget * servers_view_create(GtkListStore *store)
{
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkWidget *view;

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

    col = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

    return view;
}

/*
 * Server selection handler.
 *
 * @data - connection_data pointer
 *
 * Handles selected server and create new connection.
 */
static void server_selected(GtkWidget *widget, gpointer data)
{
    GtkApplication *app;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;

    MYSQL *con = NULL;

    gint *index;

    struct application_data *app_data = data;
    struct server_data *serv_data = app_data->data;
    struct server *serv;

    app = app_data->app;

    selection = gtk_tree_view_get_selection(serv_data->servers_view);

    /* TODO: Use gtk_tree_selection_get_selected_rows(). */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        g_print("No selected servers.\n");

        return;
    }

    path = gtk_tree_model_get_path(model, &iter);
    index = gtk_tree_path_get_indices(path);

    con = mysql_init(con);
    if (con == NULL) {
        connection_terminate(con);
        gtk_tree_path_free(path);

        return;
    }

    /* get list data placed by index to the server structure */
    serv = g_list_nth_data(serv_data->servers_list, index[0]);

    if (mysql_real_connect(con,
            serv->host,
            serv->username,
            serv->password,
            NULL, 0, NULL, 0) == NULL) {
        connection_terminate(con);
        gtk_tree_path_free(path);

        return;
    }

    gtk_tree_path_free(path);

    window_databases(app, con);
}

/*
 * Create databases view.
 *
 * @con - MySQL handler
 *
 * It's top level of databases tree creation. Here is sets tree view settings,
 * such as count of columns, columns type, etc. Tree view has tree model. In the
 * tree model is located databases list, which creating by databases_get function.
 */
static GtkWidget * databases_view_create(MYSQL *con)
{
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkWidget *view;
    GtkTreeModel *model;

    view = gtk_tree_view_new();

    col = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN);

    model = databases_get(con);
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
    g_object_unref(model);

    return view;
}

/*
 * Terminate active connection.
 *
 * @con - MySQL handler
 *
 * Termiante connection if the connection handler not NULL, otherwise
 * get error message. For example, connection create is failed. Handler is empty,
 * but we want get error.
 */
static void connection_terminate(MYSQL *con)
{
    g_print("Error %u: %s\n", mysql_errno(con), mysql_error(con));

    if (con != NULL) {
        mysql_close(con);
        con = NULL;
    }
}
