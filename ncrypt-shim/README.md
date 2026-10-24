> **⚠️ MATERIAŁ HISTORYCZNY — obalony.** Zapis wcześniejszej, błędnej hipotezy
> z drogi do rozwiązania (że „0x80090027 pochodzi z bcrypt, nie ncrypt"). Działające
> rozwiązanie jest w [`../wine-fix/`](../wine-fix/README.md) i hookuje m.in.
> właśnie `ncrypt` (`NCryptGetProperty`). Ten katalog niczego nie wdraża.

# Zastępczy ncrypt.dll z obsługą ECC (materiał, NIE wdrożony)

Zbudowany, żeby dodać obsługę importu kluczy ECC do `NCryptImportKey` w Wine
(Wine obsługuje tylko RSA; dla ECC zwraca NTE_INVALID_PARAMETER = 0x80090027).

**Dlaczego nie wdrożony:** diagnoza empiryczna pokazała, że przy połączeniu
mTLS klienta Hytale `ncrypt` NIE jest wołany — błąd 0x80090027 pochodzi z
`bcrypt`, nie ncrypt. Ten shim poprawnie rozwiązuje inny problem (import ECC
przez ncrypt), który może się przydać innym aplikacjom .NET pod Wine, ale nie
ten konkretny błąd Hytale. Dodatkowo jego forwardy BCrypt powodowały page fault.

Budowanie (gdyby ktoś potrzebował importu ECC w ncrypt pod Wine):
    x86_64-w64-mingw32-gcc -O2 -shared -o ncrypt.dll ncrypt.c ncrypt.def -lbcrypt -municode
    # wdrozenie: do prefiksu system32 + WINEDLLOVERRIDES="ncrypt=n"
