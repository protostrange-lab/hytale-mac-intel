> **⚠️ MATERIAŁ HISTORYCZNY — obalony.** Zapis wcześniejszej, błędnej hipotezy,
> że błąd 0x80090027 pochodzi z `bcrypt`. W rzeczywistości leży w `ncrypt`/`crypt32`
> i jest rozwiązany w [`../wine-fix/`](../wine-fix/README.md). Ten logger niczego
> nie naprawia; zostawiony jako ślad śledztwa.

# Forwardujący logger bcrypt (materiał, NIE działa jako podmiana)

Miał namierzyć wywołanie bcrypt zwracające STATUS_INVALID_PARAMETER (0xc000000d)
w handshake'u mTLS. Forwarduje do prawdziwego bcrypt (jako bcryptw.dll) i loguje
wynik operacji na kluczach.

**Nie zadziałał jako podmiana:** wbudowany bcrypt.dll Wine jest samowystarczalnym
PE, którego backend kryptograficzny działa tylko ładowany wewnętrznym mechanizmem
Wine. Kopia (bcryptw.dll) załadowana jako natywna nie wykonuje realnej kryptografii,
więc klient pada przy starcie. bcrypt nie da się bezpiecznie owinąć z zewnątrz.

Kod zachowany, bo logika forwardowania jest poprawna — zadziałałaby, gdyby
backend bcrypt dawało się załadować osobno (np. przy przebudowie Wine ze źródeł).
