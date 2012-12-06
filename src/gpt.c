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

#include <endian.h>
#include <iconv.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <zlib.h>

#include "gpt.h"

struct _gpt_head {
    char magic[8];
    uint32_t revision;
    uint32_t size;
} __attribute__((__packed__));

struct _gpt {
    struct _gpt_head head;

    uint32_t header_crc;
    char padding1[4];

    uint64_t current_header_lba;
    uint64_t backup_header_lba;

    uint64_t first_usable_lba;
    uint64_t last_usable_lba;

    uuid_t disk_guid;

    uint64_t pte_array_start_lba;
    uint32_t pte_array_len;
    uint32_t pte_size;
    uint32_t pte_array_crc;

    char padding2[];
} __attribute__((__packed__));

struct _gpt_part {
    uuid_t type;
    uuid_t guid;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    char name[72];
} __attribute__((__packed__));

struct _gpt_handle {
    int fd;

    struct _gpt *gpt;
    char *pte_array;

    iconv_t cd;
};

int
gpt_open_secsize(int fd, const size_t secsize, gpt_handle_t **h)
{
    int err = 0;

    const off_t gpt_start = secsize;

    struct _gpt_head head;
    size_t read = 0;
    while (read < sizeof(head)) {
        ssize_t in = pread(fd, &head + read, sizeof(head) - read,
                           read + gpt_start);
        if (in == 0) return -GPT_ERROR_INVALID;
        if (in == -1) return -GPT_ERROR_READ;

        read += in;
    }

    if (memcmp(head.magic, "EFI PART", 8) != 0) return -GPT_ERROR_INVALID;

    /* Check the header size. Don't believe anything greater than 4k. */
    uint32_t header_size = le32toh(head.size);
    if (header_size > 4*1024) return -GPT_ERROR_INVALID;

    *h = malloc(sizeof(**h));
    if (*h == NULL) abort();
    (*h)->gpt = NULL;
    (*h)->pte_array = NULL;
    (*h)->cd = iconv_open("UTF-8", "UTF-16LE");

    (*h)->gpt = malloc(header_size);
    if ((*h)->gpt == NULL) abort();

    (*h)->fd = fd;

    struct _gpt *_gpt = (*h)->gpt;
    memcpy(_gpt, &head, sizeof(head));
    while (read < le32toh(head.size)) {
        ssize_t in = pread(fd, (char *)(_gpt) + read, le32toh(head.size) - read,
                           read + gpt_start);
        if (in == 0) {
            err = -GPT_ERROR_INVALID;
            goto error;
        }
        if (in == -1) {
            err = -GPT_ERROR_READ;
            goto error;
        }

        read += in;
    }

    uint32_t header_crc = _gpt->header_crc;
    _gpt->header_crc = 0;

    uint32_t crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (Bytef *)_gpt, _gpt->head.size);
    if (crc != header_crc) {
        err = -GPT_ERROR_INVALID;

        goto error;
    }

    uint32_t pte_array_len = le32toh(_gpt->pte_array_len);
    uint32_t pte_size = le32toh(_gpt->pte_size);

    /* Sanity check partition entries metadata */
    if (pte_array_len > 1024 || pte_size > 1024) {
        err = -GPT_ERROR_INVALID;
        goto error;
    }

    const uint32_t pte_array_size = pte_array_len * pte_size;
    (*h)->pte_array = malloc(pte_array_size);
    if ((*h)->pte_array == NULL) abort();

    const off_t pte_array_start = _gpt->pte_array_start_lba * secsize;
    read = 0;
    while (read < pte_array_size) {
        ssize_t in = pread(fd, (*h)->pte_array + read, pte_array_size - read,
                           read + pte_array_start);
        if (in == 0) {
            err = -GPT_ERROR_INVALID;
            goto error;
        }
        if (in == -1) {
            err = -GPT_ERROR_READ;
            goto error;
        }

        read += in;
    }

    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (Bytef *) (*h)->pte_array, pte_array_size);
    if (crc != _gpt->pte_array_crc) {
        err = -GPT_ERROR_INVALID;
        goto error;
    }

    return 0;
error:
    if ((*h)->cd) iconv_close((*h)->cd);
    free((*h)->gpt);
    free((*h)->pte_array);
    free(*h);
    return err;
}

int
gpt_open(int fd, gpt_handle_t **h)
{
    int secsize;
    if (ioctl(fd, BLKSSZGET, &secsize) == -1) {
        secsize = 512;
    }

    return gpt_open_secsize(fd, secsize, h);
}

void
gpt_close(gpt_handle_t *h)
{
    iconv_close(h->cd);
    free(h->pte_array);
    free(h->gpt);
    free(h);
}

void
gpt_get_header(gpt_handle_t *h, gpt_t *gpt)
{
    struct _gpt *_gpt = h->gpt;

    gpt->first_usable_lba = le64toh(_gpt->first_usable_lba);
    gpt->last_usable_lba = le64toh(_gpt->last_usable_lba);
    gpt->pte_array_len = le32toh(_gpt->pte_array_len);
    gpt->pte_size = le32toh(_gpt->pte_size);
    memcpy(gpt->disk_guid, _gpt->disk_guid, sizeof(gpt->disk_guid));
}

int
gpt_get_pte(gpt_handle_t *h, uint32_t n, gpt_pte_t *part)
{
    if (n >= le32toh(h->gpt->pte_array_len))
        return -GPT_ERROR_INVALID_PART;

    struct _gpt_part *_part =
        (struct _gpt_part *) (h->pte_array + le32toh(h->gpt->pte_size) * n);

    memcpy(part->type, _part->type, sizeof(part->type));
    memcpy(part->guid, _part->guid, sizeof(part->guid));
    part->first_lba = le64toh(_part->first_lba);
    part->last_lba = le64toh(_part->last_lba);
    part->flags = le64toh(_part->flags);

    char *inbuf = _part->name;
    size_t in_rem = sizeof(_part->name);
    char *outbuf = part->name;
    size_t out_rem = sizeof(part->name);

    iconv(h->cd, &inbuf, &in_rem, &outbuf, &out_rem);

    return 0;
}
