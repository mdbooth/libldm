#include <endian.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

struct _ldm_privhead {
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

struct _ldm_tocblock_bitmap {
    char name[8];
    uint16_t flags1;
    uint64_t start;
    uint64_t size; // Relative to start of DB
    uint64_t flags2;
} __attribute__((__packed__));

struct _ldm_tocblock {
    char magic[8]; // "TOCBLOCK"

    uint32_t seq1;
    char padding1[4];
    uint32_t seq2;
    char padding2[16];

    struct _ldm_tocblock_bitmap bitmap[2];
} __attribute__((__packed__));

struct _ldm_vmdb {
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

struct _ldm_vblk_head {
    char magic[4]; // "VBLK"

    uint32_t seq;
    uint32_t group;

    uint16_t record;
    uint16_t n_records;

    uint16_t status;
    uint8_t  record_flags;
    uint8_t  record_type;
    uint32_t record_size;
} __attribute__((__packed__));

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
            { PART_LDM_ERROR_IO, "PART_LDM_ERROR_IO", "io" },
            { PART_LDM_ERROR_NOT_LDM, "PART_LDM_ERROR_NOT_LDM", "not_ldm" },
            { PART_LDM_ERROR_INVALID, "PART_LDM_ERROR_INVALID", "invalid" },
            { PART_LDM_ERROR_INCONSISTENT, "PART_LDM_ERROR_INCONSISTENT",
                                           "inconsistent" },
            { PART_LDM_ERROR_NOTSUPPORTED, "PART_LDM_ERROR_NOTSUPPORTED",
                                           "notsupported" }
        };
        etype = g_enum_register_static("PartLDMError", values);
    }
    return etype;
}

#define PART_LDM_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM, PartLDMPrivate))

struct _PartLDMPrivate
{
    GArray *disk_groups;
};

G_DEFINE_TYPE(PartLDM, part_ldm, G_TYPE_OBJECT)

static void
_unref_disk_groups(GArray *disk_groups)
{
    for (int i = 0; i < disk_groups->len; i++) {
        PartLDMDiskGroup *dg =
            PART_LDM_DISK_GROUP(g_array_index(disk_groups,
                                              PartLDMDiskGroup *, i));
        g_object_unref(dg);
    }
    g_array_unref(disk_groups);
}

static void
part_ldm_finalize(GObject *object)
{
    PartLDM *ldm = PART_LDM(object);

    _unref_disk_groups(ldm->priv->disk_groups);
}

static void
part_ldm_init(PartLDM *o)
{
    o->priv = PART_LDM_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

static void
part_ldm_class_init(PartLDMClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = part_ldm_finalize;

    g_type_class_add_private(klass, sizeof(PartLDMPrivate));
}

#define PART_LDM_DISK_GROUP_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE \
        ((obj), PART_TYPE_LDM_DISK_GROUP, PartLDMDiskGroupPrivate))

struct _PartLDMDiskGroupPrivate
{
    uuid_t guid;

    char *name;
    uint64_t sequence;

    uint32_t n_disks;
    uint32_t n_comps;
    uint32_t n_parts;
    uint32_t n_vols;

    struct _disk *disks;
    struct _comp *comps;
    struct _part *parts;
    struct _vol *vols;
};

G_DEFINE_TYPE(PartLDMDiskGroup, part_ldm_disk_group, G_TYPE_OBJECT)

static void
part_ldm_disk_group_finalize(GObject *object)
{
    PartLDMDiskGroup *dg = PART_LDM_DISK_GROUP(object);

    g_free(dg->priv->name); dg->priv->name = NULL;
    g_free(dg->priv->disks); dg->priv->disks = NULL;
    g_free(dg->priv->comps); dg->priv->comps = NULL;
    g_free(dg->priv->vols); dg->priv->vols = NULL;
}

static void
part_ldm_disk_group_class_init(PartLDMDiskGroupClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = part_ldm_disk_group_finalize;

    g_type_class_add_private(klass, sizeof(PartLDMDiskGroupPrivate));
}

static void
part_ldm_disk_group_init(PartLDMDiskGroup *o)
{
    o->priv = PART_LDM_DISK_GROUP_GET_PRIVATE(o);
    bzero(o->priv, sizeof(*o->priv));
}

struct _disk {
    uint32_t id;
    char *name;

    uuid_t guid;
    char *device; // NULL until device is found
};

struct _part {
    uint32_t id;
    uint32_t parent_id;
    char *name;

    uint64_t start;
    uint64_t vol_offset;
    uint64_t size;
    uint32_t index;

    uint32_t disk_id;
    const struct _disk *disk;
};

typedef enum {
    STRIPED = 0x1,
    SPANNED = 0x2,
    MIRRORED = 0x3
} _comp_type_t;

struct _comp {
    uint32_t id;
    uint32_t parent_id;
    char *name;

    _comp_type_t type;
    uint32_t n_parts;
    const struct _part **parts;
    uint32_t parts_i;

    uint64_t stripe_size;
    uint32_t n_columns;
};

typedef enum {
    GEN,
    RAID5
} _vol_type_t;

struct _vol {
    uint32_t id;
    char *name;

    _vol_type_t type;
    uint64_t size;
    uint8_t part_type;

    uint32_t n_comps;
    const struct _comp **comps;
    uint32_t comps_i;

    char *id1;
    char *id2;
    uint64_t size2;
    char *hint;
};

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
    /* XXX: Fix size detection for block devices */
    struct stat stat;
    if (fstat(fd, &stat) == -1) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_IO,
                    "Unable to stat %s: %m", path);
        return FALSE;
    }

    const uint64_t config_start =
        be64toh(privhead->ldm_config_start) * SECTOR_SIZE;
    const uint64_t config_size =
        be64toh(privhead->ldm_config_size) * SECTOR_SIZE;

    if (config_start > stat.st_size) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "LDM config start (%lX) is outside file in %s",
                    config_start, path);
        return FALSE;
    }
    if (config_start + config_size > stat.st_size) {
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
        goto error;
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
            goto error;
        }

        if (uuid_compare(pte.type, LDM_METADATA) == 0) {
            /* PRIVHEAD is in the last LBA of the LDM metadata partition */
            return _read_privhead_off(fd, path, pte.last_lba * SECTOR_SIZE,
                                       privhead, err);
        }
    }

    g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOT_LDM,
                "%s does not contain LDM metadata", path);

error:
    gpt_close(h);
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
func_name(const uint8_t ** const var, out_type * const out,                    \
          const gchar * const field, const gchar * const type,                 \
          GError ** const err)                                                 \
{                                                                              \
    uint8_t i = **var; (*var)++;                                               \
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

static char *
_parse_var_string(const uint8_t ** const var)
{
    uint8_t len = **var; (*var)++;
    char *ret = g_malloc(len + 1);
    memcpy(ret, *var, len); (*var) += len;
    ret[len] = '\0';

    return ret;
}

static void
_parse_var_skip(const uint8_t ** const var)
{
    uint8_t len = **var; (*var)++;
    (*var) += len;
}

static gboolean
_parse_vblk_vol(const uint8_t revision, const uint16_t flags,
                const uint8_t * vblk, struct _vol * const vol,
                GError ** const err)
{
    if (revision != 5) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported volume VBLK revision %hhu", revision);
        return FALSE;
    }

    vblk += sizeof(struct _ldm_vblk_head);

    if (!_parse_var_int32(&vblk, &vol->id, "id", "volume", err))
        return FALSE;
    vol->name = _parse_var_string(&vblk);

    char *type = _parse_var_string(&vblk);
    if (strcmp(type, "gen") == 0) {
        vol->type = GEN;
    } else if (strcmp(type, "raid5") == 0) {
        vol->type = RAID5;
    } else {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported volume VBLK type %s", type);
        g_free(type);
        return FALSE;
    }
    g_free(type);

    /* Zeroes */
    vblk += 1;

    /* Volume state */
    vblk += 14;

    /* Other volume type, not sure what this one's for */
    vblk += 1;

    /* Unknown */
    vblk += 1;

    /* Volume number */
    vblk += 1;

    /* Zeroes */
    vblk += 3;

    /* Flags */
    vblk += 1;

    if (!_parse_var_int32(&vblk, &vol->n_comps, "n_children", "volume", err))
        return FALSE;
    vol->comps = g_malloc(sizeof(vol->comps[0]) * vol->n_comps);

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
_parse_vblk_comp(const uint8_t revision, const uint16_t flags,
                 const uint8_t *vblk, struct _comp * const comp,
                 GError ** const err)
{
    if (revision != 3) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported component VBLK revision %hhu", revision);
        return FALSE;
    }

    vblk += sizeof(struct _ldm_vblk_head);
    if (!_parse_var_int32(&vblk, &comp->id, "id", "volume", err)) return FALSE;
    comp->name = _parse_var_string(&vblk);

    /* Volume state */
    _parse_var_skip(&vblk);

    comp->type = *((uint8_t *) vblk); vblk++;
    if (comp->type != STRIPED &&
        comp->type != SPANNED &&
        comp->type != MIRRORED)
    {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Component VBLK OID=%u has unsupported type %u",
                    comp->id, comp->type);
        return FALSE;
    }

    /* Zeroes */
    vblk += 4;

    if (!_parse_var_int32(&vblk, &comp->n_parts, "n_parts", "component", err))
        return FALSE;
    comp->parts = g_malloc(sizeof(comp->parts[0]) * comp->n_parts);

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
_parse_vblk_part(const uint8_t revision, const uint16_t flags,
                 const uint8_t *vblk, struct _part * const part,
                 GError ** const err)
{
    if (revision != 3) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                    "Unsupported partition VBLK revision %hhu", revision);
        return FALSE;
    }

    vblk += sizeof(struct _ldm_vblk_head);
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
_parse_vblk_disk(const uint8_t revision, const uint16_t flags,
                 const uint8_t *vblk, struct _disk * const disk,
                 GError ** const err)
{
    vblk += sizeof(struct _ldm_vblk_head);
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
_parse_vblks(const void * const config, const gchar * const path,
          const struct _ldm_vmdb * const vmdb,
          PartLDMDiskGroupPrivate * const dg, GError ** const err)
{
    dg->sequence = be64toh(vmdb->committed_seq);

    dg->n_disks = be32toh(vmdb->n_committed_vblks_disk);
    dg->n_comps = be32toh(vmdb->n_committed_vblks_comp);
    dg->n_parts = be32toh(vmdb->n_committed_vblks_part);
    dg->n_vols = be32toh(vmdb->n_committed_vblks_vol);

    dg->disks = g_malloc0(sizeof(dg->disks[0]) * dg->n_disks);
    dg->comps = g_malloc0(sizeof(dg->comps[0]) * dg->n_comps);
    dg->parts = g_malloc0(sizeof(dg->parts[0]) * dg->n_parts);
    dg->vols = g_malloc0(sizeof(dg->vols[0]) * dg->n_vols);

    size_t disks_i = 0, comps_i = 0, parts_i = 0, vols_i = 0;

    const uint16_t vblk_size = be32toh(vmdb->vblk_size);
    const void *vblk = (void *)vmdb + be32toh(vmdb->vblk_first_offset);
    for(;;) {
        const struct _ldm_vblk_head * const head = vblk;
        if (memcmp(head->magic, "VBLK", 4) != 0) break;

        const uint8_t type = head->record_type & 0x0F;
        const uint8_t revision = (head->record_type & 0xF0) >> 4;

        switch (type) {
        case 0x00:
            /* Blank VBLK */
            break;

        case 0x01:
            if (!_parse_vblk_vol(revision, head->record_flags,
                                 vblk, &dg->vols[vols_i++], err))
                return FALSE;
            break;

        case 0x02:
            if (!_parse_vblk_comp(revision, head->record_flags,
                                  vblk, &dg->comps[comps_i++], err))
                return FALSE;
            break;

        case 0x03:
            if (!_parse_vblk_part(revision, head->record_flags,
                                  vblk, &dg->parts[parts_i++], err))
                return FALSE;
            break;

        case 0x04:
            if (!_parse_vblk_disk(revision, head->record_flags,
                                  vblk, &dg->disks[disks_i++], err))
                return FALSE;
            break;

        case 0x05:
            /* We don't need any addition about the disk group */
            break;

        default:
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_NOTSUPPORTED,
                        "Unknown VBLK type %hhi in %s at config offset %lX",
                        type, path,
                        (unsigned long int)vblk - (unsigned long int)config);
            return FALSE;
        }
        
        vblk += vblk_size;
    }

    if (disks_i != dg->n_disks) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u disk VBLKs, but found %zu",
                    dg->n_disks, disks_i);
        return FALSE;
    }
    if (comps_i != dg->n_comps) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u component VBLKs, but found %zu",
                    dg->n_comps, comps_i);
        return FALSE;
    }
    if (parts_i != dg->n_parts) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u partition VBLKs, but found %zu",
                    dg->n_parts, parts_i);
        return FALSE;
    }
    if (vols_i != dg->n_vols) {
        g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                    "Expected %u volume VBLKs, but found %zu",
                    dg->n_vols, vols_i);
        return FALSE;
    }

    for (int i = 0; i < dg->n_parts; i++) {
        struct _part * const part = &dg->parts[i];

        /* Look for the underlying disk for this partition */
        for (int j = 0; j < dg->n_disks; j++) {
            if (dg->disks[j].id == part->disk_id) {
                part->disk = &dg->disks[j];
                break;
            }
        }
        if (part->disk == NULL) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Partition %u references unknown disk %u",
                        part->id, part->disk_id);
            return FALSE;
        }

        /* Look for the parent component */
        gboolean parent_found = FALSE;
        for (int j = 0; j < dg->n_comps; j++) {
            struct _comp * const comp = &dg->comps[j];
            if (comp->id == part->parent_id) {
                if (comp->parts_i == comp->n_parts) {
                    g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                                "Too many partitions for component %u",
                                comp->id);
                    return FALSE;
                }
                comp->parts[comp->parts_i++] = part;
                parent_found = TRUE;
                break;
            }
        }
        if (!parent_found) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Didn't find parent component %u for partition %u",
                        part->parent_id, part->id);
            return FALSE;
        }
    }

    for (int i = 0; i < dg->n_comps; i++) {
        const struct _comp * const comp = &dg->comps[i];

        if (comp->parts_i != comp->n_parts) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Component %u expected %u partitions, but only found "
                        "%u", comp->id, comp->n_parts, comp->parts_i);
            return FALSE;
        }

        gboolean parent_found = FALSE;
        for (int j = 0; j < dg->n_vols; j++) {
            struct _vol * const vol = &dg->vols[j];

            if (vol->id == comp->parent_id) {
                if (vol->comps_i == vol->n_comps) {
                    g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                                "Too many components for volume %u",
                                vol->id);
                    return FALSE;
                }
                vol->comps[vol->comps_i++] = comp;
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
        const struct _vol * const vol = &dg->vols[i];

        if (vol->comps_i != vol->n_comps) {
            g_set_error(err, LDM_ERROR, PART_LDM_ERROR_INVALID,
                        "Volume %u expected %u components, but only found %u",
                        vol->id, vol->n_comps, vol->comps_i);
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
part_ldm_add(PartLDM *o, const gchar * const path, GError ** const err)
{
    GArray *disk_groups = o->priv->disk_groups;

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

        if (!_parse_vblks(config, path, vmdb, dg->priv, err)) {
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

    for (int i = 0; i < dg->priv->n_disks; i++) {
        struct _disk * const disk = &dg->priv->disks[i];
        if (uuid_compare(disk_guid, disk->guid) == 0) {
            disk->device = g_strdup(path);
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

    return ldm;
}

GArray *
part_ldm_get_disk_groups(PartLDM *o, GError **err)
{
    return o->priv->disk_groups;
}

void
part_ldm_disk_group_dump(PartLDMDiskGroup *o)
{
    PartLDMDiskGroupPrivate *dg = o->priv;

    char guid_str[37];
    uuid_unparse(dg->guid, guid_str);

    g_message("GUID: %s", guid_str);
    g_message("Disks: %u", dg->n_disks);
    g_message("Components: %u", dg->n_comps);
    g_message("Partitions: %u", dg->n_parts);
    g_message("Volumes: %u", dg->n_vols);

    for (int i = 0; i < dg->n_vols; i++) {
        const struct _vol * const vol = &dg->vols[i];

        g_message("Volume: %u", vol->id);
        g_message("  Name: %s", vol->name);
        g_message("  Type: %s", vol->type == GEN ? "gen" : "raid5");
        g_message("  Size: %lu", vol->size);
        g_message("  Partition type: %hhu", vol->part_type);
        if (vol->id1) g_message("  ID1: %s", vol->id1);
        if (vol->id2) g_message("  ID2: %s", vol->id2);
        if (vol->size2 > 0) g_message("  Size2: %lu", vol->size2);
        if (vol->hint) g_message("  Drive Hint: %s", vol->hint);

        for (int j = 0; j < vol->n_comps; j++) {
            const struct _comp * const comp = vol->comps[j];

            g_message("  Component: %i", comp->id);
            g_message("    Name: %s", comp->name);
            const char *comp_type = NULL;
            switch (comp->type) {
            case STRIPED:       comp_type = "STRIPED"; break;
            case SPANNED:       comp_type = "SPANNED"; break;
            case MIRRORED:      comp_type = "MIRRORED"; break;
            }
            g_message("    Type: %s", comp_type);
            if (comp->stripe_size > 0)
                g_message("    Stripe Size: %lu", comp->stripe_size);
            if (comp->n_columns > 0)
                g_message("    Columns: %u", comp->n_columns);

            for (int k = 0; k < comp->n_parts; k++) {
                const struct _part * const part = comp->parts[k];

                g_message("    Partition: %u", part->id);
                g_message("      Name: %s", part->name);
                g_message("      Start: %lu", part->start);
                g_message("      Size: %lu", part->size);
                g_message("      Volume Offset: %lu", part->vol_offset);
                g_message("      Component Index: %u", part->index);

                const struct _disk * const disk = part->disk;
                uuid_unparse(disk->guid, guid_str);
                g_message("      Disk: %u", disk->id);
                g_message("        GUID: %s", guid_str);
                g_message("        Device: %s", disk->device);
            }
        }
    }
}
