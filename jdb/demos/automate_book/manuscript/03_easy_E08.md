<!-- pagebreak -->

## E08 Mail Templates

### The chore

Mia writes the same five mails every week: thanks for an order, the
notes after a meeting, the note that she is away. Each time she looks
for last week's version in her sent mail, copies it, changes the name
and the date, and once a month sends one with the wrong name in it.

### What you get

Your standard replies live as small text files with placeholders such
as `{{ to }}` and `{{ date }}`. One command fills a template with the
name of the person it goes to, today's date and your own values, shows
the mail and puts it on the clipboard. You paste it into your mail
program and send it.

### Before you start

The setup wizard has run, copied three example templates into
`Documents\AutomateWork\templates` and asked for your name. There is
nothing to schedule: you run this recipe when you write a mail.

### The program

The program fills the template you name and copies the result:

<!-- include recipes/easy/E08_mail_templates/mail_templates.jdb -->

The module finds the templates, fills them and turns the result into
plain text:

<!-- include recipes/easy/E08_mail_templates/mailtpl.jdb -->

One of the templates that come with the book:

<!-- include recipes/easy/E08_mail_templates/templates/thanks.txt -->

### How it works

1. `NAMES` lists the `.txt` files of the templates folder; without a
   template name the program shows that list.
2. `VALUES` collects what a template can use: `me` and `signature`
   from `work.conf`, `to` from the command line, and the date written
   out, such as `2 October 2026`, with its weekday.
3. Your own values from `work.conf` join them, so a template can use
   any name you define there.
4. `MISSING` warns about placeholders that have no value, before the
   mail goes out with a gap in it.
5. `FILL$` lets the TMPL library fill the template. TMPL was written
   for web pages and writes `&` as `&amp;`; `PLAIN$` turns those back,
   since a mail is plain text.
6. `CLIPBOARD.SET` puts the mail on the clipboard.

### Run it

```
jdbasic mail_templates.jdb thanks Ms Miller
Dear Ms Miller,

thank you for your order. We have it in our system and will confirm
the delivery date by Friday at the latest.

Kind regards
Mia Schmidt
(on the clipboard, paste it into your mail)
```

Without a template name it lists the templates; with `--dry-run` it
shows the mail and leaves the clipboard alone.

### Schedule it

Nothing to schedule. Put the command on a desktop shortcut for the
template you use most, or run it from a console window.

### Make it yours

```toml
[mail_templates]
folder = "~/Documents/AutomateWork/templates"
me = "Mia Schmidt"
signature = "Kind regards"

[mail_templates.values]
back = "Monday, 12 October"
deputy = "Jonas Becker"
```

- **A new template**: save a text file such as `invoice.txt` in the
  templates folder. Its name is the command:
  `mail_templates.jdb invoice Ms Miller`.
- **Your own placeholders**: every line under
  `[mail_templates.values]` becomes a placeholder. The away note uses
  `{{ back }}` and `{{ deputy }}`.
- **Different greetings**: a template can choose with a condition,
  such as `{% if to %}Dear {{ to }},{% else %}Hello,{% endif %}`.

### When it goes wrong

- **"No value for: back, deputy"**: the template uses placeholders
  that `work.conf` does not define. Add them under
  `[mail_templates.values]`.
- **"No template invoice"**: the message lists the templates there
  are; check the spelling and that the file ends in `.txt`.
- **The clipboard is empty**: some remote desktop sessions block it.
  The mail is printed as well; copy it from the window.

> **Balance dividend**
> About 25 minutes a week of searching and editing old mails, and no
> more replies that greet the wrong person.
