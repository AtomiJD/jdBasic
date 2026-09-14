# QR - QR codes and the EPC payment code

`lib/qr.jdb` makes QR codes in jdBasic itself, with nothing native behind
it: numeric, alphanumeric and byte mode, the four error correction levels,
versions 1 to 40, Reed-Solomon error correction over GF(256), and the eight
data masks scored by the penalty rules of ISO/IEC 18004. On top sits the EPC
QR code for SEPA credit transfers, known in Germany and Austria as GiroCode
or "Zahlen mit Code". A code goes out as text for the terminal, as SVG, or
as boxes on a PDFGEN page. It compiles with `-c` like any other module.

Stands in for: segno, qrcode (python-qrcode), segno.helpers.make_epc_qr.

## Quick start

```basic
IMPORT QR

DIM code = QR.MATRIX("https://jdbasic.org", {"ecc": "Q"})
PRINT QR.TEXT$(code)
TXTWRITER "site.svg", QR.SVG$(code)

' A GiroCode for an invoice
DIM giro = QR.EPCMATRIX("Papier & Toner GmbH", "DE89370400440532013000", "168.80", "COBADEFFXXX", "Rechnung RE-2026-0413")
TXTWRITER "girocode.svg", QR.SVG$(giro, 6)

' On a PDF invoice, 40 mm wide
IMPORT PDFGEN
DIM pdf = PDFGEN.DOC()
PDFGEN.ADDPAGE(pdf)
QR.TOPDF(pdf, giro, 150, 20, 40, 2)
PDFGEN.WRITEFILE(pdf, "rechnung.pdf")
```

## API

### Encoding

| Call | What it does |
|------|--------------|
| `MATRIX(text$, [opts])` | The code as an n x n array of 0 (light) and 1 (dark), `code[row, col]`, without the quiet zone. `opts`: `"ecc"` (`L`, `M`, `Q` or `H`; `M`), `"version"` (1 to 40; 0 picks the smallest that fits), `"mask"` (0 to 7; -1 picks the lowest penalty). A text too long, an unknown option or an out-of-range value raises. |
| `INFO()` | What the last `MATRIX` chose: `version`, `ecc`, `mode` (`numeric`, `alphanumeric`, `byte`), `mask`, `size` (modules per side) and `penalty`. |

The mode follows the whole text: only digits is numeric, only the 45
characters `0-9 A-Z space $ % * + - . / :` is alphanumeric, anything else is
byte mode with the text's UTF-8 bytes. Lower-case letters therefore end up
in byte mode; `UCASE$` a URL first when its case does not matter.

### Output

| Call | What it does |
|------|--------------|
| `TEXT$(code, [quiet], [on_light])` | Lines of text, two characters per module, `quiet` (2) light modules around the code. Light modules are full blocks and dark ones spaces, which scans on a dark terminal; `on_light` (FALSE) swaps them for a light background. |
| `SVG$(code, [module_px], [quiet])` | A standalone SVG: `module_px` (4) pixels per module, `quiet` (4) white modules around, all dark modules in one path, `shape-rendering="crispEdges"`. |
| `SVGPATH$(code, [quiet])` | Only the path data in module units, one rectangle per run of dark modules, for `SVG.ADDPATH` or an own `<path>`. |
| `TOPDF(doc, code, x, y, size, [quiet])` | The code on the current PDFGEN page: top left at `x`, `y` mm, `size` mm wide including `quiet` (0) modules, the dark runs as filled boxes. The fill colour stays black afterwards. |

### EPC payment code

| Call | What it does |
|------|--------------|
| `EPC$(name$, iban$, amount, [bic$], [remittance$], [reference$], [purpose$])` | The text of an EPC QR code (EPC069-12, version 002, character set 1 = UTF-8, SCT). |
| `EPCMATRIX(name$, iban$, amount, ...)` | The same arguments, encoded at level M as the guideline requires. |

- `name$`: the payee, 1 to 70 characters after trimming.
- `iban$`: checked with `VALID.ISIBAN`; spaces are dropped and letters put
  in capitals.
- `amount`: euros, as text (`"168.80"`, `"1.234,50"`, read by `MONEY.PARSE`
  with no rounding) or as a number (`12.3`, through `MONEY.FROMNUMBER`).
  0.01 to 999999999.99; it is written the way segno writes it, `EUR12.3`,
  `EUR10`.
- `bic$`: optional since version 002 inside the EEA; checked with
  `VALID.ISBIC` when given.
- Either `remittance$` (unstructured, up to 140 characters) or `reference$`
  (a structured creditor reference such as ISO 11649 `RF18...`, up to 35),
  not both and not neither.
- `purpose$`: an optional four-letter purpose code such as `GDDS`.
- The payload may not exceed 331 bytes, so the code is at most version 13.

Every rule that is broken raises an error naming it.

## Notes

- Every matrix of the self test equals the one segno 1.6.6 builds for the
  same text, level, version and mask, module for module: 21 codes across
  the modes, all four levels, versions 1 to 40 (version 40 with a fixed
  mask), UTF-8 text and four EPC codes. segno differs from ISO/IEC 18004 in
  one place: when the data bits already end on a byte boundary it adds a
  zero byte before the pad codewords (every byte mode code at versions 1 to
  9 hits this). QR follows the standard, and the references were made with
  segno's padding set to the ISO rule; the files mark the rows where plain
  segno differs. Both readings decode to the same text.
- The mask choice uses the four penalty rules exactly as segno implements
  them; with the same bits the same mask wins.
- Byte mode writes UTF-8 without an ECI header, as segno, python-qrcode and
  the EPC guideline do; phone scanners read it as UTF-8.
- A version 13 code (69 x 69 modules, the largest EPC code) takes about
  100 ms interpreted and 20 ms compiled, most of it scoring the eight
  masks; a fixed `mask` skips that.
- Some PDF viewers draw hairlines between neighbouring boxes at small zoom;
  printers and scanners do not see them.
- Whether a real banking app accepts the GiroCode of the demo is still to be
  confirmed by hand.

## Tests and demo

- `tests/jdlibs/qr_selftest.jdb` with `tests/jdlibs/fixtures/qr_matrices.tsv`
  and `qr_epc.tsv` (payloads in hex, matrices as rows of 0 and 1)
- `jdb/demos/jdlibs/qr_demo.jdb` (a GiroCode for an invoice: terminal
  preview, SVG file and a PDF page)
