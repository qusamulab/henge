/* CMS SignedData for code signatures, matching what `codesign` writes for an Apple Development
 * identity: SHA-256, RSA PKCS#1 v1.5, certificates WWDR G3 + Apple Root + the signer's, and the
 * signed attributes contentType, signingTime, messageDigest and Apple's two cdhash attributes
 * (1.2.840.113635.100.9.2 and .9.1). Only definite-length DER is written. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <CommonCrypto/CommonDigest.h>

#include "internal.h"

/* ---- DER writing ---- */

struct buf {
    uint8_t *p;
    size_t n, cap;
    int oom;
};

static void buf_add(struct buf *b, const void *d, size_t n)
{
    if (b->oom)
        return;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n)
            cap *= 2;
        uint8_t *p = realloc(b->p, cap);
        if (!p) {
            b->oom = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void buf_byte(struct buf *b, uint8_t v)
{
    buf_add(b, &v, 1);
}

/* tag, DER length, content */
static void buf_tlv(struct buf *dst, uint8_t tag, const uint8_t *c, size_t n)
{
    buf_byte(dst, tag);
    if (n < 0x80) {
        buf_byte(dst, (uint8_t)n);
    } else {
        uint8_t tmp[8];
        int k = 0;
        for (size_t v = n; v; v >>= 8)
            tmp[k++] = (uint8_t)v;
        buf_byte(dst, (uint8_t)(0x80 | k));
        while (k)
            buf_byte(dst, tmp[--k]);
    }
    buf_add(dst, c, n);
}

/* Wraps src in a TLV and appends it to dst, then empties src. */
static void buf_wrap(struct buf *dst, uint8_t tag, struct buf *src)
{
    buf_tlv(dst, tag, src->p, src->n);
    free(src->p);
    memset(src, 0, sizeof *src);
}

static void buf_free(struct buf *b)
{
    free(b->p);
    memset(b, 0, sizeof *b);
}

#define TAG_INT 0x02
#define TAG_OCTET 0x04
#define TAG_NULL 0x05
#define TAG_OID 0x06
#define TAG_UTCTIME 0x17
#define TAG_SEQ 0x30
#define TAG_SET 0x31
#define TAG_CTX0 0xa0

static const uint8_t OID_SIGNED_DATA[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02};
static const uint8_t OID_DATA[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x01};
static const uint8_t OID_SHA256[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
static const uint8_t OID_SHA256_RSA[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
static const uint8_t OID_CONTENT_TYPE[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x03};
static const uint8_t OID_SIGNING_TIME[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x05};
static const uint8_t OID_MESSAGE_DIGEST[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04};
static const uint8_t OID_APPLE_CDHASH_V2[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x63, 0x64, 0x09, 0x02};
static const uint8_t OID_APPLE_CDHASH_PLIST[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x63, 0x64, 0x09, 0x01};

static void add_oid(struct buf *b, const uint8_t *oid, size_t n)
{
    buf_tlv(b, TAG_OID, oid, n);
}
#define ADD_OID(b, oid) add_oid((b), (oid), sizeof(oid))

/* AlgorithmIdentifier { oid, NULL } */
static void add_alg(struct buf *b, const uint8_t *oid, size_t n)
{
    struct buf a = {0};
    add_oid(&a, oid, n);
    buf_tlv(&a, TAG_NULL, NULL, 0);
    buf_wrap(b, TAG_SEQ, &a);
}
#define ADD_ALG(b, oid) add_alg((b), (oid), sizeof(oid))

/* Attribute { oid, SET { value } } where value is already a complete TLV. */
static void add_attr(struct buf *b, const uint8_t *oid, size_t n, const struct buf *value)
{
    struct buf a = {0}, set = {0};
    add_oid(&a, oid, n);
    buf_add(&set, value->p, value->n);
    buf_wrap(&a, TAG_SET, &set);
    buf_wrap(b, TAG_SEQ, &a);
}
#define ADD_ATTR(b, oid, v) add_attr((b), (oid), sizeof(oid), (v))

/* ---- DER reading (certificates) ---- */

struct tlv {
    uint8_t tag;
    const uint8_t *start; /* first byte of the TLV */
    const uint8_t *val;
    size_t len;
    size_t total;
};

static int tlv_read(const uint8_t *p, const uint8_t *end, struct tlv *t)
{
    if (end - p < 2)
        return -1;
    t->start = p;
    t->tag = p[0];
    size_t l = p[1], hl = 2;
    if (l & 0x80) {
        size_t n = l & 0x7f;
        if (n == 0 || n > 4 || (size_t)(end - p) < 2 + n)
            return -1;
        l = 0;
        for (size_t i = 0; i < n; i++)
            l = l << 8 | p[2 + i];
        hl = 2 + n;
    }
    if (l > (size_t)(end - p) - hl)
        return -1;
    t->val = p + hl;
    t->len = l;
    t->total = hl + l;
    return 0;
}

/* Walks Certificate -> TBSCertificate and returns the issuer and serial TLVs and the subject. */
static int cert_fields(const uint8_t *cert, size_t len, struct tlv *serial, struct tlv *issuer, struct tlv *subject)
{
    const uint8_t *end = cert + len;
    struct tlv c, tbs, t;
    if (tlv_read(cert, end, &c) || c.tag != TAG_SEQ || tlv_read(c.val, c.val + c.len, &tbs) || tbs.tag != TAG_SEQ)
        return -1;
    const uint8_t *p = tbs.val, *tend = tbs.val + tbs.len;
    if (tlv_read(p, tend, &t))
        return -1;
    if (t.tag == TAG_CTX0) { /* version */
        p += t.total;
        if (tlv_read(p, tend, &t))
            return -1;
    }
    if (t.tag != TAG_INT)
        return -1;
    *serial = t;
    p += t.total;
    for (int skip = 0; skip < 1; skip++) { /* signature algorithm */
        if (tlv_read(p, tend, &t))
            return -1;
        p += t.total;
    }
    if (tlv_read(p, tend, issuer) || issuer->tag != TAG_SEQ)
        return -1;
    p += issuer->total;
    if (tlv_read(p, tend, &t)) /* validity */
        return -1;
    p += t.total;
    if (tlv_read(p, tend, subject) || subject->tag != TAG_SEQ)
        return -1;
    return 0;
}

int hc_cert_info_parse(const uint8_t *cert, size_t len, struct hc_cert_info *out, char *err, size_t errlen)
{
    static const uint8_t oid_ou[] = {0x55, 0x04, 0x0b};
    struct tlv serial, issuer, subject;
    memset(out, 0, sizeof *out);
    if (cert_fields(cert, len, &serial, &issuer, &subject))
        return hc_fail(err, errlen, "not a valid X.509 certificate");
    const uint8_t *p = subject.val, *end = subject.val + subject.len;
    struct tlv rdn;
    while (p < end && tlv_read(p, end, &rdn) == 0) {
        struct tlv atv, oid, val;
        if (rdn.tag == TAG_SET && tlv_read(rdn.val, rdn.val + rdn.len, &atv) == 0 && atv.tag == TAG_SEQ &&
            tlv_read(atv.val, atv.val + atv.len, &oid) == 0 && oid.tag == TAG_OID &&
            oid.len == sizeof oid_ou && memcmp(oid.val, oid_ou, sizeof oid_ou) == 0 &&
            tlv_read(oid.val + oid.len, atv.val + atv.len, &val) == 0 && val.len > 0 && val.len < sizeof out->team_id) {
            memcpy(out->team_id, val.val, val.len);
            return 0;
        }
        p += rdn.total;
    }
    return hc_fail(err, errlen, "the certificate has no team ID (subject OU)");
}

/* ---- CMS ---- */

static void b64_20(const uint8_t in[20], char out[29])
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < 20; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < 20 ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < 20 ? in[i + 2] : 0);
        out[o++] = tbl[v >> 18 & 63];
        out[o++] = tbl[v >> 12 & 63];
        out[o++] = i + 1 < 20 ? tbl[v >> 6 & 63] : '=';
        out[o++] = i + 2 < 20 ? tbl[v & 63] : '=';
    }
    out[o] = 0;
}

int hc_cms_build(const struct henge_signer *sg, const uint8_t *cd, size_t cd_len, int dummy_sig, uint8_t **out,
                 size_t *out_len, char *err, size_t errlen)
{
    *out = NULL;
    *out_len = 0;
    struct tlv serial, issuer, subject, wwdr_serial, wwdr_issuer, wwdr_subject;
    if (cert_fields(sg->cert, sg->cert_len, &serial, &issuer, &subject))
        return hc_fail(err, errlen, "not a valid X.509 certificate");
    if (cert_fields(hc_apple_wwdr_g3, hc_apple_wwdr_g3_len, &wwdr_serial, &wwdr_issuer, &wwdr_subject))
        return hc_fail(err, errlen, "internal error: bad bundled certificate");
    if (issuer.total != wwdr_subject.total || memcmp(issuer.start, wwdr_subject.start, issuer.total) != 0)
        return hc_fail(err, errlen,
                       "the certificate was not issued by Apple Worldwide Developer Relations CA (G3); other issuers are not supported");

    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(cd, (CC_LONG)cd_len, digest);

    char b64[29];
    b64_20(digest, b64);
    char plist[512];
    int plist_len = snprintf(plist, sizeof plist,
                             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                             "<plist version=\"1.0\">\n"
                             "<dict>\n"
                             "\t<key>cdhashes</key>\n"
                             "\t<array>\n"
                             "\t\t<data>\n"
                             "\t\t%s\n"
                             "\t\t</data>\n"
                             "\t</array>\n"
                             "</dict>\n"
                             "</plist>\n",
                             b64);

    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char when[16];
    strftime(when, sizeof when, "%y%m%d%H%M%SZ", &tm);

    /* signed attributes, in the order codesign writes them */
    struct buf attrs = {0}, v = {0};
    buf_tlv(&v, TAG_OID, OID_DATA, sizeof OID_DATA);
    ADD_ATTR(&attrs, OID_CONTENT_TYPE, &v);
    buf_free(&v);
    buf_tlv(&v, TAG_UTCTIME, (const uint8_t *)when, 13);
    ADD_ATTR(&attrs, OID_SIGNING_TIME, &v);
    buf_free(&v);
    buf_tlv(&v, TAG_OCTET, digest, sizeof digest);
    ADD_ATTR(&attrs, OID_MESSAGE_DIGEST, &v);
    buf_free(&v);
    {
        struct buf inner = {0};
        ADD_OID(&inner, OID_SHA256);
        buf_tlv(&inner, TAG_OCTET, digest, sizeof digest);
        buf_wrap(&v, TAG_SEQ, &inner);
        ADD_ATTR(&attrs, OID_APPLE_CDHASH_V2, &v);
        buf_free(&v);
    }
    buf_tlv(&v, TAG_OCTET, (const uint8_t *)plist, (size_t)plist_len);
    ADD_ATTR(&attrs, OID_APPLE_CDHASH_PLIST, &v);
    buf_free(&v);

    /* The signature covers the attributes as a SET (tag 0x31); in SignerInfo they are [0] IMPLICIT. */
    struct buf to_sign = {0};
    buf_tlv(&to_sign, TAG_SET, attrs.p, attrs.n);
    uint8_t *sig = calloc(1, sg->signature_len);
    if (!sig || to_sign.oom || attrs.oom) {
        free(sig);
        buf_free(&attrs);
        buf_free(&to_sign);
        return hc_fail(err, errlen, "out of memory");
    }
    if (!dummy_sig && sg->sign(sg->user, to_sign.p, to_sign.n, sig, err, errlen) != 0) {
        free(sig);
        buf_free(&attrs);
        buf_free(&to_sign);
        return -1;
    }
    buf_free(&to_sign);

    struct buf si = {0}, ias = {0};
    buf_add(&si, "\x02\x01\x01", 3); /* version 1 */
    buf_add(&ias, issuer.start, issuer.total);
    buf_add(&ias, serial.start, serial.total);
    buf_wrap(&si, TAG_SEQ, &ias);
    ADD_ALG(&si, OID_SHA256);
    buf_wrap(&si, TAG_CTX0, &attrs);
    ADD_ALG(&si, OID_SHA256_RSA);
    buf_tlv(&si, TAG_OCTET, sig, sg->signature_len);
    free(sig);

    struct buf sd = {0}, tmp = {0}, certs = {0}, signers = {0};
    buf_add(&sd, "\x02\x01\x01", 3); /* version 1 */
    ADD_ALG(&tmp, OID_SHA256);
    buf_wrap(&sd, TAG_SET, &tmp);
    ADD_OID(&tmp, OID_DATA);
    buf_wrap(&sd, TAG_SEQ, &tmp); /* detached: no content */
    buf_add(&certs, hc_apple_wwdr_g3, hc_apple_wwdr_g3_len);
    buf_add(&certs, hc_apple_root, hc_apple_root_len);
    buf_add(&certs, sg->cert, sg->cert_len);
    buf_wrap(&sd, TAG_CTX0, &certs);
    buf_wrap(&signers, TAG_SEQ, &si);
    buf_wrap(&sd, TAG_SET, &signers);

    struct buf ci = {0}, sdseq = {0}, ctx = {0};
    ADD_OID(&ci, OID_SIGNED_DATA);
    buf_wrap(&sdseq, TAG_SEQ, &sd);
    buf_wrap(&ctx, TAG_CTX0, &sdseq);
    buf_add(&ci, ctx.p, ctx.n);
    buf_free(&ctx);
    struct buf all = {0};
    buf_wrap(&all, TAG_SEQ, &ci);
    if (all.oom) {
        buf_free(&all);
        return hc_fail(err, errlen, "out of memory");
    }
    *out = all.p;
    *out_len = all.n;
    return 0;
}
