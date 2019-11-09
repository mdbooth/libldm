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
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libdevmapper.h>
#include <linux/fs.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "mbr.h"
#include "gpt.h"
#include "ldm.h"

#define DM_UUID_PREFIX "LDM-"

#define UUID_FMT "%02x%02x%02x%02x-%02x%02x-%02x%02x-" \
                 "%02x%02x-%02x%02x%02x%02x%02x%02x"
#define UUID_VALS(uuid) (uuid)[0], (uuid)[1], (uuid)[2], (uuid)[3], \
                        (uuid)[4], (uuid)[5], (uuid)[6], (uuid)[7], \
                        (uuid)[8], (uuid)[9], (uuid)[10], (uuid)[11], \
                        (uuid)[12], (uuid)[13], (uuid)[14], (uuid)[15]

/**
 * SECTION:ldm
 * @include: ldm.h
 */

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

struct _privhead
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

struct _tocblock_bitmap
{
    char name[8];
    uint16_t flags1;
    uint64_t start;
    uint64_t size; // Relative to start of DB
    uint64_t flags2;
} __attribute__((__packed__));

struct _tocblock
{
    char magic[8]; // "TOCBLOCK"

    uint32_t seq1;
    char padding1[4];
    uint32_t seq2;
    char padding2[16];

    struct _tocblock_bitmap bitmap[2];
} __attribute__((__packed__));

struct _vmdb
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
struct _vblk_head
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
struct _vblk_rec_head
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

static void
_free_gstring(gpointer const data)
{
    g_string_free(*(GString **)data, TRUE);
}

/* GLIB error handling */

GQuark
ldm_error_quark(void)
{
    return g_quark_from_static_string("ldm");
}

GType
ldm_error_get_type(void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {
            { LDM_ERROR_INTERNAL, "LDM_ERROR_INTERNAL", "internal" },
            { LDM_ERROR_IO, "LDM_ERROR_IO", "io" },
            { LDM_ERROR_NOT_LDM, "LDM_ERROR_NOT_LDM", "not_ldm" },
            { LDM_ERROR_INVALID, "LDM_ERROR_INVALID", "invalid" },
            { LDM_ERROR_INCONSISTENT, "LDM_ERROR_INCONSISTENT",
                                      "inconsistent" },
            { LDM_ERROR_NOTSUPPORTED, "LDM_ERROR_NOTSUPPORTED",
                                      "notsupported" },
            { LDM_ERROR_MISSING_DISK, "LDM_ERROR_MISSING_DISK",
                                      "missing-disk" },
            { LDM_ERROR_EXTERNAL, "LDM_ERROR_EXTERNAL", "external" }
        };
        etype = g_enum_register_static("LDMError", values);
    }
    return etype;
}

/* We catch log messages generated by device mapper with errno != 0 and store
 * them here */
static int _dm_err_last_level = 0;
static const char *_dm_err_last_file = NULL;
static int _dm_err_last_line = 0;
static int _dm_err_last_errno = 0;
static char *_dm_err_last_msg = NULL;

static void
_dm_log_fn(const int level, const char * const file, const int line,
           const int dm_errno, const char *f, ...)
{
    if (dm_errno == 0) return;

    _dm_err_last_level = level;
    _dm_err_last_file = file;
    _dm_err_last_line = line;

    /* device-mapper doesn't set dm_errno usefully (it only seems to use
     * EUNCLASSIFIED), so we capture errno directly and cross our fingers */
    _dm_err_last_errno = errno;

    if (_dm_err_last_msg) {
        free(_dm_err_last_msg);
        _dm_err_last_msg = NULL;
    }

    va_list ap;
    va_start(ap, f);
    if (vasprintf(&_dm_err_last_msg, f, ap) == -1) {
        g_error("vasprintf");
    }
    va_end(ap);
}

/* Macros for exporting object properties */

#define EXPORT_PROP_STRING(object, klass, property)                            \
gchar *                                                                        \
ldm_ ## object ## _get_ ## property(const klass * const o)                     \
{                                                                              \
    gpointer p = o->priv->property;                                            \
    if (p == NULL) return NULL;                                                \
                                                                               \
    const size_t len = strlen(p) + 1;                                          \
    gchar * const r = g_malloc(len);                                           \
    memcpy(r, p, len);                                                         \
    return r;                                                                  \
}

#define EXPORT_PROP_GUID(object, klass)                                        \
gchar *                                                                        \
ldm_ ## object ## _get_guid(const klass * const o)                             \
{                                                                              \
    gchar *r = g_malloc(37);                                                   \
    uuid_unparse(o->priv->guid, r);                                            \
    return r;                                                                  \
}

#define EXPORT_PROP_SCALAR(object, klass, property, type)                      \
type                                                                           \
ldm_ ## object ## _get_ ## property(const klass * const o)                     \
{                                                                              \
    return o->priv->property;                                                  \
}

/* LDM */

struct _LDMPrivate
{
    GArray *disk_groups;
};

G_DEFINE_TYPE_WITH_PRIVATE(LDM, ldm, G_TYPE_OBJECT)

static void
ldm_dispose(GObject * const object)
{
    LDM *ldm = LDM_CAST(object);

    if (ldm->priv->disk_groups) {
        g_array_unref(ldm->priv->disk_groups); ldm->priv->disk_groups = NULL;
    }

    /* Restore default logging function. */
    dm_log_with_errno_init(NULL);
}

static void
ldm_init(LDM * const o)
{
    o->priv = ldm_get_instance_private(o);
    bzero(o->priv, sizeof(*o->priv));

    /* Provide our logging function. */
    dm_log_with_errno_init(_dm_log_fn);
    dm_set_name_mangling_mode(DM_STRING_MANGLING_AUTO);
    dm_set_uuid_prefix(DM_UUID_PREFIX);
}

static void
ldm_class_init(LDMClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = ldm_dispose;

}

/* LDMDiskGroup */

struct _LDMDiskGroupPrivate
{
    uuid_t guid;
    uint32_t id;
    char *name;

    uint64_t sequence;

    /* GObjects */
    GArray *disks;
    GArray *parts;
    GArray *vols;

    /* We don't expose components, so they're no GObjects */
    uint32_t n_comps;
    GArray *comps;
};

G_DEFINE_TYPE_WITH_PRIVATE(LDMDiskGroup, ldm_disk_group, G_TYPE_OBJECT)

enum {
    PROP_LDM_DISK_GROUP_PROP0,
    PROP_LDM_DISK_GROUP_GUID,
    PROP_LDM_DISK_GROUP_NAME
};

static void
ldm_disk_group_get_property(GObject * const o, const guint property_id,
                            GValue * const value, GParamSpec * const pspec)
{
    LDMDiskGroup * const dg = LDM_DISK_GROUP(o);
    LDMDiskGroupPrivate * const priv = dg->priv;

    switch (property_id) {
    case PROP_LDM_DISK_GROUP_GUID:
        {
            char guid_str[37];
            uuid_unparse(priv->guid, guid_str);
            g_value_set_string(value, guid_str);
        }
        break;

    case PROP_LDM_DISK_GROUP_NAME:
        g_value_set_string(value, priv->name); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

EXPORT_PROP_STRING(disk_group, LDMDiskGroup, name)
EXPORT_PROP_GUID(disk_group, LDMDiskGroup)

static void
ldm_disk_group_dispose(GObject * const object)
{
    LDMDiskGroup *dg = LDM_DISK_GROUP(object);

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
ldm_disk_group_finalize(GObject * const object)
{
    LDMDiskGroup *dg = LDM_DISK_GROUP(object);

    g_free(dg->priv->name); dg->priv->name = NULL;
}

static void
ldm_disk_group_class_init(LDMDiskGroupClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = ldm_disk_group_dispose;
    object_class->finalize = ldm_disk_group_finalize;
    object_class->get_property = ldm_disk_group_get_property;


    /**
     * LDMDiskGroup:guid:
     *
     * A string representation of the disk group's GUID.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_GROUP_GUID,
        g_param_spec_string(
            "guid", "GUID",
            "A string representation of the disk group's GUID",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDiskGroup:name:
     *
     * The name of the disk group.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_GROUP_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the disk group",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
ldm_disk_group_init(LDMDiskGroup * const o)
{
    o->priv = ldm_disk_group_get_instance_private(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* LDMVolumeType */

GType
ldm_volume_type_get_type(void)
{
    static GType etype = 0;
    if (etype == 0) {
        static const GEnumValue values[] = {
            { LDM_VOLUME_TYPE_SIMPLE, "LDM_VOLUME_TYPE_SIMPLE", "simple" },
            { LDM_VOLUME_TYPE_SPANNED, "LDM_VOLUME_TYPE_SPANNED", "spanned" },
            { LDM_VOLUME_TYPE_STRIPED, "LDM_VOLUME_TYPE_STRIPED", "striped" },
            { LDM_VOLUME_TYPE_MIRRORED, "LDM_VOLUME_TYPE_MIRRORED",
                "mirrored" },
            { LDM_VOLUME_TYPE_RAID5, "LDM_VOLUME_TYPE_RAID5", "RAID5" }
        };
        etype = g_enum_register_static("LDMVolumeType", values);
    }
    return etype;
}

/* LDMVolume */

typedef enum {
    _VOLUME_TYPE_GEN = 0x3,
    _VOLUME_TYPE_RAID5 = 0x4
} _int_volume_type;

struct _LDMVolumePrivate
{
    guint32 id;
    gchar *name;
    uuid_t guid;
    gchar *dgname;

    guint64 size;
    guint8 part_type;

    guint8 flags;       /* Not exposed: unclear what it means */
    gchar *id1;         /* Not exposed: unclear what it means */
    gchar *id2;         /* Not exposed: unclear what it means */
    guint64 size2;      /* Not exposed: unclear what it means */
    gchar *hint;

    /* Derived */
    LDMVolumeType type;
    GArray *parts;
    guint64 chunk_size;

    /* Only used during parsing */
    _int_volume_type _int_type;
    guint32 _n_comps;
    guint32 _n_comps_i;
};

G_DEFINE_TYPE_WITH_PRIVATE(LDMVolume, ldm_volume, G_TYPE_OBJECT)

enum {
    PROP_LDM_VOLUME_PROP0,
    PROP_LDM_VOLUME_NAME,
    PROP_LDM_VOLUME_GUID,
    PROP_LDM_VOLUME_TYPE,
    PROP_LDM_VOLUME_SIZE,
    PROP_LDM_VOLUME_PART_TYPE,
    PROP_LDM_VOLUME_HINT,
    PROP_LDM_VOLUME_CHUNK_SIZE
};

static void
ldm_volume_get_property(GObject * const o, const guint property_id,
                        GValue * const value, GParamSpec *pspec)
{
    LDMVolume * const vol = LDM_VOLUME(o);
    LDMVolumePrivate * const priv = vol->priv;

    switch (property_id) {
    case PROP_LDM_VOLUME_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_LDM_VOLUME_GUID:
        {
            char guid_str[37];
            uuid_unparse(priv->guid, guid_str);
            g_value_set_string(value, guid_str);
        }
        break;

    case PROP_LDM_VOLUME_TYPE:
        g_value_set_enum(value, priv->type); break;

    case PROP_LDM_VOLUME_SIZE:
        g_value_set_uint64(value, priv->size); break;

    case PROP_LDM_VOLUME_PART_TYPE:
        g_value_set_uint(value, priv->part_type); break;

    case PROP_LDM_VOLUME_HINT:
        g_value_set_string(value, priv->hint); break;

    case PROP_LDM_VOLUME_CHUNK_SIZE:
        g_value_set_uint64(value, priv->chunk_size); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

EXPORT_PROP_STRING(volume, LDMVolume, name)
EXPORT_PROP_GUID(volume, LDMVolume)
EXPORT_PROP_SCALAR(volume, LDMVolume, size, guint64)
EXPORT_PROP_SCALAR(volume, LDMVolume, part_type, guint8)
EXPORT_PROP_STRING(volume, LDMVolume, hint)
EXPORT_PROP_SCALAR(volume, LDMVolume, chunk_size, guint64)

/* Sigh... another conflict with glib's _get_type() */
LDMVolumeType
ldm_volume_get_voltype(const LDMVolume * const o)
{
    return o->priv->type;
}

static void
ldm_volume_dispose(GObject * const object)
{
    LDMVolume * const vol_o = LDM_VOLUME(object);
    LDMVolumePrivate * const vol = vol_o->priv;

    if (vol->parts) { g_array_unref(vol->parts); vol->parts = NULL; }
}

static void
ldm_volume_finalize(GObject * const object)
{
    LDMVolume * const vol_o = LDM_VOLUME(object);
    LDMVolumePrivate * const vol = vol_o->priv;

    g_free(vol->name); vol->name = NULL;
    g_free(vol->dgname); vol->dgname = NULL;
    g_free(vol->id1); vol->id1 = NULL;
    g_free(vol->id2); vol->id2 = NULL;
    g_free(vol->hint); vol->hint = NULL;
}

static void
ldm_volume_class_init(LDMVolumeClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = ldm_volume_dispose;
    object_class->finalize = ldm_volume_finalize;
    object_class->get_property = ldm_volume_get_property;


    /**
     * LDMVolume:name:
     *
     * The volume's name.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_NAME,
        g_param_spec_string(
            "name", "Name", "The volume's name",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
    * LDMVolume:guid:
    *
    * The GUID of the volume.
    */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_GUID,
        g_param_spec_string(
            "guid", "GUID", "The GUID of the volume",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMVolume:type:
     *
     * The volume type: simple, spanned, striped, mirrored or raid5.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_TYPE,
        g_param_spec_enum(
            "type", "Type", "The volume type: simple, spanned, striped, "
            "mirrored or raid5",
            LDM_TYPE_VOLUME_TYPE, LDM_VOLUME_TYPE_SIMPLE,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMVolume:size:
     *
     * The volume size in sectors.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_SIZE,
        g_param_spec_uint64(
            "size", "Size", "The volume size in sectors",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMVolume:part-type:
     *
     * A 1-byte type descriptor of the volume's contents. This descriptor has
     * the same meaning as for an MBR partition.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_PART_TYPE,
        g_param_spec_uint(
            "part-type", "Partition Type", "A 1-byte type descriptor of the "
            "volume's contents. This descriptor has the same meaning as for "
            "an MBR partition",
            0, G_MAXUINT8, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMVolume:hint:
     *
     * A hint to Windows as to which drive letter to assign to this volume.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_HINT,
        g_param_spec_string(
            "hint", "Hint", "A hint to Windows as to which drive letter to "
            "assign to this volume",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMVolume:chunk-size:
     *
     * The chunk size of a striped or raided volume.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_VOLUME_CHUNK_SIZE,
        g_param_spec_uint64(
            "chunk-size", "Chunk Size",
            "The chunk size of a striped or raided volume",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
ldm_volume_init(LDMVolume * const o)
{
    o->priv = ldm_volume_get_instance_private(o);
    bzero(o->priv, sizeof(*o->priv));

    /* We don't have a trivial way to initialize this array to the correct size
     * during parsing because we cut out components. We initialize it here and
     * allow it to grow dynamically. */
    o->priv->parts = g_array_new(FALSE, FALSE, sizeof(LDMPartition *));
    g_array_set_clear_func(o->priv->parts, _unref_object);
}

/* We don't expose components externally */

typedef enum {
    _COMPONENT_TYPE_STRIPED = 0x1,
    _COMPONENT_TYPE_SPANNED = 0x2,
    _COMPONENT_TYPE_RAID    = 0x3
} _LDMComponentType;

struct _LDMComponent
{
    guint32 id;
    guint32 parent_id;

    _LDMComponentType type;
    uint32_t n_parts;
    GArray *parts;

    guint64 chunk_size;
    guint32 n_columns;
};

static void
_cleanup_comp(gpointer const data)
{
    struct _LDMComponent * const comp = (struct _LDMComponent *) data;
    g_array_unref(comp->parts);
}

/* LDMPartition */

struct _LDMPartitionPrivate
{
    guint32 id;
    guint32 parent_id;
    gchar *name;

    guint64 start;
    guint64 vol_offset; /* Not exposed: only used for sanity checking */
    guint64 size;
    guint32 index;      /* Not exposed directly: container array is sorted */

    guint32 disk_id;
    LDMDisk *disk;
};

G_DEFINE_TYPE_WITH_PRIVATE(LDMPartition, ldm_partition, G_TYPE_OBJECT)

enum {
    PROP_LDM_PARTITION_PROP0,
    PROP_LDM_PARTITION_NAME,
    PROP_LDM_PARTITION_START,
    PROP_LDM_PARTITION_SIZE
};

static void
ldm_partition_get_property(GObject * const o, const guint property_id,
                                GValue * const value, GParamSpec * const pspec)
{
    LDMPartition * const part = LDM_PARTITION(o);
    LDMPartitionPrivate * const priv = part->priv;

    switch (property_id) {
    case PROP_LDM_PARTITION_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_LDM_PARTITION_START:
        g_value_set_uint64(value, priv->start); break;

    case PROP_LDM_PARTITION_SIZE:
        g_value_set_uint64(value, priv->size); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

EXPORT_PROP_STRING(partition, LDMPartition, name)
EXPORT_PROP_SCALAR(partition, LDMPartition, start, guint64)
EXPORT_PROP_SCALAR(partition, LDMPartition, size, guint64)

static void
ldm_partition_dispose(GObject * const object)
{
    LDMPartition * const part_o = LDM_PARTITION(object);
    LDMPartitionPrivate * const part = part_o->priv;

    if (part->disk) { g_object_unref(part->disk); part->disk = NULL; }
}

static void
ldm_partition_finalize(GObject * const object)
{
    LDMPartition * const part_o = LDM_PARTITION(object);
    LDMPartitionPrivate * const part = part_o->priv;

    g_free(part->name); part->name = NULL;
}

static void
ldm_partition_class_init(LDMPartitionClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = ldm_partition_dispose;
    object_class->finalize = ldm_partition_finalize;
    object_class->get_property = ldm_partition_get_property;


    /**
     * LDMPartition:name:
     *
     * The name of the partition.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_PARTITION_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the partition",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMPartition:start:
     *
     * The start sector of the partition.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_PARTITION_START,
        g_param_spec_uint64(
            "start", "Start", "The start sector of the partition",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMPartition:size:
     *
     * The size of the partition in sectors.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_PARTITION_SIZE,
        g_param_spec_uint64(
            "size", "Size", "The size of the partition in sectors",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
ldm_partition_init(LDMPartition * const o)
{
    o->priv = ldm_partition_get_instance_private(o);
    bzero(o->priv, sizeof(*o->priv));
}

/* LDMDisk */

struct _LDMDiskPrivate
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

G_DEFINE_TYPE_WITH_PRIVATE(LDMDisk, ldm_disk, G_TYPE_OBJECT)

enum {
    PROP_LDM_DISK_PROP0,
    PROP_LDM_DISK_NAME,
    PROP_LDM_DISK_GUID,
    PROP_LDM_DISK_DEVICE,
    PROP_LDM_DISK_DATA_START,
    PROP_LDM_DISK_DATA_SIZE,
    PROP_LDM_DISK_METADATA_START,
    PROP_LDM_DISK_METADATA_SIZE
};

static void
ldm_disk_get_property(GObject * const o, const guint property_id,
                           GValue * const value, GParamSpec * const pspec)
{
    const LDMDisk * const disk = LDM_DISK(o);
    const LDMDiskPrivate * const priv = disk->priv;

    switch (property_id) {
    case PROP_LDM_DISK_NAME:
        g_value_set_string(value, priv->name); break;

    case PROP_LDM_DISK_GUID:
        {
            char guid_str[37];
            uuid_unparse(priv->guid, guid_str);
            g_value_set_string(value, guid_str);
        }
        break;

    case PROP_LDM_DISK_DEVICE:
        g_value_set_string(value, priv->device); break;

    case PROP_LDM_DISK_DATA_START:
        g_value_set_uint64(value, priv->data_start); break;

    case PROP_LDM_DISK_DATA_SIZE:
        g_value_set_uint64(value, priv->data_size); break;

    case PROP_LDM_DISK_METADATA_START:
        g_value_set_uint64(value, priv->metadata_start); break;

    case PROP_LDM_DISK_METADATA_SIZE:
        g_value_set_uint64(value, priv->metadata_size); break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, property_id, pspec);
    }
}

EXPORT_PROP_STRING(disk, LDMDisk, name)
EXPORT_PROP_GUID(disk, LDMDisk)
EXPORT_PROP_STRING(disk, LDMDisk, device)
EXPORT_PROP_SCALAR(disk, LDMDisk, data_start, guint64)
EXPORT_PROP_SCALAR(disk, LDMDisk, data_size, guint64)
EXPORT_PROP_SCALAR(disk, LDMDisk, metadata_start, guint64)
EXPORT_PROP_SCALAR(disk, LDMDisk, metadata_size, guint64)

static void
ldm_disk_finalize(GObject * const object)
{
    LDMDisk * const disk_o = LDM_DISK(object);
    LDMDiskPrivate * const disk = disk_o->priv;

    g_free(disk->name); disk->name = NULL;
    g_free(disk->dgname); disk->dgname = NULL;
    g_free(disk->device); disk->device = NULL;
}

static void
ldm_disk_class_init(LDMDiskClass * const klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = ldm_disk_finalize;
    object_class->get_property = ldm_disk_get_property;


    /**
     * LDMDisk:name:
     *
     * The name of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_NAME,
        g_param_spec_string(
            "name", "Name", "The name of the disk",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDisk:guid:
     *
     * The GUID of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_GUID,
        g_param_spec_string(
            "guid", "GUID", "The GUID of the disk",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDisk:device:
     *
     * The underlying device of this disk. This may be NULL if the disk is
     * missing from the disk group.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_DEVICE,
        g_param_spec_string(
            "device", "Device", "The underlying device of this disk. This may "
            "be NULL if the disk is missing from the disk group",
            NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDisk:data-start:
     *
     * The start sector of the data area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_DATA_START,
        g_param_spec_uint64(
            "data-start", "Data Start", "The start sector of the data area of "
            "the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDisk:data-size:
     *
     * The size, in sectors, of the data area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_DATA_SIZE,
        g_param_spec_uint64(
            "data-size", "Data Size", "The size, in sectors, of the data area "
            "of the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDisk:metadata-start:
     *
     * The start sector of the metadata area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_METADATA_START,
        g_param_spec_uint64(
            "metadata-start", "Metadata Start", "The start sector of the "
            "metadata area of the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );

    /**
     * LDMDisk:metadata-size:
     *
     * The size, in sectors, of the metadata area of the disk.
     */
    g_object_class_install_property(
        object_class,
        PROP_LDM_DISK_METADATA_SIZE,
        g_param_spec_uint64(
            "metadata-size", "Metadata Size", "The size, in sectors, of the "
            "metadata area of the disk",
            0, G_MAXUINT64, 0,
            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS
        )
    );
}

static void
ldm_disk_init(LDMDisk * const o)
{
    o->priv = ldm_disk_get_instance_private(o);
    bzero(o->priv, sizeof(*o->priv));
}

static gboolean
_find_vmdb(const void * const config, const gchar * const path,
           const guint secsize, const struct _vmdb ** const vmdb,
           GError ** const err)
{
    /* TOCBLOCK starts 2 sectors into config */
    const struct _tocblock *tocblock = config + secsize * 2;
    if (memcmp(tocblock->magic, "TOCBLOCK", 8) != 0) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "Didn't find TOCBLOCK at config offset %" PRIX64,
                    UINT64_C(0x400));
        return FALSE;
    }

    g_debug("TOCBLOCK: %s\n"
            "  Sequence1: %" PRIu64 "\n"
            "  Sequence2: %" PRIu64 "\n"
            "  Bitmap: %s\n"
            "    Flags1: %04" PRIo8 "\n"
            "    Start: %" PRIu64 "\n"
            "    Size: %" PRIu64 "\n"
            "    Flags2: %016" PRIo64 "\n"
            "  Bitmap: %s\n"
            "    Flags1: %04" PRIo8 "\n"
            "    Start: %" PRIu64 "\n"
            "    Size: %" PRIu64 "\n"
            "    Flags2: %016" PRIo64,
            path,
            (uint64_t) be64toh(tocblock->seq1),
            (uint64_t) be64toh(tocblock->seq2),
            tocblock->bitmap[0].name,
            be16toh(tocblock->bitmap[0].flags1),
            (uint64_t) be64toh(tocblock->bitmap[0].start),
            (uint64_t) be64toh(tocblock->bitmap[0].size),
            (uint64_t) be64toh(tocblock->bitmap[0].flags2),
            tocblock->bitmap[1].name,
            be16toh(tocblock->bitmap[1].flags1),
            (uint64_t) be64toh(tocblock->bitmap[1].start),
            (uint64_t) be64toh(tocblock->bitmap[1].size),
            (uint64_t) be64toh(tocblock->bitmap[1].flags2));

    /* Find the start of the DB */
    *vmdb = NULL;
    for (int i = 0; i < 2; i++) {
        const struct _tocblock_bitmap *bitmap = &tocblock->bitmap[i];
        if (strcmp(bitmap->name, "config") == 0) {
            *vmdb = config + be64toh(tocblock->bitmap[i].start) * secsize;
            break;
        }
    }

    if (*vmdb == NULL) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "TOCBLOCK doesn't contain config bitmap");
        return FALSE;
    }

    if (memcmp((*vmdb)->magic, "VMDB", 4) != 0) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "Didn't find VMDB at config offset %lX",
                    (unsigned long int)((void *) (*vmdb) - config));
        return FALSE;
    }

    g_debug("VMDB: %s\n"
            "  VBLK last: %" PRIu32 "\n"
            "  VBLK size: %" PRIu32 "\n"
            "  VBLK first offset: %" PRIu32 "\n"
            "  Version major: %" PRIu16 "\n"
            "  Version minor: %" PRIu16 "\n"
            "  Disk group GUID: %s\n"
            "  Committed sequence: %" PRIu64 "\n"
            "  Pending sequence: %" PRIu64 "\n"
            "  Committed volumes: %" PRIu32 "\n"
            "  Committed components: %" PRIu32 "\n"
            "  Committed partitions: %" PRIu32 "\n"
            "  Committed disks: %" PRIu32 "\n"
            "  Pending volumes: %" PRIu32 "\n"
            "  Pending components: %" PRIu32 "\n"
            "  Pending partitions: %" PRIu32 "\n"
            "  Pending disks: %" PRIu32,
            path,
            be32toh((*vmdb)->vblk_last),
            be32toh((*vmdb)->vblk_size),
            be32toh((*vmdb)->vblk_first_offset),
            be16toh((*vmdb)->version_major),
            be16toh((*vmdb)->version_minor),
            (*vmdb)->disk_group_guid,
            (uint64_t) be64toh((*vmdb)->committed_seq),
            (uint64_t) be64toh((*vmdb)->pending_seq),
            be32toh((*vmdb)->n_committed_vblks_vol),
            be32toh((*vmdb)->n_committed_vblks_comp),
            be32toh((*vmdb)->n_committed_vblks_part),
            be32toh((*vmdb)->n_committed_vblks_disk),
            be32toh((*vmdb)->n_pending_vblks_vol),
            be32toh((*vmdb)->n_pending_vblks_comp),
            be32toh((*vmdb)->n_pending_vblks_part),
            be32toh((*vmdb)->n_pending_vblks_disk));

    return TRUE;
}

static gboolean
_read_config(const int fd, const gchar * const path,
             const guint secsize, const struct _privhead * const privhead,
             void ** const config, GError ** const err)
{
    /* Sanity check ldm_config_start and ldm_config_size */
    struct stat stat;
    if (fstat(fd, &stat) == -1) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
                    "Unable to stat %s: %m", path);
        return FALSE;
    }

    uint64_t size = stat.st_size;
    if (S_ISBLK(stat.st_mode)) {
        if (ioctl(fd, BLKGETSIZE64, &size) == -1) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
                        "Unable to get block device size for %s: %m", path);
            return FALSE;
        }
    }

    const uint64_t config_start =
        be64toh(privhead->ldm_config_start) * secsize;
    const uint64_t config_size =
        be64toh(privhead->ldm_config_size) * secsize;

    if (config_start > size) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "LDM config start (%" PRIX64") is outside file in %s",
                    config_start, path);
        return FALSE;
    }
    if (config_start + config_size > size) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "LDM config end (%" PRIX64 ") is outside file in %s",
                    config_start + config_size, path);
        return FALSE;
    }

    *config = g_malloc(config_size);
    size_t read = 0;
    while (read < config_size) {
        ssize_t in = pread(fd, *config + read, config_size - read,
                           config_start + read);
        if (in == 0) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "%s contains invalid LDM metadata", path);
            goto error;
        }

        if (in == -1) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
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
                   struct _privhead * const privhead, GError **err)
{
    size_t read = 0;
    while (read < sizeof(*privhead)) {
        ssize_t in = pread(fd, (char *) privhead + read,
                           sizeof(*privhead) - read,
                           ph_start + read);
        if (in == 0) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "%s contains invalid LDM metadata", path);
            return FALSE;
        }

        if (in == -1) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
                        "Error reading from %s: %m", path);
            return FALSE;
        }

        read += in;
    }

    if (memcmp(privhead->magic, "PRIVHEAD", 8) != 0) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "PRIVHEAD not found at offset %" PRIX64, ph_start);
        return FALSE;
    }

    g_debug("PRIVHEAD: %s\n"
            "  Version Major: %" PRIu16 "\n"
            "  Version Minor: %" PRIu16 "\n"
            "  Disk GUID: %s\n"
            "  Disk Group GUID: %s\n"
            "  Logical Disk Start: %" PRIu64 "\n"
            "  Logical Disk Size: %" PRIu64 "\n"
            "  LDM Config Start: %" PRIu64 "\n"
            "  LDM Config Size: %" PRIu64,
            path,
            be16toh(privhead->version_major),
            be16toh(privhead->version_minor),
            privhead->disk_guid,
            privhead->disk_group_guid,
            (uint64_t) be64toh(privhead->logical_disk_start),
            (uint64_t) be64toh(privhead->logical_disk_size),
            (uint64_t) be64toh(privhead->ldm_config_start),
            (uint64_t) be64toh(privhead->ldm_config_size));

    return TRUE;
}

static gboolean
_read_privhead_mbr(const int fd, const gchar * const path, const guint secsize,
                   struct _privhead * const privhead, GError ** const err)
{
    g_debug("Device %s uses MBR", path);

    /* On an MBR disk, the first PRIVHEAD is in sector 6 */
    return _read_privhead_off(fd, path, secsize * 6, privhead, err);
}

void _map_gpt_error(const int e, const gchar * const path, GError ** const err)
{
    switch (-e) {
    case GPT_ERROR_INVALID:
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "%s contains an invalid GPT header", path);
        break;

    case GPT_ERROR_READ:
        g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
                    "Error reading from %s: %m", path);
        break;

    case GPT_ERROR_INVALID_PART:
        g_error("Request for invalid GPT partition");

    default:
        g_error("Unhandled GPT return value: %i\n", e);
    }

}

static gboolean
_read_privhead_gpt(const int fd, const gchar * const path, const guint secsize,
                   struct _privhead * const privhead, GError ** const err)
{
    g_debug("Device %s uses GPT", path);

    int r;

    gpt_handle_t *h;
    r = gpt_open_secsize(fd, secsize, &h);
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

    for (uint32_t i = 0; i < gpt.pte_array_len; i++) {
        gpt_pte_t pte;
        r = gpt_get_pte(h, i, &pte);
        if (r < 0) {
            _map_gpt_error(r, path, err);
            gpt_close(h);
            return FALSE;
        }

        if (uuid_compare(pte.type, LDM_METADATA) == 0) {
            /* PRIVHEAD is in the last LBA of the LDM metadata partition */
            gpt_close(h);
            return _read_privhead_off(fd, path, pte.last_lba * secsize,
                                       privhead, err);
        }
    }

    g_set_error(err, LDM_ERROR, LDM_ERROR_NOT_LDM,
                "%s does not contain LDM metadata", path);
    return FALSE;
}

static gboolean
_read_privhead(const int fd, const gchar * const path, const guint secsize,
               struct _privhead * const privhead, GError ** const err)
{
    // Whether the disk is MBR or GPT, we expect to find an MBR at the beginning
    mbr_t mbr;
    int r = mbr_read(fd, &mbr);
    if (r < 0) {
        switch (-r) {
        case MBR_ERROR_INVALID:
            g_set_error(err, LDM_ERROR, LDM_ERROR_NOT_LDM,
                        "Didn't detect a partition table");
            return FALSE;

        case MBR_ERROR_READ:
            g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
                        "Error reading from %s: %m", path);
            return FALSE;

        default:
            g_error("Unhandled return value from mbr_read: %i", r);
        }
    }

    switch (mbr.part[0].type) {
    case MBR_PART_WINDOWS_LDM:
        return _read_privhead_mbr(fd, path, secsize, privhead, err);

    case MBR_PART_EFI_PROTECTIVE:
        return _read_privhead_gpt(fd, path, secsize, privhead, err);

    default:
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOT_LDM,
                    "%s does not contain LDM metadata", path);
        return FALSE;
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
        g_set_error(err, LDM_ERROR, LDM_ERROR_INTERNAL,                   \
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
                const guint8 * vblk, LDMVolumePrivate * const vol,
                GError ** const err)
{
    if (revision != 5) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
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

    vol->_int_type = *(uint8_t *)vblk; vblk += 1;
    switch(vol->_int_type) {
    case _VOLUME_TYPE_GEN:
    case _VOLUME_TYPE_RAID5:
        break;

    default:
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
                    "Unsupported volume VBLK type %u", vol->_int_type);
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

    if (!_parse_var_int32(&vblk, &vol->_n_comps, "n_children", "volume", err))
        return FALSE;

    /* Commit id */
    vblk += 8;

    /* Id? */
    vblk += 8;

    if (!_parse_var_int64(&vblk, &vol->size, "size", "volume", err))
        return FALSE;

    /* Zeroes */
    vblk += 4;

    vol->part_type = *((uint8_t *)vblk); vblk++;

    /* Volume GUID */
    memcpy(&vol->guid, vblk, 16); vblk += 16;

    if (flags & 0x08) vol->id1 = _parse_var_string(&vblk);
    if (flags & 0x20) vol->id2 = _parse_var_string(&vblk);
    if (flags & 0x80 && !_parse_var_int64(&vblk, &vol->size2,
                                          "size2", "volume", err))
        return FALSE;
    if (flags & 0x02) vol->hint = _parse_var_string(&vblk);

    g_debug("Volume: %s\n"
            "  ID: %" PRIu32 "\n"
            "  Type: %" PRIi32 "\n"
            "  Flags: %" PRIu8 "\n"
            "  Children: %" PRIu32 "\n"
            "  Size: %" PRIu64 "\n"
            "  Partition Type: %" PRIu8 "\n"
            "  ID1: %s\n"
            "  ID2: %s\n"
            "  Size2: %" PRIu64 "\n"
            "  Hint: %s",
            vol->name,
            vol->id,
            vol->_int_type,
            vol->flags,
            vol->_n_comps,
            vol->size,
            vol->part_type,
            vol->id1,
            vol->id2,
            vol->size2,
            vol->hint);

    return TRUE;
}

static gboolean
_parse_vblk_comp(const guint8 revision, const guint16 flags,
                 const guint8 *vblk, struct _LDMComponent * const comp,
                 GError ** const err)
{
    if (revision != 3) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
                    "Unsupported component VBLK revision %hhu", revision);
        return FALSE;
    }

    if (!_parse_var_int32(&vblk, &comp->id, "id", "volume", err)) return FALSE;

    /* Name */
    _parse_var_skip(&vblk);

    /* Volume state */
    _parse_var_skip(&vblk);

    comp->type = *((uint8_t *) vblk); vblk++;
    switch (comp->type) {
    case _COMPONENT_TYPE_STRIPED:
    case _COMPONENT_TYPE_SPANNED:
    case _COMPONENT_TYPE_RAID:
        break;

    default:
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
                    "Component VBLK OID=%u has unsupported type %u",
                    comp->id, comp->type);
        return FALSE;
    }

    /* Zeroes */
    vblk += 4;

    if (!_parse_var_int32(&vblk, &comp->n_parts, "n_parts", "component", err))
        return FALSE;
    comp->parts = g_array_sized_new(FALSE, FALSE,
                                    sizeof(LDMPartition *), comp->n_parts);
    /* All members of the component's partition array will be passed to the
     * parent volume's partition array after initial parsing. We don't unref its
     * objects on cleanup, which makes it cleaner to bulk-transfer them. */

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
        if (!_parse_var_int64(&vblk, &comp->chunk_size,
                              "chunk_size", "component", err))
            return FALSE;

        if (!_parse_var_int32(&vblk, &comp->n_columns,
                              "n_columns", "component", err))
            return FALSE;
    }

    g_debug("Component:\n"
            "  ID: %" PRIu32 "\n"
            "  Parent ID: %" PRIu32 "\n"
            "  Type: %" PRIu32 "\n"
            "  Parts: %" PRIu32 "\n"
            "  Chunk Size: %" PRIu64 "\n"
            "  Columns: %" PRIu32,
            comp->id,
            comp->parent_id,
            comp->type,
            comp->n_parts,
            comp->chunk_size,
            comp->n_columns);

    return TRUE;
}

static gboolean
_parse_vblk_part(const guint8 revision, const guint16 flags,
                 const guint8 *vblk, LDMPartitionPrivate * const part,
                 GError ** const err)
{
    if (revision != 3) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
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

    g_debug("Partition: %s\n"
            "  ID: %" PRIu32 "\n"
            "  Parent ID: %" PRIu32 "\n"
            "  Disk ID: %" PRIu32 "\n"
            "  Index: %" PRIu32 "\n"
            "  Start: %" PRIu64 "\n"
            "  Vol Offset: %" PRIu64 "\n"
            "  Size: %" PRIu64,
            part->name,
            part->id,
            part->parent_id,
            part->disk_id,
            part->index,
            part->start,
            part->vol_offset,
            part->size);

    return TRUE;
}

static gboolean
_parse_vblk_disk(const guint8 revision, const guint16 flags,
                 const guint8 *vblk, LDMDiskPrivate * const disk,
                 GError ** const err)
{
    if (!_parse_var_int32(&vblk, &disk->id, "id", "volume", err)) return FALSE;
    disk->name = _parse_var_string(&vblk);

    if (revision == 3) {
        char *guid = _parse_var_string(&vblk);
        if (uuid_parse(guid, disk->guid) == -1) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
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
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
                    "Unsupported disk VBLK revision %hhu", revision);
        return FALSE;
    }

    g_debug("Disk: %s\n"
            "  ID: %u\n"
            "  GUID: " UUID_FMT,
            disk->name,
            disk->id,
            UUID_VALS(disk->guid));

    return TRUE;
}

static gboolean
_parse_vblk_disk_group(const guint8 revision, const guint16 flags,
                       const guint8 *vblk, LDMDiskGroupPrivate * const dg,
                       GError ** const err)
{
    if (revision != 3 && revision != 4) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
                    "Unsupported disk VBLK revision %hhu", revision);
        return FALSE;
    }

    if (!_parse_var_int32(&vblk, &dg->id, "id", "disk group", err))
        return FALSE;
    dg->name = _parse_var_string(&vblk);

    /* No need to parse rest of structure */

    g_debug("Disk Group: %s\n"
            "  ID: %u",
            dg->name,
            dg->id);

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
_parse_vblk(const void * data, LDMDiskGroup * const dg_o, GArray *comps,
            const gchar * const path, const int offset,
            GError ** const err)
{
    LDMDiskGroupPrivate * const dg = dg_o->priv;

    const struct _vblk_rec_head * const rec_head = data;

    const guint8 type = rec_head->type & 0x0F;
    const guint8 revision = (rec_head->type & 0xF0) >> 4;

    data += sizeof(struct _vblk_rec_head);

    switch (type) {
    case 0x00:
        /* Blank VBLK */
        break;

    case 0x01:
    {
        LDMVolume * const vol =
            LDM_VOLUME(g_object_new(LDM_TYPE_VOLUME, NULL));
        g_array_append_val(dg->vols, vol);
        if (!_parse_vblk_vol(revision, rec_head->flags, data, vol->priv, err))
            return FALSE;
        break;
    }

    case 0x02:
    {
        g_array_set_size(comps, comps->len + 1);
        struct _LDMComponent *comp =
            (struct _LDMComponent *) comps->data + comps->len - 1;
        if (!_parse_vblk_comp(revision, rec_head->flags, data, comp, err))
            return FALSE;
        break;
    }

    case 0x03:
    {
        LDMPartition * const part =
            LDM_PARTITION(g_object_new(LDM_TYPE_PARTITION, NULL));
        g_array_append_val(dg->parts, part);
        if (!_parse_vblk_part(revision, rec_head->flags, data, part->priv, err))
            return FALSE;
        break;
    }

    case 0x04:
    {
        LDMDisk * const disk =
            LDM_DISK(g_object_new(LDM_TYPE_DISK, NULL));
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
        g_set_error(err, LDM_ERROR, LDM_ERROR_NOTSUPPORTED,
                    "Unknown VBLK type %hhi in %s at config offset %X",
                    type, path, offset);
        return FALSE;
    }

    return TRUE;
}

gint
_cmp_component_parts(gconstpointer a, gconstpointer b)
{
    const LDMPartition * const ao = LDM_PARTITION(*(LDMPartition **)a);
    const LDMPartition * const bo = LDM_PARTITION(*(LDMPartition **)b);

    if (ao->priv->index < bo->priv->index) return -1;
    if (ao->priv->index > bo->priv->index) return 1;
    return 0;
}

static gboolean
_parse_vblks(const void * const config, const gchar * const path,
             const struct _vmdb * const vmdb,
             LDMDiskGroup * const dg_o, GError ** const err)
{
    LDMDiskGroupPrivate * const dg = dg_o->priv;
    GArray *spanned = g_array_new(FALSE, FALSE, sizeof(gpointer));
    g_array_set_clear_func(spanned, _free_pointer);

    dg->sequence = be64toh(vmdb->committed_seq);

    guint32 n_disks = be32toh(vmdb->n_committed_vblks_disk);
    guint32 n_parts = be32toh(vmdb->n_committed_vblks_part);
    guint32 n_vols = be32toh(vmdb->n_committed_vblks_vol);
    guint32 n_comps = be32toh(vmdb->n_committed_vblks_comp);

    dg->disks = g_array_sized_new(FALSE, FALSE,
                                  sizeof(LDMDisk *), n_disks);
    dg->parts = g_array_sized_new(FALSE, FALSE,
                                  sizeof(LDMPartition *), n_parts);
    dg->vols = g_array_sized_new(FALSE, FALSE,
                                 sizeof(LDMVolume *), n_vols);
    g_array_set_clear_func(dg->disks, _unref_object);
    g_array_set_clear_func(dg->parts, _unref_object);
    g_array_set_clear_func(dg->vols, _unref_object);

    GArray *comps = g_array_sized_new(FALSE, TRUE,
                                      sizeof(struct _LDMComponent), n_comps);
    g_array_set_clear_func(comps, _cleanup_comp);

    const guint16 vblk_size = be32toh(vmdb->vblk_size);
    const guint16 vblk_data_size = vblk_size - sizeof(struct _vblk_head);
    const void *vblk = (void *)vmdb + be32toh(vmdb->vblk_first_offset);
    for(;;) {
        const int offset = vblk - config;

        const struct _vblk_head * const head = vblk;
        if (memcmp(head->magic, "VBLK", 4) != 0) break;

        /* Sanity check the header */
        if (be16toh(head->entries_total) > 0 &&
            be16toh(head->entry) >= be16toh(head->entries_total))
        {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "VBLK entry %u has entry (%hu) > total entries (%hu)",
                        be32toh(head->seq), be16toh(head->entry),
                        be16toh(head->entries_total));
            goto error;
        }

        vblk += sizeof(struct _vblk_head);

        /* Check for a spanned record */
        if (be16toh(head->entries_total) > 1) {
            /* Look for an existing record */
            gboolean found = FALSE;
            for (guint i = 0; i < spanned->len; i++) {
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
            if (!_parse_vblk(vblk, dg_o, comps, path, offset, err)) goto error;
        }

        vblk += vblk_data_size;
    }

    for (guint i = 0; i < spanned->len; i++) {
        struct _spanned_rec * const rec =
            g_array_index(spanned, struct _spanned_rec *, i);

        if (rec->entries_found != rec->entries_total) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Expected to find %hu entries for record %u, but "
                        "found %hu", rec->entries_total, rec->record_id,
                        rec->entries_found);
            goto error;
        }

        if (!_parse_vblk(rec->data, dg_o, comps, path, rec->offset, err))
            goto error;
    }

    g_array_unref(spanned); spanned = NULL;

    if (dg->disks->len != n_disks) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "Expected %u disk VBLKs, but found %u",
                    n_disks, dg->disks->len);
        goto error;
    }
    if (comps->len != n_comps) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "Expected %u component VBLKs, but found %u",
                    n_comps, comps->len);
        goto error;
    }
    if (dg->parts->len != n_parts) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "Expected %u partition VBLKs, but found %u",
                    n_parts, dg->parts->len);
        goto error;
    }
    if (dg->vols->len != n_vols) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "Expected %u volume VBLKs, but found %u",
                    n_vols, dg->vols->len);
        goto error;
    }

    for (guint32 i = 0; i < n_parts; i++) {
        LDMPartition * const part_o =
                g_array_index(dg->parts, LDMPartition *, i);
        LDMPartitionPrivate * const part = part_o->priv;

        /* Look for the underlying disk for this partition */
        for (guint32 j = 0; j < n_disks; j++) {
            LDMDisk * const disk_o =
                g_array_index(dg->disks, LDMDisk *, j);
            LDMDiskPrivate * const disk = disk_o->priv;

            if (disk->id == part->disk_id) {
                part->disk = disk_o;
                g_object_ref(disk_o);
                break;
            }
        }
        if (part->disk == NULL) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Partition %u references unknown disk %u",
                        part->id, part->disk_id);
            goto error;
        }

        /* Look for the parent component */
        gboolean parent_found = FALSE;
        for (guint j = 0; j < comps->len; j++) {
            struct _LDMComponent * const comp =
                (struct _LDMComponent *)comps->data + j;

            if (comp->id == part->parent_id) {
                g_array_append_val(comp->parts, part_o);
                g_object_ref(part_o);
                parent_found = TRUE;
                break;
            }
        }
        if (!parent_found) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Didn't find parent component %u for partition %u",
                        part->parent_id, part->id);
            goto error;
        }
    }

    for (guint32 i = 0; i < n_comps; i++) {
        struct _LDMComponent * const comp =
            (struct _LDMComponent *)comps->data + i;

        if (comp->parts->len != comp->n_parts) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Component %u expected %u partitions, but found %u",
                        comp->id, comp->n_parts, comp->parts->len);
            goto error;
        }

        if (comp->n_columns > 0 && comp->n_columns != comp->parts->len) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Component %u n_columns %u doesn't match number of "
                        "partitions %u",
                        comp->id, comp->n_columns, comp->parts->len);
            goto error;
        }

        /* Sort partitions into index order */
        g_array_sort(comp->parts, _cmp_component_parts);

        gboolean parent_found = FALSE;
        for (guint32 j = 0; j < n_vols; j++) {
            LDMVolume * const vol_o = g_array_index(dg->vols, LDMVolume *, j);
            LDMVolumePrivate * const vol = vol_o->priv;

            if (vol->id == comp->parent_id) {
                parent_found = TRUE;

                g_array_append_vals(vol->parts,
                                    comp->parts->data, comp->parts->len);
                vol->chunk_size = comp->chunk_size;
                vol->_n_comps_i++;

                switch (comp->type) {
                case _COMPONENT_TYPE_SPANNED:
                    if (vol->_int_type != _VOLUME_TYPE_GEN) {
                        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                                    "Unsupported configuration: SPANNED "
                                    "component has parent volume with type %u",
                                    vol->_int_type);
                        goto error;
                    }

                    if (vol->_n_comps > 1) {
                        vol->type = LDM_VOLUME_TYPE_MIRRORED;
                    } else if (comp->n_parts > 1) {
                        vol->type = LDM_VOLUME_TYPE_SPANNED;
                    } else {
                        vol->type = LDM_VOLUME_TYPE_SIMPLE;
                    }
                    break;

                case _COMPONENT_TYPE_STRIPED:
                    if (vol->_int_type != _VOLUME_TYPE_GEN) {
                        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                                    "Unsupported configuration: STRIPED "
                                    "component has parent volume with type %u",
                                    vol->_int_type);
                        goto error;
                    }

                    if (vol->_n_comps != 1) {
                        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                                    "Unsupported configuration: STRIPED "
                                    "component has parent volume with %u "
                                    "child components", vol->_n_comps);
                        goto error;
                    }

                    vol->type = LDM_VOLUME_TYPE_STRIPED;

                    break;

                case _COMPONENT_TYPE_RAID:
                    if (vol->_int_type != _VOLUME_TYPE_RAID5) {
                        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                                    "Unsupported configuration: RAID "
                                    "component has parent volume with type %u",
                                    vol->_int_type);
                        goto error;
                    }

                    if (vol->_n_comps != 1) {
                        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                                    "Unsupported configuration: RAID "
                                    "component has parent volume with %u "
                                    "child components", vol->_n_comps);
                        goto error;
                    }

                    vol->type = LDM_VOLUME_TYPE_RAID5;
                    break;

                default:
                    /* Should be impossible */
                    g_error("Unexpected component type %u", comp->type);
                }

                break;
            }
        }
        if (!parent_found) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Didn't find parent volume %u for component %u",
                        comp->parent_id, comp->id);
            goto error;
        }
    }

    for (guint32 i = 0; i < n_vols; i++) {
        LDMVolume * const vol_o = g_array_index(dg->vols, LDMVolume *, i);
        LDMVolumePrivate * const vol = vol_o->priv;

        if (vol->_n_comps_i != vol->_n_comps) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Volume %u expected %u components, but only found %u",
                        vol->id, vol->_n_comps, vol->_n_comps_i);
            goto error;
        }

        vol->dgname = g_strdup(dg->name);
    }

    for (guint32 i = 0; i < n_disks; i++) {
        LDMDisk * const disk_o = g_array_index(dg->disks, LDMDisk *, i);
        LDMDiskPrivate * const disk = disk_o->priv;

        disk->dgname = g_strdup(dg->name);
    }

    g_array_unref(comps);

    return TRUE;

error:
    if (spanned) g_array_unref(spanned);
    if (comps) g_array_unref(comps);
    return FALSE;
}

gboolean
ldm_add(LDM * const o, const gchar * const path, GError ** const err)
{
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_IO,
                    "Error opening %s for reading: %m", path);
        return FALSE;
    }

    int secsize;
    if (ioctl(fd, BLKSSZGET, &secsize) == -1) {
        g_warning("Unable to determine sector size of %s. Assuming 512 byte "
                  "sectors", path);
        secsize = 512;
    }

    return ldm_add_fd(o, fd, secsize, path, err);
}

gboolean
ldm_add_fd(LDM * const o, const int fd, const guint secsize,
           const gchar * const path, GError ** const err)
{
    GArray *disk_groups = o->priv->disk_groups;

    /* The GObject documentation states quite clearly that method calls on an
     * object which has been disposed should *not* result in an error. Seems
     * weird, but...
     */
    if (!disk_groups) return TRUE;

    void *config = NULL;

    struct _privhead privhead;
    if (!_read_privhead(fd, path, secsize, &privhead, err)) goto error;
    if (!_read_config(fd, path, secsize, &privhead, &config, err)) goto error;

    const struct _vmdb *vmdb;
    if (!_find_vmdb(config, path, secsize, &vmdb, err)) goto error;

    uuid_t disk_guid;
    uuid_t disk_group_guid;

    if (uuid_parse(privhead.disk_guid, disk_guid) == -1) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "PRIVHEAD contains invalid GUID for disk: %s",
                    privhead.disk_guid);
        goto error;
    }
    if (uuid_parse(privhead.disk_group_guid, disk_group_guid) == -1) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                    "PRIVHEAD contains invalid GUID for disk group: %s",
                    privhead.disk_group_guid);
        goto error;
    }

    LDMDiskGroup *dg_o = NULL;
    LDMDiskGroupPrivate *dg = NULL;
    for (guint i = 0; i < disk_groups->len; i++) {
        LDMDiskGroup *c = g_array_index(disk_groups,
                                            LDMDiskGroup *, i);

        if (uuid_compare(disk_group_guid, c->priv->guid) == 0) {
            dg_o = c;
        }
    }

    if (dg_o == NULL) {
        dg_o = LDM_DISK_GROUP(g_object_new(LDM_TYPE_DISK_GROUP, NULL));
        dg = dg_o->priv;

        uuid_copy(dg_o->priv->guid, disk_group_guid);

        g_debug("Found new disk group: " UUID_FMT, UUID_VALS(disk_group_guid));

        if (!_parse_vblks(config, path, vmdb, dg_o, err)) {
            g_object_unref(dg_o); dg_o = NULL;
            goto error;
        }

        g_array_append_val(disk_groups, dg_o);
    } else {
        dg = dg_o->priv;

        /* Check this disk is consistent with other disks */
        uint64_t committed = be64toh(vmdb->committed_seq);
        if (committed != dg->sequence) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INCONSISTENT,
                        "Members of disk group " UUID_FMT " are inconsistent: "
                        "disk %s has committed sequence %" PRIu64 ", "
                        "group has committed sequence %" PRIu64,
                        UUID_VALS(disk_group_guid),
                        path, committed, dg->sequence);
            goto error;
        }
    }

    /* Find the disk VBLK for the current disk and add additional information
     * from PRIVHEAD */
    for (guint i = 0; i < dg->disks->len; i++) {
        LDMDisk * const disk_o = g_array_index(dg->disks, LDMDisk *, i);
        LDMDiskPrivate * const disk = disk_o->priv;

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

LDM *
ldm_new()
{
    LDM *ldm = LDM_CAST(g_object_new(LDM_TYPE, NULL));
    ldm->priv->disk_groups = g_array_sized_new(FALSE, FALSE,
                                               sizeof (LDMDiskGroup *), 1);
    g_array_set_clear_func(ldm->priv->disk_groups, _unref_object);

    return ldm;
}

GArray *
ldm_get_disk_groups(LDM * const o)
{
    if (o->priv->disk_groups) g_array_ref(o->priv->disk_groups);
    return o->priv->disk_groups;
}

GArray *
ldm_disk_group_get_volumes(LDMDiskGroup * const o)
{
    if (o->priv->vols) g_array_ref(o->priv->vols);
    return o->priv->vols;
}

GArray *
ldm_disk_group_get_partitions(LDMDiskGroup * const o)
{
    if (o->priv->parts) g_array_ref(o->priv->parts);
    return o->priv->parts;
}

GArray *
ldm_disk_group_get_disks(LDMDiskGroup * const o)
{
    if (o->priv->disks) g_array_ref(o->priv->disks);
    return o->priv->disks;
}

GArray *
ldm_volume_get_partitions(LDMVolume * const o)
{
    if (o->priv->parts) g_array_ref(o->priv->parts);
    return o->priv->parts;
}

LDMDisk *
ldm_partition_get_disk(LDMPartition * const o)
{
    if (o->priv->disk) g_object_ref(o->priv->disk);
    return o->priv->disk;
}

static GString *
_dm_part_name(const LDMPartitionPrivate * const part)
{
    const LDMDiskPrivate * const disk = part->disk->priv;

    GString * name = g_string_new("");
    g_string_printf(name, "ldm_part_%s_%s", disk->dgname, part->name);

    return name;
}

static GString *
_dm_part_uuid(const LDMPartitionPrivate * const part)
{
    const LDMDiskPrivate * const disk = part->disk->priv;

    char ldm_disk_guid[37];
    uuid_unparse_lower(disk->guid, ldm_disk_guid);

    GString * dm_uuid = g_string_new("");
    g_string_printf(dm_uuid, "%s%s-%s",
                    DM_UUID_PREFIX, part->name, ldm_disk_guid);

    return dm_uuid;
}

static GString *
_dm_vol_name(const LDMVolumePrivate * const vol)
{
    GString * r = g_string_new("");
    g_string_printf(r, "ldm_vol_%s_%s", vol->dgname, vol->name);
    return r;
}

static GString *
_dm_vol_uuid(const LDMVolumePrivate * const vol)
{
    char ldm_vol_uuid[37];
    uuid_unparse_lower(vol->guid, ldm_vol_uuid);

    GString * dm_uuid = g_string_new("");
    g_string_printf(dm_uuid, "%s%s-%s",
                    DM_UUID_PREFIX, vol->name, ldm_vol_uuid);

    return dm_uuid;
}

struct dm_target {
    guint64 start;
    guint64 size;
    const gchar * type;
    GString *params;
};

static struct dm_tree *
_dm_get_device_tree(GError **err)
{
    struct dm_tree *tree;
    tree = dm_tree_create();
    if (!tree) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_tree_create: %s", _dm_err_last_msg);
        return NULL;
    }

    struct dm_task *task;
    task = dm_task_create(DM_DEVICE_LIST);
    if (!task) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_task_create: %s", _dm_err_last_msg);
        goto error;
    }

    if (!dm_task_run(task)) {
        g_set_error_literal(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                            _dm_err_last_msg);
        goto error;
    }

    struct dm_names *names;
    names = dm_task_get_names(task);
    if (!task) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_task_get_names: %s", _dm_err_last_msg);
        goto error;
    }

    if (names->dev != 0) {
        for (;;) {
            if (!dm_tree_add_dev(tree, major(names->dev), minor(names->dev))) {
                g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                            "dm_tree_add_dev: %s", _dm_err_last_msg);
                goto error;
            }

            if (names->next == 0) break;

            names = (struct dm_names *)((char *)names + names->next);
        }
    }

    dm_task_destroy(task);
    return tree;

error:
    if (tree) dm_tree_free(tree);
    if (task) dm_task_destroy(task);
    return NULL;
}

/* Returns TRUE if device with specified UUID is found or FALSE otherwise.
 *
 * Found device node is returned if dm_node is not NULL. In this case dm_tree
 * MUST not be NULL as well. And the caller is responsible to free device tree
 * by calling dm_tree_free function.
 */
gboolean
_dm_find_tree_node_by_uuid(const gchar * const uuid,
                                struct dm_tree_node ** dm_node,
                                struct dm_tree ** dm_tree,
                                GError ** const err)
{
    gboolean r = FALSE;

    if (dm_node) {
        g_assert(dm_tree != NULL);
        *dm_node = NULL;
    }
    if (dm_tree) *dm_tree = NULL;

    struct dm_tree *tree = _dm_get_device_tree(err);
    if (tree) {
        struct dm_tree_node *node = dm_tree_find_node_by_uuid(tree, uuid);
        if (node) r = TRUE;

        if (dm_tree)
            *dm_tree = tree;
        else
            dm_tree_free(tree);

        if (dm_node) *dm_node = node;
    }

    return r;
}

gchar *
_dm_get_device(const gchar * const uuid, GError ** const err)
{
    GString *r = NULL;

    struct dm_tree_node *node = NULL;
    struct dm_tree *tree = NULL;
    struct dm_task *task = NULL;

    if (_dm_find_tree_node_by_uuid(uuid, &node, &tree, err)) {
        const struct dm_info *info = dm_tree_node_get_info(node);

        task = dm_task_create(DM_DEVICE_INFO);
        if (!task) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "dm_task_create: %s", _dm_err_last_msg);
            goto error;
        }

        if (!dm_task_set_major(task, info->major)) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "DM_DEVICE_INFO: dm_task_set_major(%d) failed: %s",
                        info->major, _dm_err_last_msg);
            goto error;
        }

        if (!dm_task_set_minor(task, info->minor)) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "DM_DEVICE_INFO: dm_task_set_major(%d) failed: %s",
                        info->minor, _dm_err_last_msg);
            goto error;
        }

        if (!dm_task_run(task)) {
            g_set_error_literal(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                                _dm_err_last_msg);
            goto error;
        }

        const char *dir = dm_dir();
        char *mangled_name = dm_task_get_name_mangled(task);
        r = g_string_new("");
        g_string_printf(r, "%s/%s", dir, mangled_name);
        dm_free(mangled_name);
    }

error:
    if (tree) dm_tree_free(tree);
    if (task) dm_task_destroy(task);

    /* Really FALSE here - don't free but return the character data. */
    return r ? g_string_free(r, FALSE) : NULL;
}

gboolean
_dm_create(const gchar * const name, const gchar * const uuid,
           uint32_t udev_cookie, const guint n_targets,
           const struct dm_target * const targets,
           GString **mangled_name, GError ** const err)
{
    gboolean r = TRUE;

    if (mangled_name) *mangled_name = NULL;

    struct dm_task * const task = dm_task_create(DM_DEVICE_CREATE);
    if (!task) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_task_create(DM_DEVICE_CREATE) failed: %s",
                    _dm_err_last_msg);
        return FALSE;
    }

    if (!dm_task_set_name(task, name)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "DM_DEVICE_CREATE: dm_task_set_name(%s) failed: %s",
                    name, _dm_err_last_msg);
        r = FALSE; goto out;
    }

    if (!dm_task_set_uuid(task, uuid)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "DM_DEVICE_CREATE: dm_task_set_uuid(%s) failed: %s",
                    uuid, _dm_err_last_msg);
        r = FALSE; goto out;
    }

    for (guint i = 0; i < n_targets; i++) {
        const struct dm_target * const target = &targets[i];

        if (!dm_task_add_target(task, target->start, target->size,
                                      target->type, target->params->str))
        {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "DM_DEVICE_CREATE: "
                        "dm_task_add_target(%s, %" PRIu64 ", %" PRIu64 ", "
                                           "%s, %s) failed: %s",
                        name, target->start, target->size,
                        target->type, target->params->str, _dm_err_last_msg);
            r = FALSE; goto out;
        }
    }

    if (!dm_task_set_cookie(task, &udev_cookie, 0)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "DM_DEVICE_CREATE: dm_task_set_cookie(%08X) failed: %s",
                    udev_cookie, _dm_err_last_msg);
        r = FALSE; goto out;
    }

    if (!dm_task_run(task)) {
        g_set_error_literal(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                            _dm_err_last_msg);
        r = FALSE; goto out;
    }

    if (mangled_name) {
        char *tmp = dm_task_get_name_mangled(task);
        *mangled_name = g_string_new(tmp);
        dm_free(tmp);
    }

out:
    dm_task_destroy(task);
    return r;
}

gboolean
_dm_remove(const gchar * const name, uint32_t udev_cookie, GError ** const err)
{
    gboolean r = TRUE;

    struct dm_task * const task = dm_task_create(DM_DEVICE_REMOVE);
    if (!task) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_task_create(DM_DEVICE_REMOVE) failed: %s",
                    _dm_err_last_msg);
        r = FALSE; goto out;
    }

    if (!dm_task_set_name(task, name)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "DM_DEVICE_REMOVE: dm_task_set_name(%s) failed: %s",
                    name, _dm_err_last_msg);
        r = FALSE; goto out;
    }

    if (udev_cookie && !dm_task_set_cookie(task, &udev_cookie, 0)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "DM_DEVICE_REMOVE: dm_task_set_cookie(%08X) failed: %s",
                    udev_cookie, _dm_err_last_msg);
        r = FALSE; goto out;
    }

    /* If a remove operation fails, try again in case it was only opened
     * transiently. */
    dm_task_retry_remove(task);

    if (!dm_task_run(task)) {
        if (_dm_err_last_errno == EBUSY) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "Device is still mounted");
        } else {
            g_set_error_literal(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                                _dm_err_last_msg);
        }
        r = FALSE; goto out;
    }

out:
    dm_task_destroy(task);
    return r;
}

static GString *
_dm_create_part(const LDMPartitionPrivate * const part, uint32_t cookie,
                GError ** const err)
{
    const LDMDiskPrivate * const disk = part->disk->priv;

    if (!disk->device) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_MISSING_DISK,
                    "Disk %s required by partition %s is missing",
                    disk->name, part->name);
        return NULL;
    }

    struct dm_target target;
    target.start = 0;
    target.size = part->size;
    target.type = "linear";
    target.params = g_string_new("");
    g_string_printf(target.params, "%s %" PRIu64,
                    disk->device, disk->data_start + part->start);

    GString *name = _dm_part_name(part);
    GString *uuid = _dm_part_uuid(part);
    GString *mangled_name = NULL;

    if (!_dm_create(name->str, uuid->str, cookie, 1, &target,
                    &mangled_name, err)) {
        mangled_name = NULL;
    }

    g_string_free(name, TRUE);
    g_string_free(uuid, TRUE);
    g_string_free(target.params, TRUE);

    return mangled_name;
}

static GString *
_dm_create_spanned(const LDMVolumePrivate * const vol, GError ** const err)
{
    GString *name = NULL;
    guint i = 0;
    struct dm_target *targets = g_malloc(sizeof(*targets) * vol->parts->len);

    uint64_t pos = 0;
    for (; i < vol->parts->len; i++) {
        const LDMPartition * const part_o =
            g_array_index(vol->parts, const LDMPartition *, i);
        const LDMPartitionPrivate * const part = part_o->priv;

        const LDMDiskPrivate * const disk = part->disk->priv;
        if (!disk->device) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_MISSING_DISK,
                        "Disk %s required by spanned volume %s is missing",
                        disk->name, vol->name);
            goto out;
        }

        /* Sanity check: current position from adding up sizes of partitions
         * should equal the volume offset of the partition */
        if (pos != part->vol_offset) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_INVALID,
                        "Partition volume offset does not match sizes of "
                        "preceding partitions");
            goto out;
        }

        struct dm_target *target = &targets[i];
        target->start = pos;
        target->size = part->size;
        target->type = "linear";
        target->params = g_string_new("");
        g_string_append_printf(target->params, "%s %" PRIu64,
                                               disk->device,
                                               disk->data_start + part->start);
        pos += part->size;
    }

    uint32_t cookie;
    if (!dm_udev_create_cookie(&cookie)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_udev_create_cookie: %s", _dm_err_last_msg);
        goto out;
    }

    name = _dm_vol_name(vol);
    GString *uuid = _dm_vol_uuid(vol);

    if (!_dm_create(name->str, uuid->str, cookie, vol->parts->len, targets,
                    NULL, err)) {
        g_string_free(name, TRUE);
        name = NULL;
    }

    g_string_free(uuid, TRUE);
    dm_udev_wait(cookie);

out:
    for (; i > 0; i--) {
        struct dm_target *target = &targets[i-1];
        g_string_free(target->params, TRUE);
    }
    g_free(targets);

    return name;
}

static GString *
_dm_create_striped(const LDMVolumePrivate * const vol, GError ** const err)
{
    GString *name = NULL;
    struct dm_target target;

    target.start = 0;
    target.size = vol->size;
    target.type = "striped";
    target.params = g_string_new("");
    g_string_printf(target.params, "%" PRIu32 " %" PRIu64,
                    vol->parts->len, vol->chunk_size);

    for (guint i = 0; i < vol->parts->len; i++) {
        const LDMPartition * const part_o =
            g_array_index(vol->parts, const LDMPartition *, i);
        const LDMPartitionPrivate * const part = part_o->priv;

        const LDMDiskPrivate * const disk = part->disk->priv;
        if (!disk->device) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_MISSING_DISK,
                        "Disk %s required by striped volume %s is missing",
                        disk->name, vol->name);
            goto out;
        }

        g_string_append_printf(target.params, " %s %" PRIu64,
                                               disk->device,
                                               disk->data_start + part->start);
    }

    uint32_t cookie;
    if (!dm_udev_create_cookie(&cookie)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_udev_create_cookie: %s", _dm_err_last_msg);
        goto out;
    }

    name = _dm_vol_name(vol);
    GString *uuid = _dm_vol_uuid(vol);

    if (!_dm_create(name->str, uuid->str, cookie, 1, &target, NULL, err)) {
        g_string_free(name, TRUE);
        name = NULL;
    }

    g_string_free(uuid, TRUE);
    dm_udev_wait(cookie);

out:
    g_string_free(target.params, TRUE);

    return name;
}

static GString *
_dm_create_mirrored(const LDMVolumePrivate * const vol, GError ** const err)
{
    GString *name = NULL;
    struct dm_target target;

    target.start = 0;
    target.size = vol->size;
    target.type = "raid";
    target.params = g_string_new("");
    g_string_printf(target.params, "raid1 1 128 %u", vol->parts->len);

    GArray * devices = g_array_new(FALSE, FALSE, sizeof(GString *));
    g_array_set_clear_func(devices, _free_gstring);

    uint32_t cookie;
    if (!dm_udev_create_cookie(&cookie)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_udev_create_cookie: %s", _dm_err_last_msg);
        goto out;
    }

    const char *dir = dm_dir();

    int found = 0;
    for (guint i = 0; i < vol->parts->len; i++) {
        const LDMPartition * const part_o =
            g_array_index(vol->parts, const LDMPartition *, i);
        const LDMPartitionPrivate * const part = part_o->priv;

        GString * chunk = _dm_create_part(part, cookie, err);
        if (chunk == NULL) {
            if (err && (*err)->code == LDM_ERROR_MISSING_DISK) {
                g_warning("%s", (*err)->message);
                g_error_free(*err); *err = NULL;
                g_string_append(target.params, " - -");
                continue;
            } else {
                goto out;
            }
        }

        found++;
        g_array_append_val(devices, chunk);
        g_string_append_printf(target.params, " - %s/%s", dir, chunk->str);
    }

    if (found == 0) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_MISSING_DISK,
                    "Mirrored volume is missing all partitions");
        return FALSE;
    }

    /* Wait until all partitions have been created */
    dm_udev_wait(cookie);
    if (!dm_udev_create_cookie(&cookie)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_udev_create_cookie: %s", _dm_err_last_msg);
        goto out;
    }

    name = _dm_vol_name(vol);
    GString *uuid = _dm_vol_uuid(vol);

    if (!_dm_create(name->str, uuid->str, cookie, 1, &target, NULL, err)) {
        g_string_free(name, TRUE);
        name = NULL;
    }

    g_string_free(uuid, TRUE);
    dm_udev_wait(cookie);

    g_array_unref(devices); devices = NULL;

out:
    if (devices) {
        GError *cleanup_err = NULL;
        for (int i = devices->len; i > 0; i--) {
            GString *device = g_array_index(devices, GString *, i - 1);
            if (!_dm_remove(device->str, 0, &cleanup_err)) {
                g_warning("%s", cleanup_err->message);
                g_error_free(cleanup_err); cleanup_err = NULL;
            }
        }
        g_array_unref(devices); devices = NULL;
    }

    g_string_free(target.params, TRUE);

    return name;
}

static GString *
_dm_create_raid5(const LDMVolumePrivate * const vol, GError ** const err)
{
    GString *name = NULL;
    GString *uuid = NULL;
    struct dm_target target;

    target.start = 0;
    target.size = vol->size;
    target.type = "raid";
    target.params = g_string_new("");
    g_string_append_printf(target.params, "raid5_ls 1 %" PRIu64 " %" PRIu32,
                           vol->chunk_size, vol->parts->len);

    GArray * devices = g_array_new(FALSE, FALSE, sizeof(GString *));
    g_array_set_clear_func(devices, _free_gstring);

    uint32_t cookie;
    if (!dm_udev_create_cookie(&cookie)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_udev_create_cookie: %s", _dm_err_last_msg);
        goto out;
    }

    const char *dir = dm_dir();

    guint n_found = 0;
    for (guint i = 0; i < vol->parts->len; i++) {
        const LDMPartition * const part_o =
            g_array_index(vol->parts, const LDMPartition *, i);
        const LDMPartitionPrivate * const part = part_o->priv;

        GString * chunk = _dm_create_part(part, cookie, err);
        if (chunk == NULL) {
            if (err && (*err)->code == LDM_ERROR_MISSING_DISK) {
                g_warning("%s", (*err)->message);
                g_error_free(*err); *err = NULL;
                g_string_append(target.params, " - -");
                continue;
            } else {
                goto out;
            }
        }

        n_found++;
        g_array_append_val(devices, chunk);
        g_string_append_printf(target.params, " - %s/%s", dir, chunk->str);
    }

    if (n_found < vol->parts->len - 1) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_MISSING_DISK,
                    "RAID5 volume is missing more than 1 component");
        goto out;
    }

    /* Wait until all partitions have been created */
    dm_udev_wait(cookie);
    if (!dm_udev_create_cookie(&cookie)) {
        g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                    "dm_udev_create_cookie: %s", _dm_err_last_msg);
        goto out;
    }

    name = _dm_vol_name(vol);
    uuid = _dm_vol_uuid(vol);

    if (!_dm_create(name->str, uuid->str, cookie, 1, &target, NULL, err)) {
        g_string_free(name, TRUE); name = NULL;
        g_string_free(uuid, TRUE);
        goto out;
    }

    g_string_free(uuid, TRUE);
    dm_udev_wait(cookie);

    g_array_unref(devices); devices = NULL;

out:
    if (devices) {
        GError *cleanup_err = NULL;
        for (int i = devices->len; i > 0; i--) {
            GString *device = g_array_index(devices, GString *, i - 1);
            if (!_dm_remove(device->str, 0, &cleanup_err)) {
                g_warning("%s", cleanup_err->message);
                g_error_free(cleanup_err); cleanup_err = NULL;
            }
        }
        g_array_unref(devices); devices = NULL;
    }

    g_string_free(target.params, TRUE);

    return name;
}

GString *
ldm_volume_dm_get_name(const LDMVolume * const o)
{
    return _dm_vol_name(o->priv);
}

gchar *
ldm_partition_dm_get_device(const LDMPartition * const o, GError ** const err)
{
    GString *uuid = _dm_part_uuid(o->priv);
    gchar* r = _dm_get_device(uuid->str, err);
    g_string_free(uuid, TRUE);

    return r;
}

gchar *
ldm_volume_dm_get_device(const LDMVolume * const o, GError ** const err)
{
    GString *uuid = _dm_vol_uuid(o->priv);
    gchar* r = _dm_get_device(uuid->str, err);
    g_string_free(uuid, TRUE);

    return r;
}

gboolean
ldm_volume_dm_create(const LDMVolume * const o, GString **created,
                     GError ** const err)
{
    if (created) *created = NULL;

    const LDMVolumePrivate * const vol = o->priv;

    /* Check if the device already exists */
    GString *uuid = _dm_vol_uuid(vol);
    if (_dm_find_tree_node_by_uuid(uuid->str, NULL, NULL, err)) {
        g_string_free(uuid, TRUE);
        return TRUE;
    }
    g_string_free(uuid, TRUE);

    GString *name = NULL;
    switch (vol->type) {
    case LDM_VOLUME_TYPE_SIMPLE:
    case LDM_VOLUME_TYPE_SPANNED:
        name = _dm_create_spanned(vol, err);
        break;

    case LDM_VOLUME_TYPE_STRIPED:
        name = _dm_create_striped(vol, err);
        break;

    case LDM_VOLUME_TYPE_MIRRORED:
        name = _dm_create_mirrored(vol, err);
        break;

    case LDM_VOLUME_TYPE_RAID5:
        name = _dm_create_raid5(vol, err);
        break;

    default:
        /* Should be impossible */
        g_error("Unexpected volume type: %u", vol->type);
    }

    gboolean r = name != NULL;
    
    if (created)
        *created = name;
    else if (name)
        g_string_free(name, TRUE);

    return r;
}

gboolean
ldm_volume_dm_remove(const LDMVolume * const o, GString **removed,
                     GError ** const err)
{
    if (removed) *removed = NULL;

    const LDMVolumePrivate * const vol = o->priv;

    gboolean r = FALSE;

    struct dm_tree *tree = NULL;
    struct dm_tree_node *node = NULL;

    GString *uuid = _dm_vol_uuid(vol);
    gboolean found = _dm_find_tree_node_by_uuid(uuid->str, &node, &tree, err);
    g_string_free(uuid, TRUE);

    GString *name = NULL;
    if (found) {
        uint32_t cookie;
        if (!dm_udev_create_cookie(&cookie)) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "dm_udev_create_cookie: %s", _dm_err_last_msg);
            goto out;
        }

        name = _dm_vol_name(vol);
        if (!_dm_remove(name->str, cookie, err)) {
            g_string_free(name, TRUE); name = NULL;
            goto out;
        }

        dm_tree_set_cookie(node, cookie);
        if (!dm_tree_deactivate_children(node, NULL, 0)) {
            g_set_error(err, LDM_ERROR, LDM_ERROR_EXTERNAL,
                        "removing children: %s", _dm_err_last_msg);
            g_string_free(name, TRUE); name = NULL;
            goto out;
        }

        dm_udev_wait(cookie);
    }

    r = TRUE;

out:
    if (tree) dm_tree_free(tree);

    if (removed)
        *removed = name;
    else if (name)
        g_string_free(name, TRUE);

    return r;
}
