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
    LDM *ldm = ldm_new(&err);

    const char **disk = &argv[1];
    while(*disk) {
        if (!ldm_add(ldm, *disk, &err)) {
            fprintf(stderr, "Error reading LDM: %s\n", err->message);
            g_object_unref(ldm);
            g_error_free(err);
            return 1;
        }

        disk++;
    }

    GArray *dgs = ldm_get_disk_groups(ldm, &err);
    for (int i = 0; i < dgs->len; i++) {
        LDMDiskGroup * const dg = g_array_index(dgs, LDMDiskGroup *, i);

        GArray *vols = ldm_disk_group_get_volumes(dg, &err);
        for (int j = 0; j < vols->len; j++) {
            LDMVolume * const vol = g_array_index(vols, LDMVolume *, j);
            GArray *tables = ldm_volume_generate_dm_tables(vol, &err);

            if (tables == NULL) {
                gchar *name;
                g_object_get(vol, "name", &name, NULL);

                fprintf(stderr, "Error generating tables for volume %s: %s\n",
                                name, err->message);
                g_free(name);

                g_error_free(err); err = NULL;
                continue;
            }

            for (int k = 0; k < tables->len; k++) {
                LDMDMTable * const table = g_array_index(tables, LDMDMTable *, k);

                gchar *name, *dm;
                g_object_get(table, "name", &name, "table", &dm, NULL);

                printf("%s\n%s\n", name, dm);

                g_free(name);
                g_free(dm);
            }
        }

        g_array_unref(vols);
    }
    g_array_unref(dgs); dgs = NULL;

    g_object_unref(ldm); ldm = NULL;

    return 0;
}
