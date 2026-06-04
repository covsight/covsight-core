#include <stdio.h>
#include <time.h>

#include "cJSON.h"

#include "ncdb_manifest.h"

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx;

static const uint32_t k256[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

static uint32_t rotr(uint32_t a, uint32_t b) { return ((a >> b) | (a << (32U - b))); }

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[]) {
    uint32_t a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];
    for (i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16) | ((uint32_t)data[j+2] << 8) | (uint32_t)data[j+3];
    }
    for (; i < 64; i++) {
        uint32_t s0 = rotr(m[i-15],7) ^ rotr(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = rotr(m[i-2],17) ^ rotr(m[i-2],19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        t1 = h + S1 + ch + k256[i] + m[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667U; ctx->state[1] = 0xbb67ae85U; ctx->state[2] = 0x3c6ef372U; ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU; ctx->state[5] = 0x9b05688cU; ctx->state[6] = 0x1f83d9abU; ctx->state[7] = 0x5be0cd19U;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[32]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8U;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4; i++) {
        hash[i]      = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xFFU);
        hash[i + 4]  = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xFFU);
        hash[i + 8]  = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xFFU);
        hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xFFU);
        hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xFFU);
        hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xFFU);
        hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xFFU);
        hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xFFU);
    }
}

static char *schema_hash(const uint8_t *data, size_t size) {
    sha256_ctx ctx;
    uint8_t hash[32];
    char *s = (char *)malloc(72U);
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (!s) return NULL;
    memcpy(s, "sha256:", 7);
    sha256_init(&ctx);
    sha256_update(&ctx, data, size);
    sha256_final(&ctx, hash);
    for (i = 0; i < 32; i++) {
        s[7 + i*2] = hex[(hash[i] >> 4) & 0xF];
        s[8 + i*2] = hex[hash[i] & 0xF];
    }
    s[71] = 0;
    return s;
}

void ncdb_manifest_init(ncdbManifest *m) {
    memset(m, 0, sizeof(*m));
    m->format = ncdb_impl_strdup(NCDB_FORMAT);
    m->version = ncdb_impl_strdup(NCDB_VERSION);
    m->ucis_version = ncdb_impl_strdup("1.0");
    m->path_separator = ncdb_impl_strdup("/");
    m->generator = ncdb_impl_strdup(NCDB_GENERATOR);
    m->history_format = ncdb_impl_strdup(NCDB_HISTORY_FORMAT_V1);
    m->schema_version_major = NCDB_SCHEMA_VERSION_MAJOR;
    m->schema_version_minor = NCDB_SCHEMA_VERSION_MINOR;
}

void ncdb_manifest_free(ncdbManifest *m) {
    free(m->format); free(m->version); free(m->ucis_version); free(m->created);
    free(m->path_separator); free(m->schema_hash); free(m->generator); free(m->history_format);
    free(m->vendor_id); free(m->vendor_tool); free(m->vendor_tool_version); free(m->ucis_standard);
    memset(m, 0, sizeof(*m));
}

int ncdb_manifest_build(ncdbT db, const uint8_t *scope_tree, size_t scope_tree_size, const uint64_t *counts, size_t count_count, ncdbManifest *m) {
    size_t i;
    time_t now = time(NULL);
    struct tm tmv;
    struct tm *tmvp;
    char tmp[64];
    ncdb_manifest_init(m);
    tmvp = gmtime(&now);
    if (tmvp) {
        tmv = *tmvp;
    } else {
        memset(&tmv, 0, sizeof(tmv));
    }
    strftime(tmp, sizeof(tmp), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    m->created = ncdb_impl_strdup(tmp);
    free(m->path_separator);
    m->path_separator = ncdb_impl_strdup(db->path_separator ? db->path_separator : "/");
    m->scope_count = ncdb_impl_count_scopes(db);
    m->coveritem_count = count_count;
    for (i = 0; i < count_count; i++) {
        m->total_hits += counts[i];
        if (counts[i] > 0) {
            m->covered_bins++;
        }
    }
    for (i = 0; i < db->history_count; i++) {
        if (db->history_nodes[i]->kind == NCDB_HISTORY_TEST) {
            m->test_count++;
        }
    }
    m->n_history_nodes = (uint64_t)db->history_count;
    /* n_associations is populated by M4 when tests_assoc.bin lands;
     * remains 0 here. feature_flags is populated by each member's
     * writer setting its bit on db before manifest_build. */
    m->feature_flags = db->feature_flags;
    m->schema_hash = schema_hash(scope_tree, scope_tree_size);
    if (db->vendor_id)           { free(m->vendor_id);           m->vendor_id           = ncdb_impl_strdup(db->vendor_id); }
    if (db->vendor_tool)         { free(m->vendor_tool);         m->vendor_tool         = ncdb_impl_strdup(db->vendor_tool); }
    if (db->vendor_tool_version) { free(m->vendor_tool_version); m->vendor_tool_version = ncdb_impl_strdup(db->vendor_tool_version); }
    if (db->ucis_standard)       { free(m->ucis_standard);       m->ucis_standard       = ncdb_impl_strdup(db->ucis_standard); }
    return (m->created && m->path_separator && m->schema_hash) ? 0 : -1;
}

/*
 * manifest.json — binary "NMAN" with two extant versions.
 *
 * v1 (legacy; emitted by NCDB v3.0 writers):
 *   magic[4]   "NMAN"
 *   version: u8 = 1
 *   12 length-prefixed strings (varint length, raw bytes; empty allowed):
 *     format, version, ucis_version, created, path_separator,
 *     schema_hash, generator, history_format,
 *     vendor_id, vendor_tool, vendor_tool_version, ucis_standard
 *   5 varints:
 *     scope_count, coveritem_count, test_count, total_hits, covered_bins
 *
 * v2 (Phase 4.8 / M8; emitted by NCDB v3.x-on-v4-track and v4.0):
 *   ...v1 payload above, byte-identical...
 *   schema_version_major: u32 LE
 *   schema_version_minor: u32 LE
 *   feature_flags:        u64 LE
 *   n_history_nodes:      varint
 *   n_associations:       varint
 *
 * Readers handle both. v1 readers reject v2 cleanly at the version byte
 * check ("unknown manifest binary version 2") — that is the intentional
 * cut-over signal for v3 readers encountering v4 fixtures.
 *
 * Legacy JSON readers are still accepted on deserialize for backward
 * compat with existing fixtures; the writer emits binary only.
 */

#define NCDB_MANIFEST_MAGIC    "NMAN"
#define NCDB_MANIFEST_MAGIC_SZ 4U
#define NCDB_MANIFEST_VERSION  2U   /* v2 = M8 (Phase 4.8) */
#define NCDB_MANIFEST_VERSION_V1 1U /* legacy, still accepted on deserialize */

static int write_lp_str(ncdbBuf *b, const char *s) {
    size_t len = s ? strlen(s) : 0;
    if (ncdb_varint_encode_uint64((uint64_t)len, b) != 0) return -1;
    if (len > 0 && ncdb_impl_buf_append(b, s, len) != 0) return -1;
    return 0;
}

static int read_lp_str(const uint8_t *data, size_t size, size_t *off, char **out) {
    uint64_t len; char *buf;
    if (ncdb_varint_decode_uint64(data, size, off, &len) != 0) return -1;
    if (*off + len > size) return -1;
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) return -1;
    if (len > 0) memcpy(buf, data + *off, (size_t)len);
    buf[len] = '\0';
    *off += (size_t)len;
    free(*out); *out = buf;
    return 0;
}

static int write_u32le(ncdbBuf *b, uint32_t v) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(v);       bytes[1] = (uint8_t)(v >> 8);
    bytes[2] = (uint8_t)(v >> 16); bytes[3] = (uint8_t)(v >> 24);
    return ncdb_impl_buf_append(b, bytes, 4);
}

static int read_u32le(const uint8_t *data, size_t size, size_t *off, uint32_t *out) {
    uint32_t v;
    if (*off + 4 > size) return -1;
    v  = (uint32_t)data[*off]            |
        ((uint32_t)data[*off + 1] << 8)  |
        ((uint32_t)data[*off + 2] << 16) |
        ((uint32_t)data[*off + 3] << 24);
    *off += 4; *out = v;
    return 0;
}

static int write_u64le_m(ncdbBuf *b, uint64_t v) {
    uint8_t bytes[8]; int i;
    for (i = 0; i < 8; ++i) bytes[i] = (uint8_t)(v >> (i * 8));
    return ncdb_impl_buf_append(b, bytes, 8);
}

static int read_u64le_m(const uint8_t *data, size_t size, size_t *off, uint64_t *out) {
    int i; uint64_t v = 0;
    if (*off + 8 > size) return -1;
    for (i = 0; i < 8; ++i) v |= ((uint64_t)data[*off + i]) << (i * 8);
    *off += 8; *out = v;
    return 0;
}

int ncdb_manifest_serialize(const ncdbManifest *m, ncdbBuf *out) {
    uint8_t version = NCDB_MANIFEST_VERSION;
    if (ncdb_impl_buf_append(out, NCDB_MANIFEST_MAGIC, NCDB_MANIFEST_MAGIC_SZ) != 0) return -1;
    if (ncdb_impl_buf_append(out, &version, 1) != 0) return -1;
    if (write_lp_str(out, m->format) != 0) return -1;
    if (write_lp_str(out, m->version) != 0) return -1;
    if (write_lp_str(out, m->ucis_version) != 0) return -1;
    if (write_lp_str(out, m->created) != 0) return -1;
    if (write_lp_str(out, m->path_separator) != 0) return -1;
    if (write_lp_str(out, m->schema_hash) != 0) return -1;
    if (write_lp_str(out, m->generator) != 0) return -1;
    if (write_lp_str(out, m->history_format) != 0) return -1;
    if (write_lp_str(out, m->vendor_id) != 0) return -1;
    if (write_lp_str(out, m->vendor_tool) != 0) return -1;
    if (write_lp_str(out, m->vendor_tool_version) != 0) return -1;
    if (write_lp_str(out, m->ucis_standard) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->scope_count, out) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->coveritem_count, out) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->test_count, out) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->total_hits, out) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->covered_bins, out) != 0) return -1;
    /* v2 extension (Phase 4.8 / M8). */
    if (write_u32le(out, m->schema_version_major) != 0) return -1;
    if (write_u32le(out, m->schema_version_minor) != 0) return -1;
    if (write_u64le_m(out, m->feature_flags) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->n_history_nodes, out) != 0) return -1;
    if (ncdb_varint_encode_uint64(m->n_associations, out) != 0) return -1;
    return 0;
}

static int deserialize_json_fallback(const uint8_t *data, size_t size, ncdbManifest *m, char *errbuf, size_t errbuf_sz) {
    cJSON *obj = cJSON_ParseWithLength((const char *)data, size);
    cJSON *it;
    if (!obj || !cJSON_IsObject(obj)) {
        snprintf(errbuf, errbuf_sz, "%s", "invalid manifest json");
        cJSON_Delete(obj);
        return -1;
    }
#define GETS(field,key) do { it = cJSON_GetObjectItemCaseSensitive(obj, key); if (cJSON_IsString(it) && it->valuestring) { free(m->field); m->field = ncdb_impl_strdup(it->valuestring); } } while (0)
#define GETN(field,key) do { it = cJSON_GetObjectItemCaseSensitive(obj, key); if (cJSON_IsNumber(it)) m->field = (uint64_t)it->valuedouble; } while (0)
    GETS(format, "format"); GETS(version, "version"); GETS(ucis_version, "ucis_version"); GETS(created, "created");
    GETS(path_separator, "path_separator"); GETN(scope_count, "scope_count"); GETN(coveritem_count, "coveritem_count");
    GETN(test_count, "test_count"); GETN(total_hits, "total_hits"); GETN(covered_bins, "covered_bins");
    GETS(schema_hash, "schema_hash"); GETS(generator, "generator"); GETS(history_format, "history_format");
    GETS(vendor_id, "vendor_id"); GETS(vendor_tool, "vendor_tool"); GETS(vendor_tool_version, "vendor_tool_version");
    GETS(ucis_standard, "ucis_standard");
#undef GETS
#undef GETN
    cJSON_Delete(obj);
    return 0;
}

int ncdb_manifest_deserialize(const uint8_t *data, size_t size, ncdbManifest *m, char *errbuf, size_t errbuf_sz) {
    size_t off;
    uint8_t version;
    ncdb_manifest_init(m);
    if (size < NCDB_MANIFEST_MAGIC_SZ + 1 ||
        memcmp(data, NCDB_MANIFEST_MAGIC, NCDB_MANIFEST_MAGIC_SZ) != 0) {
        return deserialize_json_fallback(data, size, m, errbuf, errbuf_sz);
    }
    off = NCDB_MANIFEST_MAGIC_SZ;
    version = data[off++];
    if (version != NCDB_MANIFEST_VERSION && version != NCDB_MANIFEST_VERSION_V1) {
        snprintf(errbuf, errbuf_sz, "unknown manifest binary version %u", (unsigned)version);
        return -1;
    }
    if (read_lp_str(data, size, &off, &m->format) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->version) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->ucis_version) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->created) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->path_separator) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->schema_hash) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->generator) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->history_format) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->vendor_id) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->vendor_tool) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->vendor_tool_version) != 0) goto bad;
    if (read_lp_str(data, size, &off, &m->ucis_standard) != 0) goto bad;
    if (ncdb_varint_decode_uint64(data, size, &off, &m->scope_count) != 0) goto bad;
    if (ncdb_varint_decode_uint64(data, size, &off, &m->coveritem_count) != 0) goto bad;
    if (ncdb_varint_decode_uint64(data, size, &off, &m->test_count) != 0) goto bad;
    if (ncdb_varint_decode_uint64(data, size, &off, &m->total_hits) != 0) goto bad;
    if (ncdb_varint_decode_uint64(data, size, &off, &m->covered_bins) != 0) goto bad;
    if (version >= NCDB_MANIFEST_VERSION) {
        /* v2 extension fields. */
        if (read_u32le(data, size, &off, &m->schema_version_major) != 0) goto bad;
        if (read_u32le(data, size, &off, &m->schema_version_minor) != 0) goto bad;
        if (read_u64le_m(data, size, &off, &m->feature_flags) != 0) goto bad;
        if (ncdb_varint_decode_uint64(data, size, &off, &m->n_history_nodes) != 0) goto bad;
        if (ncdb_varint_decode_uint64(data, size, &off, &m->n_associations) != 0) goto bad;
    } else {
        /* v1: synthesize v4-track defaults so consumers can compare uniformly. */
        m->schema_version_major = 3;
        m->schema_version_minor = 0;
        m->feature_flags = 0;
        m->n_history_nodes = m->test_count;
        m->n_associations = 0;
    }
    return 0;
bad:
    snprintf(errbuf, errbuf_sz, "%s", "truncated manifest binary");
    return -1;
}
