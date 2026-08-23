/* uw_text.h - XML escaping and text sanitisation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work items 1.3 and 1.4 of docs/ucis-writer-impl-plan.md.
 *
 * Caller strings are design identifiers, file paths and command lines. They
 * are not trusted to be XML-safe and, on real designs, they are not always
 * valid UTF-8 either. Everything that reaches the document goes through
 * uw_text_escape(), which guarantees well-formed XML 1.0 output for any byte
 * sequence, counting each repair it makes. */

#ifndef UW_TEXT_H
#define UW_TEXT_H

#include "uw_buf.h"

/* Escape and emit `n` bytes. `nwarn` accumulates the number of bytes that had
 * to be replaced because XML 1.0 cannot represent them; it may be NULL. */
UW_INTERNAL int uw_text_escape(uw_buf_t* b, const char* s, size_t n,
                               unsigned long* nwarn);

UW_INTERNAL int uw_text_escape_cstr(uw_buf_t* b, const char* s,
                                    unsigned long* nwarn);

/* Emit ` name="value"`. `name` must be a literal produced by this library and
 * is written without escaping; `value` is escaped. A NULL `value` emits
 * nothing at all, which is how optional attributes are omitted. */
UW_INTERNAL int uw_text_attr(uw_buf_t* b, const char* name, const char* value,
                             unsigned long* nwarn);

/* As uw_text_attr, for a value that is a slice of a larger string rather than
 * a NUL-terminated one. */
UW_INTERNAL int uw_text_attr_n(uw_buf_t* b, const char* name, const char* value,
                               size_t len, unsigned long* nwarn);

UW_INTERNAL int uw_text_attr_u64(uw_buf_t* b, const char* name, uint64_t value);
UW_INTERNAL int uw_text_attr_i64(uw_buf_t* b, const char* name, int64_t value);
UW_INTERNAL int uw_text_attr_double(uw_buf_t* b, const char* name, double value);

#endif /* UW_TEXT_H */
