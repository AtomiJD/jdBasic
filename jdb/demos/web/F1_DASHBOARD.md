# Formel 1 Dashboard

`f1_dashboard.jdb` - a Formula 1 desktop dashboard in jdBasic (GFX + ImGui),
built the same way as `wm_dashboard.jdb`. Data comes from
[OpenF1](https://openf1.org), a public API with no key and no account.

```
build/jdBasic.exe jdb/demos/web/f1_dashboard.jdb
```

ESC quits, F toggles fullscreen. Flags, circuit maps and driver headshots are
cached under `tmp/`, so the second start is much faster than the first.

## Tabs

| Tab | Content |
|---|---|
| Live | The current or most recent session: flag, weather, live order with gap and interval, race control messages, auto-refresh every 20 s. Between race weekends it shows the last result plus a countdown to the next session. |
| WM-Stand | Drivers and constructors championship side by side, with the points gained in the last race and the position change. |
| Rennkalender | All race weekends of the season with flag, date, circuit and winner. A click opens the weekend: circuit map, session picker, result, tyre strategy, pit stops, race control. |
| Fahrer | The field as a photo grid. A click opens the driver: championship position, every race of the season, wins, podiums, points. |
| Boxenstopp-Bestzeiten | The fastest pit stop of every race so far, loaded on request (one request per race). |

The season combo switches between 2023 and 2026. The championship endpoints
only carry data from 2025 on; for older seasons that tab stays empty while
everything else works.

## Endpoints in use

| Endpoint | Used for | When |
|---|---|---|
| `meetings?year=` | race calendar, country flag, circuit image | once per season |
| `sessions?year=` | every session of the season, dates | once per season |
| `drivers?session_key=` | name, team, team colour, headshot | reference race, then per opened session |
| `championship_drivers` / `championship_teams` | both championship tables | reference race |
| `session_result?session_key=` | classification, laps, gap, points | every completed race at start, other sessions on demand |
| `stints` / `pit` | tyre strategy, pit stops | on demand |
| `race_control` | flags and stewards messages | on demand, live every 20 s |
| `weather` | air and track temperature, wind, rain | per opened session |
| `position` / `intervals` | the live order | only while a session is running |

`intervals` is the one endpoint that is too big to pull whole (4 MB per race),
so the live view asks for the last five minutes via the `date>=` filter. All
the others fit comfortably.

## Notes

- The reference race is the last race that has already started. It carries the
  championship tables and the driver list; drivers who missed that race are
  filled in from earlier races until the field is complete.
- OpenF1 refuses a request now and then when several arrive in a burst, so
  every fetch retries three times before giving up. The WM-Stand tab offers a
  "Nochmal holen" button in case it still came up empty.
- Qualifying results carry `duration` and `gap_to_leader` as three-element
  arrays (Q1/Q2/Q3); races carry plain numbers and `"+1 LAP"` strings.
