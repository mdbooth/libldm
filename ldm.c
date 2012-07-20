#include <endian.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "mbr.h"
#include "gpt.h"
#include "ldm.h"

/*
 * The layout here is mostly derived from:
 * http://hackipedia.org/Disk%20formats/Partition%20tables/Windows%20NT%20Logical%20Disk%20Manager/html,%20ldmdoc/index.html
 *
 * Note that the above reference describes a slightly older version of LDM, but
 * the fields it describes remain accurate.
 *
 * The principal difference from the above version is the addition of support
 * for LDM on GPT disks.
 */

/* Fixed structures
 *
 * These structures don't contain any variable-length fields, and can therefore
 * be accessed directly.
 */

struct _ldm_privhead
{
    char magic[8]; // "PRIVHEAD"

    uint32_t unknown_sequence;
    uint16_t version_major;
    uint16_t version_minor;

    uint64_t unknown_timestamp;
    uint64_t unknown_number;
    uint64_t unknown_size1;
    uint64_t unknown_size2;

    char disk_guid[64];
    char host_guid[64];
    char disk_group_guid[64];
    char disk_group_name[32];

    uint16_t unknown1;
    char padding1[9];

    uint64_t logical_disk_start;
    uint64_t logical_disk_size;
    uint64_t ldm_config_start;
    uint64_t ldm_config_size;
    uint64_t n_tocs;
    uint64_t toc_size;
    uint32_t n_configs;
    uint32_t n_logs;
    uint64_t config_size;
    uint64_t log_size;

    uint32_t disk_signature;
    /* Values below aren't set in my data */
    char disk_set_guid[16];
    char disk_set_guid_dup[16];

} __attribute__((__packed__));

struct _ldm_tocblock_bitmap
{
    char name[8];
    uint16_t flags1;
    uint64_t start;
    uint64_t size; // Relative to start of DB
    uint64_t flags2;
} __attribute__((__packed__));

struct _ldm_tocblock
{
    char magic[8]; // "TOCBLOCK"

    uint32_t seq1;
    char padding1[4];
    uint32_t seq2;
    char padding2[16];

    struct _ldm_tocblock_bitmap bitmap[2];
} __attribute__((__packed__));

struct _ldm_vmdb
{
    char magic[4]; // "VMDB"

    uint32_t vblk_last;
    uint32_t vblk_size;
    uint32_t vblk_first_offset;

    uint16_t update_status;

    uint16_t version_major;
    uint16_t version_minor;

    char disk_group_name[31];
    char disk_group_guid[64];

    uint64_t committed_seq;
    uint64_t pending_seq;
    uint32_t n_committed_vblks_vol;
    uint32_t n_committed_vblks_comp;
    uint32_t n_committed_vblks_part;
    uint32_t n_committed_vblks_disk;
    char padding1[12];
    uint32_t n_pending_vblks_vol;
    uint32_t n_pending_vblks_comp;
    uint32_t n_pending_vblks_part;
    uint32_t n_pending_vblks_disk;
    char padding2[12];

    uint64_t last_accessed;
} __attribute__((__packed__));

/* This is the header of every VBLK entry */
struct _ldm_vblk_head
{
    char magic[4]; // "VBLK"

    uint32_t seq;

    uint32_t record_id;
    uint16_t entry;
    uint16_t entries_total;
} __attribute__((__packed__));

/* This is the header of every VBLK record, which may span multiple VBLK
 * entries. I.e. if a VBLK record is split across 2 entries, only the first will
 * have this header immediately following the entry header. */
struct _ldm_vblk_rec_head
{
    uint16_t status;
    uint8_t  flags;
    uint8_t  type;
    uint32_t size;
} __attribute__((__packed__));

/* Array clearing functions */

static void
_unref_object(gpointer const data)
{
    g_object_unref(*(GObject **)data);
}

static void
_free_pointer(gpointer const data)
{
    g_free(*(gpointer *)data);
}

/* GLIB error handling */

#define LDM_ERROR (part_ldm_error_quark())

static GQuark
part_ldm_error_quark(void)
{
    return g_quark_from_static_string("part_ldm");
}

GType
part_ldm_error_get_type(void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {
            { PART_LDM_ERROR_INTERNAL, "PART_LDM_ERROR_INTERNAL", "internal" },
            { PART_LDM_ERROR_IO, "PART_LDM_ERROR_IO", "io" },
            { PART_LDM_ERROR_NOT_LDM, "PART_LDM_ERROR_NOT_LDM", "not_ldm" },
            { PART_LDM_ERROR_INVALID, "PART_LDM_ERROR_INVALID", "invalid" },
            { PART_LDM_ERROR_INCONSISTENT, "PART_LDM_ERROR_INCONSISTENT",
                                           "inconsistent" },
            { PART_LDM_ERROR_NOTSUPPORTED, "PART_LDM_ERROR_NOTSUPPORTED",
                                           "notsupported" },
            { PART_LDM_ERROR_MISSING_DISK, "PART_LDM_ERROR_MISSING_DISK",
                                           "missing-disk" }
        };
        etype = g_enum_register_static("PartLDMError", values);
    }
    return etype;
}

/* PartLDM */

#define PART_LDM_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM, PartLDMPrivate))

struct _PartLDMPrivate
{
    GArray *disk_groups;
};

G_DEFINE_TYPE(PartLDM, part_ldm, G_TYPE_OBJECT)

static void
part_ldm_dispose(GObject * const object)
{
    PartLDM *ldm = PART_LDM(object);

    if (ldm->priv->disk_groups) {
        g_array_unref(ldm->priv->disk_groups); ldm->priv->disk_groups = NULL;
    }
}

static void
part_ldm_init(PartLDM * const o)
{
    o->priv = PART_LDM_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

static void
part_ldm_class_init(PartLDMClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = part_ldm_dispose;

    g_type_class_add_private(klass, sizeof(PartLDMPrivate));
}

/* PartLDMDiskGroup */

#define PART_LDM_DISK_GROUP_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_DISK_GROUP, PartLDMDiskGroupPrivate))

struct _PartLDMDiskGroupPrivate
{
    uuid_t guid;
    uint32_t id;
    char *name;

    uint64_t sequence;

    uint32_t n_disks;
    uint32_t n_comps;
    uint32_t n_parts;
    uint32_t n_vols;

    GArray *disks;
    GArray *comps;
    GArray *parts;
    GArray *vols;
};

G_DEFINE_TYPE(PartLDMDiskGroup, part_ldm_disk_group, G_TYPE_OBJECT)

enum {
    PROP_PART_LDM_DISK_GROUP_PROP0,
    PROP_PART_LDM_DISK_GROUP_GUID,
    PROP_PART_LDM_DISK_GROUP_NAME
};

static void
part_ldm_disk_group_get_property(GObject * const o, const guint property_id,
                                 GValue * const value, GParamSpec * const pspec)
{
    PartLDMDiskGroup * const dg = PART_LDM_DISK_GROUP(o);
    PartLDMDiskGroupPrivate * const priv = dg->priv;

    switch (property_id) {
    case PROP_PART_LDM_DISK_GROUP_GUID:
        {
            char guid_str[37];
            uuid_unparse(priv->guid, guid_str);
            g_value_set_string(value, guid_str);
        }
        break;

    case PROP_PART_LDM_DISK_GROUP_NAME:
        g_value_set_string(value, priv->name); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

static void
part_ldm_disk_group_dispose(GObject * const object)
{
    PartLDMDiskGroup *dg = PART_LDM_DISK_GROUP(object);

    if (dg->priv->vols) {
        g_array_unref(dg->priv->vols); dg->priv->vols = NULL;
    }
    if (dg->priv->comps) {
        g_array_unref(dg->priv->comps); dg->priv->comps = NULL;
    }
    if (dg->priv->parts) {
        g_array_unref(dg->priv->parts); dg->priv->parts = NULL;
    }
    if (dg->priv->disks) {
        g_array_unref(dg->priv->disks); dg->priv->disks = NULL;
    }
}

static void
part_ldm_disk_group_finalize(GObject * const object)
{
    PartLDMDiskGroup *dg = PART_LDM_DISK_GROUP(object);

    g_free(dg->priv->name); dg->priv->name = NULL;
}

static void
part_ldm_disk_group_class_init(PartLDMDiskGroupClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = part_ldm_disk_group_dispose;
    object_class->finalize = part_ldm_disk_group_finalize;
    object_class->get_property = part_ldm_disk_group_get_property;

    g_type_class_add_private(klass, sizeof(PartLDMDiskGroupPrivate));

    /**
     * PartLDMDiskGroup:guid:
     *
     * A string representation of the disk group's GUID.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_GROUP_GUID,
        g_param_spec_string(
            "guid", "GUID",
            "A string representation of the disk group's GUID",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDiskGroup:name:
     *
     * The name of the disk group.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_GROUP_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the disk group",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
part_ldm_disk_group_init(PartLDMDiskGroup * const o)
{
    o->priv = PART_LDM_DISK_GROUP_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* PartLDMVolumeType */

GType
part_ldm_volume_type_get_type(void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {
            { PART_LDM_VOLUME_TYPE_GEN, "PART_LDM_VOLUME_TYPE_GEN", "gen" },
            { PART_LDM_VOLUME_TYPE_RAID5,
              "PART_LDM_VOLUME_TYPE_RAID5", "raid5" }
        };
        etype = g_enum_register_static("PartLDMVolumeType", values);
    }
    return etype;
}

/* PartLDMVolume */

#define PART_LDM_VOLUME_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_VOLUME, PartLDMVolumePrivate))

struct _PartLDMVolumePrivate
{
    guint32 id;
    gchar *name;
    gchar *dgname;

    PartLDMVolumeType type;
    guint64 size;
    guint8 part_type;

    guint8 volume_type;
    guint8 flags; /* Not exposed: unclear what it means */

    guint32 n_comps;
    GArray *comps;

    gchar *id1;
    gchar *id2;
    guint64 size2;
    gchar *hint;
};

G_DEFINE_TYPE(PartLDMVolume, part_ldm_volume, G_TYPE_OBJECT)

enum {
    PROP_PART_LDM_VOLUME_PROP0,
    PROP_PART_LDM_VOLUME_NAME,
    PROP_PART_LDM_VOLUME_TYPE,
    PROP_PART_LDM_VOLUME_SIZE,
    PROP_PART_LDM_VOLUME_PART_TYPE,
    PROP_PART_LDM_VOLUME_HINT
};

static void
part_ldm_volume_get_property(GObject * const o, const guint property_id,
                             GValue * const value, GParamSpec *pspec)
{
    PartLDMVolume * const vol = PART_LDM_VOLUME(o);
    PartLDMVolumePrivate * const priv = vol->priv;

    switch (property_id) {
    case PROP_PART_LDM_VOLUME_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_PART_LDM_VOLUME_TYPE:
        g_value_set_enum(value, priv->type); break;

    case PROP_PART_LDM_VOLUME_SIZE:
        g_value_set_uint64(value, priv->size); break;

    case PROP_PART_LDM_VOLUME_PART_TYPE:
        g_value_set_uint(value, priv->part_type); break;

    case PROP_PART_LDM_VOLUME_HINT:
        g_value_set_string(value, priv->hint); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

static void
part_ldm_volume_dispose(GObject * const object)
{
    PartLDMVolume * const vol_o = PART_LDM_VOLUME(object);
    PartLDMVolumePrivate * const vol = vol_o->priv;

    if (vol->comps) { g_array_unref(vol->comps); vol->comps = NULL; }
}

static void
part_ldm_volume_finalize(GObject * const object)
{
    PartLDMVolume * const vol_o = PART_LDM_VOLUME(object);
    PartLDMVolumePrivate * const vol = vol_o->priv;

    g_free(vol->name); vol->name = NULL;
    g_free(vol->dgname); vol->dgname = NULL;
    g_free(vol->id1); vol->id1 = NULL;
    g_free(vol->id2); vol->id2 = NULL;
    g_free(vol->hint); vol->hint = NULL;
}

static void
part_ldm_volume_class_init(PartLDMVolumeClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = part_ldm_volume_dispose;
    object_class->finalize = part_ldm_volume_finalize;
    object_class->get_property = part_ldm_volume_get_property;

    g_type_class_add_private(klass, sizeof(PartLDMVolumePrivate));

    /**
     * PartLDMVolume:name:
     *
     * The volume's name.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_VOLUME_NAME,
        g_param_spec_string(
            "name", "Name", "The volume's name",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMVolume:type:
     *
     * The volume type: gen or raid5.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_VOLUME_TYPE,
        g_param_spec_enum(
            "type", "Type", "The volume type: gen or raid5",
            PART_TYPE_LDM_VOLUME_TYPE, PART_LDM_VOLUME_TYPE_GEN,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMVolume:size:
     *
     * The volume size in sectors.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_VOLUME_SIZE,
        g_param_spec_uint64(
            "size", "Size", "The volume size in sectors",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMVolume:part-type:
     *
     * A 1-byte type descriptor of the volume's contents. This descriptor has
     * the same meaning as for an MBR partition.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_VOLUME_PART_TYPE,
        g_param_spec_uint(
            "part-type", "Partition Type", "A 1-byte type descriptor of the "
            "volume's contents. This descriptor has the same meaning as for "
            "an MBR partition",
            0, G_MAXUINT8, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMVolume:hint:
     *
     * A hint to Windows as to which drive letter to assign to this volume.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_VOLUME_HINT,
        g_param_spec_string(
            "hint", "Hint", "A hint to Windows as to which drive letter to "
            "assign to this volume",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
part_ldm_volume_init(PartLDMVolume * const o)
{
    o->priv = PART_LDM_VOLUME_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* PartLDMComponentType */

GType
part_ldm_component_type_get_type(void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {
            { PART_LDM_COMPONENT_TYPE_STRIPED,
              "PART_LDM_COMPONENT_TYPE_STRIPED", "striped" },
            { PART_LDM_COMPONENT_TYPE_SPANNED,
              "PART_LDM_COMPONENT_TYPE_SPANNED", "spanned" },
            { PART_LDM_COMPONENT_TYPE_RAID,
              "PART_LDM_COMPONENT_TYPE_RAID", "raid" }
        };
        etype = g_enum_register_static("PartLDMComponentType", values);
    }
    return etype;
}

/* PartLDMComponent */

#define PART_LDM_COMPONENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_COMPONENT, PartLDMComponentPrivate))

struct _PartLDMComponentPrivate
{
    guint32 id;
    guint32 parent_id;
    gchar *name;

    PartLDMComponentType type;
    uint32_t n_parts;
    GArray *parts;

    guint64 stripe_size;
    guint32 n_columns;
};

G_DEFINE_TYPE(PartLDMComponent, part_ldm_component, G_TYPE_OBJECT)

enum {
    PROP_PART_LDM_COMPONENT_PROP0,
    PROP_PART_LDM_COMPONENT_NAME,
    PROP_PART_LDM_COMPONENT_TYPE,
    PROP_PART_LDM_COMPONENT_STRIPE_SIZE,
    PROP_PART_LDM_COMPONENT_N_COLUMNS
};

static void
part_ldm_component_get_property(GObject * const o, const guint property_id,
                                GValue * const value, GParamSpec * const pspec)
{
    PartLDMComponent * const comp = PART_LDM_COMPONENT(o);
    PartLDMComponentPrivate * const priv = comp->priv;

    switch (property_id) {
    case PROP_PART_LDM_COMPONENT_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_PART_LDM_COMPONENT_TYPE:
        g_value_set_enum(value, priv->type); break;

    case PROP_PART_LDM_COMPONENT_STRIPE_SIZE:
        g_value_set_uint64(value, priv->stripe_size); break;

    case PROP_PART_LDM_COMPONENT_N_COLUMNS:
        g_value_set_uint(value, priv->n_columns); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

static void
part_ldm_component_dispose(GObject * const object)
{
    PartLDMComponent * const comp_o = PART_LDM_COMPONENT(object);
    PartLDMComponentPrivate * const comp = comp_o->priv;

    if (comp->parts) { g_array_unref(comp->parts); comp->parts = NULL; }
}

static void
part_ldm_component_finalize(GObject * const object)
{
    PartLDMComponent * const comp_o = PART_LDM_COMPONENT(object);
    PartLDMComponentPrivate * const comp = comp_o->priv;

    g_free(comp->name); comp->name = NULL;
}

static void
part_ldm_component_class_init(PartLDMComponentClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = part_ldm_component_dispose;
    object_class->finalize = part_ldm_component_finalize;
    object_class->get_property = part_ldm_component_get_property;

    g_type_class_add_private(klass, sizeof(PartLDMComponentPrivate));

    /**
     * PartLDMComponent:name:
     *
     * The name of the component.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_COMPONENT_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the component",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMComponent:type:
     *
     * The type of the component.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_COMPONENT_TYPE,
        g_param_spec_enum(
            "type", "Type", "The type of the component",
            PART_TYPE_LDM_COMPONENT_TYPE, PART_LDM_COMPONENT_TYPE_STRIPED,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMComponent:stripe-size:
     *
     * The stripe size of the component in sectors, if relevant. This will be
     * zero if the component does not have a stripe size.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_COMPONENT_STRIPE_SIZE,
        g_param_spec_uint64(
            "stripe-size", "Stripe Size", "The stripe size of the component "
            "in sectors, if relevant. This will be zero if the component does "
            "not have a stripe size",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMComponent:n-columns:
     *
     * The number of columns the component has, if relevant. This will be zero
     * if the component does not have columns.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_COMPONENT_N_COLUMNS,
        g_param_spec_uint(
            "n-columns", "No. Columns", "The number of columns the component "
            "has, if relevant. This will be zero if the component does not "
            "have columns",
            0, G_MAXUINT32, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
part_ldm_component_init(PartLDMComponent * const o)
{
    o->priv = PART_LDM_COMPONENT_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* PartLDMPartition */

#define PART_LDM_PARTITION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_PARTITION, PartLDMPartitionPrivate))

struct _PartLDMPartitionPrivate
{
    guint32 id;
    guint32 parent_id;
    gchar *name;

    guint64 start;
    guint64 vol_offset;
    guint64 size;
    guint32 index;

    guint32 disk_id;
    PartLDMDisk *disk;
};

G_DEFINE_TYPE(PartLDMPartition, part_ldm_partition, G_TYPE_OBJECT)

enum {
    PROP_PART_LDM_PARTITION_PROP0,
    PROP_PART_LDM_PARTITION_NAME,
    PROP_PART_LDM_PARTITION_START,
    PROP_PART_LDM_PARTITION_VOL_OFFSET,
    PROP_PART_LDM_PARTITION_SIZE,
    PROP_PART_LDM_PARTITION_INDEX
};

static void
part_ldm_partition_get_property(GObject * const o, const guint property_id,
                                GValue * const value, GParamSpec * const pspec)
{
    PartLDMPartition * const part = PART_LDM_PARTITION(o);
    PartLDMPartitionPrivate * const priv = part->priv;

    switch (property_id) {
    case PROP_PART_LDM_PARTITION_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_PART_LDM_PARTITION_START:
        g_value_set_uint64(value, priv->start); break;

    case PROP_PART_LDM_PARTITION_VOL_OFFSET:
        g_value_set_uint64(value, priv->vol_offset); break;

    case PROP_PART_LDM_PARTITION_SIZE:
        g_value_set_uint64(value, priv->size); break;

    case PROP_PART_LDM_PARTITION_INDEX:
        g_value_set_uint(value, priv->index); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

static void
part_ldm_partition_dispose(GObject * const object)
{
    PartLDMPartition * const part_o = PART_LDM_PARTITION(object);
    PartLDMPartitionPrivate * const part = part_o->priv;

    if (part->disk) { g_object_unref(part->disk); part->disk = NULL; }
}

static void
part_ldm_partition_finalize(GObject * const object)
{
    PartLDMPartition * const part_o = PART_LDM_PARTITION(object);
    PartLDMPartitionPrivate * const part = part_o->priv;

    g_free(part->name); part->name = NULL;
}

static void
part_ldm_partition_class_init(PartLDMPartitionClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = part_ldm_partition_dispose;
    object_class->finalize = part_ldm_partition_finalize;
    object_class->get_property = part_ldm_partition_get_property;

    g_type_class_add_private(klass, sizeof(PartLDMPartitionPrivate));

    /**
     * PartLDMPartition:name:
     *
     * The name of the partition.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_PARTITION_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the partition",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMPartition:start:
     *
     * The start sector of the partition.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_PARTITION_START,
        g_param_spec_uint64(
            "start", "Start", "The start sector of the partition",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMPartition:vol-offset:
     *
     * The offset of the start of this partition from the start of the volume in
     * sectors.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_PARTITION_VOL_OFFSET,
        g_param_spec_uint64(
            "vol-offset", "Volume Offset", "The offset of the start of this "
            "partition from the start of the volume in sectors",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMPartition:size:
     *
     * The size of the partition in sectors.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_PARTITION_SIZE,
        g_param_spec_uint64(
            "size", "Size", "The size of the partition in sectors",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMPartition:index:
     *
     * The index of this partition in the set of partitions of the containing
     * component.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_PARTITION_INDEX,
        g_param_spec_uint(
            "index", "Index", "The index of this partition in the set of "
            "partitions of the containing component",
            0, G_MAXUINT32, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
part_ldm_partition_init(PartLDMPartition * const o)
{
    o->priv = PART_LDM_PARTITION_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* PartLDMDisk */

#define PART_LDM_DISK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_DISK, PartLDMDiskPrivate))

struct _PartLDMDiskPrivate
{
    guint32 id;
    gchar *name;
    gchar *dgname;

    guint64 data_start;
    guint64 data_size;
    guint64 metadata_start;
    guint64 metadata_size;

    uuid_t guid;
    gchar *device; // NULL until device is found
};

G_DEFINE_TYPE(PartLDMDisk, part_ldm_disk, G_TYPE_OBJECT)

enum {
    PROP_PART_LDM_DISK_PROP0,
    PROP_PART_LDM_DISK_NAME,
    PROP_PART_LDM_DISK_GUID,
    PROP_PART_LDM_DISK_DEVICE,
    PROP_PART_LDM_DISK_DATA_START,
    PROP_PART_LDM_DISK_DATA_SIZE,
    PROP_PART_LDM_DISK_METADATA_START,
    PROP_PART_LDM_DISK_METADATA_SIZE
};

static void
part_ldm_disk_get_property(GObject * const o, const guint property_id,
                           GValue * const value, GParamSpec * const pspec)
{
    const PartLDMDisk * const disk = PART_LDM_DISK(o);
    const PartLDMDiskPrivate * const priv = disk->priv;

    switch (property_id) {
    case PROP_PART_LDM_DISK_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_PART_LDM_DISK_GUID:
        {
            char guid_str[37];
            uuid_unparse(priv->guid, guid_str);
            g_value_set_string(value, guid_str);
        }
        break;

    case PROP_PART_LDM_DISK_DEVICE:
        g_value_set_string(value, priv->device); break;

    case PROP_PART_LDM_DISK_DATA_START:
        g_value_set_uint64(value, priv->data_start); break;

    case PROP_PART_LDM_DISK_DATA_SIZE:
        g_value_set_uint64(value, priv->data_size); break;

    case PROP_PART_LDM_DISK_METADATA_START:
        g_value_set_uint64(value, priv->metadata_start); break;

    case PROP_PART_LDM_DISK_METADATA_SIZE:
        g_value_set_uint64(value, priv->metadata_size); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

static void
part_ldm_disk_finalize(GObject * const object)
{
    PartLDMDisk * const disk_o = PART_LDM_DISK(object);
    PartLDMDiskPrivate * const disk = disk_o->priv;

    g_free(disk->name); disk->name = NULL;
    g_free(disk->dgname); disk->dgname = NULL;
    g_free(disk->device); disk->device = NULL;
}

static void
part_ldm_disk_class_init(PartLDMDiskClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = part_ldm_disk_finalize;
    object_class->get_property = part_ldm_disk_get_property;

    g_type_class_add_private(klass, sizeof(PartLDMDiskPrivate));

    /**
     * PartLDMDisk:name:
     *
     * The name of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the disk",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDisk:guid:
     *
     * The GUID of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_GUID,
        g_param_spec_string(
            "guid", "GUID", "The GUID of the disk",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDisk:device:
     *
     * The underlying device of this disk. This may be NULL if the disk is
     * missing from the disk group.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_DEVICE,
        g_param_spec_string(
            "device", "Device", "The underlying device of this disk. This may "
            "be NULL if the disk is missing from the disk group",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDisk:data-start:
     *
     * The start sector of the data area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_DATA_START,
        g_param_spec_uint64(
            "data-start", "Data Start", "The start sector of the data area of "
            "the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDisk:data-size:
     *
     * The size, in sectors, of the data area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_DATA_SIZE,
        g_param_spec_uint64(
            "data-size", "Data Size", "The size, in sectors, of the data area "
            "of the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDisk:metadata-start:
     *
     * The start sector of the metadata area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_METADATA_START,
        g_param_spec_uint64(
            "metadata-start", "Metadata Start", "The start sector of the "
            "metadata area of the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDisk:metadata-size:
     *
     * The size, in sectors, of the metadata area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DISK_METADATA_SIZE,
        g_param_spec_uint64(
            "metadata-size", "Metadata Size", "The size, in sectors, of the "
            "metadata area of the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
part_ldm_disk_init(PartLDMDisk * const o)
{
    o->priv = PART_LDM_DISK_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* PartLDMDMTable */

#define PART_LDM_DM_TABLE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_DM_TABLE, PartLDMDMTablePrivate))

struct _PartLDMDMTablePrivate
{
    GString * name;
    GString * table;
};

G_DEFINE_TYPE(PartLDMDMTable, part_ldm_dm_table, G_TYPE_OBJECT)

enum {
    PROP_PART_LDM_DM_TABLE_PROP0,
    PROP_PART_LDM_DM_TABLE_NAME,
    PROP_PART_LDM_DM_TABLE_TABLE
};

static void
part_ldm_dm_table_get_property(GObject * const o, const guint property_id,
                               GValue * const value, GParamSpec * const pspec)
{
    const PartLDMDMTable * const table_o = PART_LDM_DM_TABLE(o);
    const PartLDMDMTablePrivate * const table = table_o->priv;

    switch (property_id) {
    case PROP_PART_LDM_DM_TABLE_NAME:
        g_value_set_string(value, table->name->str); break;

    case PROP_PART_LDM_DM_TABLE_TABLE:
        g_value_set_string(value, table->table->str); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

static void
part_ldm_dm_table_finalize(GObject * const object)
{
    PartLDMDMTable * const table_o = PART_LDM_DM_TABLE(object);
    PartLDMDMTablePrivate * const table = table_o->priv;

    if (table->name) { g_string_free(table->name, TRUE); table->name = NULL; }
    if (table->table) {
        g_string_free(table->table, TRUE); table->table = NULL;
    }
}

static void
part_ldm_dm_table_class_init(PartLDMDMTableClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = part_ldm_dm_table_finalize;
    object_class->get_property = part_ldm_dm_table_get_property;

    g_type_class_add_private(klass, sizeof(PartLDMDiskPrivate));

    /**
     * PartLDMDMTable:name:
     *
     * The name of the device the table describes.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DM_TABLE_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the device the table describes",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * PartLDMDMTable:table:
     *
     * The table describing the device mapper device.
     */
    g_object_class_install_property(
        object_class,
        PROP_PART_LDM_DM_TABLE_TABLE,
        g_param_spec_string(
            "table", "Table", "The table describing the device mapper device",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
part_ldm_dm_table_init(PartLDMDMTable * const o)
{
    o->priv = PART_LDM_DM_TABLE_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

#define SECTOR_SIZE (0x200)

static gboolean
_find_vmdb(const void * const config, const gchar * const path,
           const struct _ldm_vmdb ** const vmdb, GError ** const err)
{
    /* TOCBLOCK starts 2 sectors into config */
    const struct _ldm_tocblock *tocblock = config + SECTOR_SIZE * 2;
    if (memcmp(tocblock->magic, "TOCBLOCK", 8) != 0) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Didn't find TOCBLOCK at config offset %lX",
                    UINT64_C(0x400));
        return FALSE;
    }

    /* Find the start of the DB */
    *vmdb = NULL;
    for (int i = 0; i < 2; i++) {
        const struct _ldm_tocblock_bitmap *bitmap = &tocblock->bitmap[i];
        if (strcmp(bitmap->name, "config") == 0) {
            *vmdb = config + be64toh(tocblock->bitmap[i].start) * SECTOR_SIZE;
            break;
        }
    }

    if (*vmdb == NULL) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "TOCBLOCK doesn't contain config bitmap");
        return FALSE;
    }

    if (memcmp((*vmdb)->magic, "VMDB", 4) != 0) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Didn't find VMDB at config offset %lX",
                    (void *) (*vmdb) - config);
        return FALSE;
    }

    return TRUE;
}

static gboolean
_read_config(const int fd, const gchar * const path,
             const struct _ldm_privhead * const privhead,
             void ** const config, GError ** const err)
{
    /* Sanity check ldm_config_start and ldm_config_size */
    struct stat stat;
    if (fstat(fd, &stat) == -1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                    "Unable to stat %s: %m", path);
        return FALSE;
    }

    uint64_t size = stat.st_size;
    if (S_ISBLK(stat.st_mode)) {
        if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                        "Unable to get block device size for %s: %m", path);
            return FALSE;
        }
    }

    const uint64_t config_start =
        be64toh(privhead->ldm_config_start) * SECTOR_SIZE;
    const uint64_t config_size =
        be64toh(privhead->ldm_config_size) * SECTOR_SIZE;

    if (config_start > size) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "LDM config start (%lX) is outside file in %s",
                    config_start, path);
        return FALSE;
    }
    if (config_start + config_size > size) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "LDM config end (%lX) is outside file in %s",
                    config_start + config_size, path);
        return FALSE;
    }

    *config = g_malloc(config_size);
    size_t read = 0;
    while (read < config_size) {
        ssize_t in = pread(fd, *config + read, config_size - read,
                           config_start + read);
        if (in == 0) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "%s contains invalid LDM metadata", path);
            goto error;
        }

        if (in == -1) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                        "Error reading from %s: %m", path);
            goto error;
        }

        read += in;
    }

    return TRUE;

error:
    g_free(*config); *config = NULL;
    return FALSE;
}

static gboolean
_read_privhead_off(const int fd, const gchar * const path,
                   const uint64_t ph_start,
                   struct _ldm_privhead * const privhead, GError **err)
{
    size_t read = 0;
    while (read < sizeof(*privhead)) {
        ssize_t in = pread(fd, (char *) privhead + read,
                           sizeof(*privhead) - read,
                           ph_start + read);
        if (in == 0) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "%s contains invalid LDM metadata", path);
            return FALSE;
        }

        if (in == -1) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                        "Error reading from %s: %m", path);
            return FALSE;
        }

        read += in;
    }

    if (memcmp(privhead->magic, "PRIVHEAD", 8) != 0) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "PRIVHEAD not found at offset %lX", ph_start);
        return FALSE;
    }

    return TRUE;
}

static gboolean
_read_privhead_mbr(const int fd, const gchar * const path,
                   struct _ldm_privhead * const privhead, GError ** const err)
{
    /* On an MBR disk, the first PRIVHEAD is in sector 6 */
    return _read_privhead_off(fd, path, SECTOR_SIZE * 6, privhead, err);
}

void _map_gpt_error(const int e, const gchar * const path, GError ** const err)
{
    switch (-e) {
    case GPT_ERROR_INVALID:
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "%s contains an invalid GPT header", path);
        break;

    case GPT_ERROR_READ:
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                    "Error reading from %s: %m", path);
        break;

    case GPT_ERROR_INVALID_PART:
        g_error("Request for invalid GPT partition");

    default:
        g_error("Unhandled GPT return value: %i\n", e);
    }

}

static gboolean
_read_privhead_gpt(const int fd, const gchar * const path,
                   struct _ldm_privhead * const privhead, GError ** const err)
{
    int r;

    gpt_handle_t *h;
    r = gpt_open(fd, &h);
    if (r < 0) {
        _map_gpt_error(r, path, err);
        return FALSE;
    }

    gpt_t gpt;
    gpt_get_header(h, &gpt);

    static const uuid_t LDM_METADATA = { 0xAA,0xC8,0x08,0x58,
                                         0x8F,0x7E,
                                         0xE0,0x42,
                                         0x85,0xD2,
                                         0xE1,0xE9,0x04,0x34,0xCF,0xB3 };

    for (int i = 0; i < gpt.pte_array_len; i++) {
        gpt_pte_t pte;
        r = gpt_get_pte(h, 0, &pte);
        if (r < 0) {
            _map_gpt_error(r, path, err);
            gpt_close(h);
            return FALSE;
        }

        if (uuid_compare(pte.type, LDM_METADATA) == 0) {
            /* PRIVHEAD is in the last LBA of the LDM metadata partition */
            gpt_close(h);
            return _read_privhead_off(fd, path, pte.last_lba * SECTOR_SIZE,
                                       privhead, err);
        }
    }

    g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOT_LDM,
                "%s does not contain LDM metadata", path);
    return FALSE;
}

static gboolean
_read_privhead(const int fd, const gchar * const path,
               struct _ldm_privhead * const privhead, GError ** const err)
{
    // Whether the disk is MBR or GPT, we expect to find an MBR at the beginning
    mbr_t mbr;
    int r = mbr_read(fd, &mbr);
    if (r < 0) {
        switch (-r) {
        case MBR_ERROR_INVALID:
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Didn't detect a partition table");
            return FALSE;

        case MBR_ERROR_READ:
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                        "Error reading from %s: %m", path);
            return FALSE;

        default:
            g_error("Unhandled return value from mbr_read: %i", r);
        }
    }

    switch (mbr.part[0].type) {
    case MBR_PART_WINDOWS_LDM:
        return _read_privhead_mbr(fd, path, privhead, err);

    case MBR_PART_EFI_PROTECTIVE:
        return _read_privhead_gpt(fd, path, privhead, err);

    default:
        return TRUE;
    }
}

#define PARSE_VAR_INT(func_name, out_type)                                     \
static gboolean                                                                \
func_name(const guint8 ** const var, out_type * const out,                     \
          const gchar * const field, const gchar * const type,                 \
          GError ** const err)                                                 \
{                                                                              \
    guint8 i = **var; (*var)++;                                                \
    if (i > sizeof(*out)) {                                                    \
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INTERNAL,                   \
                    "Found %hhu byte integer for %s:%s", i, field, type);      \
        return FALSE;                                                          \
    }                                                                          \
                                                                               \
    *out = 0;                                                                  \
    for(;i > 0; i--) {                                                         \
        *out <<= 8;                                                            \
        *out += **var; (*var)++;                                               \
    }                                                                          \
                                                                               \
    return TRUE;                                                               \
}

PARSE_VAR_INT(_parse_var_int32, uint32_t)
PARSE_VAR_INT(_parse_var_int64, uint64_t)

static gchar *
_parse_var_string(const guint8 ** const var)
{
    guint8 len = **var; (*var)++;
    gchar *ret = g_malloc(len + 1);
    memcpy(ret, *var, len); (*var) += len;
    ret[len] = '\0';

    return ret;
}

static void
_parse_var_skip(const guint8 ** const var)
{
    guint8 len = **var; (*var)++;
    (*var) += len;
}

static gboolean
_parse_vblk_vol(const guint8 revision, const guint16 flags,
                const guint8 * vblk, PartLDMVolumePrivate * const vol,
                GError ** const err)
{
    if (revision != 5) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported volume VBLK revision %hhu", revision);
        return FALSE;
    }

    if (!_parse_var_int32(&vblk, &vol->id, "id", "volume", err))
        return FALSE;
    vol->name = _parse_var_string(&vblk);

    /* Volume type: 'gen' or 'raid5'. We parse this elsewhere */
    _parse_var_skip(&vblk);

    /* Unknown. N.B. Documentation lists this as a single zero, but I have
     * observed it to have the variable length string value: '8000000000000000'
     */
    _parse_var_skip(&vblk);

    /* Volume state */
    vblk += 14;

    vol->type = *(uint8_t *)vblk; vblk += 1;
    switch(vol->type) {
    case PART_LDM_VOLUME_TYPE_GEN:
    case PART_LDM_VOLUME_TYPE_RAID5:
        break;

    default:
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported volume VBLK type %u", vol->type);
        return FALSE;
    }

    /* Unknown */
    vblk += 1;

    /* Volume number */
    vblk += 1;

    /* Zeroes */
    vblk += 3;

    /* Flags */
    vol->flags = *(uint8_t *)vblk; vblk += 1;

    if (!_parse_var_int32(&vblk, &vol->n_comps, "n_children", "volume", err))
        return FALSE;
    vol->comps = g_array_sized_new(FALSE, FALSE,
                                   sizeof(PartLDMComponent *), vol->n_comps);
    g_array_set_clear_func(vol->comps, _unref_object);

    /* Commit id */
    vblk += 8;

    /* Id? */
    vblk += 8;

    if (!_parse_var_int64(&vblk, &vol->size, "size", "volume", err))
        return FALSE;

    /* Zeroes */
    vblk += 4;

    vol->part_type = *((uint8_t *)vblk); vblk++;

    /* Volume id */
    vblk += 16;

    if (flags & 0x08) vol->id1 = _parse_var_string(&vblk);
    if (flags & 0x20) vol->id2 = _parse_var_string(&vblk);
    if (flags & 0x80 && !_parse_var_int64(&vblk, &vol->size2,
                                          "size2", "volume", err))
        return FALSE;
    if (flags & 0x02) vol->hint = _parse_var_string(&vblk);

    return TRUE;
}

static gboolean
_parse_vblk_comp(const guint8 revision, const guint16 flags,
                 const guint8 *vblk, PartLDMComponentPrivate * const comp,
                 GError ** const err)
{
    if (revision != 3) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported component VBLK revision %hhu", revision);
        return FALSE;
    }

    if (!_parse_var_int32(&vblk, &comp->id, "id", "volume", err)) return FALSE;
    comp->name = _parse_var_string(&vblk);

    /* Volume state */
    _parse_var_skip(&vblk);

    comp->type = *((uint8_t *) vblk); vblk++;
    switch (comp->type) {
    case PART_LDM_COMPONENT_TYPE_STRIPED:
    case PART_LDM_COMPONENT_TYPE_SPANNED:
    case PART_LDM_COMPONENT_TYPE_RAID:
        break;

    default:
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Component VBLK OID=%u has unsupported type %u",
                    comp->id, comp->type);
        return FALSE;
    }

    /* Zeroes */
    vblk += 4;

    if (!_parse_var_int32(&vblk, &comp->n_parts, "n_parts", "component", err))
        return FALSE;
    comp->parts = g_array_sized_new(FALSE, FALSE,
                                    sizeof(PartLDMPartition *), comp->n_parts);
    g_array_set_clear_func(comp->parts, _unref_object);

    /* Log Commit ID */
    vblk += 8;

    /* Zeroes */
    vblk += 8;

    if (!_parse_var_int32(&vblk, &comp->parent_id,
                          "parent_id", "component", err))
        return FALSE;

    /* Zeroes */
    vblk += 1;

    if (flags & 0x10) {
        if (!_parse_var_int64(&vblk, &comp->stripe_size,
                              "stripe_size", "component", err))
            return FALSE;
        if (!_parse_var_int32(&vblk, &comp->n_columns,
                              "n_columns", "component", err))
            return FALSE;
    }

    return TRUE;
}

static gboolean
_parse_vblk_part(const guint8 revision, const guint16 flags,
                 const guint8 *vblk, PartLDMPartitionPrivate * const part,
                 GError ** const err)
{
    if (revision != 3) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported partition VBLK revision %hhu", revision);
        return FALSE;
    }

    if (!_parse_var_int32(&vblk, &part->id, "id", "volume", err)) return FALSE;
    part->name = _parse_var_string(&vblk);

    /* Zeroes */
    vblk += 4;

    /* Log Commit ID */
    vblk += 8;

    part->start = be64toh(*(uint64_t *)vblk); vblk += 8;
    part->vol_offset = be64toh(*(uint64_t *)vblk); vblk += 8;

    if (!_parse_var_int64(&vblk, &part->size, "size", "partition", err))
        return FALSE;

    if (!_parse_var_int32(&vblk, &part->parent_id,
                          "parent_id", "partition", err))
        return FALSE;

    if (!_parse_var_int32(&vblk, &part->disk_id, "disk_id", "partition", err))
        return FALSE;

    if (flags & 0x08) {
        if (!_parse_var_int32(&vblk, &part->index, "index", "partition", err))
            return FALSE;
    }

    return TRUE;
}

static gboolean
_parse_vblk_disk(const guint8 revision, const guint16 flags,
                 const guint8 *vblk, PartLDMDiskPrivate * const disk,
                 GError ** const err)
{
    if (!_parse_var_int32(&vblk, &disk->id, "id", "volume", err)) return FALSE;
    disk->name = _parse_var_string(&vblk);

    if (revision == 3) {
        char *guid = _parse_var_string(&vblk);
        if (uuid_parse(guid, disk->guid) == -1) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Disk %u has invalid guid: %s", disk->id, guid);
            g_free(guid);
            return FALSE;
        }

        g_free(guid);

        /* No need to parse rest of structure */
    }

    else if (revision == 4) {
        memcpy(&disk->guid, vblk, 16);

        /* No need to parse rest of structure */
    }

    else {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported disk VBLK revision %hhu", revision);
        return FALSE;
    }

    return TRUE;
}

static gboolean
_parse_vblk_disk_group(const guint8 revision, const guint16 flags,
                       const guint8 *vblk, PartLDMDiskGroupPrivate * const dg,
                       GError ** const err)
{
    if (revision != 3 && revision != 4) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported disk VBLK revision %hhu", revision);
        return FALSE;
    }

    if (!_parse_var_int32(&vblk, &dg->id, "id", "disk group", err))
        return FALSE;
    dg->name = _parse_var_string(&vblk);

    /* No need to parse rest of structure */

    return TRUE;
}

struct _spanned_rec {
    uint32_t record_id;
    uint16_t entries_total;
    uint16_t entries_found;
    int offset;
    char data[];
};

static gboolean
_parse_vblk(const void * data, PartLDMDiskGroup * const dg_o,
            const gchar * const path, const int offset,
            GError ** const err)
{
    PartLDMDiskGroupPrivate * const dg = dg_o->priv;

    const struct _ldm_vblk_rec_head * const rec_head = data;

    const guint8 type = rec_head->type & 0x0F;
    const guint8 revision = (rec_head->type & 0xF0) >> 4;

    data += sizeof(struct _ldm_vblk_rec_head);

    switch (type) {
    case 0x00:
        /* Blank VBLK */
        break;

    case 0x01:
    {
        PartLDMVolume * const vol =
            PART_LDM_VOLUME(g_object_new(PART_TYPE_LDM_VOLUME, NULL));
        g_array_append_val(dg->vols, vol);
        if (!_parse_vblk_vol(revision, rec_head->flags, data, vol->priv, err))
            return FALSE;
        break;
    }

    case 0x02:
    {
        PartLDMComponent * const comp =
            PART_LDM_COMPONENT(g_object_new(PART_TYPE_LDM_COMPONENT, NULL));
        g_array_append_val(dg->comps, comp);
        if (!_parse_vblk_comp(revision, rec_head->flags, data, comp->priv, err))
            return FALSE;
        break;
    }

    case 0x03:
    {
        PartLDMPartition * const part =
            PART_LDM_PARTITION(g_object_new(PART_TYPE_LDM_PARTITION, NULL));
        g_array_append_val(dg->parts, part);
        if (!_parse_vblk_part(revision, rec_head->flags, data, part->priv, err))
            return FALSE;
        break;
    }

    case 0x04:
    {
        PartLDMDisk * const disk =
            PART_LDM_DISK(g_object_new(PART_TYPE_LDM_DISK, NULL));
        g_array_append_val(dg->disks, disk);
        if (!_parse_vblk_disk(revision, rec_head->flags, data, disk->priv, err))
            return FALSE;
        break;
    }

    case 0x05:
    {
        if (!_parse_vblk_disk_group(revision, rec_head->flags, data, dg, err))
            return FALSE;
        break;
    }

    default:
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unknown VBLK type %hhi in %s at config offset %X",
                    type, path, offset);
        return FALSE;
    }

    return TRUE;
}

gint
_cmp_component_parts(gconstpointer a, gconstpointer b)
{
    const PartLDMPartition * const ao = PART_LDM_PARTITION(*(PartLDMPartition **)a);
    const PartLDMPartition * const bo = PART_LDM_PARTITION(*(PartLDMPartition **)b);

    if (ao->priv->index < bo->priv->index) return -1;
    if (ao->priv->index > bo->priv->index) return 1;
    return 0;
}

static gboolean
_parse_vblks(const void * const config, const gchar * const path,
          const struct _ldm_vmdb * const vmdb,
          PartLDMDiskGroup * const dg_o, GError ** const err)
{
    PartLDMDiskGroupPrivate * const dg = dg_o->priv;
    GArray *spanned = g_array_new(FALSE, FALSE, sizeof(gpointer));
    g_array_set_clear_func(spanned, _free_pointer);

    dg->sequence = be64toh(vmdb->committed_seq);

    dg->n_disks = be32toh(vmdb->n_committed_vblks_disk);
    dg->n_comps = be32toh(vmdb->n_committed_vblks_comp);
    dg->n_parts = be32toh(vmdb->n_committed_vblks_part);
    dg->n_vols = be32toh(vmdb->n_committed_vblks_vol);

    dg->disks = g_array_sized_new(FALSE, FALSE,
                                  sizeof(PartLDMDisk *), dg->n_disks);
    dg->comps = g_array_sized_new(FALSE, FALSE,
                                  sizeof(PartLDMComponent *), dg->n_comps);
    dg->parts = g_array_sized_new(FALSE, FALSE,
                                 sizeof(PartLDMPartition *), dg->n_parts);
    dg->vols = g_array_sized_new(FALSE, FALSE,
                                 sizeof(PartLDMVolume *), dg->n_vols);
    g_array_set_clear_func(dg->disks, _unref_object);
    g_array_set_clear_func(dg->comps, _unref_object);
    g_array_set_clear_func(dg->parts, _unref_object);
    g_array_set_clear_func(dg->vols, _unref_object);

    const guint16 vblk_size = be32toh(vmdb->vblk_size);
    const guint16 vblk_data_size = vblk_size - sizeof(struct _ldm_vblk_head);
    const void *vblk = (void *)vmdb + be32toh(vmdb->vblk_first_offset);
    for(;;) {
        const int offset = vblk - config;

        const struct _ldm_vblk_head * const head = vblk;
        if (memcmp(head->magic, "VBLK", 4) != 0) break;

        /* Sanity check the header */
        if (be16toh(head->entries_total) > 0 &&
            be16toh(head->entry) >= be16toh(head->entries_total))
        {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "VBLK entry %u has entry (%hu) > total entries (%hu)",
                        be32toh(head->seq), be16toh(head->entry),
                        be16toh(head->entries_total));
            goto error;
        }

        vblk += sizeof(struct _ldm_vblk_head);

        /* Check for a spanned record */
        if (be16toh(head->entries_total) > 1) {
            /* Look for an existing record */
            gboolean found = FALSE;
            for (int i = 0; i < spanned->len; i++) {
                struct _spanned_rec * const r =
                    g_array_index(spanned, struct _spanned_rec *, i);

                if (r->record_id == head->record_id) {
                    r->entries_found++;
                    memcpy(&r->data[be16toh(head->entry) * vblk_data_size],
                           vblk, vblk_data_size);
                    found = TRUE;
                    break;
                }
            }
            if (!found) {
                struct _spanned_rec * const r =
                    g_malloc0(offsetof(struct _spanned_rec, data) +
                              head->entries_total * vblk_data_size);
                g_array_append_val(spanned, r);
                r->record_id = head->record_id;
                r->entries_total = be16toh(head->entries_total);
                r->entries_found = 1;
                r->offset = offset;

                memcpy(&r->data[be16toh(head->entry) * vblk_data_size],
                       vblk, vblk_data_size);
            }
        }

        else {
            if (!_parse_vblk(vblk, dg_o, path, offset, err)) goto error;
        }

        vblk += vblk_data_size;
    }

    for (int i = 0; i < spanned->len; i++) {
        struct _spanned_rec * const rec =
            g_array_index(spanned, struct _spanned_rec *, i);

        if (rec->entries_found != rec->entries_total) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Expected to find %hu entries for record %u, but "
                        "found %hu", rec->entries_total, rec->record_id,
                        rec->entries_found);
            goto error;
        }

        if (!_parse_vblk(rec->data, dg_o, path, rec->offset, err)) goto error;
    }

    g_array_unref(spanned); spanned = NULL;

    if (dg->disks->len != dg->n_disks) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u disk VBLKs, but found %u",
                    dg->n_disks, dg->disks->len);
        return FALSE;
    }
    if (dg->comps->len != dg->n_comps) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u component VBLKs, but found %u",
                    dg->n_comps, dg->comps->len);
        return FALSE;
    }
    if (dg->parts->len != dg->n_parts) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u partition VBLKs, but found %u",
                    dg->n_parts, dg->parts->len);
        return FALSE;
    }
    if (dg->vols->len != dg->n_vols) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u volume VBLKs, but found %u",
                    dg->n_vols, dg->vols->len);
        return FALSE;
    }

    for (int i = 0; i < dg->n_parts; i++) {
        PartLDMPartition * const part =
                g_array_index(dg->parts, PartLDMPartition *, i);

        /* Look for the underlying disk for this partition */
        for (int j = 0; j < dg->n_disks; j++) {
            PartLDMDisk * const disk =
                g_array_index(dg->disks, PartLDMDisk *, j);

            if (disk->priv->id == part->priv->disk_id) {
                part->priv->disk = disk;
                g_object_ref(disk);
                break;
            }
        }
        if (part->priv->disk == NULL) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Partition %u references unknown disk %u",
                        part->priv->id, part->priv->disk_id);
            return FALSE;
        }

        /* Look for the parent component */
        gboolean parent_found = FALSE;
        for (int j = 0; j < dg->n_comps; j++) {
            PartLDMComponent * const comp =
                g_array_index(dg->comps, PartLDMComponent *, j);

            if (comp->priv->id == part->priv->parent_id) {
                g_array_append_val(comp->priv->parts, part);
                g_object_ref(part);
                parent_found = TRUE;
                break;
            }
        }
        if (!parent_found) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Didn't find parent component %u for partition %u",
                        part->priv->parent_id, part->priv->id);
            return FALSE;
        }
    }

    for (int i = 0; i < dg->n_comps; i++) {
        PartLDMComponent * const comp_o =
            g_array_index(dg->comps, PartLDMComponent *, i);
        PartLDMComponentPrivate * const comp = comp_o->priv;

        if (comp->parts->len != comp->n_parts) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Component %u expected %u partitions, but found %u",
                        comp->id,
                        comp->n_parts, comp->parts->len);
            return FALSE;
        }

        /* Sort partitions into index order. We rely on this sorting when
         * generating DM tables */
        g_array_sort(comp->parts, _cmp_component_parts);

        gboolean parent_found = FALSE;
        for (int j = 0; j < dg->n_vols; j++) {
            PartLDMVolume * const vol_o =
                g_array_index(dg->vols, PartLDMVolume *, j);
            PartLDMVolumePrivate * const vol = vol_o->priv;

            if (vol->id == comp->parent_id) {
                g_array_append_val(vol->comps, comp_o);
                g_object_ref(comp_o);
                parent_found = TRUE;
                break;
            }
        }
        if (!parent_found) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Didn't find parent volume %u for component %u",
                        comp->parent_id, comp->id);
            return FALSE;
        }
    }

    for (int i = 0; i < dg->n_vols; i++) {
        PartLDMVolume * const vol_o =
            g_array_index(dg->vols, PartLDMVolume *, i);
        PartLDMVolumePrivate * const vol = vol_o->priv;

        if (vol->comps->len != vol->n_comps) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Volume %u expected %u components, but only found %u",
                        vol->id, vol->n_comps, vol->comps->len);
            return FALSE;
        }

        vol->dgname = g_strdup(dg->name);
    }

    for (int i = 0; i < dg->n_disks; i++) {
        PartLDMDisk * const disk_o =
            g_array_index(dg->disks, PartLDMDisk *, i);
        PartLDMDiskPrivate * const disk = disk_o->priv;

        disk->dgname = g_strdup(dg->name);
    }

    return TRUE;

error:
    if (spanned) { g_array_unref(spanned); spanned = NULL; }
    return FALSE;
}

gboolean
part_ldm_add(PartLDM *o, const gchar * const path, GError ** const err)
{
    GArray *disk_groups = o->priv->disk_groups;

    /* The GObject documentation states quite clearly that method calls on an
     * object which has been disposed should *not* result in an error. Seems
     * weird, but...
     */
    if (!disk_groups) return TRUE;

    void *config = NULL;

    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                    "Error opening %s for reading: %m", path);
        goto error;
    }

    struct _ldm_privhead privhead;
    if (!_read_privhead(fd, path, &privhead, err)) goto error;
    if (!_read_config(fd, path, &privhead, &config, err)) goto error;

    const struct _ldm_vmdb *vmdb;
    if (!_find_vmdb(config, path, &vmdb, err)) goto error;

    uuid_t disk_guid;
    uuid_t disk_group_guid;

    if (uuid_parse(privhead.disk_guid, disk_guid) == -1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "PRIVHEAD contains invalid GUID for disk: %s",
                    privhead.disk_guid);
        goto error;
    }
    if (uuid_parse(privhead.disk_group_guid, disk_group_guid) == -1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "PRIVHEAD contains invalid GUID for disk group: %s",
                    privhead.disk_group_guid);
        goto error;
    }

    PartLDMDiskGroup *dg = NULL;
    for (int i = 0; i < disk_groups->len; i++) {
        PartLDMDiskGroup *c = g_array_index(disk_groups,
                                            PartLDMDiskGroup *, i);

        if (uuid_compare(disk_group_guid, c->priv->guid) == 0) {
            dg = c;
        }
    }

    char dg_guid_str[37];
    uuid_unparse(disk_group_guid, dg_guid_str);

    if (dg == NULL) {
        dg = PART_LDM_DISK_GROUP(g_object_new(PART_TYPE_LDM_DISK_GROUP, NULL));

        uuid_copy(dg->priv->guid, disk_group_guid);

        g_debug("Found new disk group: %s", dg_guid_str);

        if (!_parse_vblks(config, path, vmdb, dg, err)) {
            g_object_unref(dg); dg = NULL;
            goto error;
        }

        g_array_append_val(disk_groups, dg);
    } else {
        /* Check this disk is consistent with other disks */
        uint64_t committed = be64toh(vmdb->committed_seq);
        if (dg && committed != dg->priv->sequence) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INCONSISTENT,
                        "Members of disk group %s are inconsistent. "
                        "Disk %s has committed sequence %lu; "
                        "group has committed sequence %lu.",
                        dg_guid_str, path, committed, dg->priv->sequence);
            goto error;
        }
    }

    /* Find the disk VBLK for the current disk and add additional information
     * from PRIVHEAD */
    for (int i = 0; i < dg->priv->n_disks; i++) {
        PartLDMDisk * const disk_o =
                g_array_index(dg->priv->disks, PartLDMDisk *, i);
        PartLDMDiskPrivate * const disk = disk_o->priv;

        if (uuid_compare(disk_guid, disk->guid) == 0) {
            disk->device = g_strdup(path);
            disk->data_start = be64toh(privhead.logical_disk_start);
            disk->data_size = be64toh(privhead.logical_disk_size);
            disk->metadata_start = be64toh(privhead.ldm_config_start);
            disk->metadata_size = be64toh(privhead.ldm_config_size);
            break;
        }
    }

    g_free(config);
    close(fd);
    return TRUE;

error:
    g_free(config);
    close(fd);
    return FALSE;
}

PartLDM *
part_ldm_new(GError ** const err)
{
    PartLDM *ldm = PART_LDM(g_object_new(PART_TYPE_LDM, NULL));
    ldm->priv->disk_groups = g_array_sized_new(FALSE, FALSE,
                                               sizeof (PartLDMDiskGroup *), 1);
    g_array_set_clear_func(ldm->priv->disk_groups, _unref_object);

    return ldm;
}

void
part_ldm_disk_group_dump(PartLDMDiskGroup * const o)
{
    const PartLDMDiskGroupPrivate * const dg = o->priv;

    char guid_str[37];
    uuid_unparse(dg->guid, guid_str);

    g_message("GUID: %s", guid_str);
    g_message("ID: %u", dg->id);
    g_message("Name: %s", dg->name);
    g_message("Disks: %u", dg->n_disks);
    g_message("Components: %u", dg->n_comps);
    g_message("Partitions: %u", dg->n_parts);
    g_message("Volumes: %u", dg->n_vols);

    for (int i = 0; i < dg->n_vols; i++) {
        const PartLDMVolume * const vol_o =
            g_array_index(dg->vols, PartLDMVolume *, i);
        const PartLDMVolumePrivate * const vol = vol_o->priv;

        g_message("Volume: %s", vol->name);
        g_message("  ID: %u", vol->id);
        const char * vol_type;
        switch (vol->type) {
        case PART_LDM_VOLUME_TYPE_GEN:          vol_type = "gen"; break;
        case PART_LDM_VOLUME_TYPE_RAID5:        vol_type = "raid5"; break;
        default:
            /* We checked this value when it was set, and it isn't possible to
             * modify it. This should be impossible. */
            g_error("Unexpected volume type: %u", vol->type);
        }
        g_message("  Type: %s", vol_type);
        g_message("  Size: %lu", vol->size);
        g_message("  Partition type: %hhu", vol->part_type);
        g_message("  Volume Type: %hhu", vol->volume_type);
        g_message("  Flags: %hhu", vol->flags);
        if (vol->id1) g_message("  ID1: %s", vol->id1);
        if (vol->id2) g_message("  ID2: %s", vol->id2);
        if (vol->size2 > 0) g_message("  Size2: %lu", vol->size2);
        if (vol->hint) g_message("  Drive Hint: %s", vol->hint);

        for (int j = 0; j < vol->n_comps; j++) {
            const PartLDMComponent * const comp_o =
                g_array_index(vol->comps, PartLDMComponent *, j);
            const PartLDMComponentPrivate * const comp = comp_o->priv;

            g_message("  Component: %s", comp->name);
            g_message("    ID: %u", comp->id);
            const char *comp_type = NULL;
            switch (comp->type) {
            case PART_LDM_COMPONENT_TYPE_STRIPED: comp_type = "STRIPED"; break;
            case PART_LDM_COMPONENT_TYPE_SPANNED: comp_type = "SPANNED"; break;
            case PART_LDM_COMPONENT_TYPE_RAID: comp_type = "RAID"; break;
            }
            g_message("    Type: %s", comp_type);
            if (comp->stripe_size > 0)
                g_message("    Stripe Size: %lu", comp->stripe_size);
            if (comp->n_columns > 0)
                g_message("    Columns: %u", comp->n_columns);

            for (int k = 0; k < comp->n_parts; k++) {
                const PartLDMPartition * const part_o =
                    g_array_index(comp->parts, PartLDMPartition *, k);
                const PartLDMPartitionPrivate * const part = part_o->priv;

                g_message("    Partition: %s", part->name);
                g_message("      ID: %u", part->id);
                g_message("      Start: %lu", part->start);
                g_message("      Size: %lu", part->size);
                g_message("      Volume Offset: %lu", part->vol_offset);
                g_message("      Component Index: %u", part->index);

                const PartLDMDiskPrivate * const disk = part->disk->priv;
                uuid_unparse(disk->guid, guid_str);
                g_message("      Disk: %s", disk->name);
                g_message("        ID: %u", disk->id);
                g_message("        GUID: %s", guid_str);
                g_message("        Device: %s", disk->device);
                g_message("        Data Start: %lu", disk->data_start);
                g_message("        Data Size: %lu", disk->data_size);
                g_message("        Metadata Start: %lu", disk->metadata_start);
                g_message("        Metadata Size: %lu", disk->metadata_size);
            }
        }
    }
}

GArray *
part_ldm_get_disk_groups(PartLDM * const o, GError ** const err)
{
    if (o->priv->disk_groups) g_array_ref(o->priv->disk_groups);
    return o->priv->disk_groups;
}

GArray *
part_ldm_disk_group_get_volumes(PartLDMDiskGroup * const o, GError ** const err)
{
    if (o->priv->vols) g_array_ref(o->priv->vols);
    return o->priv->vols;
}

GArray *
part_ldm_volume_get_components(PartLDMVolume * const o, GError ** const err)
{
    if (o->priv->comps) g_array_ref(o->priv->comps);
    return o->priv->comps;
}

GArray *
part_ldm_component_get_partitions(PartLDMComponent * const o,
                                  GError ** const err)
{
    if (o->priv->parts) g_array_ref(o->priv->parts);
    return o->priv->parts;
}

PartLDMDisk *
part_ldm_partition_get_disk(PartLDMPartition * const o, GError ** const err)
{
    if (o->priv->disk) g_object_ref(o->priv->disk);
    return o->priv->disk;
}

static PartLDMDMTable *
_generate_dm_table_part(const PartLDMPartitionPrivate * const part,
                        GError ** const err)
{
    const PartLDMDiskPrivate * const disk = part->disk->priv;

    if (!disk->device) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_MISSING_DISK,
                    "Disk %s required by partition %s is missing",
                    disk->name, part->name);
        return NULL;
    }

    PartLDMDMTable * const table_o = g_object_new(PART_TYPE_LDM_DM_TABLE, NULL);
    PartLDMDMTablePrivate * const table = table_o->priv;

    table->name = g_string_new("");
    table->table = g_string_new("");

    /* Ensure we sanitise table names */
    char * dgname_esc =
        g_uri_escape_string(disk->dgname,
                            G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT,
                            FALSE);
    char * partname_esc =
        g_uri_escape_string(part->name,
                            G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT,
                            FALSE);

    g_string_printf(table->name, "ldm_%s_%s", dgname_esc, partname_esc);

    g_free(dgname_esc);
    g_free(partname_esc);

    g_string_printf(table->table, "0 %lu linear %s %lu\n",
                                  part->size, disk->device,
                                  disk->data_start + part->start);

    return table_o;
}

static gboolean
_generate_dm_tables_mirrored(GArray * const ret,
                             const PartLDMVolumePrivate * const vol,
                             GError ** const err)
{
    PartLDMDMTable * const mirror_o =
        g_object_new(PART_TYPE_LDM_DM_TABLE, NULL);
    PartLDMDMTablePrivate * const mirror = mirror_o->priv;
    g_array_append_val(ret, mirror_o);

    mirror->name = g_string_new("");
    g_string_printf(mirror->name, "ldm_%s_%s", vol->dgname, vol->name);

    mirror->table = g_string_new("");
    g_string_printf(mirror->table, "0 %lu raid raid1 1 128 %u",
                                  vol->size, vol->comps->len);

    int found = 0;
    for (int i = 0; i < vol->comps->len; i++) {
        const PartLDMComponent * const comp_o =
            g_array_index(vol->comps, const PartLDMComponent *, i);
        const PartLDMComponentPrivate * const comp = comp_o->priv;

        /* Check component is spanned */
        if (comp->type != PART_LDM_COMPONENT_TYPE_SPANNED ||
            comp->parts->len != 1) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                        "Unsupported configuration: mirrored volume must "
                        "contain only simple partitions");
            return FALSE;
        }

        const PartLDMPartition * const part_o =
            g_array_index(comp->parts, const PartLDMPartition *, 0);
        const PartLDMPartitionPrivate * const part = part_o->priv;

        PartLDMDMTable * const chunk_o = _generate_dm_table_part(part, err);
        if (chunk_o == NULL) {
            if (err && (*err)->code == PART_LDM_ERROR_MISSING_DISK) {
                g_warning((*err)->message);
                g_error_free(*err); *err = NULL;
                g_string_append(mirror->table, " - -");
                continue;
            } else {
                return FALSE;
            }
        }

        g_array_prepend_val(ret, chunk_o);
        found++;

        g_string_append_printf(mirror->table,
                               " - /dev/mapper/%s", chunk_o->priv->name->str);
    }

    if (found == 0) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_MISSING_DISK,
                    "Mirrored volume is missing all components");
        return FALSE;
    }

    g_string_append(mirror->table, "\n");

    return TRUE;
}

static gboolean
_generate_dm_tables_spanned(GArray * const ret,
                            const PartLDMVolumePrivate * const vol,
                            const PartLDMComponentPrivate * const comp,
                            GError ** const err)
{
    PartLDMDMTable * const spanned_o =
        g_object_new(PART_TYPE_LDM_DM_TABLE, NULL);
    PartLDMDMTablePrivate * const spanned = spanned_o->priv;
    g_array_append_val(ret, spanned_o);

    spanned->name = g_string_new("");
    g_string_printf(spanned->name, "ldm_%s_%s", vol->dgname, vol->name);

    spanned->table = g_string_new("");
    uint64_t pos = 0;
    for (int i = 0; i < comp->parts->len; i++) {
        const PartLDMPartition * const part_o =
            g_array_index(comp->parts, const PartLDMPartition *, i);
        const PartLDMPartitionPrivate * const part = part_o->priv;

        const PartLDMDiskPrivate * const disk = part->disk->priv;
        if (!disk->device) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_MISSING_DISK,
                        "Disk %s required by spanned volume %s is missing",
                        disk->name, vol->name);
            return FALSE;
        }

        /* Sanity check: current position from adding up sizes of partitions
         * should equal the volume offset of the partition */
        if (pos != part->vol_offset) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Partition volume offset does not match sizes of "
                        "preceding partitions");
            return FALSE;
        }

        g_string_append_printf(spanned->table, "%lu %lu linear %s %lu\n",
                                               pos, pos + part->size,
                                               disk->device,
                                               disk->data_start + part->start);
        pos += part->size;
    }

    return TRUE;
}

static gboolean
_generate_dm_tables_striped(GArray * const ret,
                            const PartLDMVolumePrivate * const vol,
                            const PartLDMComponentPrivate * const comp,
                            GError ** const err)
{
    PartLDMDMTable * const striped_o =
        g_object_new(PART_TYPE_LDM_DM_TABLE, NULL);
    PartLDMDMTablePrivate * const striped = striped_o->priv;
    g_array_append_val(ret, striped_o);

    striped->name = g_string_new("");
    g_string_printf(striped->name, "ldm_%s_%s", vol->dgname, vol->name);

    striped->table = g_string_new("");
    g_string_printf(striped->table, "0 %lu striped %u %lu",
                                    vol->size,
                                    comp->n_columns, comp->stripe_size);

    for (int i = 0; i < comp->parts->len; i++) {
        const PartLDMPartition * const part_o =
            g_array_index(comp->parts, const PartLDMPartition *, i);
        const PartLDMPartitionPrivate * const part = part_o->priv;

        const PartLDMDiskPrivate * const disk = part->disk->priv;
        if (!disk->device) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_MISSING_DISK,
                        "Disk %s required by striped volume %s is missing",
                        disk->name, vol->name);
            return FALSE;
        }

        g_string_append_printf(striped->table, " %s %lu",
                                               disk->device,
                                               disk->data_start + part->start);
    }
    g_string_append(striped->table, "\n");

    return TRUE;
}

static gboolean
_generate_dm_tables_raid5(GArray * const ret,
                          const PartLDMVolumePrivate * const vol,
                          GError ** const err)
{
    if (vol->comps->len != 1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported configuration: volume type RAID5 should "
                    "have a single child component");
        return FALSE;
    }

    const PartLDMComponent * const comp_o =
        g_array_index(vol->comps, const PartLDMComponent *, 0);
    const PartLDMComponentPrivate * const comp = comp_o->priv;

    if (comp->type != PART_LDM_COMPONENT_TYPE_RAID) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported configuration: child component of RAID5 "
                    "volume must be of type RAID");
        return FALSE;
    }

    PartLDMDMTable * const raid5_o = g_object_new(PART_TYPE_LDM_DM_TABLE, NULL);
    PartLDMDMTablePrivate * const raid5 = raid5_o->priv;
    g_array_append_val(ret, raid5_o);

    raid5->name = g_string_new("");
    g_string_printf(raid5->name, "ldm_%s_%s", vol->dgname, vol->name);

    raid5->table = g_string_new("");
    g_string_printf(raid5->table, "0 %lu raid raid5_ls 1 %lu %u",
                                  vol->size,
                                  comp->stripe_size, comp->n_columns);

    int found = 0;
    for (int i = 0; i < comp->parts->len; i++) {
        const PartLDMPartition * const part_o =
            g_array_index(comp->parts, const PartLDMPartition *, i);
        const PartLDMPartitionPrivate * const part = part_o->priv;

        PartLDMDMTable * const chunk_o = _generate_dm_table_part(part, err);
        if (chunk_o == NULL) {
            if (err && (*err)->code == PART_LDM_ERROR_MISSING_DISK) {
                g_warning((*err)->message);
                g_error_free(*err); *err = NULL;
                g_string_append(raid5->table, " - -");
                continue;
            } else {
                return FALSE;
            }
        }

        g_array_prepend_val(ret, chunk_o);
        found++;

        g_string_append_printf(raid5->table,
                               " - /dev/mapper/%s", chunk_o->priv->name->str);
    }

    if (found < comp->n_columns - 1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_MISSING_DISK,
                    "RAID5 volume is missing more than 1 component");
        return FALSE;
    }

    g_string_append(raid5->table, "\n");

    return TRUE;
}

GArray *
part_ldm_volume_generate_dm_tables(const PartLDMVolume * const o,
                                   GError ** const err)
{
    const PartLDMVolumePrivate * const vol = o->priv;

    GArray * const ret = g_array_sized_new(FALSE, FALSE, sizeof(GString *), 1);
    g_array_set_clear_func(ret, _unref_object);

    switch (vol->type) {
    case PART_LDM_VOLUME_TYPE_GEN:
    {
        if (vol->comps->len > 1) {
            if (!_generate_dm_tables_mirrored(ret, vol, err)) goto error;
            return ret;
        }

        const PartLDMComponent * const comp_o =
            g_array_index(vol->comps, PartLDMComponent *, 0);
        const PartLDMComponentPrivate * const comp = comp_o->priv;

        switch (comp->type) {
        case PART_LDM_COMPONENT_TYPE_SPANNED:
            if (!_generate_dm_tables_spanned(ret, vol, comp, err)) goto error;
            break;

        case PART_LDM_COMPONENT_TYPE_STRIPED:
            if (!_generate_dm_tables_striped(ret, vol, comp, err)) goto error;
            break;

        case PART_LDM_COMPONENT_TYPE_RAID:
        default:
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                        "Unsupported configuration: volume is type GEN, "
                        "component is neither SPANNED nor STRIPED");
            goto error;
        }

        return ret;
    }

    case PART_LDM_VOLUME_TYPE_RAID5:
    {
        if (!_generate_dm_tables_raid5(ret, vol, err)) goto error;
        return ret;
    }

    default:
        /* Should be impossible */
        g_error("Unexpected volume type: %u", vol->type);
    }

error:
    g_array_unref(ret);
    return NULL;
}
