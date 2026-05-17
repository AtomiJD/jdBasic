---
title: Train jdBasic - Lesson 13 - HTTP and JSON
hook: Talk to the web in two function calls
tags: http, json, api, github, web, fetch, parse, intermediate, lesson-13
---

Lesson 13 of Train jdBasic - the language steps outside itself. Built-in
HTTP client + JSON parser, two functions, and almost any modern web API
becomes consumable.

## What you'll learn

- `HTTP.GET$(url$)` fetches the response body as a string
- `JSON.PARSE$(body$)` turns it into a map/array structure
- The parsed object indexes exactly like the maps from Lesson 06
- Production extras: `HTTP.SETTIMEOUT`, `HTTP.STATUSCODE`, plus an async variant for non-blocking fetches

## Code from the lesson

```basic
DIM url$ = "https://api.open-meteo.com/v1/forecast?latitude=49.40&longitude=8.69&current_weather=true"
PRINT "Fetching..."
DIM body$ = HTTP.GET$(url$)
DIM data = JSON.PARSE$(body$)
DIM w = data["current_weather"]

PRINT "Temperature : "; w["temperature"]; " degC"
PRINT "Windspeed   : "; w["windspeed"]; " km/h"
PRINT "Wind heading: "; w["winddirection"]; " deg"
```

Three live values pulled from a free weather API. No parser code, no
string slicing - just know the shape of the JSON and read what you need.

## Next up

Lesson 14 - native compilation. We compile a jdBasic program to a real
Windows .exe, see how strict mode catches type errors at compile time,
and measure the speedup.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
