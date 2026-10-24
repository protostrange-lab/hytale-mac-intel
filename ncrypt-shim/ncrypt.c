/*
 * Drop-in ncrypt.dll for Wine that adds ECC key import/generation by forwarding
 * to bcrypt, so .NET's ECDSA self-signed cert private key handling works under Wine.
 *
 * Faithful port of Wine's dlls/ncrypt/main.c object model (Santino Mazza, LGPL 2.1),
 * built with mingw-w64 against standard bcrypt.dll. The only behavioural changes:
 *   1. NCryptImportKey / NCryptCreatePersistedKey handle ECDSA/ECDH, not just RSA.
 *   2. NCRYPT_EXPORT_POLICY_PROPERTY defaults to ALLOW_EXPORT|ALLOW_PLAINTEXT_EXPORT.
 * Everything else mirrors Wine. BCrypt* exports are forwarded to bcrypt.dll via the .def.
 */
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- debug: write to a file so we can see what the shim does ---- */
static void shimlog(const char *fmt, ...)
{
    static FILE *f = NULL;
    va_list ap;
    if (!f) { f = fopen("C:\\ncrypt-shim.log", "a"); if (!f) return; }
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fflush(f);
}

/* ---- Wine object model (ncrypt_internal.h) ---- */
enum algid { ALG_AES, ALG_RSA, ALG_DSA, ALG_ECDSA, ALG_ECDH };

struct key
{
    enum algid algid;
    BCRYPT_KEY_HANDLE bcrypt_key;
    BCRYPT_ALG_HANDLE bcrypt_alg;   /* opened provider for ECC; NULL for pseudo-handle */
};
struct storage_provider { int dummy; };
enum object_type { OBJ_KEY, OBJ_STORAGE_PROVIDER };
struct object_property { WCHAR *key; DWORD value_size; void *value; };
struct object
{
    enum object_type type;
    DWORD num_properties;
    struct object_property *properties;
    union { struct key key; struct storage_provider storage_provider; } u;
};

/* NTSTATUS codes (may be absent depending on headers) */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS            ((NTSTATUS)0x00000000)
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE     ((NTSTATUS)0xC0000008)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER  ((NTSTATUS)0xC000000D)
#endif
#ifndef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY          ((NTSTATUS)0xC0000017)
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED      ((NTSTATUS)0xC00000BB)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL   ((NTSTATUS)0xC0000023)
#endif
#ifndef STATUS_INVALID_SIGNATURE
#define STATUS_INVALID_SIGNATURE  ((NTSTATUS)0xC000A000)
#endif
#ifndef BCRYPT_ECDSA_ALGORITHM
#define BCRYPT_ECDSA_ALGORITHM    L"ECDSA"
#endif
#ifndef BCRYPT_ECDH_ALGORITHM
#define BCRYPT_ECDH_ALGORITHM     L"ECDH"
#endif

/* flag/value constants (may be absent in some ncrypt.h) */
#ifndef NCRYPT_ALLOW_EXPORT_FLAG
#define NCRYPT_ALLOW_EXPORT_FLAG            0x00000001
#endif
#ifndef NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG
#define NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG  0x00000002
#endif
#ifndef NCRYPT_ALLOW_ALL_USAGES
#define NCRYPT_ALLOW_ALL_USAGES  0x00ffffff
#endif
#ifndef NCRYPT_SILENT_FLAG
#define NCRYPT_SILENT_FLAG 0x00000040
#endif

/* ECC blob magics (bcrypt.h) */
#define M_ECDSA_PUB_P256  0x31534345
#define M_ECDSA_PRV_P256  0x32534345
#define M_ECDSA_PUB_P384  0x33534345
#define M_ECDSA_PRV_P384  0x34534345
#define M_ECDSA_PUB_P521  0x35534345
#define M_ECDSA_PRV_P521  0x36534345
#define M_ECDH_PUB_P256   0x314B4345
#define M_ECDH_PRV_P256   0x324B4345
#define M_ECDH_PUB_P384   0x334B4345
#define M_ECDH_PRV_P384   0x344B4345
#define M_ECDH_PUB_P521   0x354B4345
#define M_ECDH_PRV_P521   0x364B4345
#define M_ECDSA_PUB_GEN   0x50444345   /* ECDP */
#define M_ECDSA_PRV_GEN   0x56444345   /* ECDV */
#define M_ECDH_PUB_GEN    0x504B4345   /* ECKP */
#define M_ECDH_PRV_GEN    0x564B4345   /* ECKV */

static SECURITY_STATUS map_ntstatus(NTSTATUS status)
{
    switch (status)
    {
    case STATUS_INVALID_HANDLE:    return NTE_INVALID_HANDLE;
    case STATUS_INVALID_SIGNATURE: return NTE_BAD_SIGNATURE;
    case STATUS_SUCCESS:           return ERROR_SUCCESS;
    case STATUS_INVALID_PARAMETER: return NTE_INVALID_PARAMETER;
    case STATUS_NO_MEMORY:         return NTE_NO_MEMORY;
    case STATUS_NOT_SUPPORTED:     return NTE_NOT_SUPPORTED;
    case STATUS_BUFFER_TOO_SMALL:  return NTE_BUFFER_TOO_SMALL;
    default:                       return NTE_INTERNAL_ERROR;
    }
}

static struct object *allocate_object(enum object_type type)
{
    struct object *ret = calloc(1, sizeof(*ret));
    if (ret) ret->type = type;
    return ret;
}
static struct object_property *get_object_property(struct object *o, const WCHAR *name)
{
    DWORD i;
    for (i = 0; i < o->num_properties; i++)
        if (!lstrcmpW(o->properties[i].key, name)) return &o->properties[i];
    return NULL;
}
static struct object_property *add_object_property(struct object *o, const WCHAR *name)
{
    struct object_property *tmp;
    tmp = realloc(o->properties, sizeof(*tmp) * (o->num_properties + 1));
    if (!tmp) return NULL;
    o->properties = tmp;
    struct object_property *p = &o->properties[o->num_properties++];
    memset(p, 0, sizeof(*p));
    p->key = _wcsdup(name);
    if (!p->key) return NULL;
    return p;
}
static SECURITY_STATUS set_object_property(struct object *o, const WCHAR *name, const BYTE *value, DWORD sz)
{
    struct object_property *p = get_object_property(o, name);
    void *tmp;
    if (!p && !(p = add_object_property(o, name))) return NTE_NO_MEMORY;
    p->value_size = sz;
    if (!(tmp = realloc(p->value, sz))) return NTE_NO_MEMORY;
    p->value = tmp;
    memcpy(p->value, value, sz);
    return ERROR_SUCCESS;
}

static void seed_common_props(struct object *o, DWORD export_policy, NCRYPT_PROV_HANDLE provider)
{
    DWORD v;
    v = export_policy;              set_object_property(o, NCRYPT_EXPORT_POLICY_PROPERTY, (BYTE*)&v, sizeof(v));
    v = NCRYPT_ALLOW_ALL_USAGES;    set_object_property(o, NCRYPT_KEY_USAGE_PROPERTY,     (BYTE*)&v, sizeof(v));
    v = 0;                          set_object_property(o, NCRYPT_KEY_TYPE_PROPERTY,      (BYTE*)&v, sizeof(v));
    set_object_property(o, NCRYPT_PROVIDER_HANDLE_PROPERTY, (BYTE*)&provider, sizeof(provider));
}

/* map an ECC blob magic to (algorithm string, ecdh?, bit length) */
static const WCHAR *ecc_alg_for_magic(DWORD magic, enum algid *algid, DWORD *bits)
{
    switch (magic)
    {
    case M_ECDSA_PUB_P256: case M_ECDSA_PRV_P256: *algid=ALG_ECDSA; *bits=256; return BCRYPT_ECDSA_P256_ALGORITHM;
    case M_ECDSA_PUB_P384: case M_ECDSA_PRV_P384: *algid=ALG_ECDSA; *bits=384; return BCRYPT_ECDSA_P384_ALGORITHM;
    case M_ECDSA_PUB_P521: case M_ECDSA_PRV_P521: *algid=ALG_ECDSA; *bits=521; return BCRYPT_ECDSA_P521_ALGORITHM;
    case M_ECDH_PUB_P256:  case M_ECDH_PRV_P256:  *algid=ALG_ECDH;  *bits=256; return BCRYPT_ECDH_P256_ALGORITHM;
    case M_ECDH_PUB_P384:  case M_ECDH_PRV_P384:  *algid=ALG_ECDH;  *bits=384; return BCRYPT_ECDH_P384_ALGORITHM;
    case M_ECDH_PUB_P521:  case M_ECDH_PRV_P521:  *algid=ALG_ECDH;  *bits=521; return BCRYPT_ECDH_P521_ALGORITHM;
    case M_ECDSA_PUB_GEN:  case M_ECDSA_PRV_GEN:  *algid=ALG_ECDSA; *bits=0;   return BCRYPT_ECDSA_ALGORITHM;
    case M_ECDH_PUB_GEN:   case M_ECDH_PRV_GEN:   *algid=ALG_ECDH;  *bits=0;   return BCRYPT_ECDH_ALGORITHM;
    default: return NULL;
    }
}

/* ---- storage provider ---- */
SECURITY_STATUS WINAPI NCryptOpenStorageProvider(NCRYPT_PROV_HANDLE *provider, const WCHAR *name, DWORD flags)
{
    struct object *o = allocate_object(OBJ_STORAGE_PROVIDER);
    if (!o) return NTE_NO_MEMORY;
    *provider = (NCRYPT_PROV_HANDLE)o;
    return ERROR_SUCCESS;
}

/* ---- import (RSA kept as in Wine; ECC added) ---- */
SECURITY_STATUS WINAPI NCryptImportKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE decrypt_key,
                                       const WCHAR *type, NCryptBufferDesc *params, NCRYPT_KEY_HANDLE *handle,
                                       BYTE *data, DWORD datasize, DWORD flags)
{
    BCRYPT_KEY_BLOB *header = (BCRYPT_KEY_BLOB *)data;
    struct object *object;

    shimlog("NCryptImportKey type=%ls magic=%#lx size=%lu params=%p", type?type:L"(null)",
            header?header->Magic:0, datasize, (void*)params);

    if (decrypt_key) return NTE_NOT_SUPPORTED;
    if (flags == NCRYPT_SILENT_FLAG) { /* ok */ }
    else if (flags) return NTE_BAD_FLAGS;
    if (!data) return NTE_INVALID_PARAMETER;

    switch (header->Magic)
    {
    case BCRYPT_RSAFULLPRIVATE_MAGIC:
    case BCRYPT_RSAPRIVATE_MAGIC:
    case BCRYPT_RSAPUBLIC_MAGIC:
    {
        NTSTATUS status;
        BCRYPT_RSAKEY_BLOB *rsablob = (BCRYPT_RSAKEY_BLOB *)data;
        if (params) return NTE_NOT_SUPPORTED;
        if (!(object = allocate_object(OBJ_KEY))) return NTE_NO_MEMORY;
        object->u.key.algid = ALG_RSA;
        set_object_property(object, NCRYPT_ALGORITHM_PROPERTY, (BYTE*)BCRYPT_RSA_ALGORITHM, sizeof(BCRYPT_RSA_ALGORITHM));
        set_object_property(object, NCRYPT_ALGORITHM_GROUP_PROPERTY, (BYTE*)BCRYPT_RSA_ALGORITHM, sizeof(BCRYPT_RSA_ALGORITHM));
        status = BCryptImportKeyPair(BCRYPT_RSA_ALG_HANDLE, NULL, type, &object->u.key.bcrypt_key, data, datasize, 0);
        if (status != STATUS_SUCCESS) { free(object); return map_ntstatus(status); }
        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE*)&rsablob->BitLength, sizeof(rsablob->BitLength));
        seed_common_props(object, NCRYPT_ALLOW_EXPORT_FLAG|NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG, provider);
        break;
    }
    default:
    {
        /* --- THE FIX: ECC import by forwarding to bcrypt --- */
        enum algid algid; DWORD bits = 0;
        const WCHAR *alg = ecc_alg_for_magic(header->Magic, &algid, &bits);
        BCRYPT_ALG_HANDLE halg = NULL;
        NTSTATUS status;
        BCRYPT_ECCKEY_BLOB *ecc = (BCRYPT_ECCKEY_BLOB *)data;

        if (!alg) { shimlog("  unhandled magic %#lx", header->Magic); return NTE_INVALID_PARAMETER; }
        /* .NET's named-curve import passes a curve-name buffer in params; the blob already
         * encodes the curve, so bcrypt can import it directly and we may ignore params. */
        status = BCryptOpenAlgorithmProvider(&halg, alg, NULL, 0);
        if (status != STATUS_SUCCESS) { shimlog("  open alg %ls failed %#lx", alg, status); return map_ntstatus(status); }
        if (!(object = allocate_object(OBJ_KEY))) { BCryptCloseAlgorithmProvider(halg,0); return NTE_NO_MEMORY; }
        object->u.key.algid = algid;
        object->u.key.bcrypt_alg = halg;
        status = BCryptImportKeyPair(halg, NULL, type, &object->u.key.bcrypt_key, data, datasize, 0);
        if (status != STATUS_SUCCESS) { shimlog("  import ecc failed %#lx", status); BCryptCloseAlgorithmProvider(halg,0); free(object); return map_ntstatus(status); }
        if (!bits) bits = ecc->cbKey * 8;   /* generic blob: derive from cbKey */
        set_object_property(object, NCRYPT_ALGORITHM_PROPERTY, (BYTE*)alg, (lstrlenW(alg)+1)*sizeof(WCHAR));
        set_object_property(object, NCRYPT_ALGORITHM_GROUP_PROPERTY,
                            (BYTE*)(algid==ALG_ECDH?NCRYPT_ECDH_ALGORITHM_GROUP:NCRYPT_ECDSA_ALGORITHM_GROUP),
                            (lstrlenW(algid==ALG_ECDH?NCRYPT_ECDH_ALGORITHM_GROUP:NCRYPT_ECDSA_ALGORITHM_GROUP)+1)*sizeof(WCHAR));
        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE*)&bits, sizeof(bits));
        seed_common_props(object, NCRYPT_ALLOW_EXPORT_FLAG|NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG, provider);
        shimlog("  ECC import OK alg=%ls bits=%lu", alg, bits);
        break;
    }
    }
    *handle = (NCRYPT_KEY_HANDLE)object;
    return ERROR_SUCCESS;
}

/* ---- create persisted key (RSA like Wine; ECDSA/ECDH added) ---- */
SECURITY_STATUS WINAPI NCryptCreatePersistedKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE *key,
                                                const WCHAR *algid, const WCHAR *name, DWORD keyspec, DWORD flags)
{
    struct object *object;
    NTSTATUS status;
    DWORD bits;

    shimlog("NCryptCreatePersistedKey alg=%ls", algid?algid:L"(null)");
    if (!provider) return NTE_INVALID_HANDLE;
    if (!algid) return HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER);

    if (!lstrcmpiW(algid, BCRYPT_RSA_ALGORITHM))
    {
        bits = 1024;
        if (!(object = allocate_object(OBJ_KEY))) return NTE_NO_MEMORY;
        object->u.key.algid = ALG_RSA;
        set_object_property(object, NCRYPT_ALGORITHM_PROPERTY, (BYTE*)BCRYPT_RSA_ALGORITHM, sizeof(BCRYPT_RSA_ALGORITHM));
        set_object_property(object, NCRYPT_ALGORITHM_GROUP_PROPERTY, (BYTE*)BCRYPT_RSA_ALGORITHM, sizeof(BCRYPT_RSA_ALGORITHM));
        status = BCryptGenerateKeyPair(BCRYPT_RSA_ALG_HANDLE, &object->u.key.bcrypt_key, bits, 0);
        if (status != STATUS_SUCCESS) { free(object); return map_ntstatus(status); }
        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE*)&bits, sizeof(bits));
        seed_common_props(object, NCRYPT_ALLOW_EXPORT_FLAG|NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG, provider);
    }
    else
    {
        /* ECDSA_Pxxx / ECDH_Pxxx / ECDSA / ECDH */
        enum algid a = ALG_ECDSA;
        BCRYPT_ALG_HANDLE halg = NULL;
        if (!lstrcmpiW(algid, BCRYPT_ECDSA_P256_ALGORITHM)) bits=256, a=ALG_ECDSA;
        else if (!lstrcmpiW(algid, BCRYPT_ECDSA_P384_ALGORITHM)) bits=384, a=ALG_ECDSA;
        else if (!lstrcmpiW(algid, BCRYPT_ECDSA_P521_ALGORITHM)) bits=521, a=ALG_ECDSA;
        else if (!lstrcmpiW(algid, BCRYPT_ECDH_P256_ALGORITHM)) bits=256, a=ALG_ECDH;
        else if (!lstrcmpiW(algid, BCRYPT_ECDH_P384_ALGORITHM)) bits=384, a=ALG_ECDH;
        else if (!lstrcmpiW(algid, BCRYPT_ECDH_P521_ALGORITHM)) bits=521, a=ALG_ECDH;
        else { shimlog("  alg not handled %ls", algid); return NTE_NOT_SUPPORTED; }
        status = BCryptOpenAlgorithmProvider(&halg, algid, NULL, 0);
        if (status != STATUS_SUCCESS) return map_ntstatus(status);
        if (!(object = allocate_object(OBJ_KEY))) { BCryptCloseAlgorithmProvider(halg,0); return NTE_NO_MEMORY; }
        object->u.key.algid = a;
        object->u.key.bcrypt_alg = halg;
        set_object_property(object, NCRYPT_ALGORITHM_PROPERTY, (BYTE*)algid, (lstrlenW(algid)+1)*sizeof(WCHAR));
        set_object_property(object, NCRYPT_ALGORITHM_GROUP_PROPERTY,
                            (BYTE*)(a==ALG_ECDH?NCRYPT_ECDH_ALGORITHM_GROUP:NCRYPT_ECDSA_ALGORITHM_GROUP),
                            (lstrlenW(a==ALG_ECDH?NCRYPT_ECDH_ALGORITHM_GROUP:NCRYPT_ECDSA_ALGORITHM_GROUP)+1)*sizeof(WCHAR));
        status = BCryptGenerateKeyPair(halg, &object->u.key.bcrypt_key, bits, 0);
        if (status != STATUS_SUCCESS) { BCryptCloseAlgorithmProvider(halg,0); free(object); return map_ntstatus(status); }
        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE*)&bits, sizeof(bits));
        seed_common_props(object, NCRYPT_ALLOW_EXPORT_FLAG|NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG, provider);
    }
    *key = (NCRYPT_KEY_HANDLE)object;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptFinalizeKey(NCRYPT_KEY_HANDLE handle, DWORD flags)
{
    struct object *object = (struct object *)handle;
    struct object_property *prop;
    NTSTATUS status;
    DWORD key_length;

    if (!object || object->type != OBJ_KEY) return NTE_INVALID_HANDLE;
    if (!(prop = get_object_property(object, NCRYPT_LENGTH_PROPERTY))) return NTE_INVALID_HANDLE;
    key_length = *(DWORD *)prop->value;
    /* RSA needs the key length set before finalize; ECC keys from BCryptGenerateKeyPair
     * already carry their curve, so a failure here is non-fatal for ECC. */
    if (object->u.key.algid == ALG_RSA)
        BCryptSetProperty(object->u.key.bcrypt_key, BCRYPT_KEY_LENGTH, (UCHAR*)&key_length, sizeof(key_length), 0);
    status = BCryptFinalizeKeyPair(object->u.key.bcrypt_key, 0);
    if (status != STATUS_SUCCESS) return map_ntstatus(status);
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptExportKey(NCRYPT_KEY_HANDLE key, NCRYPT_KEY_HANDLE encrypt_key, const WCHAR *type,
                                       NCryptBufferDesc *params, BYTE *output, DWORD output_len, DWORD *ret_len,
                                       DWORD flags)
{
    struct object *object = (struct object *)key;
    shimlog("NCryptExportKey type=%ls", type?type:L"(null)");
    if (!object || object->type != OBJ_KEY) return NTE_INVALID_HANDLE;
    if (encrypt_key) return NTE_NOT_SUPPORTED;
    if (params) return NTE_NOT_SUPPORTED;
    return map_ntstatus(BCryptExportKey(object->u.key.bcrypt_key, NULL, type, output, output_len, ret_len, 0));
}

SECURITY_STATUS WINAPI NCryptGetProperty(NCRYPT_HANDLE handle, const WCHAR *name, BYTE *output,
                                         DWORD outsize, DWORD *result, DWORD flags)
{
    struct object *object = (struct object *)handle;
    const struct object_property *property;
    if (!object) return NTE_INVALID_HANDLE;
    if (!(property = get_object_property(object, name))) return NTE_INVALID_PARAMETER;
    *result = property->value_size;
    if (!output) return ERROR_SUCCESS;
    if (outsize < property->value_size) return NTE_BUFFER_TOO_SMALL;
    memcpy(output, property->value, property->value_size);
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptSetProperty(NCRYPT_HANDLE handle, const WCHAR *name, BYTE *input, DWORD insize, DWORD flags)
{
    struct object *object = (struct object *)handle;
    if (!object) return NTE_INVALID_HANDLE;
    return set_object_property(object, name, input, insize);
}

SECURITY_STATUS WINAPI NCryptFreeObject(NCRYPT_HANDLE handle)
{
    struct object *object = (struct object *)handle;
    DWORD i;
    if (!object) return NTE_INVALID_HANDLE;
    if (object->type == OBJ_KEY)
    {
        if (object->u.key.bcrypt_key) BCryptDestroyKey(object->u.key.bcrypt_key);
        if (object->u.key.bcrypt_alg) BCryptCloseAlgorithmProvider(object->u.key.bcrypt_alg, 0);
    }
    else if (object->type != OBJ_STORAGE_PROVIDER) return NTE_INVALID_HANDLE;
    for (i = 0; i < object->num_properties; i++) { free(object->properties[i].key); free(object->properties[i].value); }
    free(object->properties);
    free(object);
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptFreeBuffer(PVOID buf) { free(buf); return ERROR_SUCCESS; }

SECURITY_STATUS WINAPI NCryptOpenKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE *key,
                                     const WCHAR *name, DWORD keyspec, DWORD flags)
{ return NTE_NOT_SUPPORTED; }

SECURITY_STATUS WINAPI NCryptIsAlgSupported(NCRYPT_PROV_HANDLE provider, const WCHAR *algid, DWORD flags)
{
    if (!provider) return NTE_INVALID_HANDLE;
    if (!algid) return HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER);
    if (!lstrcmpiW(algid, BCRYPT_RSA_ALGORITHM)) return ERROR_SUCCESS;
    if (!lstrcmpiW(algid, BCRYPT_ECDSA_P256_ALGORITHM)) return ERROR_SUCCESS;
    if (!lstrcmpiW(algid, BCRYPT_ECDSA_P384_ALGORITHM)) return ERROR_SUCCESS;
    if (!lstrcmpiW(algid, BCRYPT_ECDSA_P521_ALGORITHM)) return ERROR_SUCCESS;
    return NTE_NOT_SUPPORTED;
}

/* NCryptDeleteKey just frees the object (we have no persisted store) */
SECURITY_STATUS WINAPI NCryptDeleteKey(NCRYPT_KEY_HANDLE key, DWORD flags) { return NCryptFreeObject((NCRYPT_HANDLE)key); }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r){ if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h); return TRUE; }
