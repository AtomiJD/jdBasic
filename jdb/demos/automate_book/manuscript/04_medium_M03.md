<!-- pagebreak -->

## M03 Mail Merge

### The chore

Twice a year Jonas's team invites its customers: to the customer day
in November, to a training in spring. The letter is always the same
except for the name, the company and the salutation. Word has a mail
merge, but it hides in a menu that nobody remembers between two
uses, and it does not write one file per customer that can go out as
an attachment. So someone copies the letter forty times and changes
three lines in each.

### What you get

You write the letter once in Word, with placeholders such as
`{{name}}` where the personal parts go, and keep the contacts in an
Excel sheet. The Mail Merge writes one Word file per contact with the
placeholders filled, named after the person. For every contact with a
mail address it puts a mail with the letter attached into the outbox.

### Before you start

The setup wizard has run, and the outbox rule of this chapter is set
up. You need a contact list with a header row; each column name is a
placeholder:

| Name | Salutation | Company | City | Email | Date |
|---|---|---|---|---|---|
| Anna Berg | Ms Berg | Stone and Co | Leeds | anna@stone.example | 14 November |
| Ben Cole | Mr Cole | Cole and Sons | York | | 14 November |

No template yet? Run the program once. It writes a sample letter to
the template path and stops, so you can change that one in Word.

### The program

The program reads its settings, the template and the contacts, and
writes a letter and a mail per contact:

<!-- include recipes/medium/M03_mail_merge/mail_merge.jdb -->

The module `LETTERS` turns rows into contacts and contacts into
letters and mails:

<!-- include recipes/medium/M03_mail_merge/letters.jdb -->

### How it works

1. **Contacts.** `CONTACTS` reads the header row once and turns every
   further row into a map from column name to cell text. The names
   are made lower case, so `Email`, `EMAIL` and `email` are the same
   column. A number in the sheet, such as a postcode, becomes text
   without a decimal point.
2. **Checking the template.** `UNFILLED` asks `DOCX.PLACEHOLDERS` for
   the placeholders in the template and keeps with `FILTER` the ones
   no column fills. The letters are written all the same; the
   placeholder stays in them as it is, which you will notice when you
   open one.
3. **Filling.** `WRITE` builds the values for `DOCX.FILL` from the
   contact. `DOCX.FILL` replaces each `{{name}}` in the body, the
   headers and the footers, also where Word has split it into several
   runs after an edit, and keeps all formatting of the template.
4. **File names.** `FILE_NAME$` fills the `file_name` pattern with
   `FILL$`, which goes through the columns of the contact with
   `REDUCE` and replaces one placeholder after the other, and then
   replaces the characters Windows does not allow in file names. Two
   contacts with the same name get the row number added, so no letter
   overwrites another.
5. **Mails.** `MAIL_FOR` fills the subject and the text from the same
   contact, attaches the letter, and the program puts the message into
   the outbox under the name of the letter.

### Run it

Look first:

```
jdbasic mail_merge.jdb --dry-run
would write Anna Berg.docx  to anna@stone.example
would write Ben Cole.docx
would write Cleo Dunn.docx  to cleo@miller.example
```

Ben has no address; he gets a letter for the post. Then write:

```
jdbasic mail_merge.jdb
wrote Anna Berg.docx  to anna@stone.example
wrote Ben Cole.docx
wrote Cleo Dunn.docx  to cleo@miller.example
2 mails wait in the outbox. Send them with --send.
```

Open two or three of the letters before you send:

```
jdbasic mail_merge.jdb --send
```

### Schedule it

A mail merge belongs to an occasion, not to a day of the week, so the
wizard plans no task for it.

### Make it yours

The settings:

```toml
[mail_merge]
template = "~/Documents/AutomateWork/templates/letter.docx"
contacts = "~/Documents/AutomateWork/contacts.xlsx"
tab = ""
folder = "~/Documents/AutomateWork/letters"
file_name = "{{company}} {{name}}"
subject = "Invitation to our customer day"
body = "Dear {{salutation}},\n\nplease see the letter attached.\n"
mail = true
```

`file_name`, `subject` and `body` take placeholders like the template.
With `mail = false` the program writes only the letters, for post.

Changes in the code:

- **Only some contacts,** such as those with `yes` in a column
  `Invite`: in the program, after the line that reads `people`, keep
  only the contacts with `yes`:

  ```
  people = FILTER(LAMBDA p -> LCASE$(p{"invite"}) = "yes", people)
  ```
- **A placeholder that is not a column,** such as today's date: in the
  program, before the loop, add
  `DIM today$ = FORMAT_DATE(NOW(), "%d %B %Y")`, and at the top of the
  loop `person{"today"} = today$`. The template can then use
  `{{today}}`.
- **A different letter per group:** add a column `Template` with a file
  name, and in the loop pass
  `PATH.JOIN$(PATH.DIRNAME$(template$), person{"template"})` to
  `LETTERS.WRITE` instead of `template$`.
- **Copy to the account manager:** in `MAIL_FOR`, after
  `MAIL.MESSAGE`, add `MAIL.CC(m, person{"manager"})` and a column
  `Manager` to the sheet.

### When it goes wrong

- **"No column for: date"**: the template has a placeholder that the
  sheet does not fill. Add the column, or remove the placeholder.
- **A placeholder was not replaced** although the column exists: in
  the template it is written with a space or another bracket, such as
  `{{ name }}` or `{name}`. Write it exactly as `{{name}}`.
- **"No sheet ..."**: `tab` names a sheet the contact list does not
  have. Leave `tab` empty to use the first sheet.
- **The letters look odd in Word**: the formatting comes only from the
  template. Change fonts and spacing there; the program copies it as
  it is.

> **Balance dividend**
> Two invitations a year to forty customers, about two hours each
> time by hand, and a few smaller letters in between: five minutes a
> week on average, and no letter that still greets the customer
> before.
