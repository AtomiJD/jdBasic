# STRUCT - binary layouts on PACK$ and UNPACK

`lib/struct.jdb` declares a binary record once - fields with a type, a
count, a byte order - and then reads a string of bytes into a map and
writes a map back into bytes. Nested layouts, arrays, bit fields, fixed
texts and magic values are part of the declaration, so file headers,
network packets and board protocols read the same way.

Stands in for: Python's `struct`, `ctypes` structures with bit fields,
`construct`.

## Quick start

```basic
IMPORT STRUCT

DIM frame = STRUCT.LAYOUT("modbus", ">")            ' big endian by default
STRUCT.FIELD(frame, "slave", "u8")
STRUCT.FIELD(frame, "function", "u8")
STRUCT.FIELD(frame, "start", "u16")
STRUCT.FIELD(frame, "quantity", "u16")
STRUCT.FIELD(frame, "crc", "u16", 1, "<")           ' this field little endian

DIM request = STRUCT.READ(frame, bytes$)
PRINT request{"start"}, request{"crc"} = STRUCT.CRC16(LEFT$(bytes$, 6))

DIM packet = STRUCT.LAYOUT("packet", "<")
STRUCT.FIELD(packet, "kind", "u8")
STRUCT.BITS(packet, "u16", "ready:1,mode:3,level:4,channel:8")
STRUCT.FIELD(packet, "value", "i32")
DIM out$ = STRUCT.WRITE$(packet, {"kind": 7, "ready": 1, "mode": 5, "level": 9, "channel": 200, "value": -12})
PRINT STRUCT.HEXDUMP$(out$)
```

## Declaring a layout

| Call | What it does |
|------|--------------|
| `LAYOUT(name$, [endian$])` | A new layout; `"<"` (little, the default) or `">"` (big) for fields that name no order of their own. `"little"` and `"big"` work too. Answers a handle. |
| `FIELD(layout, name$, type$, [n_items], [endian$])` | A field. `type$` is `u8 i8 u16 i16 u32 i32 i64 f32 f64`, `str` (a text of `n_items` bytes) or `pad` (`n_items` skipped bytes). A number type with `n_items` above 1 is an array. |
| `NESTED(layout, name$, child, [n_items])` | A field holding another layout; an array of them when `n_items` is above 1. |
| `MAGIC(layout, name$, type$, value)` | A field with a fixed value: a number type with a number, or `str` with the text itself. `READ` refuses data where it differs, `WRITE$` writes it without being given it. |
| `BITS(layout, type$, spec$, [order$], [endian$])` | Named bit ranges inside one unsigned `u8`, `u16` or `u32`: `spec$` is `"name:width,..."`. `order$` `"lsb"` fills from the lowest bit, `"msb"` from the highest. |

The fields sit one after another with no alignment padding, as `struct`
with `<` or `>` packs them; add `pad` fields where a C structure would have
holes.

By default a little endian layout fills bit fields from the lowest bit and
a big endian one from the highest, which is the layout C compilers use and
what `ctypes.LittleEndianStructure` and `ctypes.BigEndianStructure` give
(with `_pack_ = 1`). A bit field appears in the map under its own name.

## Reading and writing

| Call | What it does |
|------|--------------|
| `READ(layout, data$, [offset])` | The fields read from `data$` at `offset` (0) into a map: numbers, arrays of numbers, texts with their trailing NUL bytes removed, maps and arrays of maps for nested layouts. Too few bytes, or a magic field that differs, raises an error. |
| `WRITE$(layout, values)` | The bytes of a map as `READ` answers it. A missing number is 0, a missing text empty, a missing nested layout all zeros. A whole number outside its type, a fraction for an integer type, a text longer than its field or a value wider than its bits raises an error. |
| `SIZE(layout)` | The size in bytes. |
| `OFFSETOF(layout, path$)` | The byte offset of a field; `"head.version"` goes into a nested layout, `"items.2.x"` into an element of an array of layouts. A bit field answers the offset of its integer. |
| `FIELDNAMES(layout)` | The names of the fields in order, bit fields by their own names, pad fields left out. |

## Helpers

| Call | What it does |
|------|--------------|
| `CRC16(data$, [init])` | CRC-16/MODBUS (reflected polynomial 0xA001, start 0xFFFF): `CRC16("123456789")` is 19255 (0x4B37). Modbus RTU carries it little endian after the payload. |
| `HEXDUMP$(data$)` | The bytes as two-digit hex separated by spaces. |

## Notes

- The byte strings are ordinary jdBasic strings and may hold `CHR$(0)`;
  a NUL inside a `str` field is kept, only trailing ones are removed on
  read. `CODEC.BASE64_ENCODE$` gives a text form for storing or comparing.
- `u64` is missing: a jdBasic number holds whole numbers exactly up to
  2^53, and `i64` is exact within that range.
- Unsigned 32-bit values read as numbers up to 4294967295, signed ones
  with their sign; `f32` reads back the double nearest to the stored
  single.
- The layout table lives in the module, so a layout handle is valid for
  the whole program.
- Checked against Python: 112 values of every type in both byte orders
  read and written byte for byte like `struct.pack`, a WAV header, a
  Modbus RTU frame with `crc16`, the bit fields of a `ctypes` little and
  big endian structure, a PNG IHDR chunk with its CRC-32, and a record
  with nested layouts and arrays.

## Tests and demo

- `tests/jdlibs/struct_selftest.jdb`
- `jdb/demos/jdlibs/struct_demo.jdb` (a WAV header, a Modbus request with its CRC, a sensor packet with bit fields, a log record with nested entries)
