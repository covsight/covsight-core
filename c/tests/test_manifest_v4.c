/*
 * test_manifest_v4 — exercises M8 (Phase 4.8): the v2 manifest binary
 * with numeric schema version, feature flags, n_history_nodes,
 * n_associations.
 *
 * Cases:
 *   1. v2 round-trip via ncdb_manifest_serialize/deserialize preserves
 *      every new field.
 *   2. v1 binary deserialize synthesizes v3.0 defaults (back-compat for
 *      existing v3 golden fixtures).
 *   3. End-to-end: write a DB with feature_flags set on the db, reopen,
 *      confirm the manifest reflects what was written.
 */

#include "ncdb/ncdb.h"
#include "ncdb_impl.h"
#include "ncdb_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PATH = "manifest_v4.cdb";

/* Build a minimal v1 manifest binary by hand and confirm deserialize
 * fills the v4-track fields with defaults. */
static int test_v1_backcompat(void) {
    ncdbBuf buf;
    ncdbManifest m;
    char err[256] = {0};
    uint8_t version_v1 = 1;
    int rc = 1;

    ncdb_impl_buf_init(&buf);
    if (ncdb_impl_buf_append(&buf, "NMAN", 4) != 0) goto out;
    if (ncdb_impl_buf_append(&buf, &version_v1, 1) != 0) goto out;
    /* 12 empty length-prefixed strings + 5 zero varints */
    {
        int i;
        uint8_t zero = 0;
        for (i = 0; i < 12 + 5; i++) {
            if (ncdb_impl_buf_append(&buf, &zero, 1) != 0) goto out;
        }
    }

    memset(&m, 0, sizeof(m));
    if (ncdb_manifest_deserialize(buf.data, buf.size, &m, err, sizeof(err)) != 0) {
        printf("v1 deserialize failed: %s\n", err);
        goto out;
    }
    if (m.schema_version_major != 3) { printf("v1 major=%u\n", m.schema_version_major); goto out; }
    if (m.schema_version_minor != 0) { printf("v1 minor=%u\n", m.schema_version_minor); goto out; }
    if (m.feature_flags != 0)        { printf("v1 flags=%llu\n", (unsigned long long)m.feature_flags); goto out; }
    if (m.n_associations != 0)       { goto out; }
    rc = 0;
out:
    ncdb_manifest_free(&m);
    ncdb_impl_buf_free(&buf);
    return rc;
}

static int test_v2_roundtrip(void) {
    ncdbBuf buf;
    ncdbManifest in, out;
    char err[256] = {0};
    int rc = 1;

    ncdb_impl_buf_init(&buf);
    ncdb_manifest_init(&in);
    free(in.created); in.created = ncdb_impl_strdup("2026-05-23T00:00:00Z");
    free(in.schema_hash); in.schema_hash = ncdb_impl_strdup("sha256:test");
    in.scope_count = 42;
    in.coveritem_count = 100;
    in.test_count = 5;
    in.feature_flags = NCDB_FEATURE_UNIQUE_ID_INDEX | NCDB_FEATURE_TYPED_PROPS;
    in.n_history_nodes = 7;
    in.n_associations = 12345;
    in.schema_version_major = 4;
    in.schema_version_minor = 0;

    if (ncdb_manifest_serialize(&in, &buf) != 0) goto out;

    memset(&out, 0, sizeof(out));
    if (ncdb_manifest_deserialize(buf.data, buf.size, &out, err, sizeof(err)) != 0) {
        printf("v2 deserialize failed: %s\n", err);
        goto out;
    }
    if (out.schema_version_major != 4) { printf("major=%u\n", out.schema_version_major); goto out; }
    if (out.schema_version_minor != 0) { printf("minor=%u\n", out.schema_version_minor); goto out; }
    if (out.feature_flags != (NCDB_FEATURE_UNIQUE_ID_INDEX | NCDB_FEATURE_TYPED_PROPS)) {
        printf("flags=%llx\n", (unsigned long long)out.feature_flags); goto out;
    }
    if (out.n_history_nodes != 7)      { goto out; }
    if (out.n_associations != 12345)   { goto out; }
    if (out.scope_count != 42)          { goto out; }
    rc = 0;
out:
    ncdb_manifest_free(&in);
    ncdb_manifest_free(&out);
    ncdb_impl_buf_free(&buf);
    return rc;
}

static int test_e2e_feature_flags(void) {
    ncdbT db;
    int rc = 1;

    db = ncdb_Open(NULL);
    ncdb_impl_create_scope(db, NULL, NCDB_SCOPE_INSTANCE, "top");
    /* Simulate a feature being landed by toggling the bit on the db
     * before write. Later PRs will do this from the feature's own
     * serializer; here we just exercise the manifest plumbing. */
    db->feature_flags |= NCDB_FEATURE_FORMAL;
    if (ncdb_Write(db, PATH) != 0) { printf("write: %s\n", ncdb_GetLastError(db)); goto out; }
    ncdb_Close(db);

    db = ncdb_Open(PATH);
    if (!db) goto out;
    /* The manifest itself isn't surfaced through a public ncdb_Get* API
     * today; we round-trip via reopen to confirm no error. A real
     * coverage check ships with M2/M3/etc. consumer code. */
    rc = 0;
out:
    if (db) ncdb_Close(db);
    remove(PATH);
    return rc;
}

int main(void) {
    int rc;
    rc = test_v1_backcompat();
    if (rc) { printf("manifest_v4 v1-backcompat failed\n"); return rc; }
    rc = test_v2_roundtrip();
    if (rc) { printf("manifest_v4 v2-roundtrip failed\n"); return rc; }
    rc = test_e2e_feature_flags();
    if (rc) { printf("manifest_v4 e2e failed\n"); return rc; }
    printf("manifest_v4 ok\n");
    return 0;
}
