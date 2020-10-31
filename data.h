#ifndef DATA_H
#define DATA_H

#include <gtk/gtk.h>
#include <mysql.h>

/* A data which will passed everywhere the auxiliary objects is needed. */
struct wrap_data {
    GObject *app;
    GObject *win;
    gpointer data;
};

struct con_data {
    GtkWidget *host;
    GtkWidget *uname;
    GtkWidget *passw;
    GList *clist; /* Keep an array of the server structures. */
    MYSQL *con;

    /* An extra. */
    GtkTreeView *cview;
    GtkListStore *cstore;
};

/* A server information structure. It needs for appending a data on a list.
 * This data will used for a connection creating, when a user wants to
 * connect to a server from a server list. */
struct con_info {
    gchar *host;
    gchar *uname;
    gchar *passw;
    gchar *name;
};

struct sel_data {
    GtkTreeSelection *sel;
    MYSQL *con;
};

struct args_data {
    const gchar *host;
    const gchar *uname;
    const gchar *passw;
};

#endif /* DATA_H */
