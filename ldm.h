#ifndef LIBPART_LDM_H__
#define LIBPART_LDM_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PartLDMError:
 * @PART_LDM_ERROR_IO: There was an IO error accessing a device
 * @PART_LDM_ERROR_NOT_LDM: The device is not part of an LDM disk group
 * @PART_LDM_ERROR_INVALID: The LDM metadata is corrupt
 * @PART_LDM_ERROR_NOTSUPPORTED: Unsupported LDM metadata
 * @PART_LDM_ERROR_INTERNAL: An internal error
 */
typedef enum {
    PART_LDM_ERROR_IO,
    PART_LDM_ERROR_NOT_LDM,
    PART_LDM_ERROR_INVALID,
    PART_LDM_ERROR_INCONSISTENT,
    PART_LDM_ERROR_NOTSUPPORTED,
    PART_LDM_ERROR_INTERNAL
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

GType part_ldm_get_type(void);
GType part_ldm_disk_group_get_type(void);

PartLDM *part_ldm_new(GError **err);
gboolean part_ldm_add(PartLDM *o, const gchar *path, GError **err);
GArray *part_ldm_get_disk_groups(PartLDM *o, GError **err);

void part_ldm_disk_group_dump(PartLDMDiskGroup *o);

G_END_DECLS

#endif /* LIBPART_LDM_H__ */
