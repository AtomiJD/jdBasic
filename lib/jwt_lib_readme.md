# JWT - JSON Web Tokens with HS256

`lib/jwt.jdb` signs a claims map into a token, reads a token back, and
verifies the signature and the standard claims. The only algorithm is
HS256, a shared secret; public key algorithms are out of scope.

Stands in for: PyJWT.

## Quick start

```basic
IMPORT JWT

DIM token$ = JWT.SIGN$({"sub": "ann", "role": "admin", "exp": NOW_EPOCH() + 3600}, secret$)

DIM check = JWT.VERIFY(token$, secret$, {"iss": "inventory", "skew": 30})
IF check{"ok"} THEN
    PRINT check{"claims"}{"sub"}
ELSE
    PRINT check{"error"}
ENDIF

DIM peek = JWT.DECODE(token$)       ' header and claims, unverified
```

## API

| Call | What it does |
|------|--------------|
| `SIGN$(claims, secret$, [alg$])` | A token with the header `{"alg": "HS256", "typ": "JWT"}`. Anything but HS256 raises. |
| `DECODE(token$)` | `{header, claims, signature}` without checking anything; raises on a malformed token. |
| `VERIFY(token$, secret$, [opts])` | `{ok, claims, header, error}`. |
| `B64URL$(bytes$)` / `UNB64URL$(text$)` | Base64url, the standard alphabet with `-` and `_` and no padding. |

`VERIFY` checks the signature first, then the claims:

| Option | Check |
|--------|-------|
| `skew` | Seconds of clock tolerance for `exp` and `nbf` (0 by default). |
| `iss` | `claims{"iss"}` must equal it. |
| `aud` | `claims{"aud"}` must equal it, or contain it when the claim is a list. |
| `now` | Seconds since 1970 to use instead of the clock, for tests. |

The errors it names: `malformed token`, `unsupported algorithm`,
`bad signature`, `token expired`, `token not yet valid`,
`wrong issuer`, `wrong audience`.

## Notes

- The signature compare walks every byte before it answers, so the
  time it takes does not tell where two signatures part.
- `exp`, `nbf` and `iat` are seconds since 1970, the value `NOW_EPOCH()`
  gives.
- Reading a token needs no secret; do not put anything in the claims
  that the bearer may not see.
- The token for `{"sub": "ann", "role": "admin", "exp": 1800000000}`
  under `top-secret` matches one built with Python's `hmac` and
  `base64` byte for byte, the construction PyJWT and jwt.io use.

## Tests and demo

- `tests/jdlibs/jwt_selftest.jdb`
- `jdb/demos/jdlibs/jwt_demo.jdb` (a login that issues tokens, an endpoint that checks them, and what an attacker tries)
