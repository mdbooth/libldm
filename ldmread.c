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
        PartLDMDiskGroup *dg = g_array_index(dgs, PartLDMDiskGroup *, i);
        part_ldm_disk_group_dump(dg);
    }

    g_object_unref(ldm);

    return 0;
}
