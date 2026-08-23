/* uw_fixture.h - one shared document fixture and its expected bytes.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Used by test_uw_minimal (split-source build) and test_uw_amalgam (the
 * generated single header). Both assert the same expected bytes, which is how
 * T-6.2 establishes that the artifact we ship behaves like the sources we
 * test. Only the public API is used here; nothing reaches into src/. */

#ifndef UW_FIXTURE_H
#define UW_FIXTURE_H

#include "uw_test_sink.h"

#include <assert.h>

static const char* const UW_FIXTURE_WANT =
    "<UCIS ucisVersion=\"1.0\" writtenBy=\"test\""
    " writtenTime=\"2026-07-26T12:00:00Z\">"
    "<sourceFiles fileName=\"rtl/top.sv\" id=\"1\"/>"
    "<historyNodes historyNodeId=\"0\" logicalName=\"run1\" kind=\"TEST\""
    " testStatus=\"true\" date=\"2026-07-26T12:00:00Z\""
    " toolCategory=\"UCIS:Simulator\" ucisVersion=\"1.0\""
    " vendorId=\"covsight\" vendorTool=\"ucis_writer\""
    " vendorToolVersion=\"" UCIS_WRITER_VERSION "\"/>"
    "<instanceCoverages name=\"top\" key=\"top\" moduleName=\"work.top\""
    " instanceId=\"1\">"
    "<id file=\"1\" line=\"1\" inlineCount=\"1\"/>"
    "</instanceCoverages>"
    "</UCIS>";

/* The smallest legal document. `close_scope` selects whether the caller
 * balances its own scope or leaves it to ucis_Close to repair. */
static ucisT uw_fixture_build(uw_memsink_t* m, ucisWriterSinkT* sink,
                              int close_scope)
{
    ucisT           db;
    ucisFileHandleT f;

    uw_memsink_free(m);
    uw_memsink_init(m, sink);
    db = ucis_writer_OpenSinkStream(sink);
    assert(db != NULL);

    assert(ucis_writer_set_written_by(db, "test") == 0);
    assert(ucis_writer_set_written_time(db, "2026-07-26T12:00:00Z") == 0);

    f = ucis_CreateFileHandle(db, "rtl/top.sv", NULL);
    assert(f != NULL);

    assert(ucis_CreateHistoryNode(db, NULL, (char*)"run1", NULL,
                                  UCIS_HISTORYNODE_TEST) != NULL);
    assert(ucis_CreateInstanceByName(db, NULL, "top", NULL, 1, UCIS_VLOG,
                                     UCIS_INSTANCE, (char*)"work.top",
                                     UCIS_INST_ONCE) != NULL);
    if (close_scope) {
        assert(ucis_WriteStreamScope(db) == 0);
    }
    return db;
}

#endif /* UW_FIXTURE_H */
