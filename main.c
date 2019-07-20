#include <gtk/gtk.h>
#include <mysql.h>

enum {
    COLUMN = 0,
    NUM_COLS
};

struct connection_data {
    GtkWidget *host;
    GtkWidget *username;
    GtkWidget *password;
    GtkListStore *store;
    int *count;
    MYSQL *con;
};

struct data {
    MYSQL *con;
    GtkTreeSelection *selection;
};

struct servers {
    gchar *host;
    gchar *username;
    gchar *password;
};

struct servers *servers;

static GdkPixbuf *    image_load(const gchar *path);
static void           server_add_connection(GtkWidget *widget, gpointer data);
static void           close_connection(GtkWidget *widget, gpointer data);
static void           window_main(int argc, char *argv[]);
static void           window_db(MYSQL *con);
static void           window_table(const char *tb_name, MYSQL *con);
static void           on_select(GtkWidget *widget, gpointer statusbar);
static GtkTreeModel * databases_get(MYSQL *con);
static GtkWidget *    databases_view_create(MYSQL *con);
static void           terminate_connection(MYSQL *con);
static void           server_add(GtkListStore **store, const gchar *s_name);
static GtkWidget *    server_view_create(GtkListStore *store);
static void           server_selected(GtkWidget *widget, gpointer data);
static void           data_free(GtkWidget *widget, gpointer data);

int main(int argc, char *argv[])
{
    /* mysql check version */
    /*g_print("MySQL client version: %s\n", mysql_get_client_info());*/

    /* init MySQL library */
    if (mysql_library_init(0, NULL, NULL)) {
        g_print("Couldn't initialize MySQL client library.\n");

        return -1;
    }

    window_main(argc, argv);

    g_free(servers);

    mysql_library_end();

    return 0;
}

/*
 * Load image by specified path.
 *
 * @path - image path
 */
static GdkPixbuf * image_load(const gchar *path)
{
    GError *error = NULL;

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (!pixbuf) {
        g_print("%s\n", error->message);
        g_error_free(error);
    }

    return pixbuf;
}

/*
 * Create MySQL connection.
 *
 * @data - user data (see above)
 *
 * If servers data don't have transmitted connection data, then
 * is created new active connection.
 */
static void server_add_connection(GtkWidget *widget, gpointer data)
{
    MYSQL *con = NULL;

    struct connection_data *cd = data;

    const gchar *host = gtk_entry_get_text(GTK_ENTRY(cd->host));
    const gchar *username = gtk_entry_get_text(GTK_ENTRY(cd->username));
    const gchar *password = gtk_entry_get_text(GTK_ENTRY(cd->password));

    gchar *s_name;

    int i;

    /* don't connect if data is equal servers data */
    for (i = 0; i < *cd->count; i++) {
        if (g_strcmp0(host, servers[i].host) == 0 &&
            g_strcmp0(username, servers[i].username) == 0) {
            g_print("Server already on the list.\n");

            return;
        }
    }

    if (g_strcmp0(host, "") == 0 || g_strcmp0(username, "") == 0)
        return;

    con = mysql_init(con);
    if (con == NULL) {
        terminate_connection(con);

        return;
    }

    cd->con = con;

    g_print("Username: %s\n", username);
    g_print("Password: ******\n");
    g_print("Connecting to %s...\n", host);

    if (mysql_real_connect(cd->con,
            host,
            username,
            password,
            NULL, 0, NULL, 0) == NULL) {
        terminate_connection(cd->con);

        return;
    }

    servers[*cd->count].host = g_strdup(host);
    servers[*cd->count].username = g_strdup(username);
    servers[*cd->count].password = g_strdup(password);

    (*cd->count)++;
    servers = g_realloc(servers, sizeof(struct servers) * (*cd->count + 1));
    s_name = g_strdup_printf("server%d", *cd->count);
    server_add(&cd->store, s_name);

    g_print("Successfully connected!\n");

    g_free(s_name);

    window_db(cd->con);
}

/*
 * Close active connection.
 *
 * @data - MySQL handler
 */
static void close_connection(GtkWidget *widget, gpointer data)
{
    MYSQL *con = data;

    if (con != NULL) {
        g_print("Close connection...\n");
        mysql_close(con);
        con = NULL;
    }
}

static void window_main(int argc, char *argv[])
{
    GtkWidget *window = NULL;
    GtkWidget *label_host = NULL;
    GtkWidget *label_username = NULL;
    GtkWidget *label_password = NULL;
    GtkWidget *button_add_server = NULL;
    GtkWidget *button_connect = NULL;
    GtkWidget *grid = NULL;
    GtkWidget *view = NULL;
    GtkListStore *store = NULL;
    GtkTreeSelection *selection = NULL;
    GdkPixbuf *icon = NULL;

    int count = 0;

    struct connection_data *cd = g_malloc(sizeof(struct connection_data));

    cd->con = NULL;

    gtk_init(&argc, &argv);

    /* window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    icon = image_load("icon.png");

    gtk_window_set_title(GTK_WINDOW(window), "Login");
    gtk_window_resize(GTK_WINDOW(window), 200, 100);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon(GTK_WINDOW(window), icon);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    /* TODO: If database(s) is opened? */
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* grid */
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
    cd->host = gtk_entry_new();
    cd->username = gtk_entry_new();
    cd->password = gtk_entry_new();

    gtk_entry_set_max_length(GTK_ENTRY(cd->host), 16);
    gtk_entry_set_max_length(GTK_ENTRY(cd->username), 32);
    gtk_entry_set_max_length(GTK_ENTRY(cd->password), 32);

    gtk_entry_set_visibility(GTK_ENTRY(cd->password), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(cd->password), L'•');

    /*
     * TODO: Add edit servers list menu when right button pressed
     * (rename server, remove server from list, show server info).
     */
    /* servers */
    store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING);

    cd->store = store;
    cd->count = &count;

    servers = g_malloc(sizeof(struct servers));

    view = server_view_create(store);
    gtk_widget_set_can_focus(view, FALSE);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

    /* button add server */
    button_add_server = gtk_button_new_with_label("Add server");

    g_signal_connect(button_add_server, "clicked",
        G_CALLBACK(server_add_connection), cd);

    /* button connect */
    button_connect = gtk_button_new_with_label("Connect");

    g_signal_connect(button_connect, "clicked", G_CALLBACK(server_selected),
        selection);

    /* place widgets onto the grid */
    gtk_grid_attach(GTK_GRID(grid), label_host,        0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_username,    0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_password,    0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cd->host,          1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cd->username,      1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), cd->password,      1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), button_add_server, 0, 3, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), view,              2, 0, 15, 3);
    gtk_grid_attach(GTK_GRID(grid), button_connect,    2, 3, 15, 1);

    gtk_widget_show_all(window);

    g_object_unref(G_OBJECT(icon));
    g_object_unref(G_OBJECT(store));

    gtk_main();

    g_free(cd);
}

static void data_free(GtkWidget *widget, gpointer data)
{
    g_free(data);
}

static void window_db(MYSQL *con)
{
    GtkWidget *window = NULL;
    GtkWidget *view = NULL;
    GtkTreeSelection *selection = NULL;
    GtkWidget *vbox = NULL;
    GtkWidget *button_disconnect = NULL;
    GtkWidget *button_open = NULL;

    struct data *data = g_malloc(sizeof(struct connection_data));

    /* window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "Databases");
    gtk_window_resize(GTK_WINDOW(window), 500, 300);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    g_signal_connect(window, "destroy", G_CALLBACK(close_connection), con);
    g_signal_connect(window, "destroy", G_CALLBACK(data_free), data);

    /* vbox */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* view */
    view = databases_view_create(con);

    /* selection */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

    /* button disconnect */
    button_disconnect = gtk_button_new_with_label("Disconnect");

    /* TODO: If table(s) is opened? */
    g_signal_connect_swapped(button_disconnect, "clicked",
        G_CALLBACK(gtk_widget_destroy), window);

    /* button open */
    button_open = gtk_button_new_with_label("Open");

    data->con = con;
    data->selection = selection;

    g_signal_connect(button_open, "clicked", G_CALLBACK(on_select), data);

    gtk_box_pack_start(GTK_BOX(vbox), view, TRUE, TRUE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), button_disconnect, FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), button_open, FALSE, FALSE, 1);

    gtk_widget_show_all(window);
}

/* TODO: Add scroll ability and make window limit. */
/* TODO: Make table appearance better. */
/* Show table data.
 *
 * @tb_name - name of the table which will be displayed
 */
static void window_table(const char *tb_name, MYSQL *con)
{
    GtkWidget *window = NULL;
    GtkWidget *grid = NULL;
    GtkWidget *label = NULL;

    gchar *cmd;

    MYSQL_RES *vls_res;
    MYSQL_FIELD *vls_fld;
    MYSQL_ROW vls_row;

    int x;
    int y;
    int vls_n;

    /* window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), tb_name);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_move(GTK_WINDOW(window), 50, 50);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_widget_destroy), NULL);

    /* grid */
    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);

    /* query */
    cmd = g_strdup_printf("select * from `%s`", tb_name);
    if (mysql_query(con, cmd))
        terminate_connection(con);

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
        int i;

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

static void on_select(GtkWidget *widget, gpointer data)
{
    GtkTreeSelection *selection = NULL;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    GtkTreeIter child;
    GtkTreeIter parent;

    MYSQL *con = NULL;

    struct data *recv_data = data;

    con = recv_data->con;
    selection = recv_data->selection;

    /* put selected item into the 'iter' */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        /* no selected node, strange... */
        return;
    }

    /* TODO: If database is empty? */
    /* if 'iter' hasn't children(s) (it's table), so show table */
    if (!gtk_tree_model_iter_children(model, &child, &iter)) {
        /*
         * maybe this table from another database,
         * so use its database just in case
         */
        gchar *value = NULL;
        gchar *cmd;

        if (!gtk_tree_model_iter_parent(model, &parent, &iter)) {
            /* something went wrong */
            return;
        }

        gtk_tree_model_get(model, &parent, COLUMN, &value, -1);

        cmd = g_strdup_printf("use %s", value);
        if (mysql_query(con, cmd))
            terminate_connection(con);

        gtk_tree_model_get(model, &iter, COLUMN, &value, -1);
        window_table(value, con);

        g_free(value);
        g_free(cmd);
    } else
        g_print("No selected tables.\n");
}

static GtkTreeModel * databases_get(MYSQL *con)
{
    GtkTreeStore *ts;
    GtkTreeIter dbs_lvl;
    GtkTreeIter tbs_lvl;

    MYSQL_RES *dbs_res;
    MYSQL_RES *tbs_res;
    MYSQL_ROW dbs_row; /* database names */
    MYSQL_ROW tbs_row; /* database's table names */

    /* unnecessary database names */
    const char dbs_pass_row[3][64] = {
        "information_schema",
        "mysql",
        "performance_schema"
    };

    int dbs_n;

    ts = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING);

    if (mysql_query(con, "show databases"))
        terminate_connection(con);

    dbs_res = mysql_store_result(con);

    if (dbs_res == NULL)
        terminate_connection(con);

    /* get number of fields from result */
    dbs_n = mysql_num_fields(dbs_res);

    /* write first row to 'dbs' */
    while ((dbs_row = mysql_fetch_row(dbs_res))) {
        gboolean pass;
        int i;
        int j;

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
                    terminate_connection(con);

                if (mysql_query(con, "show tables"))
                    terminate_connection(con);

                tbs_res = mysql_store_result(con);

                if (tbs_res == NULL)
                    terminate_connection(con);

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
 * Add server to list.
 *
 * @ls - root store
 * @s_name - server name
 */
static void server_add(GtkListStore **store, const gchar *s_name)
{
    GtkTreeIter iter;

    gtk_list_store_append(*store, &iter);
    gtk_list_store_set(*store, &iter, COLUMN, s_name, -1);
}

static GtkWidget * server_view_create(GtkListStore *store)
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

static void server_selected(GtkWidget *widget, gpointer data)
{
    GtkTreeSelection *selection = data;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    GtkTreePath *path;

    MYSQL *con = NULL;

    gint *index;

    const gchar *host;
    const gchar *username;
    const gchar *password;

    /* TODO: Use gtk_tree_selection_get_selected_rows(). */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        g_print("No selected servers.\n");

        return;
    }

    path = gtk_tree_model_get_path(model, &iter);
    index = gtk_tree_path_get_indices(path);

    con = mysql_init(con);
    if (con == NULL) {
        terminate_connection(con);
        gtk_tree_path_free(path);

        return;
    }

    host = g_strdup(servers[index[0]].host);
    username = g_strdup(servers[index[0]].username);
    password = g_strdup(servers[index[0]].password);

    if (mysql_real_connect(con,
            host,
            username,
            password,
            NULL, 0, NULL, 0) == NULL) {
        terminate_connection(con);
        gtk_tree_path_free(path);

        return;
    }

    gtk_tree_path_free(path);

    window_db(con);
}

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

static void terminate_connection(MYSQL *con)
{
    g_print("Error %u: %s\n", mysql_errno(con), mysql_error(con));

    if (con != NULL) {
        mysql_close(con);
        con = NULL;
    }
}
