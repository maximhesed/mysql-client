#include <gtk/gtk.h>
#include <mysql.h>
#include <termios.h>

#include "data.h"

#define BUTTON_RIGHT 3
#define KEY_RETURN 0xFF0D

/* TODO: It's temporary solution, while i seek the way make independent
 * size window, but so that its size didn't be very large. */
#define WIN_DBS_X 650
#define WIN_DBS_Y 550
#define WIN_TBS_X 500
#define WIN_TBS_Y 600

/* TODO: glib logging */
#define COLOR_RED "\e[0;91m"
#define COLOR_YELLOW "\e[0;93m"
#define COLOR_CYAN "\e[0;96m"
#define COLOR_DEFAULT "\e[0m"

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
static void connection_error(MYSQL *con);
static GtkWidget * servers_view_create(GtkListStore *store);
static void servers_store_add_server(GtkListStore **store, const gchar *s_name);
static void window_databases(GtkApplication *app, MYSQL *con);
static void window_table(GtkApplication *app, MYSQL *con, const gchar *tb_name);
static GtkTreeModel * databases_get(MYSQL *con);
static GtkWidget * databases_view_create(MYSQL *con);

/* servers popup menu */
static void servers_menu_view(gpointer data);
static gboolean servers_menu_call(GtkWidget *widget, GdkEventButton *ev, gpointer data);
static void servers_menu_item_rename(GtkWidget *widget, gpointer data);
static void servers_menu_item_remove(GtkWidget *widget, gpointer data);
static void servers_menu_item_info(GtkWidget *widget, gpointer data);

/* servers menu */
static void servers_menu_view(gpointer data)
{
	GtkWidget *menu;
	GtkWidget *item_rename;
	GtkWidget *item_remove;
	GtkWidget *item_info;

	menu = gtk_menu_new();
	item_rename = gtk_menu_item_new_with_label("rename");
	item_remove = gtk_menu_item_new_with_label("remove");
	item_info = gtk_menu_item_new_with_label("info");

	g_signal_connect(item_rename, "activate", G_CALLBACK(servers_menu_item_rename), data);
	g_signal_connect(item_remove, "activate", G_CALLBACK(servers_menu_item_remove), data);
	g_signal_connect(item_info, "activate", G_CALLBACK(servers_menu_item_info), data);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_rename);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_remove);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_info);

	gtk_widget_show_all(menu);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
}

/* servers menu handler */
static gboolean servers_menu_call(GtkWidget *widget, GdkEventButton *ev, gpointer data)
{
	(void) widget;

	if (ev->type == GDK_BUTTON_PRESS && ev->button == BUTTON_RIGHT) {
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

			g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

			return TRUE;
		}
	}

	return FALSE;
}

/* rename selected item from the servers list */
static void servers_menu_item_rename(GtkWidget *widget, gpointer data)
{
	(void) widget;

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

/* remove selected item(s) from the servers list */
static void servers_menu_item_remove(GtkWidget *widget, gpointer data)
{
	(void) widget;

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

		/* create rows refereces to safely modify tree model */
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

		/* remove servers from the servers store */
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

/* show selected server info */
static void servers_menu_item_info(GtkWidget *widget, gpointer data)
{
	(void) widget;

	GtkWidget *dialog;
	GtkWidget *content_area;
	GtkWidget *label_host;
	GtkWidget *label_username;
	GtkWidget *label_password;
	GtkWidget *box;
	GtkWindow *window;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GtkTreePath *path;

	gint *index;

	struct wrapped_data *wrap_data = data;
	struct server_data *serv_data = wrap_data->data;
	struct server *serv;

	gchar *host;
	gchar *username;
	gchar *password;

	window = GTK_WINDOW(wrap_data->object);

	selection = gtk_tree_view_get_selection(serv_data->servers_view);
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	if (!gtk_tree_selection_get_selected(selection, &model, &iter))
		return;

	path = gtk_tree_model_get_path(model, &iter);
	index = gtk_tree_path_get_indices(path);

	serv = g_list_nth_data(serv_data->servers_list, index[0]);

	gtk_tree_path_free(path);

	/* TODO: show/hide password */
	dialog = gtk_dialog_new_with_buttons("Info", window, GTK_DIALOG_MODAL, NULL, NULL);

	/* labels */
	host = g_strdup_printf("Host: %s", serv->host);
	username = g_strdup_printf("Username: %s", serv->username);
	password = g_strdup_printf("Password: %s", serv->password);

	label_host = gtk_label_new(host);
	label_username = gtk_label_new(username);
	label_password = gtk_label_new(password);

	gtk_label_set_xalign(GTK_LABEL(label_host), 0.0);
	gtk_label_set_xalign(GTK_LABEL(label_username), 0.0);
	gtk_label_set_xalign(GTK_LABEL(label_password), 0.0);

	/* box */
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

	gtk_box_pack_start(GTK_BOX(box), label_host, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(box), label_username, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(box), label_password, TRUE, TRUE, 2);

	/* dialog content area */
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	gtk_container_add(GTK_CONTAINER(content_area), box);

	gtk_widget_show_all(dialog);

	g_free(host);
	g_free(username);
	g_free(password);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void application_quit(GtkWidget *widget, gpointer data)
{
	(void) widget;

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

	struct args_data *data = g_malloc0(sizeof(struct args_data));

	GOptionEntry entries[] = {
		{"host", 'h', 0, G_OPTION_ARG_STRING, &data->host, "Host name", "0.0.0.0"},
		{"username", 'u', 0, G_OPTION_ARG_STRING, &data->username, "User name", "str"},
		{"password", 'p', 0, G_OPTION_ARG_STRING, &data->password, "Password", "str"},
		{NULL}
	};

	GError *error = NULL;
	GOptionContext *context;

	gint status;

	/* parse extra user arguments */
	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, entries, NULL);
	g_option_context_add_group(context, gtk_get_option_group(TRUE));
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_print("%s[err]%s: %s\n", COLOR_RED, COLOR_DEFAULT, error->message);

		g_error_free(error);

		return -1;
	}

	/* check MySQL version */
	g_print("%s[info]%s: MySQL client version: %s\n", COLOR_CYAN, COLOR_DEFAULT,
		mysql_get_client_info());

	/* MySQL library initialization */
	if (mysql_library_init(0, NULL, NULL)) {
		g_print("%s[err]%s: Couldn't initialize MySQL client library.\n", COLOR_RED,
			COLOR_DEFAULT);

		return -1;
	}

	/* launch application */
	app = gtk_application_new("org.gtk.dbviewer", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(window_main), data);
	g_signal_connect(app, "shutdown", G_CALLBACK(free_data), data);
	status = g_application_run(G_APPLICATION(app), 0, NULL);

	g_object_unref(app);

	mysql_library_end();

	/* flush all input */
	tcflush(STDOUT_FILENO, TCIFLUSH);

	return status;
}

static void connection_open(GtkWidget *widget, gpointer data)
{
	(void) widget;

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
			g_print("%s[info]%s: This server is already added on the list.\n", COLOR_CYAN,
				COLOR_DEFAULT);

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
	(void) widget;

	MYSQL *con = data;

	if (con != NULL) {
		g_print("Close connection...\n");
		mysql_close(con);
		con = NULL;
	}
}

static void window_main(GtkApplication *app, gpointer data)
{
	(void) data;

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

	struct wrapped_data *wrap_data_a = g_malloc(sizeof(struct wrapped_data));
	struct wrapped_data *wrap_data_w = g_malloc(sizeof(struct wrapped_data));
	struct server_data *serv_data = g_malloc0(sizeof(struct server_data));
	struct args_data *args_data = data;

	serv_data->con = NULL;

	/* load icon */
	icon = gdk_pixbuf_new_from_file("icon.png", &error);
	if (!icon) {
		g_print("%s[err]%s: Failed to load application icon!\n", COLOR_RED, COLOR_DEFAULT);
		g_print("%s\n", error->message);

		g_error_free(error);
	}

	/* create a window */
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Login");
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_icon(GTK_WINDOW(window), icon);
	gtk_container_set_border_width(GTK_CONTAINER(window), 15);

	/* TODO: for some reason, window remains is resizable */
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);

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

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect(view, "button-press-event", G_CALLBACK(servers_menu_call), wrap_data_w);

	serv_data->servers_view = GTK_TREE_VIEW(view);
	serv_data->servers_store = store;

	wrap_data_a->data = wrap_data_w->data = serv_data;

	/* button "Add server" */
	button_add_server = gtk_button_new_with_label("Add server");

	g_signal_connect(button_add_server, "clicked", G_CALLBACK(connection_open), wrap_data_a);

	/* passing the user arguments */
	if (args_data->username && args_data->password) {
		if (args_data->host)
			gtk_entry_set_text(GTK_ENTRY(serv_data->host), args_data->host);
		else
			gtk_entry_set_text(GTK_ENTRY(serv_data->host), "localhost");

		gtk_entry_set_text(GTK_ENTRY(serv_data->username), args_data->username);
		gtk_entry_set_text(GTK_ENTRY(serv_data->password), args_data->password);

		g_signal_emit_by_name(button_add_server, "clicked");

		gtk_entry_set_text(GTK_ENTRY(serv_data->host), "");
		gtk_entry_set_text(GTK_ENTRY(serv_data->username), "");
		gtk_entry_set_text(GTK_ENTRY(serv_data->password), "");
	}

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
	(void) widget;

	g_free(data);
}

static void free_servers_list(GtkWidget *widget, gpointer data)
{
	(void) widget;

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

/* TODO: replace window with dialog */
static void window_databases(GtkApplication *app, MYSQL *con)
{
	GtkWidget *window;
	GtkWidget *scroll_window;
	GtkWidget *view;
	GtkTreeSelection *selection;
	GtkWidget *box;
	GtkWidget *button_disconnect;
	GtkWidget *button_open;

	gchar *title;

	struct wrapped_data *wrap_data = g_malloc(sizeof(struct wrapped_data));
	struct selection_data *sel_data = g_malloc(sizeof(struct selection_data));

	wrap_data->object = G_OBJECT(app);

	title = g_strdup_printf("Databases ('%s'@'%s')", con->user, con->host);

	/* create a window */
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), title);
	gtk_window_resize(GTK_WINDOW(window), WIN_DBS_X, WIN_DBS_Y);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_container_set_border_width(GTK_CONTAINER(window), 15);

	g_signal_connect(window, "destroy", G_CALLBACK(connection_close), con);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), wrap_data);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), sel_data);
	g_signal_connect(window, "destroy", G_CALLBACK(free_data), title);

	/* create a scrolled window */
	scroll_window = gtk_scrolled_window_new(NULL, NULL);

	/* box */
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_add(GTK_CONTAINER(window), box);

	/* tree view */
	view = databases_view_create(con);
	gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(view), TRUE);
	gtk_container_add(GTK_CONTAINER(scroll_window), view);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

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

	gtk_box_pack_start(GTK_BOX(box), scroll_window, TRUE, TRUE, 1);
	gtk_box_pack_start(GTK_BOX(box), button_disconnect, FALSE, FALSE, 1);
	gtk_box_pack_start(GTK_BOX(box), button_open, FALSE, FALSE, 1);

	gtk_widget_show_all(window);
}

/* TODO: replace window with dialog */
/* TODO: edit table */
/* TODO: horizontal scrollbar is hide the last table's row */
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
	gtk_window_resize(GTK_WINDOW(window), WIN_TBS_X, WIN_TBS_Y);
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

	cmd = g_strdup_printf("select * from `%s`", tb_name);
	if (mysql_query(con, cmd)) {
		g_print("%s[err]%s: Problem with access to the table '%s'.\n", COLOR_RED,
			COLOR_DEFAULT, tb_name);

		connection_error(con);

		return;
	}

	vls_res = mysql_store_result(con);

	/* I think that edit table is impossible here.
	 * My idea: connect key press signal to each entry and get its position
	 * like (0 + x), then change row by (0 + y) index (MYSQL_ROW is two-dimensional array).
	 * But how to get the entry position? */

	/* fill the columns names */
	x = 0;
	y = 0;

	while ((vls_fld = mysql_fetch_field(vls_res))) {
		label = gtk_label_new(vls_fld->name);

		gtk_grid_attach(GTK_GRID(grid), label, x, y, 1, 1);

		x++;
	}

	x = 0;
	y++;

	/* fill the columns values */
	vls_n = mysql_num_fields(vls_res);

	while ((vls_row = mysql_fetch_row(vls_res))) {
		gint i;

		for (i = 0; i < vls_n; i++) {
			if (g_strcmp0(vls_row[i], "") == 0 || !vls_row[i]) {
				/* set markup */
				const gchar *str = "NULL";
				const gchar *format = "<span background='black'>\%s</span>";

				gchar *markup = g_markup_printf_escaped(format, str);

				label = gtk_label_new(NULL);
				gtk_label_set_markup(GTK_LABEL(label), markup);
			} else
				label = gtk_label_new(vls_row[i]);

			gtk_label_set_xalign(GTK_LABEL(label), 0.0);

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
	(void) widget;

	GtkApplication *app;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreePath *path;

	GList *rows = NULL;
	GList *tmp = NULL;

	MYSQL *con = NULL;

	struct wrapped_data *wrap_data = data;
	struct selection_data *sel_data = wrap_data->data;

	app = GTK_APPLICATION(wrap_data->object);
	selection = sel_data->selection;
	con = sel_data->con;

	rows = gtk_tree_selection_get_selected_rows(selection, &model);
	if (rows == NULL) {
		g_print("%s[warn]%s: No selected tables.\n", COLOR_YELLOW, COLOR_DEFAULT);

		return;
	}

	for (tmp = rows; tmp; tmp = tmp->next) {
		path = tmp->data;

		if (path) {
			GtkTreeIter iter;
			GtkTreeIter child;
			GtkTreeIter parent;

			gint depth;

			depth = gtk_tree_path_get_depth(path);

			if (gtk_tree_model_get_iter(model, &iter, path)) {
				if (!gtk_tree_model_iter_children(model, &child, &iter)) {
					if (depth > 1) {
						/* It's table. */

						/* Maybe, it belongs another database,
						 * so use its database just in case. */

						gchar *value;
						gchar *cmd;

						if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
							gtk_tree_model_get(model, &parent, COLUMN, &value, -1);

							cmd = g_strdup_printf("use %s", value);
							if (mysql_query(con, cmd))
								connection_terminate(con);

							gtk_tree_model_get(model, &iter, COLUMN, &value, -1);
							window_table(app, con, value);

							g_free(value);
							g_free(cmd);
						}
					}
				}
			}
		}
	}

	g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);
}

static GtkTreeModel * databases_get(MYSQL *con)
{
	GtkTreeStore *ts;
	GtkTreeIter dbs_lvl;
	GtkTreeIter tbs_lvl;

	MYSQL_RES *dbs_res;
	MYSQL_RES *tbs_res;
	MYSQL_ROW dbs_row; /* databases names */
	MYSQL_ROW tbs_row; /* tables names */

	gint dbs_n;

	if (mysql_query(con, "show databases"))
		connection_terminate(con);

	dbs_res = mysql_store_result(con);
	if (dbs_res == NULL)
		connection_terminate(con);

	ts = gtk_tree_store_new(NUM_COLS, G_TYPE_STRING);

	/* get count of fields from the result */
	dbs_n = mysql_num_fields(dbs_res);

	/* write first row to 'dbs' */
	while ((dbs_row = mysql_fetch_row(dbs_res))) {
		gint i;

		for (i = 0; i < dbs_n; i++) {
			gchar *cmd;

			gint tbs_n;

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

			tbs_n = mysql_num_fields(tbs_res);

			while ((tbs_row = mysql_fetch_row(tbs_res))) {
				gint j;

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

static void server_selected(GtkWidget *widget, gpointer data)
{
	(void) widget;

	GtkApplication *app;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreePath *path;

	GList *rows = NULL;
	GList *tmp = NULL;

	MYSQL *con = NULL;

	struct wrapped_data *wrap_data = data;
	struct server_data *serv_data = wrap_data->data;

	app = GTK_APPLICATION(wrap_data->object);

	selection = gtk_tree_view_get_selection(serv_data->servers_view);

	rows = gtk_tree_selection_get_selected_rows(selection, &model);
	if (rows == NULL) {
		g_print("%s[warn]%s: No selected servers.\n", COLOR_YELLOW, COLOR_DEFAULT);

		return;
	}

	for (tmp = rows; tmp; tmp = tmp->next) {
		path = tmp->data;

		if (path) {
			gint *index;

			struct server *serv;

			index = gtk_tree_path_get_indices(path);

			con = mysql_init(con);
			if (con == NULL) {
				connection_terminate(con);

				g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

				g_print("%s[err]%s: Failed to open connection!\n", COLOR_RED, COLOR_DEFAULT);

				return;
			}

			/* get data, placed by index in the servers list */
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

				g_print("%s[err]%s: Connection failed!\n", COLOR_RED, COLOR_DEFAULT);

				g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

				continue;
			}

			g_print("Successfully connected!\n");

			window_databases(app, con);
		}
	}

	g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);
}

static GtkWidget * databases_view_create(MYSQL *con)
{
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer;
	GtkWidget *view;
	GtkTreeModel *model;

	view = gtk_tree_view_new();

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
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

static void connection_error(MYSQL *con)
{
	g_print("Error %u: %s\n", mysql_errno(con), mysql_error(con));
}
