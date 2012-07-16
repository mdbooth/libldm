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

    g_type_init();

    GError *err = NULL;
    PartLDM *ldm = part_ldm_new(&err);

    const char **disk = &argv[1];
    while(*disk) {
        if (!part_ldm_add(ldm, *disk, &err)) {
            fprintf(stderr, "Error reading LDM: %s\n", err->message);
            g_object_unref(ldm);
            g_error_free(err);
            return 1;
        }

        disk++;
    }

    GArray *dgs = part_ldm_get_disk_groups(ldm, &err);
    for (int i = 0; i < dgs->len; i++) {
        PartLDMDiskGroup * const dg = g_array_index(dgs, PartLDMDiskGroup *, i);

        {
            gchar *guid;
            gchar *name;

            g_object_get(dg, "guid", &guid, "name", &name, NULL);

            printf("Disk Group: %s\n", name);
            printf("  GUID: %s\n", guid);

            g_free(guid);
            g_free(name);
        }

        GArray *vols = part_ldm_disk_group_get_volumes(dg, &err);
        for (int j = 0; j < vols->len; j++) {
            PartLDMVolume * const vol =
                g_array_index(vols, PartLDMVolume *, j);

            {
                gchar *name;
                PartLDMVolumeType type;
                guint64 size;
                guint32 part_type;
                gchar *hint;

                g_object_get(vol, "name", &name, "type", &type,
                                  "size", &size, "part-type", &part_type,
                                  "hint", &hint, NULL);

                GEnumValue * const type_v =
                    g_enum_get_value(g_type_class_peek(PART_TYPE_LDM_VOLUME_TYPE), type);

                printf("  Volume: %s\n", name);
                printf("    Type:      %s\n", type_v->value_nick);
                printf("    Size:      %lu\n", size);
                printf("    Part Type: %hhu\n", part_type);
                printf("    Hint:      %s\n", hint);

                g_free(name);
                g_free(hint);
            }

            GArray *comps = part_ldm_volume_get_components(vol, &err);
            for (int k = 0; k < comps->len; k++) {
                PartLDMComponent * const comp =
                    g_array_index(comps, PartLDMComponent *, k);

                {
                    gchar *name;
                    PartLDMComponentType type;
                    guint64 stripe_size;
                    guint32 n_columns;

                    g_object_get(comp, "name", &name, "type", &type,
                                       "stripe-size", &stripe_size,
                                       "n-columns", &n_columns, NULL);

                    GEnumValue * const type_v =
                        g_enum_get_value(g_type_class_peek(PART_TYPE_LDM_COMPONENT_TYPE), type);

                    printf("    Component: %s\n", name);
                    printf("      Type:        %s\n", type_v->value_nick);
                    printf("      Stripe Size: %lu\n", stripe_size);
                    printf("      N Columns:   %u\n", n_columns);

                    g_free(name);
                }
                
                GArray *parts = part_ldm_component_get_partitions(comp, &err);
                for (int l = 0; l < parts->len; l++) {
                    PartLDMPartition * const part =
                        g_array_index(parts, PartLDMPartition *, l);

                    {
                        gchar *name;
                        guint64 start;
                        guint64 vol_offset;
                        guint64 size;
                        guint32 index;

                        g_object_get(part, "name", &name, "start", &start,
                                           "vol-offset", &vol_offset,
                                           "size", &size, "index", &index,
                                           NULL);

                        printf("      Partition: %s\n", name);
                        printf("        Start:      %lu\n", start);
                        printf("        Vol Offset: %lu\n", vol_offset);
                        printf("        Size:       %lu\n", size);
                        printf("        Index:      %u\n", index);

                        g_free(name);
                    }

                    PartLDMDisk * const disk =
                        part_ldm_partition_get_disk(part, &err);

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
                        printf("          GUID:   %s\n", guid);
                        printf("          Device: %s\n", device);
                        printf("          Data Start: %lu\n", data_start);
                        printf("          Data Size: %lu\n", data_size);
                        printf("          Metadata Start: %lu\n",
                               metadata_start);
                        printf("          Metadata Size: %lu\n", metadata_size);

                        g_free(name);
                        g_free(guid);
                        g_free(device);
                    }

                    g_object_unref(disk);
                }
                g_array_unref(parts);
            }
            g_array_unref(comps);
        }
        g_array_unref(vols);

        //part_ldm_disk_group_dump(dg);
    }
    g_array_unref(dgs);


    g_object_unref(ldm);

    return 0;
}
