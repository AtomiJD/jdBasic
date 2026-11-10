# Retest der Major-Funde - "Office Automation with jdBasic"

Stand: 09.11.2026, PDF mit 789 Seiten (Druck) bzw. 781 Seiten (Bildschirm).
Seitenzahlen beziehen sich auf die Druckfassung `book_print.pdf`.
Ticketnummern wie auf dem Board (Projekt jdbook).

## A. Mit dem neuen Rezeptstand testbar (jdBasic 1.2 reicht)

| Ticket | Rezept | Was jetzt gelten soll | Im Buch |
|---|---|---|---|
| #777 | E02 Morning Launcher | Ordner und Dateien mit `/` im Pfad öffnen das Ziel, nicht "Dokumente" | E02 ab S. 110, How it works 4 |
| #778 | E04 One-Click Backup | Dry-Run und echter Lauf nennen dieselben zu löschenden Archive | E04 ab S. 120, How it works 4 |
| #795 | M01 (und jedes Rezept mit `--send`) | Falsches Passwort: genau ein Anmeldeversuch, Rest bleibt im Postausgang mit "not tried, the password was refused" | M01 ab S. 229 |
| #808 | M04 Receipt Sorter | "Rename the file" aus Make it yours: jeder Name trägt seinen eigenen Betrag | M04 ab S. 263 |
| #814 | M06 Friday Status Mail | `clear_done` und zweiter Lauf in derselben Woche: erledigte Punkte bleiben in der Mail | M06 ab S. 283, How it works 7 |
| #817 | M07 Inbox Unpacker | Anhang mit Content-ID, der nicht im HTML-Text steht, wird gespeichert | M07 ab S. 292, How it works 5 |
| #827 | M10 Hot Folder | CSV in ANSI (deutsches Excel): Umlaute kommen an, Arbeitsmappe öffnet ohne Reparatur | M10 ab S. 322, How it works 5 |
| #828 | M10 Hot Folder | CSV in Gebrauch: keine weiteren (halben) Arbeitsmappen; nach dem Schließen genau eine | M10, How it works 6 |
| #831 | M11 Data Cleaner | `1.234` / `2.500` werden 1234 / 2500, wenn die Spalte ein Dezimalkomma hat; sonst Rückfrage in Problems | M11 ab S. 335, How it works 4 |
| #841 | X01 Personal Job Server | Hängender Job wird nach `timeout_minutes` beendet, der nächste Job läuft | X01 ab S. 430, How it works 5 |
| #847 | X04 Ticket Sync | Weitergegebene Tickets und Tickets auf Seite 2 bleiben offen; jedes Abhaken wird ausgegeben | X04 ab S. 470 |
| #850 | X10 Let an AI Drive | Dry-Run von E10, E11, E17, M08, X15 schreibt nichts; ein Rezept endet spätestens nach zwei Minuten | X10 ab S. 546, How it works 5 |
| #856 | X14 Secrets and Audit Trail | `--scan` findet auch Werte ohne Anführungszeichen (INI, .bat, JSON-Schlüssel, Connection Strings) | X14 ab S. 599 |
| #861 | X16 Desktop Robot | Falscher Knopf fällt durch: "41.3 % of the region differs"; Toleranz ist jetzt ein Anteil (Standard 2) | X16 ab S. 629, How it works 4 |
| #864 | X17 Meeting Briefing | Die eigene Adresse zählt nicht als Teilnehmer; fremde Mails gehen nicht mehr ans Modell | X17 ab S. 643, How it works 3 |
| #868 | X18 Spreadsheet to Web App | Fehlgeschlagenes Speichern wird zurückgenommen; gesperrte Mappe gibt eine Meldung | X18 ab S. 660 |
| #869 | X18 Spreadsheet to Web App | Kein Code-Fix: das Buch warnt jetzt, dass Speichern Formeln und Formate verwirft | X18, Watch out |
| #877 | Kapitel 2 / alle Rezepte | Zahl in Anführungszeichen in work.conf: "... is not a number: "24". Write it without quotes in work.conf." | S. 76 (WORKCONF), Anhang B |

## B. Braucht einen neuen jdBasic-Build (noch in keinem Release)

| Ticket | Was | Gilt für |
|---|---|---|
| #794 | `.exe`: Map-Literal mit Array-Element, Rechnung mit Tabellenkopf | M01 als .exe |
| #821 | `.exe`: SORT-Element im Ausdruck, Zellen eines Arrays an Builtins | M09 als .exe |
| #826 | `.exe`: nicht ausgeführtes `IF ... THEN` (war schon vorher behoben, jetzt mit Test) | M10 als .exe |
| #844 | `.exe`: Absturz mit vorhandener job_state.json | X01 als .exe |
| #862 | `.exe`: DPI-bewusst, Klicks stimmen bei 150 % | X16/M16 als .exe |
| #788 | mcp-native: kompilierte Programme haben HTTP.* | E14 aus dem mcp-native-Paket |

## C. Noch offen

| Ticket | Stand |
|---|---|
| #792 | E18: ein offenes PowerPoint mit ungespeicherter Änderung bleibt offen. Code ist da, der Test mit Office steht noch aus. |
| #834 | CLIPBOARD.SET mit Umlauten: in jedem 1.2-Build behoben (Build 99 war 1.1). Bitte E08 und M13 mit dem 1.2 Forms-Release prüfen. |

#887 (Anführungszeichen auf der Kommandozeile) ist zurückgezogen und geschlossen. Gegenprobe aus `cmd` mit Leerzeichen im Pfad: Interpreter und `.exe` richtig.
