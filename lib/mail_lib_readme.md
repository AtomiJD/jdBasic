# MAIL - e-mail messages built, sent and read back

`lib/mail.jdb` builds e-mail messages the way a billing system sends them:
a plain text and an HTML version, images shown inside the HTML,
attachments, names and subjects with umlauts. It writes them as `.eml`
files, sends them over SMTP or SMTPS through curl, and reads a message
text back into its headers, bodies and attachments.

A message is a map. `BUILD$` turns it into an RFC 5322 message with MIME
parts: quoted-printable text, base64 attachments in lines of 76, RFC 2047
encoded words for headers beyond ASCII, RFC 2231 file names.

Stands in for: smtplib, email.

## Quick start

```basic
IMPORT MAIL

DIM msg = MAIL.MESSAGE("Papier & Toner GmbH <rechnung@papier-toner.example>", "einkauf@musterstadt.example", "Ihre Rechnung RE-2026-0413")
MAIL.PLAIN(msg, "Anbei die Rechnung über 209,80 €.")
MAIL.HTMLBODY(msg, "<p>Anbei die Rechnung über <b>209,80&nbsp;€</b>.</p><img src='cid:logo'>")
MAIL.INLINE(msg, "logo.jpg", "logo")
MAIL.ATTACH(msg, "RE-2026-0413.pdf")
MAIL.WRITEEML(msg, "RE-2026-0413.eml")

DIM sent = MAIL.SEND(msg, {"url": "smtps://smtp.example.com:465", "user": "rechnung", "password": secret$})
IF NOT sent{"ok"} THEN PRINT "not sent: "; sent{"output"}

DIM back = MAIL.PARSEFILE("RE-2026-0413.eml")
PRINT back{"subject"}, back{"text"}
MAIL.SAVEATTACHMENT(back, 1, "copy.pdf")
```

## API

### Building

| Call | What it does |
|------|--------------|
| `MESSAGE(from$, recipients, subject$)` | A new message. Recipients are one address, several separated by commas, or an array; an address may carry a name, `Ann Example <ann@example.org>`. |
| `PLAIN(msg, text$)` / `HTMLBODY(msg, html$)` | The text and the HTML version; with both, a mail client shows the one it prefers. |
| `CC(msg, recipients)` / `BCC(msg, recipients)` | More recipients. Bcc recipients receive the message but appear in no header. |
| `REPLYTO(msg, address$)` | The Reply-To address. |
| `HEADER(msg, name$, value$)` | Any other header. |
| `ATTACH(msg, path$, [name$], [mime$])` | Attaches a file; the name defaults to the file's, the MIME type to the one its extension stands for. |
| `ATTACHDATA(msg, name$, data$, [mime$])` | Attaches bytes held in a string. |
| `INLINE(msg, path$, cid$)` | An image the HTML shows with `<img src="cid:...">`. |
| `SETDATE(msg, date$)` / `SETID(msg, id$)` | A fixed Date and Message-ID instead of the time of building and a new one; for tests and for resending. |

### Writing and sending

| Call | What it does |
|------|--------------|
| `BUILD$(msg)` | The text of the message, CRLF line ends. |
| `WRITEEML(msg, path$)` | The message as an `.eml` file every mail client opens. |
| `SEND(msg, server)` | Sends through curl. `server`: `"url"` (`smtp://host:587` or `smtps://host:465`), `"user"`, `"password"`, `"starttls"` (TRUE demands TLS on a `smtp://` connection), `"timeout"` in seconds (30). Answers a map with `"ok"`, `"exit_code"` and curl's `"output"`. |
| `COMMAND(msg, server, [eml_path$], [config_path$])` | The arguments curl is called with, for logging or a dry run. |

The user and password go into a temporary curl config file that is
deleted after the call, so they never appear on a command line another
process can read. Every To, Cc and Bcc address becomes a `--mail-rcpt`.

### Reading

| Call | What it does |
|------|--------------|
| `PARSE(eml$)` / `PARSEFILE(path$)` | A message text as a map: `"headers"` (names in small letters, values decoded), `"from"`, `"to"` and `"cc"` (arrays), `"subject"`, `"date"`, `"message_id"`, `"text"`, `"html"`, and the attachments as `"att_names"`, `"att_mimes"`, `"att_cids"` and `"att_sizes"`. |
| `HEADERVALUE$(parsed, name$)` | One header of a parsed message, or `""`. |
| `ATTACHMENTDATA$(parsed, index)` / `SAVEATTACHMENT(parsed, index, path$)` | The bytes of an attachment, or the attachment written to a file. |

Multipart messages are read to any depth. Text parts in quoted-printable,
base64, 7bit or 8bit, in UTF-8, ISO-8859-1 or windows-1252 come out as
UTF-8; the first plain and the first HTML part are the bodies, every part
with a file name or a disposition of attachment is an attachment.

### Encodings and addresses

| Call | What it does |
|------|--------------|
| `ENCODEHEADER$(text$)` / `DECODEHEADER$(text$)` | RFC 2047 encoded words: UTF-8 in base64, cut between characters, folded; decoding also takes Q words and ISO-8859-1. |
| `QPENCODE$(text$)` / `QPDECODE$(text$)` | Quoted-printable with soft breaks at 76 characters, the same lines Python's email package writes. |
| `ADDRESSES(recipients)` | Addresses from a comma separated text; a comma inside quotes or angle brackets belongs to the address. |
| `BAREADDRESS$(address$)` / `ADDRESSNAME$(address$)` | The address and the name of `Name <address>`. |
| `MIMETYPE$(name$)` | The MIME type of a file name's extension. |
| `DATENOW$()` / `DATETEXT$(year, month, day, hour, minute, second, offset_minutes)` | An RFC 5322 date, now or from its parts. |

## Notes

- Sending needs `curl` with SMTP support on the path; Windows 10 and later
  ship one, and so does Git for Windows.
- An attachment is read and encoded when it is attached; changing the file
  afterwards does not change the message.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/mail_selftest.jdb`.
Demo: `jdb/demos/jdlibs/mail_demo.jdb`.
