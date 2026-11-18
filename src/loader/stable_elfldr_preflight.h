/* Read-only import preflight for the exact bytes passed to the loader. */
#ifndef KABUKI_STABLE_ELFLDR_PREFLIGHT_H
#define KABUKI_STABLE_ELFLDR_PREFLIGHT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum commonfps_preflight_status {
    COMMONFPS_PREFLIGHT_OK = 0,
    COMMONFPS_PREFLIGHT_UNRESOLVED = 1,
    COMMONFPS_PREFLIGHT_MALFORMED = 2
};

struct commonfps_preflight_result {
    size_t resolved;
    size_t unresolved;
    char first_unresolved[128];
    int pthread_create_available;
    int getpid_available;
};

/* No attach, remote writes, mapping, execution, or loader-global changes.
 * Counts are relocation occurrences, not unique symbol names. Bootstrap
 * availability is reported separately and does not affect import status.
 * On malformed input the result is zeroed. */
int commonfps_stable_elfldr_preflight(pid_t pid, const uint8_t* elf,
                                     size_t size,
                                     struct commonfps_preflight_result* result);
#ifdef __cplusplus
}
#endif
#endif
