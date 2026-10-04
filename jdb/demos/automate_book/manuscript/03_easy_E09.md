<!-- pagebreak -->

## E09 Meeting Notes Starter

### The chore

Mia takes the notes in most project meetings. Before each one she opens
Word, types the title of the meeting, looks up the time and the room in
the calendar, copies the names of the attendees from the invitation and
adds the headings she always uses. It takes five minutes, three or four
times a week, and on a busy morning the meeting has started before the
file is ready.

### What you get

Every weekday morning the Meeting Notes Starter looks at your calendar
and finds the next meeting. It writes a Word file named after the date
and the title, such as `2026-10-05 Miller project status.docx`, with the
time, the place, the attendees, the agenda from the invitation and an
empty table for the actions. When you walk into the room, the file is
already in your notes folder.

### Before you start

The recipe reads a calendar file in the iCalendar format (`.ics`). Outlook
exports one with *File, Save Calendar*; Google Calendar gives you one under
*Settings, Import and export*. Save it as `calendar.ics` in your work
folder, or point the setting `calendar` at the file your calendar program
keeps up to date.

### The program

The program the wizard runs reads the settings, asks for the next meeting
and writes the file, unless a file of that name is already there:

<!-- include recipes/easy/E09_meeting_notes/meeting_notes.jdb -->

The module does the work. It reads the calendar with the ICAL library,
which also expands repeating meetings such as a weekly team call, and it
writes the Word file with the DOCX library:

<!-- include recipes/easy/E09_meeting_notes/meetnotes.jdb -->

### How it works

1. `ICAL.PARSEFILE` reads the calendar file. `ICAL.EXPAND` turns it into
   the list of meetings in the next days, a weekly series included, in
   the order they start.
2. `NEXT_MEETING` takes the first one that has not started yet and leaves
   out all-day entries such as holidays, which are not meetings.
3. The attendees come from the `ATTENDEE` lines of the invitation:
   `SELECT(Address$@, who)` turns every line into an address. The
   agenda comes from the description, one point per line: `SELECT`
   trims the lines and `FILTER` drops the empty ones. An invitation
   without a description gets three headings: Updates, Decisions, Next
   steps.
4. `FILE_NAME$` builds the name from the date and the title and leaves out
   the characters Windows does not allow in a file name, such as `:` and
   `?`.
5. `WRITE_NOTES` writes the document: a heading, the time in your own
   clock, the place, the attendees as a list, the agenda as a numbered
   list, a space for notes and a table for the actions.

### Run it

```
jdbasic meeting_notes.jdb --dry-run
would write 2026-10-05 Miller project status.docx
   in C:\Users\mia\Documents\Meeting notes
```

Without `--dry-run` the file is written. When it is already there, the
program says so and leaves your notes alone.

### Schedule it

The wizard plans the Meeting Notes Starter for every weekday at 7:30. If
your first meeting is often earlier, choose an earlier time on the page
"Schedule".

### Make it yours

The settings are in the part of `work.conf` that starts with
`[meeting_notes]`:

```toml
[meeting_notes]
calendar = "~/Documents/AutomateWork/calendar.ics"
folder = "~/Documents/Meeting notes"
lookahead_days = 7
utc_offset = 2
```

- **Keep notes per project**: point `folder` at the project folder on
  your team drive.
- **Look further ahead**: `lookahead_days = 14` finds the next meeting
  even after a holiday.
- **Your own clock**: `utc_offset` is the difference to UTC in hours. Leave
  it out and the program takes the one of your computer.

### When it goes wrong

- **"No meeting in the next 7 days"**: the calendar file is old. Export
  it again, or let your calendar program write it on its own.
- **The times are one hour off**: set `utc_offset` to the offset of your
  summer or winter time.
- **Attendees appear as mail addresses**: the recipe shows the address of
  each attendee, since that is what every invitation carries.

> **Balance dividend**
> About 20 minutes a week, and every meeting starts with your notes
> already open.
