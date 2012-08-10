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

#ifndef LIBLDM_LDM_H__
#define LIBLDM_LDM_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define LDM_ERROR ldm_error_quark()
GQuark ldm_error_quark(void);

/**
 * LDMError:
 * @LDM_ERROR_INTERNAL: An internal error
 * @LDM_ERROR_IO: There was an IO error accessing a device
 * @LDM_ERROR_NOT_LDM: The device is not part of an LDM disk group
 * @LDM_ERROR_INVALID: The LDM metadata is corrupt
 * @LDM_ERROR_NOTSUPPORTED: Unsupported LDM metadata
 * @LDM_ERROR_MISSING_DISK: A disk is missing from a disk group
 */
typedef enum {
    LDM_ERROR_INTERNAL,
    LDM_ERROR_IO,
    LDM_ERROR_NOT_LDM,
    LDM_ERROR_INVALID,
    LDM_ERROR_INCONSISTENT,
    LDM_ERROR_NOTSUPPORTED,
    LDM_ERROR_MISSING_DISK
} LDMError;

#define LDM_TYPE_ERROR (ldm_error_get_type())

GType ldm_error_get_type(void);

/* LDM */

#define LDM_TYPE            (ldm_get_type())
#define LDM(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
                                        ((obj), LDM_TYPE, LDM))
#define LDM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
                                        ((klass), LDM_TYPE, LDMClass))
#define IS_LDM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
                                        ((obj), LDM_TYPE))
#define IS_LDM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
                                        ((klass), LDM_TYPE))
#define LDM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
                                        ((obj), LDM_TYPE, LDMClass))

typedef struct _LDM LDMPrivate;
typedef struct
{
    GObject parent;
    struct _LDM *priv;
} LDM;

typedef struct
{
    GObjectClass parent_class;
} LDMClass;

/* LDMDiskGroup */

#define LDM_TYPE_DISK_GROUP            (ldm_disk_group_get_type())
#define LDM_DISK_GROUP(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), LDM_TYPE_DISK_GROUP, LDMDiskGroup))
#define LDM_DISK_GROUP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), LDM_TYPE_DISK_GROUP, LDMDiskGroupClass))
#define LDM_IS_DISK_GROUP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), LDM_TYPE_DISK_GROUP))
#define LDM_IS_DISK_GROUP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), LDM_TYPE_DISK_GROUP))
#define LDM_DISK_GROUP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), LDM_TYPE_DISK_GROUP, LDMDiskGroupClass))

typedef struct _LDMDiskGroup LDMDiskGroupPrivate;
typedef struct
{
    GObject parent;
    LDMDiskGroupPrivate *priv;
} LDMDiskGroup;

typedef struct
{
    GObjectClass parent_class;
} LDMDiskGroupClass;

/* LDMVolumeType */

typedef enum {
    LDM_VOLUME_TYPE_SIMPLE,
    LDM_VOLUME_TYPE_SPANNED,
    LDM_VOLUME_TYPE_STRIPED,
    LDM_VOLUME_TYPE_MIRRORED,
    LDM_VOLUME_TYPE_RAID5
} LDMVolumeType;

#define LDM_TYPE_VOLUME_TYPE (ldm_volume_type_get_type())

GType ldm_volume_type_get_type(void);

/* LDMVolume */

#define LDM_TYPE_VOLUME            (ldm_volume_get_type())
#define LDM_VOLUME(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), LDM_TYPE_VOLUME, LDMVolume))
#define LDM_VOLUME_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), LDM_TYPE_VOLUME, LDMVolumeClass))
#define LDM_IS_VOLUME(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), LDM_TYPE_VOLUME))
#define LDM_IS_VOLUME_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), LDM_TYPE_VOLUME))
#define LDM_VOLUME_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), LDM_TYPE_VOLUME, LDMVolumeClass))

typedef struct _LDMVolume LDMVolumePrivate;
typedef struct
{
    GObject parent;
    LDMVolumePrivate *priv;
} LDMVolume;

typedef struct
{
    GObjectClass parent_class;
} LDMVolumeClass;

/* LDMPartition */

#define LDM_TYPE_PARTITION            (ldm_partition_get_type())
#define LDM_PARTITION(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), LDM_TYPE_PARTITION, LDMPartition))
#define LDM_PARTITION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), LDM_TYPE_PARTITION, LDMPartitionClass))
#define LDM_IS_PARTITION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), LDM_TYPE_PARTITION))
#define LDM_IS_PARTITION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), LDM_TYPE_PARTITION))
#define LDM_PARTITION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), LDM_TYPE_PARTITION, LDMPartitionClass))

typedef struct _LDMPartition LDMPartitionPrivate;
typedef struct
{
    GObject parent;
    LDMPartitionPrivate *priv;
} LDMPartition;

typedef struct
{
    GObjectClass parent_class;
} LDMPartitionClass;

/* LDMDisk */

#define LDM_TYPE_DISK            (ldm_disk_get_type())
#define LDM_DISK(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), LDM_TYPE_DISK, LDMDisk))
#define LDM_DISK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), LDM_TYPE_DISK, LDMDiskClass))
#define LDM_IS_DISK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), LDM_TYPE_DISK))
#define LDM_IS_DISK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), LDM_TYPE_DISK))
#define LDM_DISK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), LDM_TYPE_DISK, LDMDiskClass))

typedef struct _LDMDisk LDMDiskPrivate;
typedef struct
{
    GObject parent;
    LDMDiskPrivate *priv;
} LDMDisk;

typedef struct
{
    GObjectClass parent_class;
} LDMDiskClass;

/* LDMDMTable */

#define LDM_TYPE_DM_TABLE            (ldm_dm_table_get_type())
#define LDM_DM_TABLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), LDM_TYPE_DM_TABLE, LDMDMTable))
#define LDM_DM_TABLE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), LDM_TYPE_DM_TABLE, LDMDMTable))
#define LDM_IS_DM_TABLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), LDM_TYPE_DM_TABLE))
#define LDM_IS_DM_TABLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), LDM_TYPE_DM_TABLE))
#define LDM_DM_TABLE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), LDM_TYPE_DM_TABLE, LDMDMTableClass))

typedef struct _LDMDMTable LDMDMTablePrivate;
typedef struct
{
    GObject parent;
    LDMDMTablePrivate *priv;
} LDMDMTable;

typedef struct _LDMDMTableClass LDMDMTableClass;
struct _LDMDMTableClass
{
    GObjectClass parent_class;
};

GType ldm_get_type(void);
GType ldm_disk_group_get_type(void);

LDM *ldm_new(GError **err);
gboolean ldm_add(LDM *o, const gchar *path, GError **err);
gboolean ldm_add_fd(LDM *o, int fd, guint secsize, const gchar *path,
                         GError **err);

GArray *ldm_get_disk_groups(LDM *o, GError **err);
GArray *ldm_disk_group_get_volumes(LDMDiskGroup *o, GError **err);
GArray *ldm_disk_group_get_disks(LDMDiskGroup *o, GError **err);
GArray *ldm_volume_get_partitions(LDMVolume *o, GError **err);
LDMDisk *ldm_partition_get_disk(LDMPartition *o, GError **err);

GArray *ldm_volume_generate_dm_tables(const LDMVolume *o,
                                           GError **err);

G_END_DECLS

#endif /* LIBLDM_LDM_H__ */
