#include <uuid/uuid.h>
#include <stdint.h>

typedef enum {
    GPT_ERROR_OK,
    GPT_ERROR_INVALID,
    GPT_ERROR_READ,
    GPT_ERROR_INVALID_PART
} gpt_error_t;

typedef struct {
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;

    uuid_t disk_guid;

    uint32_t pte_array_len;
    uint32_t pte_size;
} gpt_t;

typedef struct {
    uuid_t type;
    uuid_t guid;
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    char name[72];
} gpt_pte_t;

typedef struct _gpt_handle gpt_handle_t;

int gpt_open(int fd, gpt_handle_t **h);
int gpt_open_secsize(int fd, size_t secsize, gpt_handle_t **h);
void gpt_close(gpt_handle_t *h);

void gpt_get_header(gpt_handle_t *h, gpt_t *gpt);
int gpt_get_pte(gpt_handle_t *h, uint32_t n, gpt_pte_t *part);
