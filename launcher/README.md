# Play Hytale - aplikacja z jednym klikiem

`make-app.sh` buduje aplikacje **Play Hytale.app**, ktora jednym dwuklikiem
uruchamia gre (przez `play.sh`), z ikona Hytale.

```bash
./launcher/make-app.sh                 # postawi app na Pulpicie
./launcher/make-app.sh ~/Applications  # albo w Programach (wtedy tez w Launchpadzie)
```

- **Ikona:** logo "H" Hytale jest pobierane ze strony hytale.com przy budowaniu,
  u Ciebie na dysku - nic cudzego nie jest trzymane w repo. Dla ladniejszej wersji
  (zaokraglone rogi, poswiata) zainstaluj `pip3 install Pillow` przed uruchomieniem;
  bez tego powstaje prosta, czysta ikona.
- **Staly przycisk:** przeciagnij gotowa aplikacje z Pulpitu na Dock.
- Aplikacja to zwykly skrypt (osascript), ktory otwiera Terminal i uruchamia
  `play.sh` w katalogu repozytorium. Zadnej magii, mozesz zajrzec do srodka.

`setup.sh` w glownym katalogu buduje ja automatycznie na koncu instalacji.
