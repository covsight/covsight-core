/* 01_hello.c - the smallest UCIS-XML document ucis_writer can produce.
 *
 * Build (vendored, single header):
 *     cc -o 01_hello 01_hello.c
 * with ucis_writer.h next to it, or from this tree:
 *     cc -I../include -o 01_hello 01_hello.c -L. -lucis_writer
 *
 * Run:  ./01_hello out.xml
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Four things are mandatory in every document, in this order:
 *   1. at least one source file
 *   2. at least one history node (the test that produced the data)
 *   3. at least one instance
 *   4. ucis_Close
 * The schema types all three lists minOccurs="1", so there is no such thing
 * as a valid empty UCIS-XML document. Start from this file.
 */

#include <stdio.h>
#include "ucis_writer.h"

/* --8<-- doc:begin */
int main(int argc, char** argv)
{
    const char*     path = (argc > 1) ? argv[1] : "01_hello.xml";
    ucisT           db;
    ucisFileHandleT file;
    ucisSourceInfoT srcinfo;

    db = ucis_OpenWriteStream(path);
    if (db == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }

    /* 1. Source files. Every id referenced later must be interned before the
     *    first instance -- after that the table has already been written. */
    file = ucis_CreateFileHandle(db, "rtl/top.sv", NULL);

    /* 2. The history node describes the run. logicalName, date, toolCategory
     *    and the vendor triple are required; ucis_writer fills in the ones
     *    you do not set. */
    ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL, UCIS_HISTORYNODE_TEST);

    /* 3. An instance of a design unit. In write-streaming mode the parent is
     *    always NULL: the library tracks the current scope for you. */
    srcinfo.filehandle = file;
    srcinfo.line       = 1;
    srcinfo.token      = 0;
    ucis_CreateInstanceByName(db, NULL, "top", &srcinfo, 1, UCIS_VLOG,
                              UCIS_INSTANCE, (char*)"work.top",
                              UCIS_INST_ONCE);
    ucis_WriteStreamScope(db);   /* done with this instance */

    /* 4. Errors latch on the database rather than being raised, so there is
     *    exactly one place to check. Read the message before ucis_Close --
     *    Close frees the handle. */
    if (ucis_writer_error(db) != UCIS_WRITER_OK) {
        fprintf(stderr, "%s: %s\n", path, ucis_writer_error_string(db));
    }
    if (ucis_Close(db) != 0) {
        fprintf(stderr, "%s: write failed\n", path);
        return 1;
    }
    return 0;
}
/* --8<-- doc:end */
