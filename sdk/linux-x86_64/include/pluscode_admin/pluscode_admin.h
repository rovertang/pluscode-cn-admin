#ifndef PLUSCODE_ADMIN_H
#define PLUSCODE_ADMIN_H
#include <stdint.h>
#if defined(_WIN32)
# if defined(PCAD_BUILDING)
#  define PCAD_API __declspec(dllexport)
# else
#  define PCAD_API __declspec(dllimport)
# endif
#else
# define PCAD_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t pcad_handle;
typedef enum pcad_status {
    PCAD_OK = 0, PCAD_INVALID_ARGUMENT = 1, PCAD_INVALID_INDEX = 2,
    PCAD_IO_ERROR = 3, PCAD_CLOSED = 4, PCAD_INTERNAL_ERROR = 5
} pcad_status;
typedef struct pcad_options {
    uint32_t struct_size;
    uint32_t cache_capacity;
    uint64_t cache_bytes;
} pcad_options;

/* All strings are UTF-8. Free returned result/error strings with pcad_free.
 * error may be NULL. Output pointers are reset before each operation.
 * Handles are safe against concurrent lookup/close; a started lookup may finish.
 * Repeated close returns PCAD_CLOSED. A capacity or byte limit of 0 disables caching.
 * NULL options selects 64 blocks / 8 MiB. The database remains an external file.
 * Cache bytes limit decoded block allocations, not total process memory. */
PCAD_API uint32_t pcad_abi_version(void);
PCAD_API const char* pcad_version(void);
PCAD_API pcad_status pcad_open(const char* database, const pcad_options* options,
                              pcad_handle* handle, char** error);
PCAD_API pcad_status pcad_lookup_code(pcad_handle handle, const char* code,
                                     char** json, char** error);
PCAD_API pcad_status pcad_lookup_latlng(pcad_handle handle, double latitude,
                                       double longitude, char** json, char** error);
PCAD_API pcad_status pcad_metadata(pcad_handle handle, char** json, char** error);
PCAD_API pcad_status pcad_cache_stats(pcad_handle handle, char** json, char** error);
/* Square neighborhood of 6-digit (0.05 degree) cells. radius: 0..20;
 * max_tiles: 0..1024. Synchronous, best effort within normal LRU limits. */
PCAD_API pcad_status pcad_prefetch_nearby(pcad_handle handle, double latitude, double longitude,
                                        int radius_tiles, int max_tiles, char** json, char** error);
PCAD_API pcad_status pcad_clear_cache(pcad_handle handle, char** error);
PCAD_API pcad_status pcad_close(pcad_handle handle, char** error);
PCAD_API void pcad_free(void* memory);
#ifdef __cplusplus
}
#endif
#endif
