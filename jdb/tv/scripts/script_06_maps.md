---
title: Train jdBasic - Lesson 06 - Maps
hook: Key-value storage for any shape of data
tags: maps, dictionary, hashmap, key-value, data-types, beginner, lesson-6
---

Lesson 06 of Train jdBasic - maps, jdBasic's key-value data type. Same
job as a Python dict, JavaScript object, or C# Dictionary. Together with
arrays they handle almost any shape of data you will meet.

## What you'll learn

- Map literals: `{"name": "Atomi", "age": 38}` (string keys, any value type)
- Read with `person["name"]`, write with the same syntax
- Add new keys just by assigning to them
- `MAP.SIZE(m)` for the entry count
- `MAP.EXISTS(m, key$)` to test for a key
- `MAP.KEYS(m)` returns all keys as an array - ready for `FOR EACH`

## Code from the lesson

```basic
DIM person = {"name": "Atomi", "age": 39, "city": "Heidelberg"}
FOR EACH k IN MAP.KEYS(person)
    PRINT k; ": "; person[k]
NEXT
```

Three fields in, three formatted lines out. Iteration order isn't
guaranteed, but every key is visited exactly once.

## Next up

Lesson 07 - INPUT for talking to the user, plus the DO loop.

## Links

- jdBasic source + scripts: https://github.com/AtomiJD/jdBasic
- Full playlist: https://www.youtube.com/playlist?list=PLowaSH4O3MGq-veO7qSIp-9EntEjY_iPZ
