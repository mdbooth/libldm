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

#include <config.h>

#include <stdio.h>
#include <fcntl.h>

#include <glib-object.h>

#include "ldm.h"

int main(int argc, const char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <drive> [<drive> ...]\n", argv[0]);
        return 1;
    }

#if !GLIB_CHECK_VERSION(2,35,0)
    g_type_init();
#endif

    LDM *ldm = ldm_new();

    const char **disk = &argv[1];
    while(*disk) {
        GError *err = NULL;
        if (!ldm_add(ldm, *disk, &err)) {
            fprintf(stderr, "Error reading LDM: %s\n", err->message);
            g_object_unref(ldm);
            g_error_free(err);
            return 1;
        }

        disk++;
    }

    GArray *dgs = ldm_get_disk_groups(ldm);
    for (int i = 0; i < dgs->len; i++) {
        LDMDiskGroup * const dg = g_array_index(dgs, LDMDiskGroup *, i);

        {
            gchar *guid;
            gchar *name;

            g_object_get(dg, "guid", &guid, "name", &name, NULL);

            printf("Disk Group: %s\n", name);
            printf("  GUID:   %s\n", guid);

            g_free(guid);
            g_free(name);
        }

        GArray *vols = ldm_disk_group_get_volumes(dg);
        for (int j = 0; j < vols->len; j++) {
            LDMVolume * const vol = g_array_index(vols, LDMVolume *, j);

            {
                gchar *name;
                gchar *guid;
                LDMVolumeType type;
                guint64 size;
                guint32 part_type;
                gchar *hint;
                guint64 chunk_size;

                g_object_get(vol, "name", &name, "type", &type,
                                  "guid", &guid, "size", &size,
                                  "part-type", &part_type, "hint", &hint,
                                  "chunk-size", &chunk_size, NULL);

                GEnumValue * const type_v =
                    g_enum_get_value(g_type_class_peek(LDM_TYPE_VOLUME_TYPE),
                                     type);

                GError *err = NULL;
                gchar *device = ldm_volume_dm_get_device(vol, &err);

                printf("  Volume: %s\n", name);
                printf("    GUID:       %s\n", guid);
                printf("    Type:       %s\n", type_v->value_nick);
                printf("    Size:       %lu\n", size);
                printf("    Part Type:  %hhu\n", part_type);
                printf("    Hint:       %s\n", hint);
                printf("    Chunk Size: %lu\n", chunk_size);
                printf("    Device:     %s\n", device);

                g_free(name);
                g_free(guid);
                g_free(hint);
                g_free(device);
                if (err) g_error_free(err);
            }

            GArray *parts = ldm_volume_get_partitions(vol);
            for (int k = 0; k < parts->len; k++) {
                LDMPartition * const part =
                    g_array_index(parts, LDMPartition *, k);

                {
                    gchar *name;
                    guint64 start;
                    guint64 size;

                    g_object_get(part, "name", &name, "start", &start,
                                       "size", &size, NULL);

                    GError *err = NULL;
                    gchar *device = ldm_partition_dm_get_device(part, &err);

                    printf("    Partition: %s\n", name);
                    printf("        Start:  %lu\n", start);
                    printf("        Size:   %lu\n", size);
                    printf("        Device: %s\n", device);

                    g_free(name);
                    g_free(device);
                    if (err) g_error_free(err);
                }

                LDMDisk * const disk = ldm_partition_get_disk(part);

                {
                    gchar *name;
                    gchar *guid;
                    gchar *device;
                    guint64 data_start;
                    guint64 data_size;
                    guint64 metadata_start;
                    guint64 metadata_size;

                    g_object_get(disk, "name", &name, "guid", &guid,
                                       "device", &device,
                                       "data-start", &data_start,
                                       "data-size", &data_size,
                                       "metadata-start", &metadata_start,
                                       "metadata-size", &metadata_size,
                                       NULL);

                    printf("        Disk: %s\n", name);
                    printf("          GUID:           %s\n", guid);
                    printf("          Device:         %s\n", device);
                    printf("          Data Start:     %lu\n", data_start);
                    printf("          Data Size:      %lu\n", data_size);
                    printf("          Metadata Start: %lu\n", metadata_start);
                    printf("          Metadata Size:  %lu\n", metadata_size);

                    g_free(name);
                    g_free(guid);
                    g_free(device);
                }

                g_object_unref(disk);
            }
            g_array_unref(parts);
        }
        g_array_unref(vols);
    }
    g_array_unref(dgs); dgs = NULL;

    g_object_unref(ldm); ldm = NULL;

    return 0;
}
