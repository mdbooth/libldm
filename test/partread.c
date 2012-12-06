/* partread
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

#include "mbr.h"
#include "gpt.h"

int main(int argc, const char *argv[])
{
    int r;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <drive>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open %s: %m", argv[1]);
        return 1;
    }

    mbr_t mbr;
    r = mbr_read(fd, &mbr);
    if (r < 0) {
        switch (r) {
        case -MBR_ERROR_INVALID:
            fprintf(stderr, "Didn't detect an MBR\n");
            break;

        case -MBR_ERROR_READ:
            fprintf(stderr, "Error reading from %s: %m\n", argv[1]);
            break;

        default:
            fprintf(stderr, "Unknown error reading mbr\n");
        }

        return 1;
    }

    printf("Disk Signature: %08X\n", mbr.signature);
    for (int i = 0; i < 4; i++) {
        mbr_part_t *part = &mbr.part[i];

        printf("Partition %u\n", i);
        if (part->type == MBR_PART_EMPTY) {
            printf("  Empty\n");
            continue;
        }

        printf("  Type:             %02X\n", part->type);

        printf("  First Cylinder:   %hu\n", part->first_cylinder);
        printf("  First Head:       %hhu\n", part->first_head);
        printf("  First Sector:     %hhu\n", part->first_sector);
        printf("  Last Cylinder:    %hu\n", part->last_cylinder);
        printf("  Last Head:        %hhu\n", part->last_head);
        printf("  Last Sector:      %hhu\n", part->last_sector);

        printf("  First LB:         %u\n", part->first_lba);
        printf("  No. Sectors:      %u\n", part->n_sectors);
    }

    if (mbr.part[0].type != MBR_PART_EFI_PROTECTIVE) return 0;

    printf("\nDisk has a GPT header\n");

    gpt_handle_t *h;
    r = gpt_open(fd, &h);
    if (r != 0) {
        fprintf(stderr, "Error opening GPT: %i\n", r);
        return 1;
    }

    gpt_t gpt;
    gpt_get_header(h, &gpt);

    char uuid[37];
    uuid_unparse(gpt.disk_guid, uuid);
    printf("Disk GUID:          %s\n", uuid);
    printf("First usable LBA:   %lu\n", gpt.first_usable_lba);
    printf("Last usable LBA:    %lu\n", gpt.last_usable_lba);
    printf("PTE Array Length:   %u\n", gpt.pte_array_len);
    printf("PTE Size:           %u\n", gpt.pte_size);

    for (int i = 0; i < gpt.pte_array_len; i++) {
        gpt_pte_t pte;
        r = gpt_get_pte(h, i, &pte);
        if (r < 0) {
            fprintf(stderr, "Error fetching partition %i: %i\n", i, r);
            return 1;
        }

        if (uuid_is_null(pte.type)) continue;

        printf("\nPTE %i\n", i);
        uuid_unparse(pte.type, uuid);
        printf("  Type:         %s\n", uuid);
        uuid_unparse(pte.guid, uuid);
        printf("  GUID:         %s\n", uuid);
        printf("  First LBA:    %lu\n", pte.first_lba);
        printf("  Last LBA:     %lu\n", pte.last_lba);
        printf("  Flags:        %lX\n", pte.flags);
        printf("  Name:         %s\n", pte.name);
    }

    gpt_close(h);

    return 0;
}
