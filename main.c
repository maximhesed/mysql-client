#include <string.h>
#include <gtk/gtk.h>
#include <mysql.h>

MYSQL *con;

enum {
    COLUMN = 0,
    NUM_COLS
};

struct data_connect {
    GtkWidget *host;
    GtkWidget *username;
    GtkWidget *password;
};

static GdkPixbuf *    image_load(const gchar *path);
static void           connect(GtkWidget *widget, struct data_connect *data);
static void           disconnect(GtkWidget *widget, gpointer *data);
static void           window_main(int argc, char *argv[]);
static void           window_db(struct data_connect *data);
static void           window_table(const char *tb_name);
static void           window_destroy(GtkWidget *widget, GtkWidget *window);
static void           on_select(GtkWidget *widget, gpointer statusbar);
static GtkTreeModel * create_and_fill_model(void);
static GtkWidget *    create_view_and_model(void);
static void           terminate_connection(MYSQL *con);

int main(int argc, char *argv[])
{
    window_main(argc, argv);

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
 * Connect to MySQL server.
 *
 * @data - user data (see above)
 */
static void connect(GtkWidget *widget, struct data_connect *data)
{
    struct data_connect *dc = data;

    const gchar *host = gtk_entry_get_text(GTK_ENTRY(dc->host));
    const gchar *username = gtk_entry_get_text(GTK_ENTRY(dc->username));
    const gchar *password = gtk_entry_get_text(GTK_ENTRY(dc->password));

    if (strcmp(host, "") == 0 || strcmp(username, "") == 0)
        return;

    con = mysql_init(NULL);
    if (con == NULL) {
        g_print("Error %u: %s\n", mysql_errno(con), mysql_error(con));

        return;
    }

    g_print("Username: %s\n", username);
    g_print("Password: ******\n");
    g_print("connecting to %s...\n", host);

    if (mysql_real_connect(con, host, username, password, NULL, 0,
        NULL, 0) == NULL) {
        terminate_connection(con);

        return;
    }

    g_print("Successfully connected!\n");

    window_db(dc);
}

static void disconnect(GtkWidget *widget, gpointer *data)
{
    if (con != NULL) {
        g_print("Close MySQL...\n");
        mysql_close(con);
        con = NULL;
    }
}

static void window_main(int argc, char *argv[])
{
    /* mysql check version */
    g_print("MySQL client version: %s\n", mysql_get_client_info());

    GtkWidget *window = NULL;
    GtkWidget *label_host = NULL;
    GtkWidget *label_username = NULL;
    GtkWidget *label_password = NULL;
    GtkWidget *button_connect = NULL;
    GtkWidget *grid = NULL;
    GdkPixbuf *icon = NULL;

    struct data_connect *dc = g_slice_new0(struct data_connect);

    gtk_init(&argc, &argv);

    /* window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    icon = image_load("icon.png");

    gtk_window_set_title(GTK_WINDOW(window), "Connect");
    gtk_window_resize(GTK_WINDOW(window), 200, 100);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon(GTK_WINDOW(window), icon);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

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

    /* edit texts */
    dc->host = gtk_entry_new();
    dc->username = gtk_entry_new();
    dc->password = gtk_entry_new();

    gtk_entry_set_max_length(GTK_ENTRY(dc->host), 16);
    gtk_entry_set_max_length(GTK_ENTRY(dc->username), 32);
    gtk_entry_set_max_length(GTK_ENTRY(dc->password), 32);

    gtk_entry_set_visibility(GTK_ENTRY(dc->password), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(dc->password), '*');

    /* button */
    button_connect = gtk_button_new_with_label("Connect");

    g_signal_connect(button_connect, "clicked", G_CALLBACK(connect), dc);

    /* pack widgets into the grid */
    gtk_grid_attach(GTK_GRID(grid), label_host,     0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_username, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_password, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dc->host,       1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dc->username,   1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dc->password,   1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), button_connect, 0, 3, 2, 1);

    gtk_widget_show_all(window);

    gtk_main();

    g_slice_free(struct data_connect, dc);
    g_object_unref(icon);
}

static void window_db(struct data_connect *data)
{
    GtkWidget *window = NULL;
    GtkWidget *view = NULL;
    GtkTreeSelection *selection = NULL;
    GtkWidget *vbox = NULL;
    GtkWidget *button_disconnect = NULL;
    GtkWidget *button_open = NULL;

    /* window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(GTK_WINDOW(window), "Databases");
    gtk_window_resize(GTK_WINDOW(window), 500, 300);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_widget_destroy), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(disconnect), NULL);

    /* vbox */
    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* view */
    view = create_view_and_model();

    /* selection */
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

    /* button disconnect */
    button_disconnect = gtk_button_new_with_label("Disconnect");

    g_signal_connect(button_disconnect, "clicked",
        G_CALLBACK(disconnect), con);
    g_signal_connect(button_disconnect, "clicked",
        G_CALLBACK(window_destroy), window);

    /* button open */
    button_open = gtk_button_new_with_label("Open");

    g_signal_connect(button_open, "clicked", G_CALLBACK(on_select), selection);

    gtk_box_pack_start(GTK_BOX(vbox), view, TRUE, TRUE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), button_disconnect, FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), button_open, FALSE, FALSE, 1);

    gtk_widget_show_all(window);
}

/* Show table data.
 *
 * @tb_name - name of the table which will be displayed
 */
static void window_table(const char *tb_name)
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
    gtk_window_resize(GTK_WINDOW(window), 500, 300);
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

/*
 * Destroy window.
 *
 * @window - window which will be destroyed
 */
static void window_destroy(GtkWidget *widget, GtkWidget *window)
{
    gtk_widget_destroy(window);
}

static void on_select(GtkWidget *widget, gpointer data)
{
    GtkTreeSelection *selection = data;
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    GtkTreeIter child;
    GtkTreeIter parent;

    /* put selected item into the 'iter' */
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        /* no selected node, strange... */
        return;
    }

    /* TODO: what to do, if database is empty? */
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
        window_table(value);

        g_free(value);
        g_free(cmd);
    }
}

static GtkTreeModel * create_and_fill_model(void)
{
    GtkTreeStore *treestore;
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

    treestore = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING);

    if (mysql_query(con, "show databases"))
        terminate_connection(con);

    dbs_res = mysql_store_result(con);

    if (dbs_res == NULL)
        terminate_connection(con);

    /* get number of fields from result */
    dbs_n = mysql_num_fields(dbs_res);

    /* TODO: there is something wrong */
    /* write first row to 'dbs' */
    while ((dbs_row = mysql_fetch_row(dbs_res))) {
        gboolean pass;
        int i;
        int j;

        for (i = 0; i < dbs_n; i++) {
            pass = FALSE;

            for (j = 0; j < 3; j++) {
                if (strcmp(dbs_pass_row[j], dbs_row[i]) == 0)
                    pass = TRUE;
            }

            if (!pass) {
                gchar *cmd;

                gtk_tree_store_append(treestore, &dbs_lvl, NULL);
                gtk_tree_store_set(treestore, &dbs_lvl, COLUMN,
                    dbs_row[i], -1);

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
                        gtk_tree_store_append(treestore, &tbs_lvl, &dbs_lvl);
                        gtk_tree_store_set(treestore, &tbs_lvl, COLUMN,
                            tbs_row[i], -1);
                    }
                }

                g_free(cmd);
            }
        }
    }

    mysql_free_result(dbs_res);
    mysql_free_result(tbs_res);

    return GTK_TREE_MODEL(treestore);
}

static GtkWidget * create_view_and_model(void)
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

    model = create_and_fill_model();
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
    g_object_unref(model);

    return view;
}

static void terminate_connection(MYSQL *con)
{
    g_print("Error %u: %s\n", mysql_errno(con), mysql_error(con));
    mysql_close(con);
}
