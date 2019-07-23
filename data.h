#ifndef DATA_H
#define DATA_H

#include <gtk/gtk.h>
#include <mysql.h>

struct connection_data {
    GtkWidget *host;
    GtkWidget *username;
    GtkWidget *password;
    GtkListStore *servers_list;
    MYSQL *con;
};

/* TODO: replace by linked list */
struct servers_data {
    gchar *host;
    gchar *username;
    gchar *password;
    gchar *s_name;
    gint *count;
};

struct selection_data {
    GtkTreeSelection *selection;
    MYSQL *con;
};

#endif /* DATA_H */
