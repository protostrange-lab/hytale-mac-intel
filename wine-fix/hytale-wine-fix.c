/*
 * hytale-wine-fix  -  version.dll proxy that patches three gaps in Wine's CNG
 * stack so Hytale's .NET client can complete its local mTLS handshake and enter
 * singleplayer worlds under Wine on Intel macOS. Loaded into HytaleClient.exe via
 * the game's own version.dll import (forwards the real version.dll to versionw.dll).
 *
 * The client builds an ephemeral RSA identity cert, round-trips it through PKCS#12,
 * then uses the key for the QUIC/mTLS CertificateVerify. Three Wine gaps break this:
 *   1. NCryptGetProperty lacks the computed properties .NET reads on the key
 *      (NCRYPT_PROVIDER_HANDLE_PROPERTY etc.) and returns NTE_INVALID_PARAMETER.
 *   2. PFXImportCertStore yields a CAPI-bridged key whose CngKey.AlgorithmGroup
 *      isn't RSA, so RSACng rejects it.
 *   3. bcrypt/ncrypt cannot export the PKCS8_PRIVATEKEY blob the client needs.
 * We fix all three with the key material the client itself imports. Pure interop;
 * no auth is bypassed - the client still logs in and signs with its own real key.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -shared -o version.dll hytale-wine-fix.c \
 *          minhook/src/{buffer,hook,trampoline}.c minhook/src/hde/hde64.c \
 *          version.def -lbcrypt -municode -Iminhook/src
 */
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minhook/include/MinHook.h"

/* Opt-in logging to C:\hytale-wine-fix.log (set HYTALE_FIX_LOG=1). Off by default. */
static int g_log;
static void L(const char *fmt, ...)
{
    static FILE *f; va_list ap;
    if (!g_log) return;
    if (!f) { f = fopen("C:\\hytale-wine-fix.log", "a"); if (!f) { g_log = 0; return; } }
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fflush(f);
}

#ifndef NTE_BUFFER_TOO_SMALL
#define NTE_BUFFER_TOO_SMALL ((SECURITY_STATUS)0x80090023)
#endif
#ifndef NTE_NOT_FOUND
#define NTE_NOT_FOUND ((SECURITY_STATUS)0x80090011)
#endif
#ifndef MS_KEY_STORAGE_PROVIDER
#define MS_KEY_STORAGE_PROVIDER L"Microsoft Software Key Storage Provider"
#endif
#define CERT_NCRYPT_KEYSPEC    0xFFFFFFFF

typedef SECURITY_STATUS (WINAPI *t_OpenProv)(NCRYPT_PROV_HANDLE*,LPCWSTR,DWORD);
typedef SECURITY_STATUS (WINAPI *t_ImportKey)(NCRYPT_PROV_HANDLE,NCRYPT_KEY_HANDLE,LPCWSTR,NCryptBufferDesc*,NCRYPT_KEY_HANDLE*,PBYTE,DWORD,DWORD);
typedef SECURITY_STATUS (WINAPI *t_GetProp)(NCRYPT_HANDLE,LPCWSTR,PBYTE,DWORD,DWORD*,DWORD);
typedef SECURITY_STATUS (WINAPI *t_ExportKey)(NCRYPT_KEY_HANDLE,NCRYPT_KEY_HANDLE,LPCWSTR,NCryptBufferDesc*,PBYTE,DWORD,DWORD*,DWORD);
typedef BOOL (WINAPI *t_AcquirePk)(PCCERT_CONTEXT,DWORD,void*,HCRYPTPROV_OR_NCRYPT_KEY_HANDLE*,DWORD*,BOOL*);

static t_OpenProv   p_OpenProv;      /* real, resolved once (not hooked) */
static t_ImportKey  o_ImportKey;
static t_GetProp    o_GetProp;
static t_ExportKey  o_ExportKey;
static t_AcquirePk  o_AcquirePk;

/* RSA private blob (BCRYPT_RSAPRIVATE_BLOB) the client imports, cached to rebuild
 * a proper CNG key after the PKCS#12 round-trip. Guarded by g_cs. */
static BYTE *g_rsablob; static DWORD g_rsabloblen;
static NCRYPT_PROV_HANDLE g_prov;        /* one long-lived provider for re-imports */
static CRITICAL_SECTION g_cs; static int g_cs_ok;

/* Wine ncrypt object layout (dlls/ncrypt/ncrypt_internal.h): type@0 (KEY=0),
 * union key@16 { algid@16 }. algid: AES=0,RSA=1,DSA=2,ECDSA=3. */
static const WCHAR *key_algo(NCRYPT_HANDLE h)
{
    if (*(unsigned int *)(ULONG_PTR)h != 0) return NULL;        /* not a KEY */
    switch (*(int *)((char *)(ULONG_PTR)h + 16)) {
        case 1: return L"RSA"; case 2: return L"DSA"; case 3: return L"ECDSA"; default: return NULL;
    }
}
static SECURITY_STATUS ret_wstr(const WCHAR *v, PBYTE o, DWORD ol, DWORD *rl)
{
    DWORD need = (wcslen(v)+1)*sizeof(WCHAR);
    if (rl) *rl = need;
    if (!o) return ERROR_SUCCESS;
    if (ol < need) return NTE_BUFFER_TOO_SMALL;
    memcpy(o, v, need); return ERROR_SUCCESS;
}

/* ---- Fix 1: computed properties Wine's NCryptGetProperty lacks ---- */
static SECURITY_STATUS WINAPI h_GetProp(NCRYPT_HANDLE o,LPCWSTR p,PBYTE v,DWORD vl,DWORD*rl,DWORD f)
{
    SECURITY_STATUS r = o_GetProp(o,p,v,vl,rl,f);
    if (r==NTE_INVALID_PARAMETER && p) {
        if (!wcscmp(p,L"Provider Handle")) {
            if (rl) *rl = sizeof(NCRYPT_PROV_HANDLE);
            if (!v) return ERROR_SUCCESS;
            if (vl < sizeof(NCRYPT_PROV_HANDLE)) return NTE_BUFFER_TOO_SMALL;
            NCRYPT_PROV_HANDLE prov=0;
            if (p_OpenProv && p_OpenProv(&prov, MS_KEY_STORAGE_PROVIDER, 0)==ERROR_SUCCESS) {
                memcpy(v,&prov,sizeof(prov)); return ERROR_SUCCESS;
            }
            return r;
        }
        if (!wcscmp(p,L"Algorithm Group") || !wcscmp(p,NCRYPT_ALGORITHM_PROPERTY)) {
            const WCHAR *a = key_algo(o);
            if (a) return ret_wstr(a,v,vl,rl);
            return NTE_NOT_FOUND;
        }
        return NTE_NOT_FOUND;   /* optional property absent, not an error */
    }
    return r;
}

/* ---- cache the imported RSA private blob ---- */
static SECURITY_STATUS WINAPI h_ImportKey(NCRYPT_PROV_HANDLE pr,NCRYPT_KEY_HANDLE i,LPCWSTR t,NCryptBufferDesc*pl,NCRYPT_KEY_HANDLE*k,PBYTE d,DWORD dl,DWORD f)
{
    SECURITY_STATUS r = o_ImportKey(pr,i,t,pl,k,d,dl,f);
    if (r==ERROR_SUCCESS && t && !wcscmp(t,L"RSAPRIVATEBLOB") && d && dl) {
        BYTE *nb = (BYTE*)malloc(dl);
        if (nb) {
            memcpy(nb,d,dl);
            if (g_cs_ok) EnterCriticalSection(&g_cs);
            free(g_rsablob); g_rsablob=nb; g_rsabloblen=dl;
            if (g_cs_ok) LeaveCriticalSection(&g_cs);
            L("cached RSA blob %lu", dl);
        }
    }
    return r;
}

/* ---- Fix 2: substitute a proper CNG RSA key when PFX import gave a bridged one ---- */
static BOOL WINAPI h_AcquirePk(PCCERT_CONTEXT c,DWORD f,void*p,HCRYPTPROV_OR_NCRYPT_KEY_HANDLE*h,DWORD*s,BOOL*caller)
{
    BOOL r = o_AcquirePk(c,f,p,h,s,caller);
    if (!(r && h && *h) || !g_prov) return r;
    /* take a private copy of the cached blob under lock (pointer+len stay consistent) */
    BYTE *blob=NULL; DWORD bloblen=0;
    if (g_cs_ok) EnterCriticalSection(&g_cs);
    if (g_rsablob && g_rsabloblen && (blob=(BYTE*)malloc(g_rsabloblen))) { memcpy(blob,g_rsablob,g_rsabloblen); bloblen=g_rsabloblen; }
    if (g_cs_ok) LeaveCriticalSection(&g_cs);
    if (!blob) return r;
    /* Decide if the acquired key is usable as a CNG RSA key. Only a genuine NCRYPT
     * handle (keyspec==CERT_NCRYPT_KEYSPEC) may be read as a Wine ncrypt object;
     * a CAPI-bridged handle is not one, so never dereference it - just substitute. */
    int needsub = 1;
    if (s && *s==CERT_NCRYPT_KEYSPEC) {
        unsigned int ty = *(unsigned int*)(ULONG_PTR)*h;
        WCHAR grp[32]={0}; DWORD gl=0;
        SECURITY_STATUS gr = o_GetProp((NCRYPT_HANDLE)*h, L"Algorithm Group",(PBYTE)grp,sizeof(grp),&gl,0);
        needsub = (ty!=0 || gr!=ERROR_SUCCESS);
    }
    if (needsub) {
        NCRYPT_KEY_HANDLE k2=0;
        if (o_ImportKey(g_prov,0,L"RSAPRIVATEBLOB",NULL,&k2,blob,bloblen,0)==ERROR_SUCCESS && k2) {
            *h = (HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)k2;   /* original handle intentionally left to the cert store */
            if (s) *s = CERT_NCRYPT_KEYSPEC;
            if (caller) *caller = TRUE;                 /* .NET frees k2 (NCryptFreeObject) */
            L("substituted acquired key -> re-imported CNG RSA key");
        }
    }
    free(blob);
    return r;
}

/* ---- Fix 3: build PKCS#8 PrivateKeyInfo DER from a RSAFULLPRIVATEBLOB ---- */
static void der_len(BYTE **p, size_t n){
    if (n<0x80) *(*p)++=(BYTE)n;
    else if (n<=0xFF){ *(*p)++=0x81; *(*p)++=(BYTE)n; }
    else { *(*p)++=0x82; *(*p)++=(BYTE)(n>>8); *(*p)++=(BYTE)n; }
}
static void der_int(BYTE **p, const BYTE *d, size_t n){
    while (n>1 && d[0]==0){ d++; n--; }
    int pad = (n>0 && (d[0]&0x80))?1:0;
    *(*p)++=0x02; der_len(p,n+pad); if(pad)*(*p)++=0x00; memcpy(*p,d,n); *p+=n;
}
static BYTE *rsa_blob_to_pkcs8(const BYTE *blob, DWORD blen, DWORD *outlen)
{
    if (blen<24) return NULL;
    const ULONG *H=(const ULONG*)blob; DWORD cbE=H[2],cbN=H[3],cbP=H[4],cbQ=H[5];
    const BYTE *q=blob+24;
    const BYTE *E=q;q+=cbE; const BYTE *N=q;q+=cbN; const BYTE *P=q;q+=cbP; const BYTE *Q=q;q+=cbQ;
    const BYTE *DP=q;q+=cbP; const BYTE *DQ=q;q+=cbQ; const BYTE *QI=q;q+=cbP; const BYTE *D=q;q+=cbN;
    if ((DWORD)(q-blob)>blen) return NULL;
    /* guard the fixed buffers below (body is largest): reject keys too big to fit.
     * body holds 2*cbN+3*cbP+2*cbQ+cbE plus DER overhead; 5000 covers up to ~RSA-8700. */
    if ((size_t)cbN*2 + (size_t)cbP*3 + (size_t)cbQ*2 + (size_t)cbE + 64 > 5000) return NULL;
    BYTE z=0, body[5000],*bp=body;
    der_int(&bp,&z,1); der_int(&bp,N,cbN); der_int(&bp,E,cbE); der_int(&bp,D,cbN);
    der_int(&bp,P,cbP); der_int(&bp,Q,cbQ); der_int(&bp,DP,cbP); der_int(&bp,DQ,cbQ); der_int(&bp,QI,cbP);
    size_t bl=bp-body;
    BYTE pk1[5100],*p1=pk1; *p1++=0x30; der_len(&p1,bl); memcpy(p1,body,bl); p1+=bl; size_t pk1l=p1-pk1;
    static const BYTE alg[]={0x30,0x0d,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01,0x05,0x00};
    BYTE in[5300],*ip=in; der_int(&ip,&z,1); memcpy(ip,alg,sizeof(alg)); ip+=sizeof(alg);
    *ip++=0x04; der_len(&ip,pk1l); memcpy(ip,pk1,pk1l); ip+=pk1l; size_t il=ip-in;
    BYTE *out=(BYTE*)malloc(il+8); if(!out) return NULL;
    BYTE *op=out; *op++=0x30; der_len(&op,il); memcpy(op,in,il); op+=il;
    *outlen=(DWORD)(op-out); return out;
}
static SECURITY_STATUS WINAPI h_ExportKey(NCRYPT_KEY_HANDLE k,NCRYPT_KEY_HANDLE e,LPCWSTR t,NCryptBufferDesc*pl,PBYTE o,DWORD ol,DWORD*rl,DWORD f)
{
    SECURITY_STATUS r = o_ExportKey(k,e,t,pl,o,ol,rl,f);
    if (r!=ERROR_SUCCESS && t && !wcscmp(t,L"PKCS8_PRIVATEKEY")) {
        DWORD bl=0;
        if (o_ExportKey(k,e,BCRYPT_RSAFULLPRIVATE_BLOB,pl,NULL,0,&bl,f)==ERROR_SUCCESS && bl) {
            BYTE *blob=(BYTE*)malloc(bl);
            if (blob && o_ExportKey(k,e,BCRYPT_RSAFULLPRIVATE_BLOB,pl,blob,bl,&bl,f)==ERROR_SUCCESS) {
                DWORD pl8=0; BYTE *p8=rsa_blob_to_pkcs8(blob,bl,&pl8);
                if (p8) {
                    if (rl) *rl=pl8;
                    if (!o){ free(p8); free(blob); return ERROR_SUCCESS; }
                    if (ol<pl8){ free(p8); free(blob); return NTE_BUFFER_TOO_SMALL; }
                    memcpy(o,p8,pl8); free(p8); free(blob); L("built PKCS8 %lu", pl8); return ERROR_SUCCESS;
                }
            }
            free(blob);
        }
    }
    return r;
}

static DWORD WINAPI installer(LPVOID unused)
{
    HMODULE nc=NULL, c32=NULL;
    for (int i=0;i<600 && !(nc&&c32);i++){ nc=GetModuleHandleW(L"ncrypt.dll"); c32=GetModuleHandleW(L"crypt32.dll");
        if(!(nc&&c32)){ if(!nc)nc=LoadLibraryW(L"ncrypt.dll"); if(!c32)c32=LoadLibraryW(L"crypt32.dll"); Sleep(30);} }
    if (!nc || !c32) { L("crypto dlls not found"); return 0; }
    p_OpenProv = (t_OpenProv)GetProcAddress(nc,"NCryptOpenStorageProvider");
    if (p_OpenProv) p_OpenProv(&g_prov, MS_KEY_STORAGE_PROVIDER, 0);   /* one provider for re-imports */
    if (MH_Initialize()!=MH_OK){ L("MH_Initialize failed"); return 0; }
    int ok = 1;
    ok &= MH_CreateHook(GetProcAddress(nc,"NCryptGetProperty"),(void*)h_GetProp,(void**)&o_GetProp)==MH_OK;
    ok &= MH_CreateHook(GetProcAddress(nc,"NCryptImportKey"),(void*)h_ImportKey,(void**)&o_ImportKey)==MH_OK;
    ok &= MH_CreateHook(GetProcAddress(nc,"NCryptExportKey"),(void*)h_ExportKey,(void**)&o_ExportKey)==MH_OK;
    ok &= MH_CreateHook(GetProcAddress(c32,"CryptAcquireCertificatePrivateKey"),(void*)h_AcquirePk,(void**)&o_AcquirePk)==MH_OK;
    /* fail-closed: either all hooks install and enable, or none run (no half-hooked state
     * where an o_* trampoline is NULL and a hook calls through it). */
    if (!ok) { L("hook creation failed - not enabling"); MH_Uninitialize(); return 0; }
    if (MH_EnableHook(MH_ALL_HOOKS)!=MH_OK) { L("MH_EnableHook failed"); MH_Uninitialize(); return 0; }
    L("hytale-wine-fix installed");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID rv)
{
    if (reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        { char b[8]; g_log = GetEnvironmentVariableA("HYTALE_FIX_LOG", b, sizeof(b)) > 0; }
        InitializeCriticalSection(&g_cs); g_cs_ok = 1;
        CreateThread(NULL,0,installer,NULL,0,NULL);
    }
    return TRUE;
}
