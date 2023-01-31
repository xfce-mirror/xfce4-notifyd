/*
 *  xfce4-notifyd
 *
 *  Copyright (c) 2023 Brian Tarricone <brian@tarricone.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License ONLY.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>

#include <libxfce4util/libxfce4util.h>

#include "xfce-notify-log.h"

#define TABLE "notifications"

#define COL_ID "id"
#define COL_TIMESTAMP "timestamp"
#define COL_TZ_IDENTIFIER "tz_identifier"
#define COL_APP_ID "app_id"
#define COL_APP_NAME "app_name"
#define COL_ICON_ID "icon_id"
#define COL_SUMMARY "summary"
#define COL_BODY "body"
#define COL_ACTIONS "actions"
#define COL_EXPIRE_TIMEOUT "expire_timeout"
#define COL_IS_READ "is_read"

#define SCHEMA \
    "CREATE TABLE IF NOT EXISTS " TABLE " (" \
        COL_ID " TEXT PRIMARY KEY NOT NULL," \
        COL_TIMESTAMP " INTEGER NOT NULL," \
        COL_TZ_IDENTIFIER " TEXT NOT NULL," \
        COL_APP_ID " TEXT," \
        COL_APP_NAME " TEXT," \
        COL_ICON_ID " TEXT," \
        COL_SUMMARY" TEXT," \
        COL_BODY " TEXT," \
        COL_ACTIONS " BLOB," \
        COL_EXPIRE_TIMEOUT " INTEGER," \
        COL_IS_READ " INTEGER NOT NULL DEFAULT FALSE" \
    ") STRICT"

#define INDEX_TIMESTAMP \
    "CREATE INDEX IF NOT EXISTS idx_" TABLE "_" COL_TIMESTAMP " ON " TABLE "(" COL_TIMESTAMP " DESC)"
#define INDEX_IS_READ \
    "CREATE INDEX IF NOT EXISTS idx_" TABLE "_" COL_IS_READ " ON " TABLE "(" COL_IS_READ ")"

#define BIND_LIMIT  "limit"

#define BIND_INDEX(stmt, param_name) sqlite3_bind_parameter_index(stmt, ":" param_name)


typedef struct _XfceNotifyLog {
    GObject parent;

    /*< private >*/
    sqlite3 *db;
    sqlite3_stmt *stmt_get;
    sqlite3_stmt *stmt_get_timestamp;
    sqlite3_stmt *stmt_read;
    sqlite3_stmt *stmt_read_with_timestamp;
    sqlite3_stmt *stmt_count_unreads;
    sqlite3_stmt *stmt_count_app_ids;
    sqlite3_stmt *stmt_write;
    sqlite3_stmt *stmt_mark_read;
    sqlite3_stmt *stmt_mark_all_read;
    sqlite3_stmt *stmt_delete;
    sqlite3_stmt *stmt_delete_before;
    sqlite3_stmt *stmt_delete_all;

    GFileMonitor *monitor;
} XfceNotifyLog;

enum {
    SIG_CHANGED = 0,

    N_SIGNALS,
};


static void xfce_notify_log_initable_init(GInitableIface *iface);
static gboolean xfce_notify_log_initable_real_init(GInitable *initable,
                                                   GCancellable *cancellable,
                                                   GError **error);

static void xfce_notify_log_finalize(GObject *object);

static GFile *notify_log_dir(void);
static GError *transform_error(sqlite3 *db,
                               int errcode,
                               const gchar *message_fmt);
static gboolean prepare_statements(XfceNotifyLog *log,
                                   GError **error);
static gboolean ensure_tables(XfceNotifyLog *log,
                              GError **error);

static void db_changed_callback(void *data,
                                int what,
                                char const *database,
                                char const *table,
                                sqlite_int64 rowid);
static void db_file_changed(GFileMonitor *monitor,
                            GFile *file,
                            GFile *other_file,
                            GFileMonitorEvent event_type,
                            XfceNotifyLog *log);

static gboolean migrate_old_keyfile(XfceNotifyLog *log);


G_DEFINE_TYPE_WITH_CODE(XfceNotifyLog, xfce_notify_log, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(G_TYPE_INITABLE, xfce_notify_log_initable_init))


static guint log_signals[N_SIGNALS] = { 0, };

static void
xfce_notify_log_class_init(XfceNotifyLogClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = xfce_notify_log_finalize;

    log_signals[SIG_CHANGED] = g_signal_new("changed",
                                            XFCE_TYPE_NOTIFY_LOG,
                                            G_SIGNAL_RUN_LAST,
                                            0,
                                            NULL,
                                            NULL,
                                            g_cclosure_marshal_VOID__VOID,
                                            G_TYPE_NONE, 0);
}

static void
xfce_notify_log_initable_init(GInitableIface *iface) {
    iface->init = xfce_notify_log_initable_real_init;
}

static void
xfce_notify_log_init(XfceNotifyLog *log) {}

static gboolean
xfce_notify_log_initable_real_init(GInitable *initable, GCancellable *cancellable, GError **error) {
    XfceNotifyLog *log = XFCE_NOTIFY_LOG(initable);
    gboolean success = TRUE;
    GFile *log_dir = notify_log_dir();

    if (!g_file_query_exists(log_dir, NULL)) {
        if (G_UNLIKELY(!g_file_make_directory_with_parents(log_dir, NULL, error))) {
            success = FALSE;
        }
    } else if (G_UNLIKELY(g_file_query_file_type(log_dir, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY)) {
        if (error != NULL) {
            *error = g_error_new(G_IO_ERROR,
                                 G_IO_ERROR_NOT_DIRECTORY,
                                 _("The notification log directory (%s) is not a directory"),
                                 g_file_peek_path(log_dir));
        }
        success = FALSE;
    }

    if (G_LIKELY(success)) {
        GFile *log_file = g_file_get_child(log_dir, "log.sqlite");
        int rc = sqlite3_open(g_file_peek_path(log_file), &log->db);

        if (G_UNLIKELY(rc != SQLITE_OK)) {
            if (error != NULL) {
                *error = transform_error(log->db, rc, _("Failed to open notification log: %s"));
            }
            success = FALSE;
        } else if (G_UNLIKELY(!ensure_tables(log, error) || !prepare_statements(log, error))) {
            success = FALSE;
        } else {
            sqlite3_db_config(log->db, SQLITE_DBCONFIG_DEFENSIVE, 1, NULL);

            migrate_old_keyfile(log);

            sqlite3_update_hook(log->db,
                                db_changed_callback,
                                log);

            log->monitor = g_file_monitor_file(log_file, G_FILE_MONITOR_NONE, NULL, NULL);
            g_signal_connect(log->monitor, "changed",
                             G_CALLBACK(db_file_changed), log);
        }

        g_object_unref(log_file);
    }

    g_object_unref(log_dir);

    return success;
}

static void
xfce_notify_log_finalize(GObject *object) {
    XfceNotifyLog *log = XFCE_NOTIFY_LOG(object);

    if (log->monitor != NULL) {
        g_object_unref(log->monitor);
    }

    if (log->stmt_get != NULL) {
        sqlite3_finalize(log->stmt_get);
    }
    if (log->stmt_get_timestamp != NULL) {
        sqlite3_finalize(log->stmt_get_timestamp);
    }
    if (log->stmt_read != NULL) {
        sqlite3_finalize(log->stmt_read);
    }
    if (log->stmt_read_with_timestamp != NULL) {
        sqlite3_finalize(log->stmt_read_with_timestamp);
    }
    if (log->stmt_count_unreads != NULL) {
        sqlite3_finalize(log->stmt_count_unreads);
    }
    if (log->stmt_count_app_ids != NULL) {
        sqlite3_finalize(log->stmt_count_app_ids);
    }
    if (log->stmt_write != NULL) {
        sqlite3_finalize(log->stmt_write);
    }
    if (log->stmt_mark_read != NULL) {
        sqlite3_finalize(log->stmt_mark_read);
    }
    if (log->stmt_mark_all_read != NULL) {
        sqlite3_finalize(log->stmt_mark_all_read);
    }
    if (log->stmt_delete != NULL) {
        sqlite3_finalize(log->stmt_delete);
    }
    if (log->stmt_delete_before != NULL) {
        sqlite3_finalize(log->stmt_delete_before);
    }

    if (log->db != NULL) {
        sqlite3_close(log->db);
    }

    G_OBJECT_CLASS(xfce_notify_log_parent_class)->finalize(object);
}

static GError *
transform_error(sqlite3 *db, int errcode, const gchar *message_fmt) {
    return g_error_new(G_IO_ERROR,
                       G_IO_ERROR_FAILED,
                       message_fmt,
                       db != NULL ? sqlite3_errmsg(db) : sqlite3_errstr(errcode));

}

static sqlite3_stmt *
prepare_statement(sqlite3 *db, const gchar *sql, GError **error) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    const char *tail = NULL;

    g_return_val_if_fail(db != NULL, NULL);
    g_return_val_if_fail(sql != NULL, NULL);
    g_return_val_if_fail(error == NULL || *error == NULL, NULL);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, &tail);
    if (G_UNLIKELY(rc != SQLITE_OK)) {
        if (error != NULL) {
            gchar *fmt = g_strdup_printf(_("Failed to prepare SQL statement: %%s (%s)"), sql);
            *error = transform_error(db, rc, fmt);
            g_free(fmt);
        }
    }

    g_assert(tail == NULL || *tail == '\0');

    return stmt;
}

static gboolean
prepare_statements(XfceNotifyLog *log, GError **error) {
#define COLUMN_NAMES COL_ID ", " \
                     COL_TIMESTAMP ", " \
                     COL_TZ_IDENTIFIER ", " \
                     COL_APP_ID ", " \
                     COL_APP_NAME ", " \
                     COL_ICON_ID ", " \
                     COL_SUMMARY ", " \
                     COL_BODY ", " \
                     COL_ACTIONS ", " \
                     COL_EXPIRE_TIMEOUT ", " \
                     COL_IS_READ
    log->stmt_get = prepare_statement(log->db, "SELECT " COLUMN_NAMES " FROM " TABLE " WHERE " COL_ID " = :" COL_ID, error);

    if (G_LIKELY(log->stmt_get != NULL)) {
        log->stmt_get_timestamp = prepare_statement(log->db, "SELECT timestamp FROM " TABLE " WHERE " COL_ID " = :" COL_ID, error);
    }

    if (G_LIKELY(log->stmt_get_timestamp != NULL)) {
        log->stmt_read = prepare_statement(log->db, "SELECT " COLUMN_NAMES " FROM " TABLE " ORDER BY " COL_TIMESTAMP " DESC LIMIT :" BIND_LIMIT, error);
    }

    if (G_LIKELY(log->stmt_read != NULL)) {
        log->stmt_read_with_timestamp = prepare_statement(log->db, "SELECT " COLUMN_NAMES " FROM " TABLE " WHERE " COL_TIMESTAMP " < :" COL_TIMESTAMP " ORDER BY " COL_TIMESTAMP " DESC LIMIT :" BIND_LIMIT, error);
    }

    if (G_LIKELY(log->stmt_read_with_timestamp != NULL)) {
        log->stmt_count_unreads = prepare_statement(log->db, "SELECT COUNT(id) FROM " TABLE " WHERE " COL_IS_READ " = FALSE", error);
    }

    if (G_LIKELY(log->stmt_count_unreads != NULL)) {
        log->stmt_count_app_ids = prepare_statement(log->db, "SELECT " COL_APP_ID ", count(" COL_APP_ID ") FROM " TABLE " GROUP BY " COL_APP_ID, error);
    }

    if (G_LIKELY(log->stmt_count_app_ids != NULL)) {
        log->stmt_write = prepare_statement(log->db, "INSERT INTO " TABLE " (" COLUMN_NAMES ") VALUES ("
                                            ":" COL_ID ", "
                                            ":" COL_TIMESTAMP ", "
                                            ":" COL_TZ_IDENTIFIER ", "
                                            ":" COL_APP_ID ", "
                                            ":" COL_APP_NAME ", "
                                            ":" COL_ICON_ID ", "
                                            ":" COL_SUMMARY ", "
                                            ":" COL_BODY ", "
                                            ":" COL_ACTIONS ", "
                                            ":" COL_EXPIRE_TIMEOUT ", "
                                            ":" COL_IS_READ ")", error);
    }

    if (G_LIKELY(log->stmt_write != NULL)) {
        log->stmt_mark_read = prepare_statement(log->db, "UPDATE " TABLE " SET " COL_IS_READ " = TRUE WHERE " COL_ID " = :" COL_ID, error);
    }

    if (G_LIKELY(log->stmt_mark_read != NULL)) {
        log->stmt_mark_all_read = prepare_statement(log->db, "UPDATE " TABLE " SET " COL_IS_READ " = TRUE", error);
    }

    if (G_LIKELY(log->stmt_mark_all_read != NULL)) {
        log->stmt_delete = prepare_statement(log->db, "DELETE FROM " TABLE " WHERE " COL_ID " = :" COL_ID, error);
    }

    if (G_LIKELY(log->stmt_delete != NULL)) {
        log->stmt_delete_before = prepare_statement(log->db, "DELETE FROM " TABLE " WHERE " COL_TIMESTAMP " < :" COL_TIMESTAMP, error);
    }

    if (G_LIKELY(log->stmt_delete_before != NULL)) {
        log->stmt_delete_all = prepare_statement(log->db, "DELETE FROM " TABLE, error);
    }

    return log->stmt_delete_all != NULL;
#undef COLUMN_NAMES
}

static void
db_changed_callback(void *data, int what, char const *database, char const *table, sqlite_int64 rowid) {
    XfceNotifyLog *log = XFCE_NOTIFY_LOG(data);
    g_signal_emit(log, log_signals[SIG_CHANGED], 0, NULL);
}

static void
db_file_changed(GFileMonitor *monitor,
                GFile *file,
                GFile *other_file,
                GFileMonitorEvent event_type,
                XfceNotifyLog *log)
{
    g_signal_emit(log, log_signals[SIG_CHANGED], 0, NULL);
}

static gboolean
stmt_run_oneshot(XfceNotifyLog *log, const gchar *sql, const gchar *error_message_fmt, GError **error) {
    gboolean success = FALSE;
    sqlite3_stmt *stmt;

    stmt = prepare_statement(log->db, sql, error);
    if (G_LIKELY(stmt != NULL)) {
        int rc = sqlite3_step(stmt);
        if (G_LIKELY(rc == SQLITE_DONE)) {
            success = TRUE;
        } else {
            if (error != NULL) {
                *error = transform_error(log->db, rc, error_message_fmt);
            }
        }

        sqlite3_finalize(stmt);
    }

    return success;
}

static gboolean
ensure_tables(XfceNotifyLog *log, GError **error) {
    return stmt_run_oneshot(log, SCHEMA, _("Failed to create 'notifications' table: %s"), error)
        && stmt_run_oneshot(log, INDEX_TIMESTAMP, _("Failed to create DB timestamp index: %s"), error)
        && stmt_run_oneshot(log, INDEX_IS_READ, _("Failed to create DB is_read index: %s"), error);
}

static GFile *
notify_log_dir(void) {
    gchar *path = g_strconcat(g_get_user_cache_dir(), G_DIR_SEPARATOR_S,
                              "xfce4", G_DIR_SEPARATOR_S,
                              "notifyd",
                              NULL);
    GFile *file = g_file_new_for_path(path);
    g_free(path);
    return file;
}

XfceNotifyLog *
xfce_notify_log_open(GError **error) {
    g_return_val_if_fail(error == NULL || *error == NULL, NULL);

    return g_initable_new(XFCE_TYPE_NOTIFY_LOG, NULL, error, NULL);
}

static GList *
stmt_parse_actions(sqlite3_stmt *stmt, int col_num) {
    const char *actions_blob = sqlite3_column_blob(stmt, col_num);
    int actions_blob_len = sqlite3_column_bytes(stmt, col_num);

    if (actions_blob == NULL || actions_blob_len <= 0) {
        return NULL;
    } else {
        GList *actions = NULL;
        const char *cur = actions_blob;
        const char *actions_blob_end = actions_blob + actions_blob_len;

        while (cur < actions_blob_end) {
            const char *id_end = memchr(cur, '\0', actions_blob_end - cur);

            if (id_end != NULL && G_LIKELY(id_end < actions_blob_end)) {
                const char *label_end = memchr(id_end + 1, '\0', actions_blob_end - id_end - 1);

                if (G_LIKELY(label_end != NULL)) {
                    XfceNotifyLogEntryAction *action = g_new0(XfceNotifyLogEntryAction, 1);
                    action->id = g_strndup(cur, id_end - cur);
                    action->label = g_strndup(id_end + 1, label_end - id_end - 1);
                    actions = g_list_prepend(actions, action);

                    cur = label_end + 1;
                } else {
                    g_warning("Malformed actions blob in DB: no NUL byte for label end");
                    break;
                }
            } else {
                g_warning("Malformed actions blob in DB: blob truncated looking for ID end");
                break;
            }
        }

        return g_list_reverse(actions);
    }
}

static XfceNotifyLogEntry *
stmt_get_log_entry(sqlite3_stmt *stmt, GTimeZone *default_tz) {
    XfceNotifyLogEntry *entry = g_new0(XfceNotifyLogEntry, 1);
    gint64 timestamp_utc;
    const unsigned char *tz_identifier;
    GTimeZone *tz = NULL;
    GDateTime *dt_utc_no_us, *dt_utc;
    GHashTable *column_names = g_hash_table_new(g_str_hash, g_str_equal);

    // this is dumb
    for (guint i = 0; ; ++i) {
        const char *name = sqlite3_column_name(stmt, i);
        if (name != NULL) {
            g_hash_table_insert(column_names, (gpointer)name, GUINT_TO_POINTER(i));
        } else {
            break;
        }
    }

#define COL_INDEX(name) GPOINTER_TO_UINT(g_hash_table_lookup(column_names, name))

    timestamp_utc = sqlite3_column_int64(stmt, COL_INDEX(COL_TIMESTAMP));
    dt_utc_no_us = g_date_time_new_from_unix_utc(timestamp_utc / 1000000);
    dt_utc = g_date_time_add(dt_utc_no_us, timestamp_utc % 1000000);
    tz_identifier = sqlite3_column_text(stmt, COL_INDEX(COL_TZ_IDENTIFIER));
    if (G_LIKELY(tz_identifier != NULL)) {
        tz = g_time_zone_new_identifier((const gchar *)tz_identifier);
    }
    if (G_UNLIKELY(tz == NULL)) {
        tz = g_time_zone_ref(default_tz);
    }

    entry->id = g_strdup((const gchar *)sqlite3_column_text(stmt, COL_INDEX(COL_ID)));
    entry->timestamp = g_date_time_to_timezone(dt_utc, tz);
    entry->app_id = g_strdup((const gchar *)sqlite3_column_text(stmt, COL_INDEX(COL_APP_ID)));
    entry->app_name = g_strdup((const gchar *)sqlite3_column_text(stmt, COL_INDEX(COL_APP_NAME)));
    entry->icon_id = g_strdup((const gchar *)sqlite3_column_text(stmt, COL_INDEX(COL_ICON_ID)));
    entry->summary = g_strdup((const gchar *)sqlite3_column_text(stmt, COL_INDEX(COL_SUMMARY)));
    entry->body = g_strdup((const gchar *)sqlite3_column_text(stmt, COL_INDEX(COL_BODY)));
    entry->actions = stmt_parse_actions(stmt, COL_INDEX(COL_ACTIONS));
    entry->expire_timeout = sqlite3_column_int(stmt, COL_INDEX(COL_EXPIRE_TIMEOUT));
    entry->is_read = sqlite3_column_int(stmt, COL_INDEX(COL_IS_READ)) != 0 ? TRUE : FALSE;

    if (G_UNLIKELY(entry->id == NULL || entry->timestamp == NULL)) {
        xfce_notify_log_entry_free(entry);
        entry = NULL;
    }

    g_date_time_unref(dt_utc_no_us);
    g_date_time_unref(dt_utc);
    g_time_zone_unref(tz);
    g_hash_table_destroy(column_names);

    return entry;

#undef COL_INDEX
}

XfceNotifyLogEntry *
xfce_notify_log_get(XfceNotifyLog *log, const gchar *id) {
    XfceNotifyLogEntry *entry = NULL;
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), NULL);
    g_return_val_if_fail(id != NULL, NULL);

    rc = sqlite3_bind_text(log->stmt_get,
                           BIND_INDEX(log->stmt_get, COL_ID),
                           g_strdup(id),
                           -1,
                           g_free);

    if (G_LIKELY(rc == SQLITE_OK)) {
        rc = sqlite3_step(log->stmt_get);
    }
    if (rc == SQLITE_ROW) {
        GTimeZone *default_tz = g_time_zone_new_local();
        entry = stmt_get_log_entry(log->stmt_get, default_tz);
        g_time_zone_unref(default_tz);
    }

    sqlite3_reset(log->stmt_get);
    sqlite3_clear_bindings(log->stmt_get);

    return entry;
}

GList *
xfce_notify_log_read(XfceNotifyLog *log, const gchar *start_after_id, guint count) {
    GList *entries = NULL;
    sqlite3_stmt *stmt;
    int rc  = SQLITE_OK;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), NULL);
    g_return_val_if_fail(count > 0, NULL);

    if (start_after_id == NULL) {
        stmt = log->stmt_read;
    } else {
        gint64 start_after_timestamp = G_MININT64;

        rc = sqlite3_bind_text(log->stmt_get_timestamp,
                               BIND_INDEX(log->stmt_get_timestamp, COL_ID),
                               g_strdup(start_after_id),
                               -1,
                               g_free);
        if (G_LIKELY(rc == SQLITE_OK)) {
            rc = sqlite3_step(log->stmt_get_timestamp);
            if (rc == SQLITE_ROW) {
                start_after_timestamp = sqlite3_column_int64(log->stmt_get_timestamp, 0);
                rc = SQLITE_OK;
            }
        }

        sqlite3_reset(log->stmt_get_timestamp);
        sqlite3_clear_bindings(log->stmt_get_timestamp);

        stmt = log->stmt_read_with_timestamp;
        if (rc == SQLITE_OK) {
            rc = sqlite3_bind_int64(stmt, BIND_INDEX(stmt, COL_TIMESTAMP), start_after_timestamp);
        }
    }

    if (G_LIKELY(rc == SQLITE_OK)) {
        rc = sqlite3_bind_int(stmt,
                              BIND_INDEX(stmt, BIND_LIMIT),
                              count);
    }

    if (G_LIKELY(rc == SQLITE_OK)) {
        GTimeZone *default_tz = g_time_zone_new_local();

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            XfceNotifyLogEntry *entry = stmt_get_log_entry(stmt, default_tz);
            if (entry != NULL) {
                entries = g_list_prepend(entries, entry);
            }
        }
        entries = g_list_reverse(entries);

        g_time_zone_unref(default_tz);
    }

    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to fetch entries: %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    return entries;
}

guint
xfce_notify_log_count_unread_messages(XfceNotifyLog *log) {
    guint count = 0;
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), 0);

    rc = sqlite3_step(log->stmt_count_unreads);
    if (G_LIKELY(rc == SQLITE_ROW)) {
        count = sqlite3_column_int(log->stmt_count_unreads, 0);
    } else {
        g_warning("Failed to get unread count: %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_count_unreads);

    return count;
}

GHashTable *
xfce_notify_log_get_app_id_counts(XfceNotifyLog *log) {
    GHashTable *app_id_counts;
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), NULL);

    app_id_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    while ((rc = sqlite3_step(log->stmt_count_app_ids)) == SQLITE_ROW) {
        const unsigned char *app_id = sqlite3_column_text(log->stmt_count_app_ids, 0);
        guint count = sqlite3_column_int(log->stmt_count_app_ids, 1);
        g_hash_table_insert(app_id_counts, g_strdup((const gchar *)app_id), GUINT_TO_POINTER(count));
    }

    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to enumerate all app IDs: %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_count_app_ids);

    return app_id_counts;
}

static void *
serialize_actions(GList *actions, guint *bytes) {
    char *data;
    guint len = 0;

    for (GList *l = actions; l != NULL; l = l->next) {
        XfceNotifyLogEntryAction *action = l->data;
        len += (action->id != NULL ? strlen(action->id) : 0) + 1;
        len += (action->label != NULL ? strlen(action->label) : 0) + 1;
    }

    data = g_malloc0(len);
    *bytes = len;

    len = 0;
    for (GList *l = actions; l != NULL; l = l->next) {
        XfceNotifyLogEntryAction *action = l->data;
        if (action->id != NULL) {
            guint n = strlen(action->id);
            memcpy(data + len, action->id, n);
            len += n;
        }
        len += 1;
        if (action->label != NULL) {
            guint n = strlen(action->label);
            memcpy(data + len, action->label, n);
            len += n;
        }
        len += 1;
    }

    return data;
}

gboolean
xfce_notify_log_write(XfceNotifyLog *log, XfceNotifyLogEntry *entry) {
    int rc;
    gint64 timestamp;
    void *actions_blob;
    guint actions_blob_len = 0;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), FALSE);
    g_return_val_if_fail(entry != NULL, FALSE);

    if (entry->id == NULL) {
        entry->id = g_uuid_string_random();
    }

    DBG("writing new entry with id %s", entry->id);

    timestamp = g_date_time_to_unix(entry->timestamp) * 1000000 + g_date_time_get_microsecond(entry->timestamp);
    actions_blob = serialize_actions(entry->actions, &actions_blob_len);

    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_ID), g_strdup(entry->id), -1, g_free);
    sqlite3_bind_int64(log->stmt_write, BIND_INDEX(log->stmt_write, COL_TIMESTAMP), timestamp);
    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_TZ_IDENTIFIER), g_strdup(g_time_zone_get_identifier(g_date_time_get_timezone(entry->timestamp))), -1, g_free);
    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_APP_ID), g_strdup(entry->app_id), -1, g_free);
    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_APP_NAME), g_strdup(entry->app_name), -1, g_free);
    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_ICON_ID), g_strdup(entry->icon_id), -1, g_free);
    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_SUMMARY), g_strdup(entry->summary), -1, g_free);
    sqlite3_bind_text(log->stmt_write, BIND_INDEX(log->stmt_write, COL_BODY), g_strdup(entry->body), -1, g_free);
    sqlite3_bind_blob(log->stmt_write, BIND_INDEX(log->stmt_write, COL_ACTIONS), actions_blob, actions_blob_len, g_free);
    sqlite3_bind_int(log->stmt_write, BIND_INDEX(log->stmt_write, COL_EXPIRE_TIMEOUT), entry->expire_timeout);
    sqlite3_bind_int(log->stmt_write, BIND_INDEX(log->stmt_write, COL_IS_READ), entry->is_read ? 1 : 0);

    rc = sqlite3_step(log->stmt_write);
    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to write new log entry to DB: %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_write);
    sqlite3_clear_bindings(log->stmt_write);

    return rc == SQLITE_DONE;
}

gboolean
xfce_notify_log_mark_read(XfceNotifyLog *log, const gchar *id) {
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), FALSE);
    g_return_val_if_fail(id != NULL && id[0] != '\0', FALSE);

    rc = sqlite3_bind_text(log->stmt_mark_read,
                           BIND_INDEX(log->stmt_mark_read, COL_ID),
                           g_strdup(id),
                           -1,
                           g_free);
    if (G_LIKELY(rc == SQLITE_OK)) {
        rc = sqlite3_step(log->stmt_mark_read);
    }

    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to mark log entry %s read: %s", id, sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_mark_read);
    sqlite3_clear_bindings(log->stmt_mark_read);

    return rc == SQLITE_DONE;
}

gboolean
xfce_notify_log_mark_all_read(XfceNotifyLog *log) {
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), FALSE);

    rc = sqlite3_step(log->stmt_mark_all_read);
    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to mark all log entries read: %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_mark_all_read);

    return rc == SQLITE_DONE;
}

gboolean
xfce_notify_log_delete(XfceNotifyLog *log, const gchar *id) {
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), FALSE);
    g_return_val_if_fail(id != NULL, FALSE);

    rc = sqlite3_bind_text(log->stmt_delete,
                           BIND_INDEX(log->stmt_delete, COL_ID),
                           g_strdup(id),
                           -1,
                           g_free);
    if (G_LIKELY(rc == SQLITE_OK)) {
        rc = sqlite3_step(log->stmt_delete);
    }

    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to delete log entry ID %s: %s", id, sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_delete);
    sqlite3_clear_bindings(log->stmt_delete);

    return rc == SQLITE_DONE;
}

gboolean
xfce_notify_log_delete_before(XfceNotifyLog *log, GDateTime *oldest_to_keep) {
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), FALSE);
    g_return_val_if_fail(oldest_to_keep != NULL, FALSE);

    rc = sqlite3_bind_int64(log->stmt_delete,
                            BIND_INDEX(log->stmt_delete, COL_TIMESTAMP),
                            g_date_time_to_unix(oldest_to_keep));
    if (G_LIKELY(rc == SQLITE_OK)) {
        rc = sqlite3_step(log->stmt_delete);
    }

    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to delete log entries %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_delete_before);
    sqlite3_clear_bindings(log->stmt_delete_before);

    return rc == SQLITE_DONE;
}

gboolean
xfce_notify_log_clear(XfceNotifyLog *log) {
    int rc;

    g_return_val_if_fail(XFCE_IS_NOTIFY_LOG(log), FALSE);

    rc = sqlite3_step(log->stmt_delete_all);
    if (G_UNLIKELY(rc != SQLITE_DONE)) {
        g_warning("Failed to delete log entries %s", sqlite3_errmsg(log->db));
    }

    sqlite3_reset(log->stmt_delete_all);

    return rc == SQLITE_DONE;
}

static inline void
xfce_notify_log_entry_action_free(XfceNotifyLogEntryAction *action) {
    g_free(action->id);
    g_free(action->label);
    g_free(action);
}

void
xfce_notify_log_entry_free(XfceNotifyLogEntry *entry) {
    if (entry != NULL) {
        g_free(entry->id);
        if (G_LIKELY(entry->timestamp != NULL)) {
            g_date_time_unref(entry->timestamp);
        }
        g_free(entry->app_id);
        g_free(entry->app_name);
        g_free(entry->icon_id);
        g_free(entry->summary);
        g_free(entry->body);
        g_list_free_full(entry->actions, (GDestroyNotify)xfce_notify_log_entry_action_free);
        g_free(entry);
    }
}

static GList *
parse_keyfile_actions(GKeyFile *keyfile, const gchar *group) {
    GList *actions = NULL;
    gint i = 0;

    for (;;) {
        gchar *action_id_key = g_strdup_printf("action-id-%d", i);
        gchar *action_label_key = g_strdup_printf("action-label-%d", i);
        gchar *action_id = g_key_file_get_string(keyfile, group, action_id_key, NULL);
        gchar *action_label = g_key_file_get_string(keyfile, group, action_label_key, NULL);

        g_free(action_id_key);
        g_free(action_label_key);

        if (action_id != NULL && action_label != NULL) {
            XfceNotifyLogEntryAction *action = g_new0(XfceNotifyLogEntryAction, 1);
            action->id = action_id;
            action->label = action_label;
            actions = g_list_prepend(actions, action);
        } else {
            g_free(action_id);
            g_free(action_label);
            break;
        }

        ++i;
    }

    return g_list_reverse(actions);
}

static gboolean
migrate_old_keyfile(XfceNotifyLog *log) {
    gboolean migrated = TRUE;
    GFile *log_dir = notify_log_dir();
    GFile *log_file = g_file_get_child(log_dir, "log");

    if (g_file_query_exists(log_file, NULL)) {
        GKeyFile *keyfile = g_key_file_new();

        if (G_LIKELY(g_key_file_load_from_file(keyfile, g_file_peek_path(log_file), G_KEY_FILE_NONE, NULL))) {
            GTimeZone *default_tz = g_time_zone_new_local();
            gchar **groups = g_key_file_get_groups(keyfile, NULL);

            for (guint i = 0; groups[i] != NULL; ++i) {
                XfceNotifyLogEntry *entry = g_new0(XfceNotifyLogEntry, 1);
                gboolean success;
                gchar *group = groups[i];
                entry->timestamp = g_date_time_new_from_iso8601(group, default_tz);
                entry->app_id = g_key_file_get_string(keyfile, group, "app_name", NULL);
                entry->icon_id = g_key_file_get_string(keyfile, group, "app_icon", NULL);
                entry->summary = g_key_file_get_string(keyfile, group, "summary", NULL);
                entry->body = g_key_file_get_string(keyfile, group, "body", NULL);
                entry->actions = parse_keyfile_actions(keyfile, group);
                entry->expire_timeout = g_key_file_get_integer(keyfile, group, "expire-timeout", NULL);
                entry->is_read = TRUE;

                success = xfce_notify_log_write(log, entry);
                xfce_notify_log_entry_free(entry);
                if (G_UNLIKELY(!success)) {
                    migrated = FALSE;
                    break;
                }
            }

            g_time_zone_unref(default_tz);
            g_strfreev(groups);
        } else {
            migrated = FALSE;
        }

        if (G_LIKELY(migrated)) {
            GFile *dest = g_file_get_child(log_dir, "log.old.safe-to-delete");
            GError *error = NULL;
            if (!g_file_move(log_file, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
                g_warning("Failed to move old log out of the way; you may get duplicate log entries next time (%s)", error != NULL ? error->message : "unknown error");
                if (error != NULL) {
                    g_error_free(error);
                }
            }
            g_object_unref(dest);
        }

        g_key_file_unref(keyfile);
    }

    g_object_unref(log_dir);
    g_object_unref(log_file);

    return migrated;
}
