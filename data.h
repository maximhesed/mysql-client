#ifndef DATA_H
#define DATA_H

#include <gtk/gtk.h>
#include <mysql.h>

struct application_data {
    GtkApplication *app;
    gpointer data;
};

struct server_data {
    GtkWidget *host;
    GtkWidget *username;
    GtkWidget *password;
    GList *servers_list; /* keep array of the server structures */
    MYSQL *con;

    /* extra */
    GtkTreeView *servers_view;
    GtkListStore *servers_store;
};

struct server {
    gchar *host;
    gchar *username;
    gchar *password;
    gchar *name;
};

struct selection_data {
    GtkTreeSelection *selection;
    MYSQL *con;
};

#endif /* DATA_H */
