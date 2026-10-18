/* henge_signer backed by Security.framework: a PKCS#12 identity (any platform) or a keychain
 * identity (macOS). The private key never leaves its SecKeyRef. */
#include <stdlib.h>
#include <string.h>

#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#include <Security/Security.h>

#include "internal.h"

struct sec_signer {
    struct henge_signer pub; /* first, so the pointers convert */
    SecKeyRef key;
    CFDataRef cert;
};

static int sec_sign(void *user, const uint8_t *data, size_t len, uint8_t *sig, char *err, size_t errlen)
{
    struct sec_signer *s = user;
    CFDataRef in = CFDataCreate(NULL, data, (CFIndex)len);
    CFErrorRef e = NULL;
    CFDataRef out = SecKeyCreateSignature(s->key, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256, in, &e);
    CFRelease(in);
    if (!out) {
        char msg[256] = "unknown error";
        if (e) {
            CFStringRef d = CFErrorCopyDescription(e);
            if (d) {
                CFStringGetCString(d, msg, sizeof msg, kCFStringEncodingUTF8);
                CFRelease(d);
            }
            CFRelease(e);
        }
        return hc_fail(err, errlen, "SecKeyCreateSignature: %s", msg);
    }
    int rc = 0;
    if ((size_t)CFDataGetLength(out) != s->pub.signature_len)
        rc = hc_fail(err, errlen, "unexpected signature length %ld (an RSA key is required)", (long)CFDataGetLength(out));
    else
        memcpy(sig, CFDataGetBytePtr(out), s->pub.signature_len);
    CFRelease(out);
    return rc;
}

/* Takes ownership of key and cert (one retain each). */
static struct henge_signer *make_signer(SecKeyRef key, CFDataRef cert, char *err, size_t errlen)
{
    struct sec_signer *s = calloc(1, sizeof *s);
    if (!s) {
        CFRelease(key);
        CFRelease(cert);
        hc_fail(err, errlen, "out of memory");
        return NULL;
    }
    s->key = key;
    s->cert = cert;
    s->pub.cert = CFDataGetBytePtr(cert);
    s->pub.cert_len = (size_t)CFDataGetLength(cert);
    s->pub.signature_len = SecKeyGetBlockSize(key);
    s->pub.sign = sec_sign;
    s->pub.user = s;
    return &s->pub;
}

/* identity: borrowed. */
static struct henge_signer *from_identity(SecIdentityRef identity, char *err, size_t errlen)
{
    SecKeyRef key = NULL;
    SecCertificateRef cert = NULL;
    if (SecIdentityCopyPrivateKey(identity, &key) != errSecSuccess || SecIdentityCopyCertificate(identity, &cert) != errSecSuccess) {
        if (key) CFRelease(key);
        if (cert) CFRelease(cert);
        hc_fail(err, errlen, "could not read the identity's key or certificate");
        return NULL;
    }
    CFDataRef der = SecCertificateCopyData(cert);
    CFRelease(cert);
    if (!der) {
        CFRelease(key);
        hc_fail(err, errlen, "could not read the certificate");
        return NULL;
    }
    return make_signer(key, der, err, errlen);
}

struct henge_signer *henge_signer_from_pkcs12(const uint8_t *p12, size_t len, const char *password, char *err, size_t errlen)
{
    CFDataRef data = CFDataCreate(NULL, p12, (CFIndex)len);
    CFStringRef pass = CFStringCreateWithCString(NULL, password ? password : "", kCFStringEncodingUTF8);
    const void *keys[] = {kSecImportExportPassphrase};
    const void *vals[] = {pass};
    CFDictionaryRef opts = CFDictionaryCreate(NULL, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef items = NULL;
    OSStatus st = SecPKCS12Import(data, opts, &items);
    CFRelease(opts);
    CFRelease(pass);
    CFRelease(data);
    if (st != errSecSuccess || !items || CFArrayGetCount(items) < 1) {
        if (items) CFRelease(items);
        hc_fail(err, errlen, st == errSecAuthFailed ? "wrong password for the .p12 file" : "could not import the .p12 file (OSStatus %d)", (int)st);
        return NULL;
    }
    CFDictionaryRef item = CFArrayGetValueAtIndex(items, 0);
    SecIdentityRef identity = (SecIdentityRef)CFDictionaryGetValue(item, kSecImportItemIdentity);
    struct henge_signer *s = identity ? from_identity(identity, err, errlen) : NULL;
    if (!identity)
        hc_fail(err, errlen, "the .p12 file has no identity (certificate and private key)");
    CFRelease(items);
    return s;
}

struct henge_signer *henge_signer_from_keychain(const char *name, char *err, size_t errlen)
{
#if TARGET_OS_OSX
    const void *keys[] = {kSecClass, kSecMatchLimit, kSecReturnRef};
    const void *vals[] = {kSecClassIdentity, kSecMatchLimitAll, kCFBooleanTrue};
    CFDictionaryRef q = CFDictionaryCreate(NULL, keys, vals, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef found = NULL;
    OSStatus st = SecItemCopyMatching(q, (CFTypeRef *)&found);
    CFRelease(q);
    if (st != errSecSuccess || !found) {
        hc_fail(err, errlen, "no identities in the keychain (OSStatus %d)", (int)st);
        return NULL;
    }
    struct henge_signer *s = NULL;
    for (CFIndex i = 0; i < CFArrayGetCount(found) && !s; i++) {
        SecIdentityRef id = (SecIdentityRef)CFArrayGetValueAtIndex(found, i);
        SecCertificateRef cert = NULL;
        if (SecIdentityCopyCertificate(id, &cert) != errSecSuccess)
            continue;
        CFStringRef cn = NULL;
        SecCertificateCopyCommonName(cert, &cn);
        CFRelease(cert);
        char buf[256] = "";
        if (cn) {
            CFStringGetCString(cn, buf, sizeof buf, kCFStringEncodingUTF8);
            CFRelease(cn);
        }
        if (strstr(buf, name))
            s = from_identity(id, err, errlen);
    }
    CFRelease(found);
    if (!s && (!err || !*err))
        hc_fail(err, errlen, "no keychain identity matches \"%s\"", name);
    return s;
#else
    (void)name;
    hc_fail(err, errlen, "keychain identities are only available on macOS");
    return NULL;
#endif
}

void henge_signer_free(struct henge_signer *signer)
{
    if (!signer)
        return;
    struct sec_signer *s = (struct sec_signer *)signer;
    CFRelease(s->key);
    CFRelease(s->cert);
    free(s);
}
