/*
 * Forwarding+logging bcrypt.dll for Wine diagnosis.
 * Loads the real Wine bcrypt (renamed bcryptw.dll), forwards everything, and for
 * the asymmetric-key operations logs the call + its NTSTATUS return, so the one
 * returning STATUS_INVALID_PARAMETER (0xC000000D) can be pinpointed.
 * Behaviour is unchanged (pure pass-through); only logging is added.
 */
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>

static HMODULE real;
static void logln(const char *fmt, ...)
{
    static FILE *f = NULL;
    va_list ap;
    if (!f) { f = fopen("C:\\bcrypt-log.txt", "a"); if (!f) return; }
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fflush(f);
}
#define LOADR() do { if(!real) real = LoadLibraryW(L"bcryptw.dll"); } while(0)
#define P(name) ((void*)GetProcAddress(real, name))

/* function pointer typedefs via the real prototypes */
typedef NTSTATUS (WINAPI *t_ImportKeyPair)(BCRYPT_ALG_HANDLE,BCRYPT_KEY_HANDLE,LPCWSTR,BCRYPT_KEY_HANDLE*,PUCHAR,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_ImportKey)(BCRYPT_ALG_HANDLE,BCRYPT_KEY_HANDLE,LPCWSTR,BCRYPT_KEY_HANDLE*,PUCHAR,ULONG,PUCHAR,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_ExportKey)(BCRYPT_KEY_HANDLE,BCRYPT_KEY_HANDLE,LPCWSTR,PUCHAR,ULONG,ULONG*,ULONG);
typedef NTSTATUS (WINAPI *t_SignHash)(BCRYPT_KEY_HANDLE,void*,PUCHAR,ULONG,PUCHAR,ULONG,ULONG*,ULONG);
typedef NTSTATUS (WINAPI *t_VerifySignature)(BCRYPT_KEY_HANDLE,void*,PUCHAR,ULONG,PUCHAR,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_GenerateKeyPair)(BCRYPT_ALG_HANDLE,BCRYPT_KEY_HANDLE*,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_GenerateSymmetricKey)(BCRYPT_ALG_HANDLE,BCRYPT_KEY_HANDLE*,PUCHAR,ULONG,PUCHAR,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_FinalizeKeyPair)(BCRYPT_KEY_HANDLE,ULONG);
typedef NTSTATUS (WINAPI *t_Decrypt)(BCRYPT_KEY_HANDLE,PUCHAR,ULONG,void*,PUCHAR,ULONG,PUCHAR,ULONG,ULONG*,ULONG);
typedef NTSTATUS (WINAPI *t_Encrypt)(BCRYPT_KEY_HANDLE,PUCHAR,ULONG,void*,PUCHAR,ULONG,PUCHAR,ULONG,ULONG*,ULONG);
typedef NTSTATUS (WINAPI *t_DeriveKey)(BCRYPT_SECRET_HANDLE,LPCWSTR,BCryptBufferDesc*,PUCHAR,ULONG,ULONG*,ULONG);
typedef NTSTATUS (WINAPI *t_DeriveKeyPBKDF2)(BCRYPT_ALG_HANDLE,PUCHAR,ULONG,PUCHAR,ULONG,ULONGLONG,PUCHAR,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_KeyDerivation)(BCRYPT_KEY_HANDLE,BCryptBufferDesc*,PUCHAR,ULONG,ULONG*,ULONG);
typedef NTSTATUS (WINAPI *t_SecretAgreement)(BCRYPT_KEY_HANDLE,BCRYPT_KEY_HANDLE,BCRYPT_SECRET_HANDLE*,ULONG);
typedef NTSTATUS (WINAPI *t_OpenAlg)(BCRYPT_ALG_HANDLE*,LPCWSTR,LPCWSTR,ULONG);
typedef NTSTATUS (WINAPI *t_SetProperty)(BCRYPT_HANDLE,LPCWSTR,PUCHAR,ULONG,ULONG);
typedef NTSTATUS (WINAPI *t_GetProperty)(BCRYPT_HANDLE,LPCWSTR,PUCHAR,ULONG,ULONG*,ULONG);

#define MARK(r) ((r)==0?"":"  <<< NONZERO")

NTSTATUS WINAPI BCryptImportKeyPair(BCRYPT_ALG_HANDLE a,BCRYPT_KEY_HANDLE b,LPCWSTR type,BCRYPT_KEY_HANDLE*k,PUCHAR d,ULONG dl,ULONG f)
{ LOADR(); NTSTATUS r=((t_ImportKeyPair)P("BCryptImportKeyPair"))(a,b,type,k,d,dl,f); logln("BCryptImportKeyPair type=%ls len=%lu -> %#lx%s", type?type:L"(null)", dl, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptImportKey(BCRYPT_ALG_HANDLE a,BCRYPT_KEY_HANDLE b,LPCWSTR type,BCRYPT_KEY_HANDLE*k,PUCHAR o,ULONG ol,PUCHAR d,ULONG dl,ULONG f)
{ LOADR(); NTSTATUS r=((t_ImportKey)P("BCryptImportKey"))(a,b,type,k,o,ol,d,dl,f); logln("BCryptImportKey type=%ls len=%lu -> %#lx%s", type?type:L"(null)", dl, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptExportKey(BCRYPT_KEY_HANDLE a,BCRYPT_KEY_HANDLE b,LPCWSTR type,PUCHAR o,ULONG ol,ULONG*rl,ULONG f)
{ LOADR(); NTSTATUS r=((t_ExportKey)P("BCryptExportKey"))(a,b,type,o,ol,rl,f); logln("BCryptExportKey type=%ls -> %#lx%s", type?type:L"(null)", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptSignHash(BCRYPT_KEY_HANDLE a,void*pad,PUCHAR h,ULONG hl,PUCHAR s,ULONG sl,ULONG*rl,ULONG f)
{ LOADR(); NTSTATUS r=((t_SignHash)P("BCryptSignHash"))(a,pad,h,hl,s,sl,rl,f); logln("BCryptSignHash hlen=%lu flags=%#lx -> %#lx%s", hl, f, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptVerifySignature(BCRYPT_KEY_HANDLE a,void*pad,PUCHAR h,ULONG hl,PUCHAR s,ULONG sl,ULONG f)
{ LOADR(); NTSTATUS r=((t_VerifySignature)P("BCryptVerifySignature"))(a,pad,h,hl,s,sl,f); logln("BCryptVerifySignature hlen=%lu -> %#lx%s", hl, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptGenerateKeyPair(BCRYPT_ALG_HANDLE a,BCRYPT_KEY_HANDLE*k,ULONG len,ULONG f)
{ LOADR(); NTSTATUS r=((t_GenerateKeyPair)P("BCryptGenerateKeyPair"))(a,k,len,f); logln("BCryptGenerateKeyPair bits=%lu -> %#lx%s", len, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptGenerateSymmetricKey(BCRYPT_ALG_HANDLE a,BCRYPT_KEY_HANDLE*k,PUCHAR o,ULONG ol,PUCHAR s,ULONG sl,ULONG f)
{ LOADR(); NTSTATUS r=((t_GenerateSymmetricKey)P("BCryptGenerateSymmetricKey"))(a,k,o,ol,s,sl,f); logln("BCryptGenerateSymmetricKey keylen=%lu -> %#lx%s", sl, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptFinalizeKeyPair(BCRYPT_KEY_HANDLE a,ULONG f)
{ LOADR(); NTSTATUS r=((t_FinalizeKeyPair)P("BCryptFinalizeKeyPair"))(a,f); logln("BCryptFinalizeKeyPair -> %#lx%s", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptDecrypt(BCRYPT_KEY_HANDLE a,PUCHAR in,ULONG il,void*pad,PUCHAR iv,ULONG ivl,PUCHAR o,ULONG ol,ULONG*rl,ULONG f)
{ LOADR(); NTSTATUS r=((t_Decrypt)P("BCryptDecrypt"))(a,in,il,pad,iv,ivl,o,ol,rl,f); logln("BCryptDecrypt inlen=%lu flags=%#lx -> %#lx%s", il, f, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptEncrypt(BCRYPT_KEY_HANDLE a,PUCHAR in,ULONG il,void*pad,PUCHAR iv,ULONG ivl,PUCHAR o,ULONG ol,ULONG*rl,ULONG f)
{ LOADR(); NTSTATUS r=((t_Encrypt)P("BCryptEncrypt"))(a,in,il,pad,iv,ivl,o,ol,rl,f); logln("BCryptEncrypt inlen=%lu flags=%#lx -> %#lx%s", il, f, r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptDeriveKey(BCRYPT_SECRET_HANDLE a,LPCWSTR kdf,BCryptBufferDesc*pl,PUCHAR d,ULONG dl,ULONG*rl,ULONG f)
{ LOADR(); NTSTATUS r=((t_DeriveKey)P("BCryptDeriveKey"))(a,kdf,pl,d,dl,rl,f); logln("BCryptDeriveKey kdf=%ls -> %#lx%s", kdf?kdf:L"(null)", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptDeriveKeyPBKDF2(BCRYPT_ALG_HANDLE a,PUCHAR pw,ULONG pwl,PUCHAR salt,ULONG saltl,ULONGLONG it,PUCHAR d,ULONG dl,ULONG f)
{ LOADR(); NTSTATUS r=((t_DeriveKeyPBKDF2)P("BCryptDeriveKeyPBKDF2"))(a,pw,pwl,salt,saltl,it,d,dl,f); logln("BCryptDeriveKeyPBKDF2 -> %#lx%s", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptKeyDerivation(BCRYPT_KEY_HANDLE a,BCryptBufferDesc*pl,PUCHAR d,ULONG dl,ULONG*rl,ULONG f)
{ LOADR(); NTSTATUS r=((t_KeyDerivation)P("BCryptKeyDerivation"))(a,pl,d,dl,rl,f); logln("BCryptKeyDerivation -> %#lx%s", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptSecretAgreement(BCRYPT_KEY_HANDLE a,BCRYPT_KEY_HANDLE b,BCRYPT_SECRET_HANDLE*s,ULONG f)
{ LOADR(); NTSTATUS r=((t_SecretAgreement)P("BCryptSecretAgreement"))(a,b,s,f); logln("BCryptSecretAgreement -> %#lx%s", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptOpenAlgorithmProvider(BCRYPT_ALG_HANDLE*a,LPCWSTR id,LPCWSTR impl,ULONG f)
{ LOADR(); NTSTATUS r=((t_OpenAlg)P("BCryptOpenAlgorithmProvider"))(a,id,impl,f); logln("BCryptOpenAlgorithmProvider id=%ls -> %#lx%s", id?id:L"(null)", r, MARK(r)); return r; }

NTSTATUS WINAPI BCryptSetProperty(BCRYPT_HANDLE a,LPCWSTR prop,PUCHAR v,ULONG vl,ULONG f)
{ LOADR(); NTSTATUS r=((t_SetProperty)P("BCryptSetProperty"))(a,prop,v,vl,f); logln("BCryptSetProperty prop=%ls -> %#lx%s", prop?prop:L"(null)", r, MARK(r)); return r; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID rv)
{ if (reason==DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); LOADR(); if(!real) return FALSE; } return TRUE; }
