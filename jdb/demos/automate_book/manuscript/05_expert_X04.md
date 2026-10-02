<!-- pagebreak -->

## X04 Ticket Sync

### The chore

Lena's team takes requests through a ticket tracker: a cost centre to
rename, a report that shows the wrong figure, a question from an
auditor. Lena herself plans her day in a plain to-do file, because it
is quick and it is hers. So every ticket lives twice. When a ticket is
assigned to her, she copies it into the file; when she ticks a task,
she opens the tracker to close the ticket; and when a colleague closes
one for her, the task stays open in her file until she notices. Half
an hour a week goes into keeping the two lists in step, and they are
still out of step most of the time.

### What you get

Ticket Sync reads your to-do file and the open tickets the tracker
has for you, and brings the two together:

- a ticket assigned to you that is not in the file becomes a task;
- a task you ticked closes its ticket in the tracker;
- a task you mark with `(#new)` becomes a new ticket, and the file
  gets its number;
- a ticket someone else closed is ticked in your file;
- a ticket that was renamed in the tracker renames its task.

Everything else in the file stays as you wrote it: headings, notes and
tasks without a ticket. A task belongs to a ticket when it ends with
the ticket number in brackets, such as `(#204)`.

### Before you start

You need the address of your tracker, your user name there and a
token, the kind of password a program uses for a tracker's REST
interface. Most trackers let you create one in your profile. Put it
into an environment variable once, in a command prompt:

```
setx TICKETS_TOKEN "the token from your profile"
```

New command prompts see it; the recipe asks for the token when the
variable is empty. The token is never written into `work.conf`.

The recipe talks to a simple REST interface: `GET /api/tickets` with
`assignee` and `state` lists the open tickets, `PATCH
/api/tickets/<number>` closes one, `POST /api/tickets` creates one.
Real trackers differ in their paths and field names. *Make it yours*
shows where to adapt that, in three functions of the module.

### The program

The program reads the settings and the file, and leaves the work to
the module:

<!-- include recipes/expert/X04_ticket_sync/ticket_sync.jdb -->

The module reads the file, asks the tracker, makes the plan and
carries it out:

<!-- include recipes/expert/X04_ticket_sync/ticketsync.jdb -->

### How it works

1. **A session.** `SESSION` creates a REQ session: the tracker's
   address, the token as a bearer header, a timeout of 30 seconds and
   three tries with a growing wait when the tracker answers that it is
   busy. Every call of the recipe goes through that session.
2. **The open tickets.** `OPEN_TICKETS` asks for the tickets of one
   person in the state *open* and keeps the number and the title of
   each. A number is kept as text, so `204` from the tracker and
   `(#204)` from the file compare as equal.
3. **The file as items.** `PARSE` turns every line into an item. A
   line that starts with `- [ ] ` or `- [x] ` is a task, with its title
   and, at the end, its ticket number. Any other line is text and is
   written back unchanged. `RENDER$` does the reverse, so a file read
   and written without changes comes out exactly as it went in.
4. **The plan.** `PLAN` walks through the tasks and decides, with the
   open tickets at hand. A ticked task whose ticket is still open
   needs a *close* call. A task marked `(#new)` needs a *create* call.
   An open task whose ticket is no longer open is ticked. Then every
   open ticket that no task names is added at the end of the file. The
   plan changes nothing yet; the calls for the tracker wait in
   `remote`.
5. **Carrying it out.** `APPLY` sends the calls. A *close* is a
   `PATCH` with the state *closed*; a *create* is a `POST`, and the
   number the tracker answers goes into the task. With `--dry-run` it
   only says what it would send.
6. **Writing the file.** Only when the file would change does the
   program write it, and it keeps the version before as
   `todo.txt.bak`. Each action is a line in `logs/ticket_sync.log`.
7. **Stopping cleanly.** When the tracker cannot be reached or refuses
   the token, `REQ.RAISE_FOR_STATUS` raises an error, the program
   prints it, logs it and ends with exit code 1. The file is not
   touched in that case.

### Run it

Lena's file before the run:

```
- [ ] Fix VAT rounding (#201)
- [x] Update cost centres (#202)
- [ ] Ask IT for SAP access (#new)
```

A dry run shows what would happen:

```
jdbasic ticket_sync.jdb --dry-run
would create Ask IT for SAP access
would write C:\Users\lena\Documents\AutomateWork\todo.txt
```

Ticket 202 is already closed in the tracker, so ticking it needs no
call. The real run creates the ticket and writes the file:

```
jdbasic ticket_sync.jdb
create Ask IT for SAP access as #206
wrote C:\Users\lena\Documents\AutomateWork\todo.txt
```

```
- [ ] Fix VAT rounding in the report (#201)
- [x] Update cost centres (#202)
- [ ] Ask IT for SAP access (#206)
- [ ] Check travel costs of Q3 (#204)
```

The first task took the title of its ticket, the new task has its
number, and ticket 204, which was assigned to Lena in the meantime,
is now a task. A second run right after finds nothing to do.

### Schedule it

Once the token is in the environment variable, set the schedule to
`every 2 hours` or `weekdays 08:00` in the wizard. Keep the to-do file
closed in your editor while the task runs, or save it first: the
recipe writes the file as it read it a moment before.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[ticket_sync]`:

```toml
[ticket_sync]
tracker = "https://tickets.example.com"
user = "lena"
todo = "~/Documents/AutomateWork/todo.txt"
token_env = "TICKETS_TOKEN"
```

- **Another tracker.** The paths and fields of the tracker are in
  three places of the module: the `GET` in `OPEN_TICKETS`, the `PATCH`
  and the `POST` in `APPLY`. For GitLab, for example, the list is
  `/api/v4/issues` with `assignee_username` and `state = "opened"`,
  and closing is a `PUT` on the issue with `state_event = "close"`. Change those
  lines and the field names `id` and `title` if your tracker calls
  them differently; the plan itself stays as it is.
- **A different marker.** If `(#new)` clashes with your notes, change
  the word `"new"` in `PLAN` to something else, such as `"ticket"`.
- **Only some tickets.** Add a filter to the query in `OPEN_TICKETS`,
  for example `query{"label"} = "controlling"`, so only one kind of
  ticket comes into the file.
- **The same file as the dashboard.** The default is the `todo.txt`
  of the Personal Dashboard (M08), so the open tickets also show up
  there.

### When it goes wrong

- **"answered 401" or "403"**: the token is wrong, expired, or lacks
  the right to change tickets. Create a new one with write access and
  put it into the variable again.
- **"transport failure"**: the address in `tracker` is wrong, or the
  tracker is only reachable inside the company network. Try the
  address in a browser on the same computer.
- **A task was ticked that you had not finished**: someone closed its
  ticket in the tracker. Untick the task first and then reopen the
  ticket. In the other order the next run sees a ticked task with an
  open ticket and closes the ticket again.
- **The file lost your last change**: you saved it while the recipe
  ran. The version before the run is in `todo.txt.bak`.

> **Balance dividend**
> About 30 minutes a week, and one list you can trust instead of two
> you have to compare.
