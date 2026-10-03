<!-- pagebreak -->

## Bonus: X19 Outlook Bridge

### The chore

Lena's company runs on Outlook. Her calendar is there, her mail is
there, and the mail server only takes mail from Outlook, so the
outbox of Chapter 4 has no server to talk to. The Meeting Briefing
(X17) wants her day as an `.ics` file and her mails as `.eml` files.
Each morning she saves the calendar under *File, Save Calendar* and
drags the latest mails into a folder, and on the mornings she forgets,
the briefing is about yesterday.

### What you get

Each weekday at seven the Outlook Bridge reads today's appointments
and your newest mails from Outlook and writes them where the other
recipes look: the appointments as `calendar.ics`, the mails as one
`.eml` file each in `saved mail`. Half an hour later the Meeting
Briefing finds both. And with one line in `work.conf`, every recipe
that puts mail into the outbox sends it through Outlook, from your own
account, with the copy in your *Sent Items*.

### Before you start

You need classic Outlook for Windows, the program that comes with
Microsoft Office, set up with your account. The recipe only reads
from Outlook; it never changes, moves or deletes an appointment or a
mail. To send through Outlook, add one line to the `[mail]` part of
`work.conf`:

```toml
[mail]
send_with = "outlook"
```

From then on `--send` of any recipe hands the outbox to Outlook
instead of a mail server, and no password is asked for. Without the
line, the outbox works as Chapter 4 describes.

> **Watch out**
> Only classic Outlook works. The new Outlook for Windows, and Outlook
> on the web, have no interface that other programs can use. If your
> Outlook has a switch *New Outlook* in the corner, it must be off.
> The recipe runs under your own account while you are logged in,
> from the Task Scheduler, not as a Windows service. Some companies
> set Outlook to ask before another program reads addresses or sends
> mail. Then Outlook shows a question the first time, and a run at
> seven in the morning waits for an answer nobody gives. Ask IT
> whether that warning is on before you schedule the recipe.

### The program

The program sends the outbox with `--send`; otherwise it reads the
calendar and the inbox and writes the files:

<!-- include recipes/expert/X19_outlook_bridge/outlook_bridge.jdb -->

The module reads Outlook and writes the open formats:

<!-- include recipes/expert/X19_outlook_bridge/olbridge.jdb -->

### How it works

1. `CREATEOBJECT("Outlook.Application")` connects to Outlook. If it
   is already open, the recipe uses it; if not, Outlook starts in the
   background. The recipe never quits it, so an Outlook you have open
   stays open.
2. `OLBRIDGE.EVENTS` opens the default calendar and asks it with
   `Restrict` for the appointments that overlap today. Outlook reads
   the dates in that filter in your Windows date format, such as
   `05.10.2026 00:00` in Germany or `10/5/2026 00:00` in the United
   States; `OFFICEKIT.SHORT_DATE$` finds that format and
   `OFFICEKIT.LOCAL_DATE$` writes it. `IncludeRecurrences` makes a
   weekly meeting show up as today's occurrence.
3. For each appointment it keeps the subject, the times, the place and
   the people with their names and addresses. A colleague in the
   company address book has an internal Exchange address; the recipe
   asks for the normal mail address instead, the one X17 looks for in
   your mails.
4. `OLBRIDGE.ICS$` writes the appointments with the library ICAL,
   one `ATTENDEE` line per person with the name in `CN`, as Outlook's
   own export does.
5. `OLBRIDGE.MAILS` goes through the inbox from the newest mail on and
   keeps sender, recipients, subject, text and the time it arrived.
   Meeting requests and read receipts are left out. `OLBRIDGE.EML$`
   builds an `.eml` file of each with the library MAIL, and
   `OLBRIDGE.FILE_NAME$` names it after its time and subject. A mail
   that is already in the folder is not written again.
6. With `--send`, `OUTBOX.SEND_OUTLOOK` reads each `.eml` file of the
   outbox and lets `OFFICEKIT.OUTLOOK_SEND` create a new Outlook mail
   with the same recipients, subject, text and attachments, and send
   it. A mail that went out moves to `outbox/sent`, as with a mail
   server.

### Run it

See what would happen first:

```
jdbasic outlook_bridge.jdb --dry-run
would read 1 day(s) of appointments from 2026-10-05
  into C:/Users/lena/Documents/AutomateWork/calendar.ics
would save the newest 50 mails of the inbox
  into C:/Users/lena/Documents/AutomateWork/saved mail
```

Then without `--dry-run`:

```
jdbasic outlook_bridge.jdb
4 appointment(s) in C:/Users/lena/Documents/AutomateWork/calendar.ics
50 new mail(s) in C:/Users/lena/Documents/AutomateWork/saved mail
```

Open `calendar.ics` with a text editor to see what Outlook gave, and
run the Meeting Briefing after it. To try sending, put one mail to
yourself into the outbox, run `jdbasic outlook_bridge.jdb --send`, and
look into *Sent Items*.

### Schedule it

The wizard plans it on weekdays at seven, half an hour before the
Meeting Briefing, and leaves it off at first because it needs classic
Outlook. Switch it on in the wizard once a run by hand worked.

### Make it yours

The settings are the `[outlook_bridge]` part of `work.conf`:

```toml
[outlook_bridge]
calendar_file = "~/Documents/AutomateWork/calendar.ics"
mail_folder = "~/Documents/AutomateWork/saved mail"
days = 1
mails = 50
```

- **The week ahead.** Set `days = 7`, and the calendar file holds the
  next seven days, for the Calendar Bridge (X05) or your phone.
- **Calendar only.** Set `mails = 0`, and the inbox is not read.
- **Other folders.** The inbox and the calendar are Outlook's default
  folders. `GetDefaultFolder` in `EVENTS` and `MAILS` names them by a
  number; a folder of your own is found with `Folders` by its name.

### When it goes wrong

- **"COM: Cannot find ProgID 'Outlook.Application'"**: classic Outlook
  is not installed, or only the new Outlook is. The recipe cannot work
  with the new one.
- **The run hangs**: Outlook is waiting for an answer, either the
  security question above or a dialog asking for your password. Look
  for an Outlook window, answer it, and talk to IT about the warning.
- **"0 appointment(s)" although your day is full**: the appointments
  are in another calendar than the default one, for example a shared
  team calendar. See *Other folders* above.
- **"not sent: invoice.eml: ..."**: Outlook refused that mail, most
  often because an address is not valid. The mail stays in the
  outbox; correct it and send again.

> **Balance dividend**
> About 20 minutes a week for anyone whose calendar and mail live in
> Outlook, and the other recipes of this book in a company where
> Outlook is the only way out.
