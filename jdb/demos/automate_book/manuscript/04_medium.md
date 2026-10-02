# Chapter 4: Medium: Fifteen Programs for Documents, Mail and Data

Jonas leads a team of eight. His week is made of sheets: the hours of
his people, the budgets of three projects, the invoices for two
customers, the vacation plan, the Friday status that the head of the
department reads on Monday. None of it is hard. All of it takes time,
and most of it is copying numbers from one place to another and
checking that nothing went missing on the way.

The recipes of this chapter take over that kind of work. They read and
write the files an office runs on: Excel workbooks, Word documents, PDF,
calendars, mail. Each one does a job that is worth an hour or two a
week, and each one is a program of 80 to 200 lines that you will read
and change.

## What changes at the medium level

At the easy level you changed settings. Here you change code. The
recipes are written so that the parts you are likely to change sit
together and are easy to find, and every recipe's *Make it yours*
section names the lines and shows the change. You do not need to
understand every line before you start; you need to know where the
line is that decides the thing you want different.

Three habits make that safe:

1. **Copy before you change.** Keep the recipe folder as it came, work
   on a copy, and switch the wizard over when the copy works. If you
   know git, a repository for your work folder is better still.
2. **Run the test.** Every recipe comes with a file that ends in
   `_test.jdb`. Run it after each change with `jdbasic` like any other
   program. It prints one line per check and ends with how many passed.
   A change that breaks a check shows you the line and what it expected.
3. **Run it dry.** As before, `--dry-run` shows what the program would
   do. With real documents of your own, look at that list before you
   let it act.

## Mail without passwords in files

Several recipes of this chapter write mail: invoices, letters, the
Friday status, the timesheet for your boss. None of them stores a
password, in `work.conf` or anywhere else. They follow one rule
instead:

- A recipe that runs on its own builds the messages and puts them as
  `.eml` files into the folder `outbox` in your work folder. Any mail
  program opens such a file, so you can look at each message before it
  goes out.
- When you run the recipe yourself with `--send`, it asks for your
  mail password, sends what is in the outbox, and moves every sent
  message into `outbox/sent`.

The password lives in memory for the few seconds the sending takes and
is never written down. If your company does not allow programs to send
mail with your account at all, the outbox alone still saves most of the
work: open each file and press *Send* in your mail program.

The mail server settings go into `work.conf` once, in the part that
belongs to you:

```toml
[mail]
server = "smtps://mail.example.com:465"
user = "jonas@example.com"
from = "Jonas Example <jonas@example.com>"
```

Your IT department knows the server name and port; many offices use
port 465 with `smtps`, or 587 with `smtp` and STARTTLS.

## The recipes

| Recipe | What it takes off your desk |
|---|---|
| M01 Invoice Generator | Invoices from the rows of a sheet, as Word or PDF, ready to mail |
| M02 Report Merger | The monthly sheets of the team in one workbook with totals |
| M03 Mail Merge | Personal letters from a Word template and a contact list |
| M04 Receipt Sorter | Receipts filed by month and category, and the expense report |
| M05 Shift and Vacation Planner | The team calendar as a file every calendar program reads |
| M06 Friday Status Mail | The weekly status from your notes, as a mail to the team |
| M07 Inbox Unpacker | Attachments of saved mails filed by sender and subject |
| M08 Personal Dashboard | Your numbers on one page in the browser |
| M09 Log Detective | Errors per hour from a log file, as a chart |
| M10 Hot Folder | Whatever lands in a folder processed by rules |
| M11 Data Cleaner | Messy exports made consistent, with a report of what changed |
| M12 Meeting Cost Meter | What your meetings cost in hours and money |
| M13 Snippet Tool | The text blocks you type ten times a day |
| M14 Contract Diff | A readable report of what changed between two Word files |
| M15 Timesheet for the Boss | The monthly timesheet from the time tracker, mailed |

The order is a suggestion, not a path. Each recipe stands on its own;
where one builds on another, such as the timesheet on the time tracker
of E07, the text says so.
