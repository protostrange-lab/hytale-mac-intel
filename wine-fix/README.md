# wine-fix - łata kryptografii Wine (CNG) dla klienta Hytale

Wtyczka, która pozwala klientowi Hytale (.NET 10 NativeAOT) dokończyć **lokalny
handshake mTLS** pod Wine na Intel Macu. Bez niej klient pada na
`CryptographicException: Unknown error (0x80090027)` przy wejściu do świata.

To jest **uzupełnienie braków w emulacji CNG przez Wine (interop)** - nie
ingerencja w grę i nie obejście uwierzytelniania. Patrz „Granice" na dole.

## Co dokładnie naprawia

Wejście do świata to połączenie klienta z serwerem przez QUIC z wzajemnym TLS
(mTLS): w singleplayer klient uruchamia własny, lokalny serwer; w multiplayer
łączy się ze zdalnym. Do uwierzytelnienia po stronie klienta .NET buduje
efemeryczny certyfikat RSA, normalizuje go round-tripem PKCS#12 i używa jego
klucza prywatnego do podpisu w handshake. Trzy funkcje CNG Wine 11.18 są tu
niekompletne i zwracają `NTE_INVALID_PARAMETER` (0x80090027):

1. **`NCryptGetProperty`** - nie zna wyliczanych właściwości klucza, o które
   pyta .NET (`Provider Handle`, `Algorithm Group`, `Algorithm Name`…). Łata
   dostarcza je: świeży uchwyt providera (który .NET sam zwalnia), grupę/nazwę
   algorytmu odczytaną z typu klucza, a nieznane właściwości zgłasza jako
   `NTE_NOT_FOUND` (czyli „brak", zamiast błędu), by .NET je pominął.
2. **`CryptAcquireCertificatePrivateKey`** - round-trip PKCS#12 pod Wine daje
   uchwyt klucza **mostkowany przez CAPI**, którego .NET `RSACng` odrzuca
   („keys used with the RSACng algorithm must have an algorithm group of RSA").
   Łata podmienia go na świeżo zaimportowany, prawidłowy klucz CNG RSA -
   zbudowany z **tego samego** materiału klucza, który klient sam wcześniej
   zaimportował (zapamiętany blob `BCRYPT_RSAPRIVATE_BLOB`).
3. **`NCryptExportKey` (PKCS8_PRIVATEKEY)** - Wine nie eksportuje klucza w tym
   formacie, a .NET go potrzebuje, by przekazać klucz bibliotece QUIC. Łata
   eksportuje `BCRYPT_RSAFULLPRIVATE_BLOB` (to Wine umie) i sama koduje PKCS#8
   DER (RFC 8017 + RFC 5208).

## Jak działa (mechanizm)

- Klient importuje statycznie `version.dll`. Wtyczka to **proxy `version.dll`**:
  forwarduje wszystkie 16 eksportów prawdziwego `version.dll` Wine (kopiowanego
  obok jako `versionw.dll`), a przy załadowaniu zakłada **inline-hooki (MinHook)**
  na powyższe funkcje w `ncrypt.dll` i `crypt32.dll` **Wine**.
- `play.sh` wgrywa `version.dll` + `versionw.dll` do katalogu klienta i ustawia
  `WINEDLLOVERRIDES=…;version=n,b` (ładuj natywne proxy, z builtin jako zapas).
- Hooki są „fail-closed": albo wszystkie się założą, albo żaden (gra bez łaty,
  nie crash).

## Budowanie

Wymaga `x86_64-w64-mingw32-gcc`:

```bash
brew install mingw-w64
cd wine-fix && ./build.sh      # produkuje version.dll
```

Gotowej binarki nie trzymamy w repo - zbuduj ją ze źródła (`hytale-wine-fix.c`).
Build jest powtarzalny (`-Wl,--no-insert-timestamp`).

Diagnostyka: ustaw `HYTALE_FIX_LOG=1` przed `./play.sh`, a wtyczka zapisze log
do `C:\hytale-wine-fix.log` w prefiksie (domyślnie cicha).

## Granice (co to NIE jest)

- **Binarka gry pozostaje nietknięta.** Hooki zakładane są na funkcje **Wine**
  (`ncrypt`/`crypt32`), nie na kod Hytale.
- Łata operuje **wyłącznie na własnym, efemerycznym kluczu klienta** - tym samym,
  który klient sam importuje. Nie generuje obcych kluczy, nie podrabia tokenów,
  nie wyłącza żadnej weryfikacji certyfikatu ani podpisu. Eksport PKCS#8
  wykonywany jest tylko wtedy, gdy klient sam o niego prosi.
- Logowanie (token) i podpis pozostają po stronie klienta, na Twoim koncie.
