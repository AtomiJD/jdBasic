# SECRET - password hashes, constant-time comparison, tokens

`lib/secret.jdb` keeps passwords as slow salted hashes, compares secrets
without leaking where they differ, and makes random tokens and API keys.
It builds on the natives `CODEC.PBKDF2$`, `CODEC.RANDOMBYTES$` and
`CODEC.HMAC$`.

Stands in for: passlib, Django's password hashers, Python's `secrets`
and `hmac.compare_digest`.

## Quick start

```basic
IMPORT SECRET

' sign-up
DIM stored$ = SECRET.HASH$(password$)            ' 600000 rounds, about 0.3 s

' sign-in
IF SECRET.VERIFY(attempt$, stored$) THEN
    IF SECRET.NEEDS_REHASH(stored$) THEN stored$ = SECRET.HASH$(attempt$)
    DIM session_id$ = SECRET.TOKEN$()             ' 43 characters, base64url
ENDIF

' API keys: show the key once, keep the hash
DIM key$ = SECRET.APIKEY$("jdk")                  ' jdk_ and 43 characters
DIM kept$ = SECRET.KEYHASH$(key$)
IF SECRET.KEYMATCH(request_key$, kept$) THEN PRINT "allowed"
```

## API

| Call | What it does |
|------|--------------|
| `HASH$(password$, [iterations])` | A new hash `pbkdf2-sha256$iterations$salt$key` with a random 16-byte salt (32 hex digits) and a 32-byte key (64 hex digits). `iterations` defaults to 600000; less than 1 raises. |
| `VERIFY(password$, stored$)` | Whether the password matches. Reads the own format and Django's `pbkdf2_sha256$iterations$salt$base64key`. Anything else, or a damaged hash, is `FALSE`, never an error. |
| `NEEDS_REHASH(stored$, [iterations])` | `TRUE` for another format (Django, a bare SHA-256), fewer rounds than `iterations` (600000), a salt shorter than 32 characters or a damaged hash. |
| `EQUAL(a$, b$)` | Equality whose time does not depend on where the texts differ. |
| `TOKEN$([n_bytes], [format$])` | `n_bytes` (32) random bytes as `"base64url"` (default, no padding), `"base64"` or `"hex"`. |
| `APIKEY$([prefix$])` | A key of 32 random bytes in base64url, after `prefix$` and `_` when a prefix is given. |
| `KEYHASH$(key$)` | The SHA-256 of a key in hex, the value to store. |
| `KEYMATCH(key$, stored_hash$)` | Whether the key belongs to the stored hash, in constant time; the stored hash may be in capitals. |

## Notes

- The salt is used as its hex text, the way Django uses its salt string,
  so `hashlib.pbkdf2_hmac("sha256", password, salt.encode(), iterations, 32)`
  gives the same key. The self test carries hashes made that way and by
  Django's format.
- 600000 rounds is the OWASP figure for PBKDF2-HMAC-SHA256 (2023). One
  hash takes about 0.32 s in either backend, which is the point: a stolen
  table costs an attacker the same per guess. Raise the figure over the
  years; `NEEDS_REHASH` then tells which stored hashes to renew at the
  next sign-in.
- A migration from an older scheme checks the old hash once, then stores
  `HASH$` of the password the user just typed:

  ```basic
  IF LEFT$(stored$, 14) <> "pbkdf2-sha256$" THEN
      ok = CODEC.SHA256$(salt$ + ":" + attempt$) = stored$
  ELSE
      ok = SECRET.VERIFY(attempt$, stored$)
  ENDIF
  IF ok ANDALSO SECRET.NEEDS_REHASH(stored$) THEN stored$ = SECRET.HASH$(attempt$)
  ```

- `EQUAL`, `VERIFY` and `KEYMATCH` compare the HMACs of both texts under a
  key drawn when the module loads, so an ordinary string compare runs on
  values an attacker cannot steer.
- API keys carry 256 random bits, so a fast SHA-256 is enough to store
  them; a password needs the slow hash because people pick guessable ones.
- `CODEC.RANDOMBYTES$` reads the operating system's generator. It is not
  in board builds, so SECRET does not load there.

## Tests and demo

- `tests/jdlibs/secret_selftest.jdb`
- `jdb/demos/jdlibs/secret_demo.jdb` (sign-up, sign-in with a migration from salted SHA-256, a password reset token and API keys)
