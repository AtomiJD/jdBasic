<!-- pagebreak -->

## Talking to IT and Security

Earlier chapters sent you here more than once: when Windows refuses to
start a program from your own folder, when you want to hand a compiled
recipe to colleagues, when a recipe talks to a service outside your
computer. This section is the conversation itself: what to show, what
IT will ask, and answers you can give without stretching the truth.

Most IT departments are not against automation. They are against
surprises: a program nobody approved, a password in a text file, a
port that is open to the whole network. The recipes of this book were
written to avoid exactly those, which makes the conversation shorter
than you might fear.

### When to ask

- **Before you install jdBasic on a work computer**, if your company
  allows only software that IT installs. Chapter 2 points here for that
  case.
- **Before you hand out a program**, compiled or not. A program you
  compiled yourself is not from a known publisher, and many companies
  allow only those.
- **Before a recipe talks to a company system** such as the ticket
  system of X04 or the calendar of X05, which needs an app registration
  or a token that IT issues.
- **Before a recipe sends company data to an outside service**, such
  as the language model of X02 and X17.
- **Before a recipe types or clicks for you**, as E16, M16 and X16 do.
  Some companies forbid programs that send key strokes.

### What to show

Bring a one-page description and the folder. The description answers
the questions below in your own words; the folder lets IT check them.

**The program.** jdBasic is one program, `jdbasic.exe`, with a few
libraries next to it. It needs no administrator rights and installs no
service. The recipes write into the folders named here and, while they
run, into the Windows folder for temporary files. The releases come
from the project page on GitHub; the *Digital Signatures* tab in the
file's properties shows who signed a release. jdBasic makes no network
connection of its own; only a program that asks for one does.

**Where things live.**

| What | Where |
|---|---|
| Settings | `Documents\AutomateWork\config\work.conf` |
| The recipes you use | `Documents\AutomateWork\recipes` |
| Shared modules | `.jdbasic\lib` in your user folder |
| Logs and reports | inside `Documents\AutomateWork` |
| Scheduled tasks | the folder *AutomateWork* of the Task Scheduler |

The scheduled tasks run under your own account, only while you are
logged in, with your rights and no more.

**What each recipe does.** Every recipe is a text file IT can read,
with a `recipe.toml` that names its program, its modules, its settings
and its schedule. Each one also has a test, and each one that changes
files has a dry run that shows what it would do.

**What talks to the network.** Most recipes touch only your own files.
These are the ones that reach further:

| Recipe | Talks to | When |
|---|---|---|
| E14 Page Watcher | the web pages you name | on its schedule |
| M01, M03, M06, M15, M18, X02, X09, X12, X17 | your mail server | only with `--send` |
| X19 Outlook Bridge | your mail server, through Outlook | only with `--send` |
| X02 Inbox Assistant | a language model service, or one on your computer | when you run it |
| X17 Meeting Briefing | the same, unless `use_model = false` | on its schedule |
| X04 Ticket Sync | the ticket system | when you run it |
| X05 Calendar Bridge | Microsoft 365 | on its schedule |
| X11 Web Harvester | the web pages you name | on its schedule |
| X07 Tools for Colleagues | the release folder or an address inside the company | on `check` and `update` |
| X13 Health Checks | your mail server | only with `send_alerts` on |

**What listens.** The Personal Dashboard (M08), the search page of
Find Anything (X03), the Balance Score (X15) and the Spreadsheet to Web
App (X18) show a page in your browser. They listen on `localhost` only,
on a port set in `work.conf`; no other computer can open them. The AI tools of X10 run
over a pipe between the agent and jdBasic, not over the network, and
they start with `--tools-only`, so the agent sees only the tools you
wrote and none of jdBasic's own.

**What drives other programs.** A few recipes work through programs
you already have instead of through files. E18 Office to PDF, M18
Excel Refresh and X19 Outlook Bridge start Word, Excel or classic
Outlook through COM, the interface Office offers other programs for
this purpose. They do what you could do by hand, under your account
and with your rights. E16 Form Filler, M16 Macro Player and X16 Desktop
Robot type and click into windows through the same Windows input a
keyboard uses. They stop as soon as the mouse is pushed into the
top-left corner of the screen, and they belong on a screen that nobody
else can see or use. Outlook may ask before a program sends mail
through it; whether that question stays is for IT to decide.

**Passwords and keys.** No recipe writes a password into `work.conf`
or any other file of the work folder.

- Mail goes into an outbox first. Sending asks for the password, which
  the prompt does not show, or reads it from the Windows Credential
  Manager if you stored it there yourself under `AutomateWork/mail`.
- The Credential Manager keeps entries encrypted for your Windows
  account. Recipe X14 stores and lists them, and every entry of the
  book starts with `AutomateWork/`, so IT can see them all in one
  place.
- Keys for outside services come from environment variables, such as
  `OPENAI_API_KEY` for X02 or `TICKETS_TOKEN` for X04, or from a
  prompt.
- The Calendar Bridge (X05) keeps its refresh token in
  `.automatework\calendar_token.json` in your user folder, outside the
  work folder, because it has to survive a restart. That file is as
  sensitive as a password, and its page says so.

### Questions IT will ask

**"Who maintains this when you are away?"** You, and the second owner
the previous section asked for. Show the page of text that comes with
each recipe and the monthly check of this chapter.

**"Can we see the code?"** Yes, all of it. Every recipe is a plain
text file, and this book prints and explains each one.

**"Is the program you hand out signed?"** A program you compiled with
X07 is not signed unless IT signs it. Some companies sign approved
internal tools with their own certificate; ask whether yours does. The
checksums that X07 writes show that a copy was not changed on the way,
but they do not replace a signature.

**"Will our antivirus complain?"** It may. A new program from an
unknown publisher is exactly what such software looks at more closely.
Ask IT for the approved way: a signed build, an approved folder, or an
exception for the release folder on the shared drive. Do not look for
a way around it.

**"What data leaves the computer?"** Only what the table above lists,
and only for the recipes you switched on. For X02 and X17 the text of
your mail goes to the language model service you configured, unless
you run a model on your own computer; that is a question for your data
protection rules before you start, not after.

**"What if it goes wrong?"** The recipes do not overwrite files, show
their plan with `--dry-run` before they act, and stop with a message
instead of guessing. Mail waits in the outbox until a person sends it.
The worst case for most recipes is a chore that did not happen.

**"Why not use the tools we already have?"** A fair question. If IT
offers a tool that does the same job, such as rules in the mail
program or a flow in a platform the company already pays for, compare
them honestly. The recipes are small and readable; a supported tool
has a team behind it. Sometimes the answer is to use the recipe as the
precise description of what the supported tool should do.

### A first mail

A short mail is often all it takes to start. Lena sent this one before
she set up the job server:

```text
Subject: Request: small automation scripts on my work PC

I would like to run a few small scripts that tidy my folders and
prepare reports. They use jdBasic, a single program without
installer or admin rights, from github.com/AtomiJD/jdBasic.
Everything runs under my account and stays in Documents\AutomateWork.
No passwords are stored in files. Two scripts show a page in my
browser on localhost only. One sends mail through our server when I
start it by hand.

I can show you the folder and the code at any time. Is this fine,
or is there a process I should follow?
```

### If the answer is no

Then the answer is no, and you run nothing on that computer. The work
of this book is not lost: the audit of Chapter 1, the ledger and the
recipe pages describe precisely which chores cost you time and what a
tool would have to do. That is a good basis for a request for a tool
IT does support, and for the next conversation in half a year.