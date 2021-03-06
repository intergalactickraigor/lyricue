/***************************************************************************
 *            lyricue_display.c
 *
 *  Tue Jul 20 15:49:24 2010
 *  Copyright  2010  Chris Debenham
 *  <chris@adebenham.com>
 ****************************************************************************/

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
	 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#include "lyricue_display.h"

extern MYSQL *lyricDb;
extern MYSQL *mediaDb;
extern MYSQL *bibleDb;
extern gchar *bible_name;
extern gchar *bible_table;
extern gfloat stage_width;
extern gfloat stage_height;
extern gint bg_is_video;
extern ClutterActor *background;

gint blanked_state = BLANK_NONE;
gchar *default_bg = NULL;
gchar *current_bg = NULL;
gchar *temp_bg = NULL;
int current_item = 0;
int current_list = 0;
int old_list = 0;
GHashTable *config = NULL;
GHashTable *miniviews = NULL;

// Command line options
gboolean windowed = FALSE;
gboolean debugging = FALSE;
gboolean name_at_top = TRUE;
gboolean details_at_top = FALSE;
gboolean info_on_all_pages = TRUE;
int server_port = 2346;
gchar *server_ip = "";
gchar *server_type = "normal";
int server_mode = NORMAL_SERVER;
gchar *dbhostname = "localhost";
gchar *geometry = NULL;
gchar *profile = NULL;
gchar *extra_data = NULL;
unsigned long windowid = 0;
gchar hostname[32];
gchar ipaddr[16] = "127.0.0.1\0";
guint tracker_timeout = 0;


static GOptionEntry entries[] = {
    {"type", 't', 0, G_OPTION_ARG_STRING, &server_type, "Server type",
     "[normal | preview | miniview | simple | headless ] "},
    {"profile", 'p', 0, G_OPTION_ARG_STRING, &profile, "Profile",
     "profile_name"},
    {"remote", 'r', 0, G_OPTION_ARG_STRING, &dbhostname, "Database hostname",
     "hostname"},
    {"geometry", 'g', 0, G_OPTION_ARG_STRING, &geometry, "Window Geometry",
     "geom"},
    {"listen", 'l', 0, G_OPTION_ARG_INT, &server_port, "Port to listen on",
     "port_number"},
    {"ipaddr", 'i', 0, G_OPTION_ARG_STRING, &server_ip, "IP to listen on",
     "ip_number"},
    {"miniview", 'm', 0, G_OPTION_ARG_INT, &windowid, "Embed in windowid",
     "windowid"},
    {"extra", 'e', 0, G_OPTION_ARG_STRING, &extra_data, "Extra data to report to avahi",
     "data"},
    {"window", 'w', 0, G_OPTION_ARG_NONE, &windowed, "Run in a window", NULL},
    {"debug", 'd', 0, G_OPTION_ARG_NONE, &debugging, "Enable debug output",
     NULL},
    {NULL}
};


int
main (int argc, char *argv[])
{
    unsetenv ("LIBGL_ALWAYS_INDIRECT");
    setenv ("CLUTTER_DISABLE_MIPMAPPED_TEXT", "1", 0);
    bindtextdomain (GETTEXT_PACKAGE, NULL);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    srand(time(NULL));

    g_type_init ();
    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new ("- Lyricue display");
    g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
    g_option_context_set_ignore_unknown_options (context, TRUE);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }
    int ret = db_select ();
    if (ret) {
        // Always true
    }
    gethostname(hostname,sizeof(hostname));

    if (g_strcmp0(server_ip,"") != 0 ) {
        g_snprintf(ipaddr,sizeof(ipaddr),"%s",server_ip);
    } else {
    
        // Find the external IP address (by finding default route)
        FILE *f;
        char line[100] , *p , *c;

        f = fopen("/proc/net/route" , "r");
        while(fgets(line , 100 , f)) {
            p = strtok(line , " \t");
            c = strtok(NULL , " \t");

            if(p!=NULL && c!=NULL) {
                if(strcmp(c , "00000000") == 0) {
                    //printf("Default interface is : %s \n" , p);
                    break;
                }
            }
        }

        int fm = AF_INET;
        struct ifaddrs *ifaddr, *ifa;
        int family , s;

        if (getifaddrs(&ifaddr) == -1) {
            perror("getifaddrs");
            exit(EXIT_FAILURE);
        }
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) {
                continue;
            }
    
            family = ifa->ifa_addr->sa_family;
    
            if(strcmp( ifa->ifa_name , p) == 0) {
                if (family == fm) {
                    s = getnameinfo( ifa->ifa_addr, (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6) , ipaddr, sizeof(ipaddr), NULL , 0 , NI_NUMERICHOST);
    
                    if (s != 0) {
                        printf("getnameinfo() failed: %s\n", gai_strerror(s));
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        freeifaddrs(ifaddr);
    }

    do_query(FALSE, lyricDb, "SELECT profile FROM profiles WHERE host='%s'", hostname);
    MYSQL_ROW row;
    MYSQL_RES *result;
    result = mysql_store_result (lyricDb);
    row = mysql_fetch_row (result);
    if (row != NULL) {
        if (profile == NULL) {
            profile=g_strdup(row[0]);
        }
    } else {
        if (profile == NULL) {
            profile = g_strdup("Default");
        }
        do_query(FALSE, lyricDb, "INSERT INTO profiles (host, profile) VALUES ('%s', '%s')", hostname, profile);
    }
    load_configuration (lyricDb);
    bible_load ((gchar *) g_hash_table_lookup (config, "DefBible"));


    // Setup network
    GSocketService *service = g_socket_service_new ();
    GInetAddress *address = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);

    if (!g_socket_listener_add_inet_port
        (G_SOCKET_LISTENER (service), server_port, NULL, NULL)) {
        l_debug ("Unable to listen on port %d", server_port);
        server_port = g_socket_listener_add_any_inet_port
                        (G_SOCKET_LISTENER (service), NULL, NULL);
        if (server_port == 0) {
            return EXIT_FAILURE;
        }
    }
    l_debug("Listening on port %d", server_port);
    g_object_unref (address);
    g_socket_service_start (service);
    g_signal_connect (service, "incoming", G_CALLBACK (new_connection), NULL);

    if (g_strcmp0(server_type, "headless") != 0) {
        ret = create_main_window (argc, argv);
    }

    gchar *showinfochar;
    showinfochar = (gchar *) g_hash_table_lookup (config, "ShowInfoLocation");
    if (showinfochar != NULL) {
        if (g_strcmp0(showinfochar, "top") == 0){
            name_at_top = TRUE;
            details_at_top = TRUE;
        } else if (g_strcmp0(showinfochar, "split") == 0){
            name_at_top = TRUE;
            details_at_top = FALSE;
        } else if (g_strcmp0(showinfochar, "bottom") == 0){
            name_at_top = FALSE;
            details_at_top = FALSE;
        }
    }
    g_free(showinfochar);

    gchar *infopageschar;
    infopageschar = (gchar *) g_hash_table_lookup (config, "ShowInfoPages");
    if (infopageschar != NULL && g_strcmp0(infopageschar, "all") == 0) {
        info_on_all_pages = TRUE;
    } else {
        info_on_all_pages = FALSE;
    }
    g_free(infopageschar);


    // Publish to avahi (zeroconf/bonjour)
    publish_avahi(server_port, server_type);
    if (g_strcmp0(server_type, "normal") == 0) {
        server_mode=NORMAL_SERVER;
    } else if (g_strcmp0(server_type, "preview") == 0) {
        server_mode=PREVIEW_SERVER;
    } else if (g_strcmp0(server_type, "miniview") == 0) {
        server_mode=MINIVIEW_SERVER;
    } else if (g_strcmp0(server_type, "simple") == 0) {
        server_mode=SIMPLE_SERVER;
    } else if (g_strcmp0(server_type, "headless") == 0) {
        server_mode=HEADLESS_SERVER;
    }

    // Create tracker update timeout
    tracker_timeout = g_timeout_add_seconds (1, (GSourceFunc) update_tracker,
                                         NULL);


    // Setup tracker entry in DB
    do_query (FALSE, lyricDb, "DELETE FROM status WHERE host='%s:%d'",hostname, server_port);
    do_query (FALSE, lyricDb, "INSERT INTO status SET host='%s:%d',ref=0,title='', profile='%s', type='%s#%s', ip='%s'",hostname, server_port, profile, server_type, extra_data, ipaddr);
    if (server_mode==NORMAL_SERVER) {
        clutter_main ();
    } else {
        gtk_main ();
    }

    do_query (FALSE, lyricDb,
              "UPDATE status SET lastupdate = 0 WHERE host=\"%s:%d\"",
              hostname, server_port);
    unpublish_avahi();

    ret = db_deselect ();

    l_debug("Exiting");
    return EXIT_SUCCESS;
}

gboolean
network_read (GIOChannel * source, GIOCondition cond, gpointer data)
{
    GString *s = g_string_new (NULL);
    GError *error = NULL;
    GIOStatus ret = g_io_channel_read_line_string (source, s, NULL, &error);

    if (ret == G_IO_STATUS_EOF) {
        l_debug ("eof");
        return FALSE;
    } else if (ret == G_IO_STATUS_ERROR) {
        g_warning ("Error reading: %s\n", error->message);
        // Drop last reference on connection
        g_object_unref (data);
        // Remove the event source
        return FALSE;
    } else {
        s->str = g_strstrip (s->str);
        if (g_utf8_strlen (s->str, -1) > 0) {
            handle_command (source, s->str);
        }
        g_io_channel_shutdown (source, TRUE, NULL);
        g_object_unref (data);
        return FALSE;
    }

    return TRUE;
}

gboolean
new_connection (GSocketService * service,
                GSocketConnection * connection,
                GObject * source_object, gpointer user_data)
{
    GSocketAddress *sockaddr =
      g_socket_connection_get_remote_address (connection, NULL);
    GInetAddress *addr =
      g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (sockaddr));
    guint16 port =
      g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (sockaddr));

    l_debug ("New Connection from %s:%d", g_inet_address_to_string (addr),
             port);

    g_object_ref (connection);
    GSocket *socket = g_socket_connection_get_socket (connection);

    gint fd = g_socket_get_fd (socket);
    GIOChannel *channel = g_io_channel_unix_new (fd);
    g_io_add_watch (channel, G_IO_IN, (GIOFunc) network_read, connection);
    return TRUE;
}

void
handle_command (GIOChannel * source, const char *command)
{
    l_debug ("Received: %s", command);
    GString *returnstring = NULL;
    gchar **line = g_strsplit (command, ":", 2);
    update_miniview (command);
    if (line[1] != NULL) {
        line[0] = g_utf8_strdown (line[0], -1);
        if (g_strcmp0 (line[0], "preview") == 0) {
            do_preview (line[1]);
        } else if (g_strcmp0 (line[0], "status") == 0) {
            returnstring = do_status ();
        } else if (g_strcmp0 (line[0], "dbsnapshot") == 0) {
            do_dbsnapshot (line[1]);
        } else if (g_strcmp0 (line[0], "plsnapshot") == 0) {
            do_plsnapshot (line[1]);
            returnstring = g_string_new("done");
        } else if (g_strcmp0 (line[0], "snapshot") == 0) {
            returnstring = do_snapshot (line[1]);
        } else if (g_strcmp0 (line[0], "reconfig") == 0) {
            do_reconfig ();
        } else if (g_strcmp0 (line[0], "backdrop") == 0) {
            do_backdrop (line[1]);
        } else if (g_strcmp0 (line[0], "blank") == 0) {
            do_blank (line[1]);
        } else if (g_strcmp0 (line[0], "change_to_db") == 0) {
            do_change_to_db (line[1]);
        } else if (g_strcmp0 (line[0], "next_point") == 0) {
            do_next_point (line[1]);
        } else if (g_strcmp0 (line[0], "get") == 0) {
            returnstring = do_get (line[1]);
        } else if (g_strcmp0 (line[0], "display") == 0) {
            do_display (line[1],FALSE);
        } else if (g_strcmp0 (line[0], "osd") == 0) {
            do_osd (line[1]);
        } else if (g_strcmp0 (line[0], "media") == 0) {
            do_media (line[1]);
        } else if (g_strcmp0 (line[0], "fade") == 0) {
            do_fade (line[1]);
        } else if (g_strcmp0 (line[0], "blur") == 0) {
            do_blur (line[1]);
        } else if (g_strcmp0 (line[0], "save") == 0) {
            do_save (line[1]);
        } else if (g_strcmp0 (line[0], "query") == 0) {
            returnstring = do_query_json (line[1]);
        } else if (g_strcmp0 (line[0], "bible") == 0) {
            returnstring = do_bible(line[1]);
        } else if (g_strcmp0 (line[0], "profile") == 0) {
            do_profile_change(line[1]);
        }
    }
    g_strfreev (line);
    if (returnstring != NULL) {
        if ((returnstring->len > 1024) && (g_strrstr_len(returnstring->str, returnstring->len, " ") == NULL)){
            l_debug ("The status message sent is (%d chars): [uuencoded data]", returnstring->len);
        } else {
            l_debug ("The status message sent is (%d chars): %s", returnstring->len, returnstring->str);
        }
        gsize bytes_written = 0;
        GError *err = NULL;
        GIOStatus res = g_io_channel_write_chars (source, returnstring->str,
                                                  returnstring->len, &bytes_written, &err);
        if (res) {
            // Ignore it
        }
        gsize total = bytes_written;
        // Repeat until all sent - needed since socket is non-blocking
        while (bytes_written < returnstring->len) {
            g_string_erase(returnstring, 0, bytes_written);
            res = g_io_channel_write_chars (source, returnstring->str,
                                                  returnstring->len, &bytes_written, &err);
            total = total + bytes_written;
        }
        l_debug("Sent %d chars", total);
        if (err != NULL) {
            l_debug("Error: %s",err->message);
            g_error_free(err);
        }
        g_string_free (returnstring, TRUE);

        /* force flushing of the write buffer */
        if (source !=NULL) {
            res = g_io_channel_flush (source, NULL);
        }
    }
    update_tracker ();
}

void
do_media (const char *options)
{
    if (bg_is_video) {
        gchar **line = g_strsplit (options, ":", 2);
        if (g_ascii_strncasecmp (line[0], "pause", 5) == 0) {
            media_pause ();
        } else if (g_ascii_strncasecmp (line[0], "skip", 4) == 0) {
            media_skip (atoi (line[1]));
        }
        g_strfreev (line);
    }
}

void
do_fade (const char *options)
{
    fade_backdrop (atoi (options));
}

void
do_blur (const char *options)
{
    blur_backdrop (atoi (options));
}

void
do_preview (const char *options)
{
    gchar **line = g_strsplit (options, ":", 2);
    gboolean wrap = TRUE;
    unblank ();
    if (g_strcmp0 (line[0], "ignore") != 0) {
        gchar **extras = g_strsplit (parse_special (line[0]), "\n", 4);
        if ((g_strv_length (extras) == 6)
            && (g_strcmp0 (extras[3], "nowrap") == 0)) {
            wrap = FALSE;
        }
        set_headtext (parse_special (extras[0]), 0, 1);

        if (g_strv_length (extras) >= 3) {
            GString *footer = g_string_new (NULL);

            if (g_utf8_strlen (extras[2], 10) != 0) {
                g_string_printf (footer, "%s %s - %s",
                                 gettext ("Written by "), extras[1],
                                 extras[2]);
            } else {
                if (g_utf8_strlen (extras[1], 10) != 0) {
                    g_string_printf (footer, "%s %s",
                                     gettext ("Written by "), extras[1]);
                } else {
                    g_string_assign (footer, "");
                }
            }
            set_foottext (footer->str, 0, 1);
            g_string_free (footer, TRUE);
        }
        g_strfreev (extras);

    }
    set_maintext (parse_special (line[1]), 0, wrap);
    g_strfreev (line);
}

GString *
do_status ()
{
    l_debug ("Return status");
    GString *ret = g_string_new (NULL);
    g_string_printf (ret, "Status,W:%.0f,H:%.0f,F:%s,T:%s,B:%s\n", stage_width,
                     stage_height, (gchar *) g_hash_table_lookup (config,
                                                                  "Main"),
                     bible_table, bible_name);
    return ret;
}

GString *
do_snapshot (const char *options)
{
    l_debug ("do_snapshot");
    gchar **line = g_strsplit (options, ":", 2);
    int width=0;
    if (line[1] != NULL) {
        width=atoi(line[1]);
    }
    GString *ret = take_snapshot (line[0], width);
    g_strfreev (line);
    return ret;
}

void
do_dbsnapshot (const char *options)
{
    l_debug ("do_snapshot");
    gchar **line = g_strsplit (options, ":", 2);
    take_dbsnapshot (atoi(line[0]));
    g_strfreev (line);
}

void
do_plsnapshot (const char *options)
{
    l_debug ("do_snapshot");
    gchar **line = g_strsplit (options, ":", 2);
    playlist_snapshot(atoi(line[0]));
    g_strfreev (line);
}

void
do_reconfig ()
{
    l_debug ("do_reconfig");
    load_configuration (lyricDb);
}

void
do_backdrop (const char *options)
{
    l_debug ("do_backdrop: %s", options);
    if (server_mode != SIMPLE_SERVER) {
        gchar **line = g_strsplit (options, ":", 2);
        temp_bg = NULL;
        default_bg = parse_special (line[0]);
        change_backdrop (default_bg, TRUE, DEFAULT);
        g_strfreev (line);
    } else {
        if (background != NULL) destroy_actor(background);
    }
}

void
unblank ()
{
    if (blanked_state == BLANK_BG) {
        change_backdrop (temp_bg, TRUE, DEFAULT);
    }
    blanked_state = BLANK_NONE;
}

void
do_blank (const char *options)
{
    l_debug ("do_blank: %s", options);
    gchar **line = g_strsplit (options, ":", 2);
    if (strlen (options) <= 1) {
        options = NULL;
    }

    if (blanked_state == BLANK_BG) {
        l_debug ("Re-show text");
        do_display ("current",FALSE);
    } else if ((blanked_state == BLANK_TEXT) && options != NULL) {
        l_debug ("clear text and set BG");
        temp_bg = current_bg;
        change_backdrop (line[0], TRUE, DEFAULT);
        blanked_state = BLANK_BG;
    } else if ((blanked_state == BLANK_TEXT) && options == NULL) {
        l_debug ("Re-show text - 2");
        do_display ("current",FALSE);
    } else if (options != NULL) {
        l_debug ("clear text and set BG - 2");
        temp_bg = current_bg;
        change_backdrop (line[0], TRUE, DEFAULT);
        set_maintext ("", 0, FALSE);
        set_headtext ("", 0, FALSE);
        set_foottext ("", 0, FALSE);
        blanked_state = BLANK_BG;
    } else {
        l_debug ("Clear text");
        set_maintext ("", 0, FALSE);
        set_headtext ("", 0, FALSE);
        set_foottext ("", 0, FALSE);
        blanked_state = BLANK_TEXT;
    }
    g_strfreev (line);
}

void
do_change_to_db (const char *options)
{
    l_debug ("do_change_to_db: %s", options);
    bible_load (options);
}

void
do_next_point (const char *options)
{
    l_debug ("do_next_point not implemented");
}

void
do_osd (const char *options)
{
    l_debug ("do_osd");
    if (options != NULL) {
        gchar **line = g_strsplit (options, ":", 2);
        int speed = 10000;
        if (g_strcmp0 (line[0], "slow") == 0) {
            speed = 20000;
        } else if (g_strcmp0 (line[0], "fast") == 0) {
            speed = 5000;
        } else if (g_strcmp0 (line[0], "default") == 0) {
            speed = 10000;
        } else {
            speed = atoi (line[0]);
        }
        gchar *text = parse_special (line[1]);
        set_osd (speed, text);
    }
}

void
do_display (const char *options, const int quick_show)
{
    l_debug ("do_display");
    if (options != NULL) {
        gchar **line = g_strsplit (options, ":", 2);
        unblank ();
        MYSQL_ROW row;
        MYSQL_RES *result;
        gboolean bg_changed = FALSE;
        do_query (FALSE, lyricDb, "SELECT playlist FROM playlist WHERE playorder=%d",
                  current_item);
        result = mysql_store_result (lyricDb);
        row = mysql_fetch_row (result);
        if (row != NULL) {
            current_list = atoi (row[0]);
        }
        mysql_free_result (result);

        gchar *command = g_utf8_strdown (line[0], -1);

        if (g_strcmp0 (command, "playlist") == 0) {
            current_list = atoi (line[1]);

        } else if (g_strcmp0 (command, "current") == 0) {
            // Clear text and then redisplay same page
            set_maintext ("", 0, FALSE);
            set_headtext ("", 0, FALSE);
            set_foottext ("", 0, FALSE);
            if (g_strcmp0 (line[1], "nobg") == 0) {
                bg_changed = TRUE;
            }
        } else if (g_strcmp0 (command, "next_page") == 0) {
            do_query (FALSE, lyricDb,
                      "SELECT MIN(playorder) FROM playlist WHERE playlist=%d AND playorder > %d ORDER BY playorder",
                      current_list, current_item);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            mysql_free_result (result);
            if (row[0]) {
                // Show next page
                current_item = atoi (row[0]);
            } else {
                // End of song reached
                gchar **loop = g_strsplit (line[1], ";", 2);
                if (g_strcmp0 (loop[0], "loop") == 0) {
                    // Looping
                    int loop_parent = 0;
                    if (loop[1] != NULL) {
                        loop_parent = atoi (loop[1]);
                    }
                    if (loop_parent == 0) {
                        l_debug ("Looping a song, back to page 1");
                        do_query (FALSE, lyricDb,
                                  "SELECT MIN(playorder) FROM playlist WHERE playlist=%d",
                                  current_list);
                        result = mysql_store_result (lyricDb);
                        row = mysql_fetch_row (result);
                        mysql_free_result (result);
                        if (row[0] != NULL) {
                            current_item = atoi (row[0]);
                        }
                    } else {
                        l_debug ("Looping a sublist");
                        do_query (FALSE, lyricDb,
                                  "SELECT MIN(p1.playorder) FROM playlist AS p1, playlist AS p2 WHERE p1.playorder>p2.playorder AND p2.type='play' AND p2.data=%d AND p1.playlist=%d",
                                  current_list, loop_parent);
                        result = mysql_store_result (lyricDb);
                        row = mysql_fetch_row (result);
                        mysql_free_result (result);
                        if (row[0] != NULL) {
                            current_item = atoi (row[0]);
                        } else {
                            // Loop back to top of parent
                            do_query (FALSE, lyricDb,
                                      "SELECT MIN(playorder) FROM playlist WHERE playlist=%d",
                                      loop_parent);
                            result = mysql_store_result (lyricDb);
                            row = mysql_fetch_row (result);
                            mysql_free_result (result);
                            if (row[0] != NULL) {
                                current_item = atoi (row[0]);
                            }
                        }
                    }
                } else {
                    // Jump to next song
                    do_display ("next_song:0",FALSE);
                    return;
                }
            }

        } else if (g_strcmp0 (command, "prev_page") == 0) {
            do_query (FALSE, lyricDb,
                      "SELECT MAX(playorder) FROM playlist WHERE playlist=%d AND playorder < %d ORDER BY playorder",
                      current_list, current_item);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            mysql_free_result (result);
            if (row[0]) {
                current_item = atoi (row[0]);
            } else {
                if (g_strcmp0 (line[1], "loop") == 0) {
                    // Loop back to end of playlist
                    do_query (FALSE, lyricDb,
                              "SELECT MAX(playorder) FROM playlist WHERE playlist=%d",
                              current_list);
                    result = mysql_store_result (lyricDb);
                    row = mysql_fetch_row (result);
                    mysql_free_result (result);
                    if (row[0] != NULL) {
                        current_item = atoi (row[0]);
                    }
                } else {
                    // Jump back to last page of previous song
                    do_query(FALSE, lyricDb,
                             "SELECT MAX(playorder) FROM playlist "
                             "WHERE playlist="
                             "(SELECT data FROM playlist "
                             " WHERE playorder= "
                             "  (SELECT MAX(playorder) FROM playlist "
                             "   WHERE playorder < "
                             "   (SELECT playorder FROM playlist "
                             "    WHERE type='play' AND data=%d) "
                             "    AND playlist = "
                             "    (SELECT playlist FROM playlist "
                             "     WHERE type='play' AND data=%d)))",
                             current_list,
                             current_list);
                    result = mysql_store_result (lyricDb);
                    row = mysql_fetch_row (result);
                    mysql_free_result (result);
                    if (row[0] != NULL) {
                        current_item = atoi (row[0]);
                    }
                }
            }

        } else if (g_strcmp0 (command, "next_song") == 0) {
            do_query (FALSE, lyricDb,
                      "SELECT a.playorder,a.playlist FROM playlist AS a, playlist AS b WHERE a.data=b.playlist AND a.type=\"play\" AND b.playorder=%d",
                      current_item);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            mysql_free_result (result);

            if (row && (row[0] != NULL)) {
                current_item = atoi (row[0]);
                current_list = atoi (row[1]);
            }
            do_query (FALSE, lyricDb,
                      "SELECT MIN(playorder) FROM playlist WHERE playorder > %d AND playlist=%d",
                      current_item, current_list);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            if (row[0] != NULL) {
                current_item = atoi (row[0]);
            }
            mysql_free_result (result);

        } else if (g_strcmp0 (command, "prev_song") == 0) {
            do_query (FALSE, lyricDb,
                      "SELECT a.playorder,a.playlist FROM playlist AS a, playlist AS b WHERE a.data=b.playlist AND a.type=\"play\" AND b.playorder=%d",
                      current_item);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            mysql_free_result (result);
            if (row && (row[0] != NULL)) {
                current_item = atoi (row[0]);
                current_list = atoi (row[1]);
            }
            do_query (FALSE, lyricDb,
                      "SELECT MAX(playorder) FROM playlist WHERE playorder < %d AND playlist=%d",
                      current_item, current_list);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            mysql_free_result (result);
            if (row[0] != NULL) {
                current_item = atoi (row[0]);
            }

        } else if (g_strcmp0 (command, "page") == 0) {
            do_query (FALSE, lyricDb,
                      "SELECT playorder FROM playlist WHERE playlist=%d",
                      current_list);
            result = mysql_store_result (lyricDb);
            int count = 0;
            while ((count < atoi (line[1]))
                   && (row = mysql_fetch_row (result))) {
                count++;
            }
            if (row && (row[0] != NULL)) {
                current_item = atoi (row[0]);
            }
        } else {
            current_item = atoi (command);
            do_query (FALSE, lyricDb, "SELECT playlist FROM playlist WHERE playorder=%d",
                      current_item);
            result = mysql_store_result (lyricDb);
            row = mysql_fetch_row (result);
            if (row != NULL) {
                current_list = atoi (row[0]);
            }
            mysql_free_result (result);
        }

        do_query (FALSE, lyricDb,
                  "SELECT type,data,transition FROM playlist WHERE playorder=%d",
                  current_item);
        result = mysql_store_result (lyricDb);
        row = mysql_fetch_row (result);
        if (row != NULL) {
            gchar *type = g_strdup (row[0]);
            gchar *data = g_strdup (row[1]);
            gchar *lyrics = "";
            gchar *header = "";
            gchar *footer = "";
            gboolean wrap = TRUE;
            int transition = atoi (row[2]);
            if (quick_show) {
                transition = NO_EFFECT;
            }

            if (g_strcmp0 (type, "back") == 0) {
                default_bg = g_strdup (data);
                if (server_mode == NORMAL_SERVER) {
                    change_backdrop (default_bg, TRUE, transition);
                    bg_changed = TRUE;
                }
                g_strfreev (line);
            } else if (g_strcmp0 (type, "file") == 0) {
                change_backdrop (data, FALSE, transition);
                bg_changed = TRUE;
                g_strfreev (line);
            } else if (g_strcmp0 (type, "imag") == 0) {
                change_backdrop (data, FALSE, transition);
                bg_changed = TRUE;
            } else if (g_strcmp0 (type, "vers") == 0) {
                do_query (FALSE, lyricDb,
                          "SELECT title FROM playlist,playlists WHERE playlist.playlist=playlists.id AND playorder=%d",
                          current_item);
                result = mysql_store_result (lyricDb);
                row = mysql_fetch_row (result);
                gchar **selected = g_strsplit (data, ":", 2);
                gchar *verse = NULL;
                if (selected[1] == NULL) {
                    selected = g_strsplit (row[0], ":", 3);
                    gchar **datasplt = g_strsplit (data, "-", 2);
                    GString *verseref = g_string_new (NULL);
                    g_string_printf (verseref, "%s:%s:%s-%s:%s", selected[0],
                                     selected[1], datasplt[0], selected[1],
                                     datasplt[1]);
                    verse = do_grab_verse (g_string_free (verseref, FALSE));
                } else {
                    // Broken
                }

                if (verse != NULL) {
                    lyrics = g_strdup (verse);
                }
                header = g_strdup (row[0]);
                footer = g_strdup (bible_name);
                wrap = TRUE;
            } else if ((g_strcmp0 (type, "play") == 0) ||
                       (g_strcmp0 (type, "sub") == 0)) {
                do_query (FALSE, lyricDb,
                          "SELECT playorder FROM playlist WHERE playlist=%s ORDER BY playorder",
                          data);
                result = mysql_store_result (lyricDb);
                row = mysql_fetch_row (result);
                do_display (row[0],FALSE);
                return;
            } else {            // Song page
                do_query (FALSE, lyricDb,
                          "SELECT title,artist,lyrics,copyright FROM lyricMain AS l, page AS pa WHERE (pa.songid=l.id OR pa.songid=-l.id) AND pa.pageid=%s",
                          data);
                result = mysql_store_result (lyricDb);
                row = mysql_fetch_row (result);
                mysql_free_result (result);
                if (row != NULL) {
                    gchar *title = g_strdup (row[0]);
                    gchar *artist = g_strdup (row[1]);
                    gchar *lyrictmp = g_strdup (row[2]);
                    gchar *copyright = g_strdup (row[3]);
                    GString *foot = g_string_new (NULL);
                    if (g_utf8_strlen (artist, 10) != 0) {
                        g_string_printf (foot, "Written by %s", artist);
                    }
                    if (g_utf8_strlen (copyright, 10) != 0) {
                        if (g_ascii_strncasecmp(copyright,"Preset",6) == 0) {
                            copyright=(gchar *) g_hash_table_lookup (config,copyright);
                        }
                        g_string_append_printf (foot, " - %s", copyright);
                    }
                    lyrics = g_strdup (lyrictmp);
                    header = g_strdup (title);
                    footer = g_strdup (foot->str);
                    g_string_free (foot, TRUE);
                }
            }

            // Look for associated background image
            if (!bg_changed) {
                if (server_mode == SIMPLE_SERVER) {
                    if (background != NULL) clutter_actor_destroy(background);
                } else {
                    int res = do_query (FALSE, lyricDb,
                                        "SELECT imagename FROM associations WHERE playlist=%d",
                                        current_item);
                    int bg_changed = FALSE;
                    if (res == 0) {
                        result = mysql_store_result (lyricDb);
                        row = mysql_fetch_row (result);
                        if (row != NULL) {
                            change_backdrop (row[0], TRUE, transition);
                            bg_changed = TRUE;
                        }
                        mysql_free_result (result);
                    }
                    if (!bg_changed) {
                        res =
                          do_query (FALSE, lyricDb,
                                    "SELECT a.imagename,q.data FROM associations as a, playlist AS p, playlist AS q WHERE p.type='play' AND p.data=q.playlist and a.playlist=p.playorder AND q.playorder=%d",
                                    current_item);
                        if (res == 0) {
                            result = mysql_store_result (lyricDb);
                            row = mysql_fetch_row (result);
                            mysql_free_result (result);
                            if (row != NULL) {
                                change_backdrop (row[0], TRUE, transition);
                                bg_changed = TRUE;
                            }
                        }
                    }
                    if (!bg_changed && (g_strcmp0 (default_bg, current_bg) != 0)) {
                        l_debug ("Reset bg to default");
                        change_backdrop (default_bg, TRUE, transition);
                    }
                }
            }

            if (quick_show) {
                transition = NO_EFFECT;
            }
            set_maintext (parse_special (lyrics), transition, wrap);
            if (info_on_all_pages || (current_list != old_list)) {
                if (name_at_top && details_at_top) {
                    if (g_strcmp0(footer,"") != 0) {
                        header = g_strconcat(header,"\n<small><small>",footer,"</small></small>",NULL);
                    }
                    set_headtext (parse_special (header), transition, wrap);
                    set_foottext ("", transition, wrap);
                } else if (!name_at_top && !details_at_top) {
                    if (g_strcmp0(footer,"") != 0) {
                        footer = g_strconcat(header,"\n<small><small>",footer,"</small></small>",NULL);
                    }
                    set_headtext ("", transition, wrap);
                    set_foottext (parse_special (footer), transition, wrap);
                } else {
                    set_headtext (parse_special (header), transition, wrap);
                    set_foottext (parse_special (footer), transition, wrap);
                }
            } else {
                set_headtext ("", transition, wrap);
                set_foottext ("", transition, wrap);
            }
            old_list=current_list;
        }
    }
}

gboolean
update_tracker ()
{
    //l_debug ("Updating tracker");

    GString *title = g_string_new (NULL);
    if (blanked_state == BLANK_BG) {
        g_string_assign (title, "blank_bg");
    } else if (blanked_state == BLANK_TEXT) {
        g_string_assign (title, "blank_text");
    }
    if (bg_is_video) {
        g_string_append_printf (title, "%.0f;%.0f;%d",
                                clutter_media_get_progress (CLUTTER_MEDIA
                                                            (background))
                                *
                                clutter_media_get_duration (CLUTTER_MEDIA
                                                            (background)),
                                clutter_media_get_duration (CLUTTER_MEDIA
                                                            (background)),
                                clutter_media_get_playing (CLUTTER_MEDIA
                                                           (background)));
    } else {
        g_string_append (title, "0;0;0");
    }
    do_query (TRUE, lyricDb,
              "UPDATE status SET ref = %d, title = \"%s\", lastupdate = NOW() WHERE host=\"%s:%d\"",
              current_item, g_string_free (title, FALSE), hostname, server_port);
    return TRUE;
}

void
update_miniview (const char *command)
{
    if (server_mode == NORMAL_SERVER || server_mode==HEADLESS_SERVER) {
        l_debug ("miniview time");
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, miniviews);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            l_debug("Updating miniview %s on %s",key,value);
            GSocketClient *client = g_socket_client_new ();
            GSocketConnection *conn =
              g_socket_client_connect_to_host (client, value, 2348, NULL,
                                               NULL);
            if (conn != NULL) {
                GOutputStream *out =
                  g_io_stream_get_output_stream (G_IO_STREAM (conn));
                g_output_stream_write (out, command, strlen (command), NULL,
                                       NULL);
                g_object_unref (conn);
            }
            g_object_unref (client);
        }
    }
}

void
do_save (const char *options)
{
    l_debug ("Save as presentation");
    gchar **line = g_strsplit (options, ":", 2);
    gchar *cmd = g_strdup_printf ("playlist:%d", atoi (line[0]));
    do_display (cmd,TRUE);
    do_display ("display:0",TRUE);
    do_display ("next_page:0",TRUE);
    g_free (cmd);
    int count = 1;
    int last_item = -1;
    while (last_item < current_item) {
        l_debug ("%d", current_item);
        gchar *filename = g_strdup_printf ("%s/slide-%d.jpg", line[1], count);
        last_item = current_item;
        do_display ("next_page:0",TRUE);
        take_snapshot (filename,0);
        g_free (filename);
        count++;
    }
    g_strfreev (line);
}

GString *
do_query_json (const char *options)
{
    l_debug ("Query with response as JSON");
    gchar **line = g_strsplit (options, ":", 2);
    if (line[1] != NULL) {
        line[0] = g_utf8_strdown (line[0], -1);
        MYSQL_ROW row;
        MYSQL_RES *result;

        if (g_strcmp0(line[0],"lyricdb") == 0) {
            do_query (FALSE, lyricDb,"%s",parse_special(line[1]));
            result = mysql_store_result (lyricDb);
        } else if (g_strcmp0(line[0],"mediadb") == 0) {
            do_query (FALSE, mediaDb,"%s",parse_special(line[1]));
            result = mysql_store_result (mediaDb);
        } else if (g_strcmp0(line[0],"bibledb") == 0) {
            do_query (FALSE, bibleDb,"%s",parse_special(line[1]));
            result = mysql_store_result (bibleDb);
        } else {
            g_strfreev (line);
            return NULL;
        }

        if (result == NULL) return NULL;

        JsonGenerator *generator = json_generator_new();
        JsonNode *rootnode = json_node_new(JSON_NODE_OBJECT);

        JsonObject *resultnode = json_object_new();

        JsonArray *resultarray = json_array_new();

    
        unsigned int num_fields = mysql_num_fields(result);
        MYSQL_FIELD *fields;
        fields = mysql_fetch_fields(result);

        unsigned int i;
        while ((row = mysql_fetch_row (result))) {
            JsonObject *newobject = json_object_new();
            for(i = 0; i < num_fields; i++) {
                json_object_set_string_member(newobject,fields[i].name,row[i] ? row[i] : "NULL");
            }
            json_array_add_object_element(resultarray, newobject);
        }
        mysql_free_result (result);
        json_object_set_array_member(resultnode, "results",resultarray);
        json_node_set_object(rootnode,resultnode);
        json_generator_set_root(generator, rootnode);

        gchar *str = json_generator_to_data (generator,NULL);

        GString *ret = g_string_new(str);
        json_array_unref (resultarray);
        g_object_unref (generator);
        g_strfreev (line);
        return ret;
    }
    
    g_strfreev (line);
    return NULL;
}

void
do_profile_change(const char *options)
{
    gchar **line = g_strsplit (options, ":", 2);
    if (line[0] != NULL) {
        profile = g_strdup(line[0]);
        load_configuration();
        update_service_profile();
    }
    
    g_strfreev (line);
}
