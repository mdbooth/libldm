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

#include <stdint.h>

typedef enum {
    MBR_ERROR_OK,
    MBR_ERROR_READ,
    MBR_ERROR_INVALID
} mbr_error_t;

typedef enum {
    MBR_PART_EMPTY              = 0x00,
    MBR_PART_FAT16              = 0x04,
    MBR_PART_EXTENDED           = 0x05,
    MBR_PART_FAT16B             = 0x06,
    MBR_PART_NTFS               = 0x07,
    MBR_PART_FAT32              = 0x0B,
    MBR_PART_FAT32_LBA          = 0x0C,
    MBR_PART_FAT16_LBA          = 0x0E,
    MBR_PART_EXTENDED_LBA       = 0x0F,
    MBR_PART_NTFS_HIDDEN        = 0x27,
    MBR_PART_WINDOWS_LDM        = 0x42,
    MBR_PART_LINUX              = 0x83,
    MBR_PART_EXTENDED_LINUX     = 0x85,
    MBR_PART_LINUX_LVM          = 0x8E,
    MBR_PART_EFI_PROTECTIVE     = 0xEE
} mbr_part_type_t;

typedef struct _mbr_ext_t mbr_ext_t;

typedef struct {
    uint8_t     status;

    uint8_t     first_head;
    uint16_t    first_cylinder; /* Only 10 bits are usable */
    uint8_t     first_sector;

    mbr_part_type_t type;

    uint8_t     last_head;
    uint16_t    last_cylinder; /* Only 10 bits are usable */
    uint8_t     last_sector;

    uint32_t    first_lba;
    uint32_t    n_sectors;

    mbr_ext_t   *ext;
} mbr_part_t;

typedef struct {
    char        code[440];
    uint32_t    signature;

    mbr_part_t  part[4];
} mbr_t;

struct _mbr_ext_t {
    char        code[446];
    mbr_part_t  part[4];

    mbr_part_t  *_parent;
    uint32_t    _start;
};

int mbr_read(int fd, mbr_t *mbr);
//int mbr_read_extended(int fd, mbr_part *part, mbr_ext *ext);
