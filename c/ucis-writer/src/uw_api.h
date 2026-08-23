/* uw_api.h - internals shared between the entry points and the emitters.
 * SPDX-License-Identifier: Apache-2.0 */

#ifndef UW_API_H
#define UW_API_H

#include "uw_types.h"

/* Write the root element's required attributes and stop accepting changes to
 * them. Idempotent. */
UW_INTERNAL int uw_root_seal(uw_db_t* db);

/* Transition into the document body: seal the root, then emit the resident
 * source-file and history-node tables if they have not gone out yet. Every
 * entry point that writes body content calls this first. */
UW_INTERNAL int uw_body(uw_db_t* db);

#endif /* UW_API_H */
