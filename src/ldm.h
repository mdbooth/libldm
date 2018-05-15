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
 * @LDM_ERROR_INCONSISTENT: Detected two disks from the same disk group with
 *                          inconsistent metadata
 * @LDM_ERROR_NOTSUPPORTED: Unsupported LDM metadata
 * @LDM_ERROR_MISSING_DISK: A disk is missing from a disk group
 * @LDM_ERROR_EXTERNAL: An error reported by an external library
 */
typedef enum {
    LDM_ERROR_INTERNAL,
    LDM_ERROR_IO,
    LDM_ERROR_NOT_LDM,
    LDM_ERROR_INVALID,
    LDM_ERROR_INCONSISTENT,
    LDM_ERROR_NOTSUPPORTED,
    LDM_ERROR_MISSING_DISK,
    LDM_ERROR_EXTERNAL
} LDMError;

#define LDM_TYPE_ERROR (ldm_error_get_type())

GType ldm_error_get_type(void);

/* LDM */

#define LDM_TYPE            (ldm_get_type())
#define LDM_CAST(obj)       (G_TYPE_CHECK_INSTANCE_CAST \
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

/**
 * LDM:
 *
 * An LDM metadata scanner
 */
typedef struct _LDM LDM;
struct _LDM
{
    GObject parent;
    LDMPrivate *priv;
};

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

typedef struct _LDMDiskGroupPrivate LDMDiskGroupPrivate;

/**
 * LDMDiskGroup:
 *
 * An LDM Disk Group
 */
typedef struct _LDMDiskGroup LDMDiskGroup;
struct _LDMDiskGroup
{
    GObject parent;
    LDMDiskGroupPrivate *priv;
};

typedef struct
{
    GObjectClass parent_class;
} LDMDiskGroupClass;

/* LDMVolumeType */

/**
 * LDMVolumeType:
 * @LDM_VOLUME_TYPE_SIMPLE: A simple volume
 * @LDM_VOLUME_TYPE_SPANNED: A spanned volume
 * @LDM_VOLUME_TYPE_STRIPED: A striped volume
 * @LDM_VOLUME_TYPE_MIRRORED: A mirrored volume
 * @LDM_VOLUME_TYPE_RAID5: A raid5 volume
 */
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

typedef struct _LDMVolumePrivate LDMVolumePrivate;

/**
 * LDMVolume:
 * @parent: The parent #GObject
 * @priv: Private data
 *
 * An LDM Volume.
 */
typedef struct _LDMVolume LDMVolume;
struct _LDMVolume
{
    GObject parent;
    LDMVolumePrivate *priv;
};

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

typedef struct _LDMPartitionPrivate LDMPartitionPrivate;

/**
 * LDMPartition:
 * @parent: The parent #GObject
 * @priv: Private data
 *
 * An LDM Parition.
 */
typedef struct _LDMPartition LDMPartition;
struct _LDMPartition
{
    GObject parent;
    LDMPartitionPrivate *priv;
};

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

typedef struct _LDMDiskPrivate LDMDiskPrivate;

/**
 * LDMDisk:
 * @parent: The parent #GObject
 * @priv: Private data
 *
 * An LDM Disk.
 */
typedef struct _LDMDisk LDMDisk;
struct _LDMDisk
{
    GObject parent;
    LDMDiskPrivate *priv;
};

typedef struct
{
    GObjectClass parent_class;
} LDMDiskClass;

GType ldm_get_type(void);
GType ldm_disk_group_get_type(void);

/**
 * ldm_new:
 *
 * Instantiate a new LDM object. LDM scans devices and stores detected metadata.
 *
 * Returns: (transfer full): a new #LDM object
 */
LDM *ldm_new();

/**
 * ldm_add:
 * @o: An #LDM object
 * @path: The path of the device 
 * @err: A #GError to receive any generated errors
 *
 * Scan device @path and add its metadata to LDM object @o.
 *
 * Returns: true on success, false on error
 */
gboolean ldm_add(LDM *o, const gchar *path, GError **err);

/**
 * ldm_add_fd:
 * @o: An #LDM object
 * @fd: A file descriptor for reading from the device
 * @secsize: The size of a sector on the device
 * @path: The path of the device (for messages)
 * @err: A #GError to receive any generated errors
 *
 * Scan a device which has been previously opened for reading and add its
 * metadata to LDM object @o.
 *
 * Returns: true on success, false on error
 */
gboolean ldm_add_fd(LDM *o, int fd, guint secsize, const gchar *path,
                    GError **err);

/**
 * ldm_get_disk_groups:
 * @o: An #LDM object
 *
 * Get an array of discovered disk groups.
 *
 * Returns: (element-type LDMDiskGroup)(transfer container):
 *      An array of disk groups
 */
GArray *ldm_get_disk_groups(LDM *o);

/**
 * ldm_disk_group_get_volumes:
 * @o: An #LDMDiskGroup
 *
 * Get an array of all volumes in a disk group.
 *
 * Returns: (element-type LDMVolume)(transfer container):
 *      An array of volumes
 */ 
GArray *ldm_disk_group_get_volumes(LDMDiskGroup *o);

/**
 * ldm_disk_group_get_partitions:
 * @o: An #LDMPartition
 *
 * Get an array of all partitions in a disk group.
 *
 * Returns: (element-type LDMPartition)(transfer container):
 *      An array of partitions
 */
GArray *ldm_disk_group_get_partitions(LDMDiskGroup *o);

/**
 * ldm_disk_group_get_disks:
 * @o: An #LDMDiskGroup
 *
 * Get an array of all disks in a disk group.
 *
 * Returns: (element-type LDMDisk)(transfer container):
 *      An array of disks
 */
GArray *ldm_disk_group_get_disks(LDMDiskGroup *o);

/**
 * ldm_disk_group_get_name:
 * @o: An #LDMDiskGroup
 *
 * Get the Windows-assigned name of a disk group.
 *
 * Returns: (transfer full): The name
 */
gchar *ldm_disk_group_get_name(const LDMDiskGroup *o);

/**
 * ldm_disk_group_get_guid:
 * @o: An #LDMDiskGroup
 *
 * Get the Windows-assigned GUID of a disk group.
 *
 * Returns: (transfer full): The string representation of the GUID
 */
gchar *ldm_disk_group_get_guid(const LDMDiskGroup *o);

/**
 * ldm_volume_get_partitions:
 * @o: An #LDMVolume
 *
 * Get an array of all partitions in a volume.
 *
 * Returns: (element-type LDMPartition)(transfer container):
 *      An array of partitions
 */
GArray *ldm_volume_get_partitions(LDMVolume *o);

/**
 * ldm_volume_get_name:
 * @o: An #LDMVolume
 *
 * Get the Windows-assigned name of a volume.
 *
 * Returns: (transfer full): The name
 */
gchar *ldm_volume_get_name(const LDMVolume *o);

/**
 * ldm_volume_get_guid:
 * @o: An #LDMVolume
 *
 * Get the Windows-assigned GUID of a volume.
 *
 * Returns: (transfer full): The string representation of the GUID
 */
gchar *ldm_volume_get_guid(const LDMVolume *o);

/**
 * ldm_volume_get_voltype:
 * @o: An #LDMVolume
 *
 * Get the volume type. This can be:
 * - %LDM_VOLUME_TYPE_SIMPLE
 * - %LDM_VOLUME_TYPE_SPANNED
 * - %LDM_VOLUME_TYPE_STRIPED
 * - %LDM_VOLUME_TYPE_MIRRORED
 * - %LDM_VOLUME_TYPE_RAID5
 *
 * Returns: The volume type
 */
LDMVolumeType ldm_volume_get_voltype(const LDMVolume *o);

/**
 * ldm_volume_get_size:
 * @o: An #LDMVolume
 *
 * Get the volume size in sectors.
 *
 * Returns: The volume size in sectors
 */
guint64 ldm_volume_get_size(const LDMVolume *o);

/**
 * ldm_volume_get_part_type:
 * @o: An #LDMVolume
 *
 * Get the 'partition type' of the volume. This is the same 8-bit value that is
 * used to describe partition types on an MBR disk. It is 0x07 for NTFS volumes.
 *
 * Returns: The partition type
 */
guint8 ldm_volume_get_part_type(const LDMVolume *o);

/**
 * ldm_volume_get_hint:
 * @o: An #LDMVolume
 *
 * Get the volume mounting hint. This value specifies how Windows expects the
 * volume to be mounted. For a volume with an assigned drive letter, it might be
 * 'E:'.
 *
 * Returns: (transfer full): The mounting hint
 */
gchar *ldm_volume_get_hint(const LDMVolume *o);

/**
 * ldm_volume_get_chunk_size:
 * @o: An #LDMVolume
 *
 * Get the chunk size, in sectors, used by striped and raid5 volumes. For other
 * volume types it will be 0.
 *
 * Returns: The chunk size in sectors
 */
guint64 ldm_volume_get_chunk_size(const LDMVolume *o);

/**
 * ldm_volume_dm_get_name:
 * @o: An #LDMVolume
 *
 * Get the name of the device mapper device which will be created for this
 * volume. Note that returned name is unmangled. Device mapper will mangle
 * actual device name if it contains invalid characters.
 *
 * Returns: (transfer full): The device mapper name
 */
GString *ldm_volume_dm_get_name(const LDMVolume *o);

/**
 * ldm_volume_dm_get_device:
 * @o: An #LDMVolume
 * @err: A #GError to receive any generated errors
 *
 * Get the host device mapper device which was created for this volume
 * (e.g. /dev/mapper/ldm_vol_Red-nzv8x6obywgDg0_Volume3). It is dynamic
 * runtime property and it will be NULL if device mapper device is absent.
 *
 * Returns: (transfer full): The host device mapper device if present,
 *          or NULL otherwise
 */
gchar *ldm_volume_dm_get_device(const LDMVolume * const o, GError **err);

/**
 * ldm_volume_dm_create:
 * @o: An #LDMVolume
 * @created: (out): The name of the created device, if any
 * @err: A #GError to receive any generated errors
 *
 * Create a device mapper device for a volume. If this function is called for
 * volume whose device already exists it will still return success. However,
 * @created will not be set.
 *
 * Returns: True if, following the call, the device exists. False if it doesn't.
 *          @created will only be set if the call actually created the device.
 */
gboolean ldm_volume_dm_create(const LDMVolume *o, GString **created,
                              GError **err);

/**
 * ldm_volume_dm_remove:
 * @o: An #LDMVolume
 * @removed: (out): The name of the removed device, if any
 * @err: A #GError to receive any generated errors
 *
 * Remove a device mapper device for a volume. If this function is called for a
 * volume whose device does not already exist, it will still return success.
 * However, @removed will not be set.
 *
 * Returns: True if, following the call, the device does not exist. False if it
 *          does. @removed will only be set if the call actually removed the
 *          device.
 */
gboolean ldm_volume_dm_remove(const LDMVolume *o, GString **removed,
                              GError **err);

/**
 * ldm_partition_get_disk:
 * @o: An #LDMPartition
 *
 * Get the #LDMDisk underlying a partition.
 *
 * Returns: (transfer full): The underlying disk
 */
LDMDisk *ldm_partition_get_disk(LDMPartition *o);

/**
 * ldm_partition_get_name:
 * @o: An #LDMPartition
 *
 * Get the Windows-assigned name of a partition.
 *
 * Returns: (transfer full): The name
 */
gchar *ldm_partition_get_name(const LDMPartition *o);

/**
 * ldm_partition_get_start:
 * @o: An #LDMPartition
 *
 * Get the start sector of a disk, measured from the start of the underlying
 * disk.
 *
 * Returns: The start sector
 */
guint64 ldm_partition_get_start(const LDMPartition *o);

/**
 * ldm_partition_get_size:
 * @o: An #LDMPartition
 *
 * Get the size of a partition in sectors.
 *
 * Returns: The size, in sectors
 */
guint64 ldm_partition_get_size(const LDMPartition *o);

/**
 * ldm_partition_dm_get_device:
 * @o: An #LDMPartition
 * @err: A #GError to receive any generated errors
 *
 * Get the host device mapper device which was created for this partition
 * (e.g. /dev/mapper/ldm_part_Red-nzv8x6obywgDg0_Disk1-01). It is dynamic
 * runtime property and it will be NULL if device mapper device is absent.
 *
 * Returns: (transfer full): The host device mapper device if present,
 *          or NULL otherwise
 */
gchar *ldm_partition_dm_get_device(const LDMPartition * const o, GError **err);

/**
 * ldm_disk_get_name
 * @o: An #LDMDisk
 *
 * Get the Windows-assigned name of a disk.
 *
 * Returns: (transfer full): The name
 */
gchar *ldm_disk_get_name(const LDMDisk *o);

/**
 * ldm_disk_get_guid:
 * @o: An #LDMDisk
 *
 * Get the Windows-assigned GUID of a disk.
 *
 * Returns: (transfer full): The string representation of the GUID
 */
gchar *ldm_disk_get_guid(const LDMDisk *o);

/**
 * ldm_disk_get_device:
 * @o: An #LDMDisk
 *
 * Get the name of the host device (e.g. /dev/sda) of a disk. This will be NULL
 * if the disk has been identified from metadata on another disk, but has not
 * been discovered during scanning.
 *
 * Returns: The name of the host device, or NULL if the disk is missing
 */
gchar *ldm_disk_get_device(const LDMDisk *o);

/**
 * ldm_disk_get_data_start:
 * @o: An #LDMDisk
 *
 * Get the start sector of the data portion of a disk.
 *
 * Returns: The start sector
 */
guint64 ldm_disk_get_data_start(const LDMDisk *o);

/**
 * ldm_disk_get_data_size:
 * @o: An #LDMDisk
 *
 * Get the size, in sectors, of the data portion of a disk.
 *
 * Returns: The size in sectors
 */
guint64 ldm_disk_get_data_size(const LDMDisk *o);

/**
 * ldm_disk_get_metadata_start:
 * @o: An #LDMDisk
 *
 * Get the start sector of the metadata portion of a disk.
 *
 * Returns: The start sector
 */
guint64 ldm_disk_get_metadata_start(const LDMDisk *o);

/**
 * ldm_disk_get_metadata_size:
 * @o: An #LDMDisk
 *
 * Get the size, in sectors, of the metadata portion of a disk.
 *
 * Returns: The size in sectors
 */
guint64 ldm_disk_get_metadata_size(const LDMDisk *o);

G_END_DECLS

#endif /* LIBLDM_LDM_H__ */
