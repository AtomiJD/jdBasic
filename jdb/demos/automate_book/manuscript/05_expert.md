# Chapter 5: Expert: Fifteen Programs That Run on Their Own

Lena works in the controlling department of a company with four
hundred people. Over two years she has written a dozen small programs
for her team: one fetches the bookings every night, one builds the
Monday report, one sorts the invoices that arrive by mail. They work.
They also break now and then, always on a day she is away, and then
somebody calls her because the report did not come.

This chapter is about the step from a program that works when you run
it to a program that runs without you. The recipes are larger, between
150 and 400 lines with their modules, and most of them are not single
chores but parts of a small system: a server that starts every job on
time, checks that tell you when one failed, a place for passwords that
is not a text file, a window for colleagues who never open a command
prompt.

## What changes at the expert level

A program that runs on its own has nobody watching it. Everything a
person would notice has to be written down or handled by the program
itself. Four habits run through every recipe of this chapter:

1. **Every run leaves a line in a log.** When it started, what it did,
   how it ended. A log line costs nothing, and it is the only witness
   you have of a run at three in the morning.
2. **Failures are loud, and only the right ones are retried.** A web
   server that does not answer may answer in a minute, so the program
   tries again with a pause that grows. A missing file or a wrong
   password will not get better; the program stops and says so, and a
   health check sends you a mail.
3. **Secrets stay out of files.** API keys, tokens and passwords come
   from the protected store of Windows, from an environment variable
   or from a prompt. None of them is written into `work.conf` or into
   a recipe folder, where a backup or a colleague might copy it.
4. **A test runs before a change goes live.** Each recipe has its test
   as before. Several of them also test the programs of the earlier
   chapters, so that a change you make on Thursday does not break the
   report on Monday.

## Talking to other systems

Most recipes of this chapter talk to something: a ticket system, a
calendar service, a language model, a web site. Three rules keep that
safe for you and polite towards the other side:

- The services the recipes start themselves, such as a search page or
  a dashboard, listen only on `localhost`. Nobody else on the network
  can open them.
- A recipe that reads from somebody else's server asks for as little
  as it needs, waits between requests, and keeps what it fetched in a
  cache so it does not ask twice.
- Every test talks to a small fake server that the test starts on your
  computer. No test needs an account, a key or the internet, so you
  can run all of them on the train.

Where a recipe needs an account with an outside service, the page says
which one, what it costs if anything, and what to ask your IT
department before you use it at work.

## The recipes

| Recipe | What it takes off your desk |
|---|---|
| X01 Personal Job Server | Every automation started on time, with locks, retries and a log |
| X02 Inbox Assistant | Summaries of new mail and reply drafts you approve one by one |
| X03 Find Anything | A search page over all your documents |
| X04 Ticket Sync | Tasks kept the same in the ticket system and your to-do list |
| X05 Calendar Bridge | Your work calendar and your personal one kept in step |
| X06 Work Cockpit | A window with a button for every automation and its last result |
| X07 Tools for Colleagues | Recipes as programs your colleagues can run without jdBasic |
| X08 Parallel Crunching | Thousands of files processed on all cores |
| X09 Report Pipeline | Database to charts to Word, PDF and mail, every Monday at seven |
| X10 Let an AI Drive | Your automations offered as tools to an AI assistant |
| X11 Web Harvester | Data from web pages, collected politely |
| X12 Tested Automations | Tests that keep a change from breaking Monday's report |
| X13 Health Checks and Alerts | A mail when a job fails, and numbers for every job |
| X14 Secrets and Audit Trail | Passwords out of scripts, and a signed record of each run |
| X15 Balance Score | Your week in one number, and the trend |
| X16 Desktop Robot (bonus) | An old program driven by keyboard and mouse, checked by screenshots |
| X17 Meeting Briefing (bonus) | One page per meeting of the day, with mails, open items and a summary |
| X18 Spreadsheet to Web App (bonus) | An Excel table as a small app in the browser, saved back to Excel |

X01 is the backbone: the other recipes can run under it instead of the
Windows Task Scheduler. X13 and X14 make that backbone safe to rely on.
Read those three first if you plan to run more than a handful of jobs;
the others stand on their own.
