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

typedef struct _LDMPrivate LDMPrivate;

typedef struct _LDM LDM;
struct _LDM
{
    GObject parent;
    LDMPrivate *priv;
};

typedef struct _LDMClass LDMClass;
struct _LDMClass
{
    GObjectClass parent_class;
};

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

typedef struct _LDMDiskGroupPrivate LDMDiskGroupPrivate;

typedef struct _LDMDiskGroup LDMDiskGroup;
struct _LDMDiskGroup
{
    GObject parent;
    LDMDiskGroupPrivate *priv;
};

typedef struct _LDMDiskGroupClass LDMDiskGroupClass;
struct _LDMDiskGroupClass
{
    GObjectClass parent_class;
};

/* LDMVolumeType */

typedef enum {
    LDM_VOLUME_TYPE_GEN = 0x3,
    LDM_VOLUME_TYPE_RAID5 = 0x4
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

typedef struct _LDMVolumePrivate LDMVolumePrivate;

typedef struct _LDMVolume LDMVolume;
struct _LDMVolume
{
    GObject parent;
    LDMVolumePrivate *priv;
};

typedef struct _LDMVolumeClass LDMVolumeClass;
struct _LDMVolumeClass
{
    GObjectClass parent_class;
};

/* LDMComponentType */

typedef enum {
    LDM_COMPONENT_TYPE_STRIPED = 0x1,
    LDM_COMPONENT_TYPE_SPANNED = 0x2,
    LDM_COMPONENT_TYPE_RAID    = 0x3
} LDMComponentType;

#define LDM_TYPE_COMPONENT_TYPE (ldm_component_type_get_type())

GType ldm_component_type_get_type(void);

/* LDMComponent */

#define LDM_TYPE_COMPONENT            (ldm_component_get_type())
#define LDM_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), LDM_TYPE_COMPONENT, LDMComponent))
#define LDM_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), LDM_TYPE_COMPONENT, LDMComponentClass))
#define LDM_IS_COMPONENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), LDM_TYPE_COMPONENT))
#define LDM_IS_COMPONENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), LDM_TYPE_COMPONENT))
#define LDM_COMPONENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), LDM_TYPE_COMPONENT, LDMComponentClass))

typedef struct _LDMComponentPrivate LDMComponentPrivate;

typedef struct _LDMComponent LDMComponent;
struct _LDMComponent
{
    GObject parent;
    LDMComponentPrivate *priv;
};

typedef struct _LDMComponentClass LDMComponentClass;
struct _LDMComponentClass
{
    GObjectClass parent_class;
};

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

typedef struct _LDMPartitionPrivate LDMPartitionPrivate;

typedef struct _LDMPartition LDMPartition;
struct _LDMPartition
{
    GObject parent;
    LDMPartitionPrivate *priv;
};

typedef struct _LDMPartitionClass LDMPartitionClass;
struct _LDMPartitionClass
{
    GObjectClass parent_class;
};

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

typedef struct _LDMDiskPrivate LDMDiskPrivate;

typedef struct _LDMDisk LDMDisk;
struct _LDMDisk
{
    GObject parent;
    LDMDiskPrivate *priv;
};

typedef struct _LDMDiskClass LDMDiskClass;
struct _LDMDiskClass
{
    GObjectClass parent_class;
};

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

typedef struct _LDMDMTablePrivate LDMDMTablePrivate;

typedef struct _LDMDMTable LDMDMTable;
struct _LDMDMTable
{
    GObject parent;
    LDMDMTablePrivate *priv;
};

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
GArray *ldm_volume_get_components(LDMVolume *o, GError **err);
GArray *ldm_component_get_partitions(LDMComponent *o, GError **err);
LDMDisk *ldm_partition_get_disk(LDMPartition *o, GError **err);

GArray *ldm_volume_generate_dm_tables(const LDMVolume *o,
                                           GError **err);

G_END_DECLS

#endif /* LIBLDM_LDM_H__ */
