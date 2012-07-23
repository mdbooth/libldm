#ifndef LIBPART_LDM_H__
#define LIBPART_LDM_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PartLDMError:
 * @PART_LDM_ERROR_INTERNAL: An internal error
 * @PART_LDM_ERROR_IO: There was an IO error accessing a device
 * @PART_LDM_ERROR_NOT_LDM: The device is not part of an LDM disk group
 * @PART_LDM_ERROR_INVALID: The LDM metadata is corrupt
 * @PART_LDM_ERROR_NOTSUPPORTED: Unsupported LDM metadata
 * @PART_LDM_ERROR_MISSING_DISK: A disk is missing from a disk group
 */
typedef enum {
    PART_LDM_ERROR_INTERNAL,
    PART_LDM_ERROR_IO,
    PART_LDM_ERROR_NOT_LDM,
    PART_LDM_ERROR_INVALID,
    PART_LDM_ERROR_INCONSISTENT,
    PART_LDM_ERROR_NOTSUPPORTED,
    PART_LDM_ERROR_MISSING_DISK
} PartLDMError;

#define PART_TYPE_LDM_ERROR (part_ldm_error_get_type())

GType part_ldm_error_get_type(void);

/* PartLDM */

#define PART_TYPE_LDM               (part_ldm_get_type())
#define PART_LDM(obj)               (G_TYPE_CHECK_INSTANCE_CAST \
                                        ((obj), PART_TYPE_LDM, PartLDM))
#define PART_LDM_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST \
                                        ((klass), PART_TYPE_LDM, PartLDMClass))
#define PART_IS_LDM(obj)            (G_TYPE_CHECK_INSTANCE_TYPE \
                                        ((obj), PART_TYPE_LDM))
#define PART_IS_LDM_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE \
                                        ((klass), PART_TYPE_LDM))
#define PART_LDM_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS \
                                        ((obj), PART_TYPE_LDM, PartLDMClass))

typedef struct _PartLDMPrivate PartLDMPrivate;

typedef struct _PartLDM PartLDM;
struct _PartLDM
{
    GObject parent;
    PartLDMPrivate *priv;
};

typedef struct _PartLDMClass PartLDMClass;
struct _PartLDMClass
{
    GObjectClass parent_class;
};

/* PartLDMDiskGroup */

#define PART_TYPE_LDM_DISK_GROUP            (part_ldm_disk_group_get_type())
#define PART_LDM_DISK_GROUP(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), PART_TYPE_LDM_DISK_GROUP, PartLDMDiskGroup))
#define PART_LDM_DISK_GROUP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), PART_TYPE_LDM_DISK_GROUP, PartLDMDiskGroupClass))
#define PART_IS_LDM_DISK_GROUP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), PART_TYPE_LDM_DISK_GROUP))
#define PART_IS_LDM_DISK_GROUP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), PART_TYPE_LDM_DISK_GROUP))
#define PART_LDM_DISK_GROUP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), PART_TYPE_LDM_DISK_GROUP, PartLDMDiskGroupClass))

typedef struct _PartLDMDiskGroupPrivate PartLDMDiskGroupPrivate;

typedef struct _PartLDMDiskGroup PartLDMDiskGroup;
struct _PartLDMDiskGroup
{
    GObject parent;
    PartLDMDiskGroupPrivate *priv;
};

typedef struct _PartLDMDiskGroupClass PartLDMDiskGroupClass;
struct _PartLDMDiskGroupClass
{
    GObjectClass parent_class;
};

/* PartLDMVolumeType */

typedef enum {
    PART_LDM_VOLUME_TYPE_GEN = 0x3,
    PART_LDM_VOLUME_TYPE_RAID5 = 0x4
} PartLDMVolumeType;

#define PART_TYPE_LDM_VOLUME_TYPE (part_ldm_volume_type_get_type())

GType part_ldm_volume_type_get_type(void);

/* PartLDMVolume */

#define PART_TYPE_LDM_VOLUME            (part_ldm_volume_get_type())
#define PART_LDM_VOLUME(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), PART_TYPE_LDM_VOLUME, PartLDMVolume))
#define PART_LDM_VOLUME_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), PART_TYPE_LDM_VOLUME, PartLDMVolumeClass))
#define PART_IS_LDM_VOLUME(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), PART_TYPE_LDM_VOLUME))
#define PART_IS_LDM_VOLUME_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), PART_TYPE_LDM_VOLUME))
#define PART_LDM_VOLUME_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), PART_TYPE_LDM_VOLUME, PartLDMVolumeClass))

typedef struct _PartLDMVolumePrivate PartLDMVolumePrivate;

typedef struct _PartLDMVolume PartLDMVolume;
struct _PartLDMVolume
{
    GObject parent;
    PartLDMVolumePrivate *priv;
};

typedef struct _PartLDMVolumeClass PartLDMVolumeClass;
struct _PartLDMVolumeClass
{
    GObjectClass parent_class;
};

/* PartLDMComponentType */

typedef enum {
    PART_LDM_COMPONENT_TYPE_STRIPED = 0x1,
    PART_LDM_COMPONENT_TYPE_SPANNED = 0x2,
    PART_LDM_COMPONENT_TYPE_RAID    = 0x3
} PartLDMComponentType;

#define PART_TYPE_LDM_COMPONENT_TYPE (part_ldm_component_type_get_type())

GType part_ldm_component_type_get_type(void);

/* PartLDMComponent */

#define PART_TYPE_LDM_COMPONENT            (part_ldm_component_get_type())
#define PART_LDM_COMPONENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), PART_TYPE_LDM_COMPONENT, PartLDMComponent))
#define PART_LDM_COMPONENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), PART_TYPE_LDM_COMPONENT, PartLDMComponentClass))
#define PART_IS_LDM_COMPONENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), PART_TYPE_LDM_COMPONENT))
#define PART_IS_LDM_COMPONENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), PART_TYPE_LDM_COMPONENT))
#define PART_LDM_COMPONENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), PART_TYPE_LDM_COMPONENT, PartLDMComponentClass))

typedef struct _PartLDMComponentPrivate PartLDMComponentPrivate;

typedef struct _PartLDMComponent PartLDMComponent;
struct _PartLDMComponent
{
    GObject parent;
    PartLDMComponentPrivate *priv;
};

typedef struct _PartLDMComponentClass PartLDMComponentClass;
struct _PartLDMComponentClass
{
    GObjectClass parent_class;
};

/* PartLDMPartition */

#define PART_TYPE_LDM_PARTITION            (part_ldm_partition_get_type())
#define PART_LDM_PARTITION(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), PART_TYPE_LDM_PARTITION, PartLDMPartition))
#define PART_LDM_PARTITION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), PART_TYPE_LDM_PARTITION, PartLDMPartitionClass))
#define PART_IS_LDM_PARTITION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), PART_TYPE_LDM_PARTITION))
#define PART_IS_LDM_PARTITION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), PART_TYPE_LDM_PARTITION))
#define PART_LDM_PARTITION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), PART_TYPE_LDM_PARTITION, PartLDMPartitionClass))

typedef struct _PartLDMPartitionPrivate PartLDMPartitionPrivate;

typedef struct _PartLDMPartition PartLDMPartition;
struct _PartLDMPartition
{
    GObject parent;
    PartLDMPartitionPrivate *priv;
};

typedef struct _PartLDMPartitionClass PartLDMPartitionClass;
struct _PartLDMPartitionClass
{
    GObjectClass parent_class;
};

/* PartLDMDisk */

#define PART_TYPE_LDM_DISK            (part_ldm_disk_get_type())
#define PART_LDM_DISK(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), PART_TYPE_LDM_DISK, PartLDMDisk))
#define PART_LDM_DISK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), PART_TYPE_LDM_DISK, PartLDMDiskClass))
#define PART_IS_LDM_DISK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), PART_TYPE_LDM_DISK))
#define PART_IS_LDM_DISK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), PART_TYPE_LDM_DISK))
#define PART_LDM_DISK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), PART_TYPE_LDM_DISK, PartLDMDiskClass))

typedef struct _PartLDMDiskPrivate PartLDMDiskPrivate;

typedef struct _PartLDMDisk PartLDMDisk;
struct _PartLDMDisk
{
    GObject parent;
    PartLDMDiskPrivate *priv;
};

typedef struct _PartLDMDiskClass PartLDMDiskClass;
struct _PartLDMDiskClass
{
    GObjectClass parent_class;
};

/* PartLDMDMTable */

#define PART_TYPE_LDM_DM_TABLE            (part_ldm_dm_table_get_type())
#define PART_LDM_DM_TABLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST \
        ((obj), PART_TYPE_LDM_DM_TABLE, PartLDMDMTable))
#define PART_LDM_DM_TABLE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST \
        ((klass), PART_TYPE_LDM_DM_TABLE, PartLDMDMTable))
#define PART_IS_LDM_DM_TABLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE \
        ((obj), PART_TYPE_LDM_DM_TABLE))
#define PART_IS_LDM_DM_TABLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE \
        ((klass), PART_TYPE_LDM_DM_TABLE))
#define PART_LDM_DM_TABLE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS \
        ((obj), PART_TYPE_LDM_DM_TABLE, PartLDMDMTableClass))

typedef struct _PartLDMDMTablePrivate PartLDMDMTablePrivate;

typedef struct _PartLDMDMTable PartLDMDMTable;
struct _PartLDMDMTable
{
    GObject parent;
    PartLDMDMTablePrivate *priv;
};

typedef struct _PartLDMDMTableClass PartLDMDMTableClass;
struct _PartLDMDMTableClass
{
    GObjectClass parent_class;
};

GType part_ldm_get_type(void);
GType part_ldm_disk_group_get_type(void);

PartLDM *part_ldm_new(GError **err);
gboolean part_ldm_add(PartLDM *o, const gchar *path, GError **err);
gboolean part_ldm_add_fd(PartLDM *o, int fd, guint secsize, const gchar *path,
                         GError **err);

void part_ldm_disk_group_dump(PartLDMDiskGroup *o);

GArray *part_ldm_get_disk_groups(PartLDM *o, GError **err);
GArray *part_ldm_disk_group_get_volumes(PartLDMDiskGroup *o, GError **err);
GArray *part_ldm_volume_get_components(PartLDMVolume *o, GError **err);
GArray *part_ldm_component_get_partitions(PartLDMComponent *o, GError **err);
PartLDMDisk *part_ldm_partition_get_disk(PartLDMPartition *o, GError **err);

GArray *part_ldm_volume_generate_dm_tables(const PartLDMVolume *o,
                                           GError **err);

G_END_DECLS

#endif /* LIBPART_LDM_H__ */
