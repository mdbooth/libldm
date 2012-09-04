/* ldmread
 * Copyright 2012 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libdevmapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#include <glib-object.h>
#include <gio/gunixoutputstream.h>
#include <json-glib/json-glib.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "ldm.h"

#define USAGE_SCAN \
    "  scan [<device...>]"

#define USAGE_SHOW \
    "  show diskgroup <guid>\n" \
    "  show volume <disk group guid> <name>\n" \
    "  show partition <disk group guid> <name>\n" \
    "  show disk <disk group guid> <name>"

#define USAGE_CREATE \
    "  create all\n" \
    "  create volume <disk group guid> <name>"

#define USAGE_ALL USAGE_SCAN "\n" USAGE_SHOW "\n" USAGE_CREATE

gboolean
usage_show(void)
{
    g_warning(USAGE_SHOW);
    return FALSE;
}

gboolean usage_create(void)
{
    g_warning(USAGE_CREATE);
    return FALSE;
}

typedef gboolean (*_action_t) (LDM *ldm, gint argc, gchar **argv,
                               JsonBuilder *jb);

gboolean ldm_scan(LDM *ldm, gint argc, gchar **argv, JsonBuilder *jb);
gboolean ldm_show(LDM *ldm, gint argc, gchar **argv, JsonBuilder *jb);
gboolean ldm_create(LDM *ldm, gint argc, gchar **argv, JsonBuilder *jb);

typedef struct {
    const char * name;
    const _action_t action;
} _command_t;

static const _command_t const commands[] = {
    { "scan", ldm_scan },
    { "show", ldm_show },
    { "create", ldm_create },
    { NULL }
};

gboolean
do_command(LDM * const ldm, const int argc, char *argv[],
           GOutputStream * const out,
           JsonGenerator * const jg, JsonBuilder * const jb)
{
    const _command_t *i = commands;
    while (i->name) {
        if (g_strcmp0(i->name, argv[0]) == 0) {
            if ((i->action)(ldm, argc - 1, argv + 1, jb)) {
                GError *err = NULL;
                json_generator_set_root(jg, json_builder_get_root(jb));
                if (!json_generator_to_stream(jg, out, NULL, &err)) {
                    g_warning("Error writing JSON output: %s",
                              err ? err->message : "(no detail)");
                    if (err) { g_error_free(err); err = NULL; }
                }
                printf("\n");
            }
            return TRUE;
        }
        i++;
    }

    return FALSE;
}

gboolean
_scan(LDM *const ldm, gboolean ignore_errors,
      const gint argc, gchar ** const argv,
      JsonBuilder * const jb)
{
    GError *err = NULL;

    wordexp_t p = {};
    for (int i = 0; i < argc; i++) {
        gchar * const pattern = argv[i];

        wordexp(pattern, &p, WRDE_REUSE);
        for (int j = 0; j < p.we_wordc; j++) {
            gchar * const path = p.we_wordv[j];

            if (!ldm_add(ldm, path, &err)) {
                if (
                    !ignore_errors &&
                    (err->domain != LDM_ERROR || err->code != LDM_ERROR_NOT_LDM)
                ) {
                    g_warning("Error scanning %s: %s", path, err->message);
                }
                g_error_free(err); err = NULL;
            }
        }
    }
    wordfree(&p);

    if (jb) {
        json_builder_begin_array(jb);

        GArray * const dgs = ldm_get_disk_groups(ldm, &err);
        if (!dgs) {
            g_warning("Error listing disk groups: %s", err->message);
            return FALSE;
        }
        for (int i = 0; i < dgs->len; i++) {
            LDMDiskGroup * const dg = g_array_index(dgs, LDMDiskGroup *, i);

            gchar *guid = NULL;

            g_object_get(dg, "guid", &guid, NULL);

            json_builder_add_string_value(jb, guid);

            g_free(guid);
        }
        g_array_unref(dgs);

        json_builder_end_array(jb);
    }

    return TRUE;
}

gboolean
ldm_scan(LDM *const ldm, const gint argc, gchar ** const argv,
         JsonBuilder * const jb)
{
    return _scan(ldm, FALSE, argc, argv, jb);
}

void
show_json_array(JsonBuilder * const jb, const GArray * const array,
                 const gchar * const field, const gchar * const name)
{
    json_builder_set_member_name(jb, name);
    json_builder_begin_array(jb);
    for (int i = 0; i < array->len; i++) {
        GObject * const o = g_array_index(array, GObject *, i);

        gchar *value;
        g_object_get(o, field, &value, NULL);
        json_builder_add_string_value(jb, value);
        g_free(value);
    }
    json_builder_end_array(jb);
}

LDMDiskGroup *
find_diskgroup(LDM * const ldm, const gchar * const guid)
{
    GError *err = NULL;
    LDMDiskGroup *dg = NULL;

    GArray * const diskgroups = ldm_get_disk_groups(ldm, &err);
    if (!diskgroups) {
        g_warning("Error listing disk groups: %s", err->message);
        return NULL;
    }
    for (int i = 0; i < diskgroups->len; i++) {
        LDMDiskGroup * const dg_i =
            g_array_index(diskgroups, LDMDiskGroup *, i);

        gchar *guid_i;
        g_object_get(dg_i, "guid", &guid_i, NULL);

        if (g_strcmp0(guid_i, guid) == 0) {
            dg = dg_i;
            g_free(guid_i);
            break;
        }

        g_free(guid_i);
    }

    if (dg) {
        g_object_ref(dg);
    } else {
        g_warning("No such disk group: %s", guid);
    }

    g_array_unref(diskgroups);

    return dg;
}

gboolean
show_diskgroup(LDM * const ldm, const gint argc, gchar ** const argv,
                JsonBuilder * const jb)
{
    if (argc != 1) return usage_show();

    LDMDiskGroup *dg = find_diskgroup(ldm, argv[0]);
    if (!dg) return FALSE;

    gchar *name;
    g_object_get(dg, "name", &name, NULL);

    json_builder_begin_object(jb);

    json_builder_set_member_name(jb, "name");
    json_builder_add_string_value(jb, name);
    json_builder_set_member_name(jb, "guid");
    json_builder_add_string_value(jb, argv[0]);

    g_free(name);

    GError *err = NULL;

    GArray * const volumes = ldm_disk_group_get_volumes(dg, &err);
    if (!volumes) {
        g_warning("Error getting disk group volumes: %s", err->message);
        return FALSE;
    }
    show_json_array(jb, volumes, "name", "volumes");
    g_array_unref(volumes);

    GArray * const disks = ldm_disk_group_get_disks(dg, &err);
    if (!disks) {
        g_warning("Error getting disk group disks: %s", err->message);
        return FALSE;
    }
    show_json_array(jb, disks, "name", "disks");
    g_array_unref(disks);

    json_builder_end_object(jb);

    g_object_unref(dg);
    return TRUE;
}

gboolean
show_volume(LDM *const ldm, const gint argc, gchar ** const argv,
             JsonBuilder * const jb)
{
    if (argc != 2) return usage_show();

    LDMDiskGroup *dg = find_diskgroup(ldm, argv[0]);
    if (!dg) return FALSE;

    GError *err = NULL;

    GArray * const volumes = ldm_disk_group_get_volumes(dg, &err);
    g_object_unref(dg);
    if (!volumes) {
        g_warning("Unable to get volumes from disk group: %s", err->message);
        return FALSE;
    }
    for (int i = 0; i < volumes->len; i++) {
        LDMVolume * const vol = g_array_index(volumes, LDMVolume *, i);

        gchar *name;
        LDMVolumeType type;
        guint64 size;
        guint64 chunk_size;
        gchar *hint;

        g_object_get(vol, "name", &name, "type", &type, "size", &size,
                          "chunk-size", &chunk_size, "hint", &hint, NULL);

        gboolean found = FALSE;
        if (g_strcmp0(name, argv[1]) == 0) {
            found = TRUE;

            json_builder_begin_object(jb);

            GEnumValue * const type_v =
                g_enum_get_value(g_type_class_peek(LDM_TYPE_VOLUME_TYPE), type);

            json_builder_set_member_name(jb, "name");
            json_builder_add_string_value(jb, name);
            json_builder_set_member_name(jb, "type");
            json_builder_add_string_value(jb, type_v->value_nick);
            json_builder_set_member_name(jb, "size");
            json_builder_add_int_value(jb, size);
            json_builder_set_member_name(jb, "chunk-size");
            json_builder_add_int_value(jb, chunk_size);
            json_builder_set_member_name(jb, "hint");
            json_builder_add_string_value(jb, hint);

            json_builder_set_member_name(jb, "partitions");
            json_builder_begin_array(jb);
            GArray * const partitions = ldm_volume_get_partitions(vol, &err);
            if (!partitions) {
                g_warning("Unable to get partitions from volume: %s",
                          err->message);

                g_array_unref(volumes);
                g_free(name);
                g_free(hint);

                return FALSE;
            }
            for (int j = 0; j < partitions->len; j++) {
                LDMPartition * const part =
                    g_array_index(partitions, LDMPartition *, j);

                gchar *partname;

                g_object_get(part, "name", &partname, NULL);

                json_builder_add_string_value(jb, partname);

                g_free(partname);
            }
            g_array_unref(partitions);
            json_builder_end_array(jb);

            json_builder_end_object(jb);
        }

        g_free(name);
        g_free(hint);

        if (found) break;
    }
    g_array_unref(volumes);

    return TRUE;
}

gboolean
show_partition(LDM *const ldm, const gint argc, gchar ** const argv,
                JsonBuilder * const jb)
{
    if (argc != 2) return usage_show();

    LDMDiskGroup *dg = find_diskgroup(ldm, argv[0]);
    if (!dg) return FALSE;

    GError *err = NULL;

    GArray * const parts = ldm_disk_group_get_partitions(dg, &err);
    g_object_unref(dg);
    if (!parts) {
        g_warning("Unable to get partitions from disk group: %s", err->message);
        return FALSE;
    }
    for (int i = 0; i < parts->len; i++) {
        LDMPartition * const part = g_array_index(parts, LDMPartition *, i);

        gchar *name;
        guint64 start;
        guint64 size;

        g_object_get(part, "name", &name, "start", &start, "size", &size, NULL);

        gboolean found = FALSE;
        if (g_strcmp0(name, argv[1]) == 0) {
            found = TRUE;

            LDMDisk * const disk = ldm_partition_get_disk(part, &err);
            if (!disk) {
                g_warning("Error getting disk for partition: %s", err->message);
                g_array_unref(parts);
                return FALSE;
            }

            gchar *diskname;

            g_object_get(disk, "name", &diskname, NULL);
            g_object_unref(disk);

            json_builder_begin_object(jb);

            json_builder_set_member_name(jb, "name");
            json_builder_add_string_value(jb, name);
            json_builder_set_member_name(jb, "start");
            json_builder_add_int_value(jb, start);
            json_builder_set_member_name(jb, "size");
            json_builder_add_int_value(jb, size);
            json_builder_set_member_name(jb, "disk");
            json_builder_add_string_value(jb, diskname);

            json_builder_end_object(jb);

            g_free(diskname);
        }

        g_free(name);

        if (found) break;
    }
    g_array_unref(parts);

    return TRUE;
}

gboolean
show_disk(LDM *const ldm, const gint argc, gchar ** const argv,
           JsonBuilder * const jb)
{
    if (argc != 2) return usage_show();

    LDMDiskGroup *dg = find_diskgroup(ldm, argv[0]);
    if (!dg) return FALSE;

    GError *err = NULL;

    GArray * const disks = ldm_disk_group_get_disks(dg, &err);
    g_object_unref(dg);
    if (!disks) {
        g_warning("Error getting disks for disk group: %s", err->message);
        return FALSE;
    }
    gboolean found = FALSE;
    for (int i = 0; i < disks->len; i++) {
        LDMDisk * const disk = g_array_index(disks, LDMDisk *, i);

        gchar *name;
        gchar *guid;
        gchar *device;
        guint64 data_start;
        guint64 data_size;
        guint64 metadata_start;
        guint64 metadata_size;

        g_object_get(disk, "name", &name, "guid", &guid, "device", &device,
                           "data-start", &data_start, "data-size", &data_size,
                           "metadata-start", &metadata_start,
                           "metadata-size", &metadata_size, NULL);

        if (g_strcmp0(name, argv[1]) == 0) {
            found = TRUE;

            json_builder_begin_object(jb);

            json_builder_set_member_name(jb, "name");
            json_builder_add_string_value(jb, name);
            json_builder_set_member_name(jb, "guid");
            json_builder_add_string_value(jb, guid);
            json_builder_set_member_name(jb, "present");
            json_builder_add_boolean_value(jb, device ? TRUE : FALSE);
            if (device) {
                json_builder_set_member_name(jb, "device");
                json_builder_add_string_value(jb, device);
                json_builder_set_member_name(jb, "data-start");
                json_builder_add_int_value(jb, data_start);
                json_builder_set_member_name(jb, "data-size");
                json_builder_add_int_value(jb, data_size);
                json_builder_set_member_name(jb, "metadata-start");
                json_builder_add_int_value(jb, metadata_start);
                json_builder_set_member_name(jb, "metadata-size");
                json_builder_add_int_value(jb, metadata_size);
            }

            json_builder_end_object(jb);
        }

        g_free(name);
        g_free(guid);
        g_free(device);

        if (found) break;
    }
    g_array_unref(disks);

    return TRUE;
}

gboolean
ldm_show(LDM *const ldm, const gint argc, gchar ** const argv,
         JsonBuilder * const jb)
{
    if (argc == 0) return usage_show();

    if (g_strcmp0(argv[0], "diskgroup") == 0) {
        return show_diskgroup(ldm, argc - 1, argv + 1, jb);
    } else if (g_strcmp0(argv[0], "volume") == 0) {
        return show_volume(ldm, argc - 1, argv + 1, jb);
    } else if (g_strcmp0(argv[0], "partition") == 0) {
        return show_partition(ldm, argc - 1, argv + 1, jb);
    } else if (g_strcmp0(argv[0], "disk") == 0) {
        return show_disk(ldm, argc - 1, argv + 1, jb);
    }

    return usage_show();
}

gboolean
ldm_create(LDM *const ldm, const gint argc, gchar ** const argv,
           JsonBuilder * const jb)
{
    GError *err = NULL;

    json_builder_begin_array(jb);

    if (argc == 1) {
        if (g_strcmp0(argv[0], "all") != 0) return usage_create();

        GArray *dgs = ldm_get_disk_groups(ldm, &err);
        for (int i = 0; i < dgs->len; i++) {
            LDMDiskGroup * const dg = g_array_index(dgs, LDMDiskGroup *, i);

            GArray *volumes = ldm_disk_group_get_volumes(dg, &err);
            for (int j = 0; j < volumes->len; j++) {
                LDMVolume * const vol = g_array_index(volumes, LDMVolume *, j);

                GString * const device = ldm_volume_dm_create(vol, &err);
                if (device) {
                    json_builder_add_string_value(jb, device->str);
                    g_string_free(device, TRUE);
                } else {
                    gchar *vol_name;
                    g_object_get(vol, "name", &vol_name, NULL);

                    gchar *dg_guid;
                    g_object_get(dg, "guid", &dg_guid, NULL);

                    g_warning("Unable to create volume %s in disk group %s: %s",
                              vol_name, dg_guid, err->message);
                              
                    g_free(vol_name);
                    g_free(dg_guid);
                    
                    g_error_free(err); err = NULL;
                }
            }
        }
        g_array_unref(dgs);
    }

    else if (argc == 3) {
        if (g_strcmp0(argv[0], "volume") != 0) return usage_create();

        LDMDiskGroup * const dg = find_diskgroup(ldm, argv[1]);
        if (!dg) return FALSE;

        GArray * const volumes = ldm_disk_group_get_volumes(dg, &err);
        LDMVolume *vol = NULL;
        for (int i = 0; i < volumes->len; i++) {
            LDMVolume * const o = g_array_index(volumes, LDMVolume *, i);

            gchar *name;
            g_object_get(o, "name", &name, NULL);
            if (g_strcmp0(name, argv[2]) == 0) vol = o;
            g_free(name);

            if (vol) break;
        }
        g_object_unref(dg);
        g_array_unref(volumes);

        if (!vol) {
            g_warning("Disk group %s doesn't contain volume %s",
                      argv[1], argv[2]);
            return FALSE;
        }

        GString * const device = ldm_volume_dm_create(vol, &err);
        if (device) {
            json_builder_add_string_value(jb, device->str);
            g_string_free(device, TRUE);
        } else {
            g_warning("Unable to create volume %s in disk group %s: %s",
                      argv[2], argv[1], err->message);
            g_error_free(err); err = NULL;
            return FALSE;
        }
    }

    else {
        return usage_create();
    }

    json_builder_end_array(jb);

    return TRUE;
}

gboolean
shell(LDM * const ldm, gchar ** const devices,
      JsonGenerator * const jg, GOutputStream * const out)
{
    int history_len = 0;

    rl_readline_name = "ldmtool";

    char histfile[1024] = "";
    const char * const home = getenv("HOME");
    if (home) {
        snprintf(histfile, sizeof(histfile), "%s/.ldmtool", home);
        using_history();

        int r = read_history(histfile);
        if (r != 0 && r != ENOENT) {
            g_warning("Unable to read history from %s: %s",
                      histfile, strerror(r));
        }
    }

    JsonBuilder *jb = json_builder_new();

    for (;;) {
        char * line = readline("ldm> ");
        if (!line) {
            printf("\n");
            break;
        }

        GError *err = NULL;
        gint argc = 0;
        gchar **argv = NULL;
        if (!g_shell_parse_argv(line, &argc, &argv, &err)) {
            if (err->domain != G_SHELL_ERROR ||
                err->code != G_SHELL_ERROR_EMPTY_STRING)
            {
                g_warning("Error parsing command: %s", err->message);
            }
            g_error_free(err); err = NULL;
        }

        if (argc == 0) {
            free(line);
            continue;
        }

        add_history(line);
        history_len++;
        free(line);

        if (!do_command(ldm, argc, argv, out, jg, jb)) {
            if (g_strcmp0("quit", argv[0]) == 0 ||
                g_strcmp0("exit", argv[0]) == 0)
            {
                g_strfreev(argv);
                break;
            }

            printf("Unrecognised command: %s\n", argv[0]);
        }
        json_builder_reset(jb);

        g_strfreev(argv);
    }

    g_object_unref(jb);

    if (histfile[0] != '\0') {
        /* append_history requires the file to exist already */
        int fd = open(histfile, O_WRONLY|O_CREAT|O_NOCTTY|O_CLOEXEC, 0600);
        if (fd == -1) {
            g_warning("Unable to create history file %s: %m", histfile);
        } else {
            close(fd);

            int r = append_history(history_len, histfile);
            if (r != 0) {
                g_warning("Unable to save history to %s: %s",
                          histfile, strerror(r));
            }
        }
    }

    return TRUE;
}

void
_array_free(gpointer data)
{
    char **str_p = data;
    g_free(*str_p);
}

GArray *
get_devices(void)
{
    DIR *dir = opendir("/sys/block");
    if (dir == NULL) {
        g_warning("Unable to open /sys/block: %m");
        return NULL;
    }

    GArray *ret = g_array_new(TRUE, FALSE, sizeof(char *));
    g_array_set_clear_func(ret, _array_free);

    struct dirent *entry;
    for (;;) {
        entry = readdir(dir);
        if (entry == NULL) break;

        if (g_strcmp0(entry->d_name, ".") == 0 ||
            g_strcmp0(entry->d_name, "..") == 0) continue;

        char *device;
        if (asprintf(&device, "/dev/%s", entry->d_name) == -1) {
            g_error("malloc failure in asprintf");
        }
        g_array_append_val(ret, device);
    }

    closedir(dir);

    return ret;
}

gboolean
cmdline(LDM * const ldm, gchar **devices,
        JsonGenerator * const jg, GOutputStream * const out,
        const int argc, char *argv[])
{
    GArray * scanned = NULL;
    if (!devices) {
        scanned = get_devices();
        devices = (gchar **) scanned->data;
    }

    if (!_scan(ldm, TRUE, g_strv_length(devices), devices, NULL)) goto error;

    JsonBuilder *jb = json_builder_new();
    if (!do_command(ldm, argc, argv, out, jg, jb)) {
        g_warning("Unrecognised command: %s", argv[0]);
        goto error;
    }

    if (scanned) g_array_unref(scanned);
    g_object_unref(jb);
    return TRUE;

error:
    if (scanned) g_array_unref(scanned);
    g_object_unref(jb);
    return FALSE;

}

void
ldmtool_log(const gchar * const log_domain, const GLogLevelFlags log_level,
            const gchar * const message, gpointer const user_data)
{
    if (log_level & G_LOG_LEVEL_DEBUG) {
        /* Ignore debug messages */
    } else if (log_level & (G_LOG_LEVEL_INFO | G_LOG_LEVEL_MESSAGE)) {
        printf("%s\n", message);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

int
main(int argc, char *argv[])
{
    static gchar **devices = NULL;

    static const GOptionEntry entries[] =
    {
        { "device", 'd', 0, G_OPTION_ARG_FILENAME_ARRAY,
          &devices, "Block device to scan for LDM metadata", NULL },
        { NULL }
    };

    GError *err = NULL;

    g_log_set_handler(NULL, G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE |
                      G_LOG_LEVEL_INFO, ldmtool_log, NULL);

    GOptionContext *context = g_option_context_new("[command <arguments>]");

    g_option_context_set_summary(context, "Available commands:\n" USAGE_ALL);

    g_option_context_add_main_entries(context, entries, NULL);
    if(!g_option_context_parse(context, &argc, &argv, &err)) {
        g_warning("option parsing failed: %s", err->message);
        return 1;
    }
    g_option_context_free(context);

    g_type_init();

    LDM * const ldm = ldm_new(&err);

    int ret = 0;

    GOutputStream *out = g_unix_output_stream_new(STDOUT_FILENO, FALSE);

    JsonGenerator *jg = json_generator_new();
    json_generator_set_pretty(jg, TRUE);
    json_generator_set_indent(jg, 2);

    if (argc > 1) {
        if (!cmdline(ldm, devices, jg, out, argc - 1, argv + 1)) {
            ret = 1;
        }
    } else {
        if (!shell(ldm, devices, jg, out)) {
            ret = 1;
        }
    }

    g_object_unref(jg);
    g_strfreev(devices);
    g_object_unref(ldm);

    if (!g_output_stream_close(out, NULL, &err)) {
        g_warning("Error closing output stream: %s", err->message);
    }
    g_object_unref(out);

    return ret;
}
