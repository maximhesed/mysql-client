#ifndef DATA_H
#define DATA_H

#include <gtk/gtk.h>
#include <mysql.h>

struct connection_data {
    GtkWidget *host;
    GtkWidget *username;
    GtkWidget *password;
    GtkListStore *store;
    int *count;
    MYSQL *con;
};

struct servers_data {
    gchar *host;
    gchar *username;
    gchar *password;
};

#endif /* DATA_H */
