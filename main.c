#include <gtk/gtk.h>
#include <mysql.h>

#include "data.h"

#define RIGHT_BUTTON 3

/* TODO: It's temporary solution, while i seek the way make independent
 * size window, but so that its size didn't be very large. */
#define WINDOW_TABLE_X 300
#define WINDOW_TABLE_Y 500

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
static void free_data(GtkWidget *widget, gpointer data);
static void free_servers_list(GtkWidget *widget, gpointer data);

/* direct calls */
static void connection_terminate(MYSQL *con);
static GtkWidget * servers_view_create(GtkListStore *store);
static void servers_store_add_server(GtkListStore **store, const gchar *s_name);
static void window_databases(GtkApplication *app, MYSQL *con);
static void window_table(GtkApplication *app, MYSQL *con, const gchar *tb_name);
static GtkTreeModel * databases_get(MYSQL *con);
static GtkWidget * databases_view_create(MYSQL *con);

/* servers popup menu */
static gboolean servers_menu_view(gpointer data);
static gboolean servers_menu_call(GtkWidget *treeview, GdkEventButton *ev, gpointer data);
static void servers_menu_item_rename(GtkWidget *widget, gpointer data);
static void servers_menu_item_remove(GtkWidget *widget, gpointer data);

/* servers menu */
static gboolean servers_menu_view(gpointer data)
{
	GtkWidget *menu;
	GtkWidget *item_rename;
	GtkWidget *item_remove;

	menu = gtk_menu_new();
	item_rename = gtk_menu_item_new_with_label("rename");
	item_remove = gtk_menu_item_new_with_label("remove");

	g_signal_connect(item_rename, "activate", G_CALLBACK(servers_menu_item_rename), data);
	g_signal_connect(item_remove, "activate", G_CALLBACK(servers_menu_item_remove), data);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_rename);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_remove);

	gtk_widget_show_all(menu);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);

	return TRUE;
}

/* servers menu handler */
static gboolean servers_menu_call(GtkWidget *treeview, GdkEventButton *ev, gpointer data)
{
	if (ev->type == GDK_BUTTON_PRESS && ev->button == RIGHT_BUTTON) {
		GtkTreeSelection *selection;
		GtkTreeModel *model;

		struct wrapped_data *wrap_data = data;
		struct server_data *serv_data = wrap_data->data;

		GList *rows = NULL;

		selection = gtk_tree_view_get_selection(serv_data->servers_view);
		model = gtk_tree_view_get_model(serv_data->servers_view);

		rows = gtk_tree_selection_get_selected_rows(selection, &model);
		if (rows) {
			servers_menu_view(data);

			return TRUE;
		}
	}

	return FALSE;
}

static void servers_menu_item_rename(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
	GtkWidget *content_area;
	GtkWidget *entry;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkWindow *window;

	gint result;
	gint *index;

	const gchar *s_name;

	struct wrapped_data *wrap_data = data;
	struct server_data *serv_data = wrap_data->data;
	struct server *serv;

	window = GTK_WINDOW(wrap_data->object);

	dialog = gtk_dialog_new_with_buttons("Rename", window, GTK_DIALOG_MODAL,
		"OK", GTK_RESPONSE_ACCEPT,
		"Cancel", GTK_RESPONSE_REJECT,
		NULL);

	entry = gtk_entry_new();

	/* dialog content area */
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	gtk_container_add(GTK_CONTAINER(content_area), entry);

	gtk_widget_show_all(dialog);

	/* run the dialog */
	result = gtk_dialog_run(GTK_DIALOG(dialog));

	switch (result) {
	case GTK_RESPONSE_ACCEPT:
		selection = gtk_tree_view_get_selection(serv_data->servers_view);
		gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

		if (!gtk_tree_selection_get_selected(selection, &model, &iter))
			break;

		/* get selection index */
		path = gtk_tree_model_get_path(model, &iter);
		index = gtk_tree_path_get_indices(path);

		serv = g_list_nth_data(serv_data->servers_list, index[0]);

		s_name = gtk_entry_get_text(GTK_ENTRY(entry));
		gtk_list_store_set(serv_data->servers_store, &iter, COLUMN, s_name, -1);

		serv->name = g_strdup(s_name);

		gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

		gtk_tree_path_free(path);

		break;
	default:
		break;
	}

	gtk_widget_destroy(dialog);
}

/* remove selected item(s) from servers list */
static void servers_menu_item_remove(GtkWidget *widget, gpointer data)
{
	GtkWidget *dialog;
	GtkWindow *window;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeRowReference *row_ref;

	gint result;
	gint *index;

	struct wrapped_data *wrap_data = data;
	struct server_data *serv_data = wrap_data->data;
	struct server *serv;

	GList *rr_list = NULL;
	GList *node;
	GList *tmp;
	GList *rows;

	window = GTK_WINDOW(wrap_data->object);

	dialog = gtk_message_dialog_new(window,
		GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
		"You really want remove it?");

	result = gtk_dialog_run(GTK_DIALOG(dialog));

	switch (result) {
	case GTK_RESPONSE_YES:
		model = gtk_tree_view_get_model(serv_data->servers_view);
		selection = gtk_tree_view_get_selection(serv_data->servers_view);
		store = serv_data->servers_store;

		rows = gtk_tree_selection_get_selected_rows(selection, &model);
		if (rows == NULL)
			break;

		/* "turn over" list to delete items, beginning from the end */
		rows = g_list_reverse(rows);

		/* create rows refereces because safety modify tree model */
		for (node = rows; node; node = node->next) {
			path = node->data;

			if (path) {
				row_ref = gtk_tree_row_reference_new(model, path);
				rr_list = g_list_append(rr_list, row_ref);

				/* remove server from the servers list */
				index = gtk_tree_path_get_indices(path);

				tmp = g_list_nth(serv_data->servers_list, index[0]);
				serv_data->servers_list = g_list_remove_link(serv_data->servers_list, tmp);
				serv = tmp->data;

				/* free server data */
				g_free(serv->host);
				g_free(serv->username);
				g_free(serv->password);
				g_free(serv->name);
				g_free(serv);

				/* free temp list because it's has allocated by g_list_nth() */
				g_list_free(tmp);
			}
		}

		/* remove server from the servers store */
		for (node = rr_list; node; node = node->next) {
			path = gtk_tree_row_reference_get_path(node->data);

			if (path) {
				if (gtk_tree_model_get_iter(model, &iter, path))
					gtk_list_store_remove(store, &iter);
			}
		}

		g_list_free_full(rr_list, (GDestroyNotify) gtk_tree_row_reference_free);
		g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

		break;
	default:
		break;
	}

	gtk_widget_destroy(dialog);
}

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
	g_print("MySQL client version: %s\n", mysql_get_client_info());

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

static void connection_open(GtkWidget *widget, gpointer data)
{
	GtkApplication *app;

	GList *tmp;

	MYSQL *con = NULL;

	const gchar *host;
	const gchar *username;
	const gchar *password;

	struct wrapped_data *wrap_data = data;
	struct server_data *serv_data = wrap_data->data;
	struct server *serv = g_malloc0(sizeof(struct server));

	app = GTK_APPLICATION(wrap_data->object);
	serv_data = wrap_data->data;

	/* get user input from received data */
	host = gtk_entry_get_text(GTK_ENTRY(serv_data->host));
	username = gtk_entry_get_text(GTK_ENTRY(serv_data->username));
	password = gtk_entry_get_text(GTK_ENTRY(serv_data->password));

	/* don't connect if passed data already located in the servers list */
	tmp = serv_data->servers_list;

	while (tmp != NULL) {
		struct server *serv = tmp->data;

		if ((g_strcmp0(host, serv->host) == 0) &&
			(g_strcmp0(username, serv->username) == 0)) {
			g_print("This server has already added on the list.\n");

			return;
		}

		tmp = tmp->next;
	}

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

	/* Connection established here,
	 * so pass connection handle into connection data. */

	serv_data->con = con;

	/* fill server */
	serv->host = g_strdup(host);
	serv->username = g_strdup(username);
	serv->password = g_strdup(password);
	serv->name = g_strdup_printf(serv->username);

	/* to name the server */
	servers_store_add_server(&serv_data->servers_store, serv->name);

	/* append the server to the servers list */
	serv_data->servers_list = g_list_append(serv_data->servers_list, serv);

	window_databases(app, serv_data->con);
}

static void connection_close(GtkWidget *widget, gpointer data)
{
	MYSQL *con = data;

	if (con != NULL) {
		g_print("Close connection...\n");
		mysql_close(con);
		con = NULL;
	}
}

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
	GtkTreeSelection *selection;
	GdkPixbuf *icon;

	GError *error = NULL;

	/* object is application */
	struct wrapped_data *wrap_data_a = g_malloc(sizeof(struct wrapped_data));

	/* object is window */
	struct wrapped_data *wrap_data_w = g_malloc(sizeof(struct wrapped_data));

	struct server_data *serv_data = g_malloc0(sizeof(struct server_data));

	serv_data->con = NULL;

	/* load icon */
	icon = gdk_pixbuf_new_from_file("icon.png", &error);
	if (!icon) {
		g_print("Failed to load application icon!\n");
		g_print("%s\n", error->message);
		g_error_free(error);
	}

	/* create a window */
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Login");
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_icon(GTK_WINDOW(window), icon);

	/* TODO: for some reason, window remains is resizable */
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

	gtk_container_set_border_width(GTK_CONTAINER(window), 15);

	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), wrap_data_a);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), wrap_data_w);
	g_signal_connect(window, "destroy", G_CALLBACK(free_servers_list),
		serv_data->servers_list);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), serv_data);
	g_signal_connect(window, "destroy", G_CALLBACK(application_quit), app);

	wrap_data_a->object = G_OBJECT(app);
	wrap_data_w->object = G_OBJECT(window);

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

	/* create a servers list */
	store = gtk_list_store_new(NUM_COLS, G_TYPE_STRING);

	view = servers_view_create(store);

	/* put selected row(s) from view to serv_data */
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect(view, "button-press-event", G_CALLBACK(servers_menu_call), wrap_data_w);

	serv_data->servers_view = GTK_TREE_VIEW(view);
	serv_data->servers_store = store;

	wrap_data_a->data = wrap_data_w->data = serv_data;

	/* button "Add server" */
	button_add_server = gtk_button_new_with_label("Add server");

	g_signal_connect(button_add_server, "clicked", G_CALLBACK(connection_open), wrap_data_a);

	/* button "Connect" */
	button_connect = gtk_button_new_with_label("Connect");

	g_signal_connect(button_connect, "clicked", G_CALLBACK(server_selected), wrap_data_a);

	/* place widgets onto the grid */
	gtk_grid_attach(GTK_GRID(grid), label_host, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), label_username, 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), label_password, 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), serv_data->host, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), serv_data->username, 1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), serv_data->password, 1, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), button_add_server, 0, 3, 2, 1);
	gtk_grid_attach(GTK_GRID(grid), view, 2, 0, 15, 3);
	gtk_grid_attach(GTK_GRID(grid), button_connect, 2, 3, 15, 1);

	gtk_widget_show_all(window);

	g_object_unref(G_OBJECT(icon));
	g_object_unref(G_OBJECT(store));

	gtk_main();
}

static void free_data(GtkWidget *widget, gpointer data)
{
	g_free(data);
}

static void free_servers_list(GtkWidget *widget, gpointer data)
{
	GList *list = data;
	GList *tmp = list;

	struct server *serv;

	while (tmp != NULL) {
		tmp = list, list = list->next;
		serv = tmp->data;

		g_free(serv->host);
		g_free(serv->username);
		g_free(serv->password);
		g_free(serv->name);
		g_free(serv);
	}

	g_list_free(list);
}

/* TODO: open multiple tables */
static void window_databases(GtkApplication *app, MYSQL *con)
{
	GtkWidget *window;
	GtkWidget *scroll_window;
	GtkWidget *view;
	GtkTreeSelection *selection;
	GtkWidget *vbox;
	GtkWidget *button_disconnect;
	GtkWidget *button_open;

	struct wrapped_data *wrap_data = g_malloc(sizeof(struct wrapped_data));
	struct selection_data *sel_data = g_malloc(sizeof(struct selection_data));

	wrap_data->object = G_OBJECT(app);

	/* create a window */
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Databases");
	gtk_window_resize(GTK_WINDOW(window), 500, 300);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(window), 15);

	g_signal_connect(window, "destroy", G_CALLBACK(connection_close), con);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), wrap_data);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), sel_data);

	/* create a scrolled window */
	scroll_window = gtk_scrolled_window_new(NULL, NULL);

	/* vertical oriented box */
	vbox = gtk_vbox_new(FALSE, 2);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* tree view */
	view = databases_view_create(con);
	gtk_container_add(GTK_CONTAINER(scroll_window), view);

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

	wrap_data->data = sel_data;

	g_signal_connect(button_open, "clicked", G_CALLBACK(table_selected), wrap_data);

	gtk_box_pack_start(GTK_BOX(vbox), scroll_window, TRUE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(vbox), button_disconnect, FALSE, FALSE, 1);
	gtk_box_pack_start(GTK_BOX(vbox), button_open, FALSE, FALSE, 1);

	gtk_widget_show_all(window);
}

/* TODO: make table appearance better */
static void window_table(GtkApplication *app, MYSQL *con, const gchar *tb_name)
{
	GtkWidget *window;
	GtkWidget *scroll_window;
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
	gtk_window_resize(GTK_WINDOW(window), WINDOW_TABLE_X, WINDOW_TABLE_Y);
	gtk_window_set_title(GTK_WINDOW(window), tb_name);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_move(GTK_WINDOW(window), 50, 50);
	gtk_container_set_border_width(GTK_CONTAINER(window), 15);

	g_signal_connect(window, "destroy", G_CALLBACK(gtk_widget_destroy), NULL);

	/* create a scrolled window */
	scroll_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(window), scroll_window);

	/* hang a grid */
	grid = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(scroll_window), grid);
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

static void table_selected(GtkWidget *widget, gpointer data)
{
	GtkApplication *app;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeIter child;
	GtkTreeIter parent;

	MYSQL *con = NULL;

	struct wrapped_data *wrap_data = data;
	struct selection_data *sel_data = wrap_data->data;

	app = GTK_APPLICATION(wrap_data->object);
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
		/* Maybe this table from another database,
		 * so use its database just in case. */

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

static GtkTreeModel * databases_get(MYSQL *con)
{
	GtkTreeStore *ts;
	GtkTreeIter dbs_lvl;
	GtkTreeIter tbs_lvl;

	MYSQL_RES *dbs_res;
	MYSQL_RES *tbs_res;
	MYSQL_ROW dbs_row; /* database names */
	MYSQL_ROW tbs_row; /* table names */

	/* It's old code. Simple user hasn't access to some databases
	 * (with the exception of "information_schema"). I gave a mark such
	 * databases "unnecessary", because user doesn't own them. But if root
	 * is log in, then it can't see these databases, what is wrong.
	 * This needs to be rewrite, but not now. */

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

static void servers_store_add_server(GtkListStore **store, const gchar *s_name)
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

/* TODO: open multiple servers */
static void server_selected(GtkWidget *widget, gpointer data)
{
	GtkApplication *app;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreePath *path;

	MYSQL *con = NULL;

	gint *index;

	struct wrapped_data *wrap_data = data;
	struct server_data *serv_data = wrap_data->data;
	struct server *serv;

	app = GTK_APPLICATION(wrap_data->object);

	selection = gtk_tree_view_get_selection(serv_data->servers_view);
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

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

	g_print("Username: %s\n", serv->username);
	g_print("Password: ******\n");
	g_print("Connecting to %s...\n", serv->host);

	if (mysql_real_connect(con,
			serv->host,
			serv->username,
			serv->password,
			NULL, 0, NULL, 0) == NULL) {
		connection_terminate(con);
		gtk_tree_path_free(path);

		return;
	}

	g_print("Successfully connected!\n");

	gtk_tree_path_free(path);

	window_databases(app, con);
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

static void connection_terminate(MYSQL *con)
{
	g_print("Error %u: %s\n", mysql_errno(con), mysql_error(con));

	if (con != NULL) {
		mysql_close(con);
		con = NULL;
	}
}
