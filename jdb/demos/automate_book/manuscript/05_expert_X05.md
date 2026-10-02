<!-- pagebreak -->

## X05 Calendar Bridge

### The chore

Lena's family plans the week in a shared calendar on their phones.
Her work meetings live in Outlook at the office, and the two never
meet. So on Sunday evening she copies the important ones by hand,
forgets the ones that move during the week, and on Tuesday her
partner books the dentist into the budget review. Copying the whole
work calendar is no answer either: the subjects of her meetings are
nobody's business at home, and some of them are confidential.

### What you get

A calendar file with your work meetings of the next two weeks as busy
blocks. Each block has the right start and end and says *Busy
(work)*, nothing more. Private meetings never show their subject,
cancelled ones and time marked as free are left out, a tentative
meeting says so. The program runs every two hours, so a meeting that
moves at work moves at home as well.

The program reads the work calendar from Microsoft 365 with the
access you grant it once, and writes the file. Your personal calendar
subscribes to that file.

### Before you start

The bridge needs to be allowed to read your work calendar. That is
granted through OAuth, the sign-in that shows "this app wants to read
your calendar" and asks you to agree. For that, your IT department
registers the bridge once as an app in Microsoft Entra ID (the
directory behind Microsoft 365). Ask for:

- an app registration with the delegated permission
  `Calendars.Read` and `offline_access`,
- *Allow public client flows* switched on, because the bridge signs
  in with a device code,
- the *Application (client) ID* of that registration.

The client id goes into `work.conf`. It is no secret; it only names
the app. Many companies let you register such an app yourself, and
it costs nothing. If your IT registers it as a confidential app with
a client secret, that secret goes into the environment variable
`AUTOMATEWORK_CALENDAR_SECRET`, never into a file.

On the personal side you need a calendar that subscribes to a file.
Thunderbird does this with a file on your computer; a phone calendar
needs the file in a cloud folder it can reach. If your calendar can
only import, import the file once a week instead.

> **Watch out**
> After you sign in, the bridge keeps a *refresh token*: a key that
> lets it read your calendar without asking you again. It is kept in
> `.automatework/calendar_token.json` in your user folder, outside
> the work folder, so it is not copied with your settings or picked
> up by a backup of `AutomateWork`. Treat this file like a password.
> Delete it to sign the bridge out, or remove the app in your account
> settings. X14 shows how to keep such a token in the protected store
> of Windows instead of a file.

### The program

The program reads the settings, signs in or uses the kept tokens,
reads the calendar and writes the file:

<!-- include recipes/expert/X05_calendar_bridge/calendar_bridge.jdb -->

The module does the talking to Microsoft and the turning into blocks:

<!-- include recipes/expert/X05_calendar_bridge/calbridge.jdb -->

### How it works

1. `CLIENT` makes an OAuth client from `lib/oauth.jdb` with the three
   addresses of the sign-in service: one to ask for a device code,
   one to trade the code for tokens, one to authorize in the browser.
2. `KEEP` connects the client to the token file. `OAUTH.STORE` takes
   any tokens already in the file, and every new token the client
   gets is put into the same store; `SAVE` writes it back with
   `CACHE.SAVE`. The program saves after every run, also after one
   that failed, because the sign-in service hands out a new refresh
   token from time to time and the old one stops working.
3. `LOGIN` runs the device code flow: `OAUTH.DEVICESTART` asks for a
   short code and an address, the program prints both, you open the
   address on any device and type the code, and `OAUTH.DEVICEPOLL`
   waits until you have agreed. No password passes through the
   program.
4. `FETCH` asks the Microsoft Graph API for the events between two
   instants at `/me/calendarView`. The `Prefer` header asks for all
   times in UTC, so the bridge never has to know about time zones.
   `OAUTH.GET` puts the access token on each request and gets a new
   one with the refresh token when it has run out.
5. The API answers in pages of up to 100 events, each with the
   address of the next page in `@odata.nextLink`. `FETCH` follows
   them. Each page goes through `RETRY.ATTEMPT`: when the server
   answers 429 (too many requests) or a 5xx error, or does not answer
   at all, `GetPage` raises an error that starts with *transient*,
   and `Transient` tells RETRY to try again after 2, 4 and 8 seconds.
   Any other answer, such as a revoked sign-in, stops at once with
   the message of the server.
6. `Event` turns each API event into a small map with the times as DT
   instants (seconds since 1970 in UTC), and `BLOCKS` decides what
   your family sees. The id of each block is a hash of the event's id,
   so the same meeting has the same id in every run and a calendar
   moves it instead of adding a second one.
7. `ICS$` writes the blocks with ICAL: a meeting as an event from
   start to end, an all-day event as a day, marked busy.
8. `CHANGES` reads the file of the last run and counts the blocks that
   are new, that moved or that went, for the log and the dry run.

### Run it

Sign in once:

```
jdbasic calendar_bridge.jdb --login
To sign in, use a web browser to open the page
https://microsoft.com/devicelogin and enter the code KJ7Q2ZPRT to
authenticate.
Signed in. The tokens are kept in
  C:\Users\lena\.automatework/calendar_token.json
```

Then look before you write, and write:

```
jdbasic calendar_bridge.jdb --dry-run
Mon 05.10. 09:00  Busy (work)
Mon 05.10. 14:00  Busy (work)
Tue 06.10. 13:00  Busy (work) (tentative)
Thu 08.10. all day  Busy (work)
4 new, 0 changed, 0 gone; nothing written
jdbasic calendar_bridge.jdb
```

The log `bridge.log` next to the calendar file has a line per run.

### Schedule it

The wizard runs the bridge every two hours while you are logged on.
A run takes a second or two. If the computer was off, the next run
catches up, because each run writes the whole two weeks again.

### Make it yours

The settings are the `[calendar_bridge]` part of `work.conf`:

```toml
[calendar_bridge]
client_id = "8f2c51d7-0a4e-4c55-9b1e-3d6e2a7f9c10"
tenant = "organizations"
days = 14
label = "Busy (work)"
show_titles = false
output = "~/OneDrive/Calendar/work-busy.ics"
```

With `show_titles = true` your blocks carry the meeting subjects,
except for private and confidential meetings, which always stay
*Busy (work)*.

Changes in the code:

- **Only the core hours.** To leave out early and late meetings, add
  a condition in `BLOCKS` next to the one for free time, for example
  with `DT.FMT$(ev{"start"}, "%H", 2)` for the hour in UTC+2.
- **Mark home office days.** If you mark them with an all-day event
  called *Home office*, let `BLOCKS` give such events their subject
  even when `show_titles` is off. Put this line before the one that
  checks for `"tentative"`:

  ```
  IF ev{"subject"} = "Home office" THEN summary$ = "Home office"
  ```

- **Another calendar service.** `CLIENT` takes the address of any
  OAuth server, and the settings `authority` and `api` override the
  Microsoft ones. A different service also answers in a different
  shape, so `Event` and the page loop in `FETCH` would change with it.

### When it goes wrong

- **"Set client_id in [calendar_bridge]"**: the client id from your
  IT is missing in `work.conf`.
- **"Not signed in yet"**: no token file, or it was deleted. Run
  `--login` once.
- **"OAUTH: invalid_grant"**: the sign-in has run out or was revoked,
  after a password change for example. Run `--login` again.
- **"The calendar answered 403"**: the app registration lacks the
  `Calendars.Read` permission, or your company requires an admin to
  approve it. Ask your IT.
- **The personal calendar shows old blocks**: many calendar apps
  read a subscribed calendar only every few hours. The file itself is
  current; look at the time in `bridge.log`.

> **Balance dividend**
> About 25 minutes a week of copying meetings, and no more evenings
> booked twice.
