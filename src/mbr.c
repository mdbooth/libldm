/* libldm
 * Copyright 2012 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <unistd.h>
#include <endian.h>

#include "mbr.h"

struct _part {
    uint8_t     status;

    uint8_t     first_head;
    uint8_t     first_cs[2];

    uint8_t     type;

    uint8_t     last_head;
    uint8_t     last_cs[2];

    uint32_t    first_lba;
    uint32_t    n_sectors;
} __attribute__((__packed__));

struct _mbr {
    char code[446];

    struct _part part[4];

    uint8_t magic[2];
} __attribute__((__packed__));

int mbr_read(int fd, mbr_t *mbr)
{
    struct _mbr _mbr;

    size_t rb = 0;
    while (rb < sizeof(_mbr)) {
        ssize_t in = pread(fd, &_mbr + rb, sizeof(struct _mbr) - rb, rb);
        if (in == 0) return -MBR_ERROR_INVALID;
        if (in == -1) return -MBR_ERROR_READ;

        rb += in;
    }

    if (_mbr.magic[0] != 0x55 || _mbr.magic[1] != 0xAA)
        return -MBR_ERROR_INVALID;

    for (int i = 0; i < 4; i++) {
        struct _part *_part = &_mbr.part[i];
        mbr_part_t *part = &mbr->part[i];

        part->status = _part->status;

        part->first_head = _part->first_head;
        part->first_sector = _part->first_cs[0] & 0x3F;
        part->first_cylinder = (uint16_t) (_part->first_cs[0] & 0xC0) +
                               (uint16_t) (_part->first_cs[1]);

        part->type = _part->type;

        part->last_head = _part->last_head;
        part->last_sector = _part->last_cs[0] & 0x3F;
        part->last_cylinder = (uint16_t) (_part->last_cs[0] & 0xC0) +
                              (uint16_t) (_part->last_cs[1]);

        part->first_lba = le32toh(_part->first_lba);
        part->n_sectors = le32toh(_part->n_sectors);
    }

    return 0;
}
