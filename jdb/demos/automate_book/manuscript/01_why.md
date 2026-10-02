# Chapter 1: Why Automate: Time, Focus and Balance

This is the only chapter of the book without a program in it, apart from
one small example at the end. It asks three questions before any code is
written. Where does your week go? Which parts of it should a program do
for you, and which should stay yours? And what will you do with the time
that comes back?

The last question sounds like the easiest. In practice it is the one
people skip, and it is the reason many automation projects leave their
owners exactly as busy as before. The chapter ends with a simple tool,
the life balance ledger, that keeps the third question in front of you
for as long as you use the recipes of this book.

## The Hidden Week

Ask someone what they did last week and you will hear about the
meetings, the project that went live and the customer who called twice.
You will rarely hear about the forty times they renamed a file, the
half hour they spent looking for an offer they knew they had received,
or the Friday afternoon that went into copying numbers from five sheets
into a sixth. These chores are invisible because each one is small.
None of them takes long enough to be remembered. Together they fill a
surprising part of the week.

### Why small chores hide

A chore that takes two minutes does not feel like work. It feels like
the space between pieces of work. You do it while you think about
something else, or while a call connects, or at the end of the day when
you no longer have the energy for anything that needs thought. That is
exactly why it is hard to see. Your memory of the week is built from the
things that needed your attention, and these did not.

There is a second reason. A chore that happens a few times a day is
spread over the whole week. Nobody does forty file renames in one
sitting; they do one here and two there. The total only appears when you
count.

So the first step of this book is counting.

### The self-audit

For one ordinary week, keep a list of the small recurring things you do
at the computer. You do not need to time them to the second. A rough
guess written down at the moment is far better than a careful estimate
made on Friday from memory.

Keep the list next to you, on paper or in a text file, and write a line
whenever you notice yourself doing something you have done before. Use
these columns:

| Activity | How often | Minutes each | Minutes per week | Could a program do it? |
|---|---|---|---|---|
| | | | | |

- **Activity**: what you did, in a few words. "Saved the invoice from
  the mail into the right folder" is better than "admin".
- **How often**: per day or per week, whichever is natural.
- **Minutes each**: your honest guess. Round up rather than down.
- **Minutes per week**: how often times minutes each, filled in at the
  end of the week.
- **Could a program do it?**: yes, no or partly. Do not think long
  about this column yet; the next section gives you a test.

Some activities will only appear once in your week and you will be
tempted to leave them out. Write them down anyway, with a note on how
often they come back: monthly reports, quarterly lists, the yearly
inventory. A chore that costs three hours once a quarter costs about
fifteen minutes a week, and it often costs more in the days before it,
when you know it is coming.

> **Try this**
> Put a sticky note on the edge of your screen for the week of the
> audit. A note you can see reminds you far more often than a file you
> have to open.

### Mia's week

Mia is a project assistant in a small engineering office. She keeps the
documents of three projects in order, prepares meetings, handles the
first round of questions from customers and suppliers, and keeps track
of who worked how long on what. Here is her audit after one week. The
numbers are hers, estimated at the moment and added up on Friday.

| Activity | How often | Minutes each | Minutes per week | Could a program do it? |
|---|---|---|---|---|
| Finding a file in Downloads | 12 a week | 3 | 36 | yes |
| Renaming scans so they sort by date | 25 a week | 1 | 25 | yes |
| Opening the same programs and folders in the morning | 5 a week | 4 | 20 | yes |
| Answering the same three questions by mail | 15 a week | 3 | 45 | partly |
| Writing the agenda file for a meeting | 4 a week | 8 | 32 | partly |
| Noting hours per project in a sheet | 5 a week | 6 | 30 | yes |
| Building the weekly hours summary for the boss | 1 a week | 40 | 40 | yes |
| Backing up the project folder to the USB disk | 1 a week | 15 | 15 | yes |
| Checking the supplier's page for the delivery date | 5 a week | 4 | 20 | yes |
| Remembering birthdays of colleagues and clients | 1 a week | 10 | 10 | yes |
| Printing the week's to-do list | 1 a week | 10 | 10 | yes |
| Calls with the site manager | 6 a week | 15 | 90 | no |
| Reading and judging offers | 3 a week | 30 | 90 | no |

The last two lines are real work. They need Mia's judgement and her
relationships with people, and she would not want a program anywhere
near them. Everything above them is different. Added up, the chores
marked yes or partly come to a little under five hours in her week.
That is more than half a working day, spent on things that need her
hands but not her head.

Mia was surprised by two lines in particular. The search in the
Downloads folder felt like nothing while she did it, three minutes here
and there. The answers to the same three questions felt like real work,
because each mail was written to a real person. Only when she counted
did she see that she wrote nearly the same text fifteen times.

### What your own audit tells you

When your week is counted, look at the list in three ways.

1. **Sort by minutes per week.** The top five lines are where a program
   can give you the most time back. They are often not the chores that
   annoy you most, which is useful to know.
2. **Look for clusters.** Several small lines may be parts of one
   chore: saving an attachment, renaming it, moving it to a project
   folder. A single program can take care of all three.
3. **Mark the lines you dread.** Some chores cost little time but a lot
   of mood, such as the weekly report you put off until Friday evening.
   These deserve automation too, even if the minutes are small, because
   the dread costs more than the minutes.

Keep the list. You will use it again at the end of this chapter and at
the end of the book.

## The Automation Test

Not every chore that a program could do should be done by a program.
Before you pick a recipe, run each line of your audit through four
questions.

### How often does it happen?

A chore you do every day is a good candidate even if it is small. A
chore you do once a year is usually not worth a program, unless it is
long, error prone or stressful. As a rough guide, anything you do at
least once a week is worth a second look.

Frequency has a second effect that is easy to overlook. The more often
you do something, the more variations you have seen. A program for a
daily chore can be built from a good picture of the real cases. A
program for a yearly chore is built from memory, and memory forgets the
exceptions.

### How much effort is it, and how much would the program be?

Compare the minutes the chore costs per week with the time it takes to
set up the program. For the easy recipes in Chapter 3 the setup takes a
few minutes in the wizard, so almost anything is worth it. For the
medium and expert recipes the setup can take an afternoon, including
reading the program and adapting it. That pays back quickly for a chore
of an hour a week and slowly for a chore of five minutes a week.

There is also the effort of keeping the program working. Folders get
renamed, web pages change their layout, and passwords expire. The
recipes are written to fail clearly when something has changed, but
somebody still has to read the message and fix the setting. Count a few
minutes a month for that.

### What happens when it goes wrong?

This is the most important question. A program that sorts your
Downloads folder can make a mistake, and the cost is that you look for
a file in the wrong folder. A program that sends mail to customers can
make a mistake too, and the cost is a confused customer, an apology and
perhaps a lost order.

The recipes in this book reduce risk in a few fixed ways. They plan
first and act second, so you can see what they would do before they do
it. They never overwrite a file; they choose a free name instead. They
write a log. And the ones that send something to other people prepare
the message for you to check rather than sending it on their own,
unless you deliberately change that.

Still, the question stays with you. If a mistake would hurt someone, or
cost money, or cannot be undone, keep a person in the loop.

### Does it need you?

Some work is yours because it needs judgement: deciding which offer to
accept, how to answer an angry customer, whether a draft is good
enough. Some work is yours because it is part of a relationship: the
call to the site manager, the thank you to a colleague, the
conversation that starts with a question about the weekend. A program
can remind you of these things. It should not do them.

There is a simple check. Imagine the person on the other side finding
out that a program did it. If they would shrug, automate it. If they
would feel less respected, keep it.

### A decision table

Put the four questions together and most lines of your audit sort
themselves.

| Frequency | Effort per week | Cost of a mistake | Needs judgement or a relationship | Decision |
|---|---|---|---|---|
| weekly or more | any | small, easy to undo | no | automate |
| weekly or more | any | larger | no | automate, with a dry run and a check |
| weekly or more | any | any | yes | keep, perhaps add a reminder |
| monthly or less | over an hour | small | no | automate if the steps are clear |
| monthly or less | under an hour | any | no | keep, write down the steps instead |
| any | any | cannot be undone | any | keep a person in the loop |

The middle line deserves a word. A chore that happens rarely and takes
little time is often best served by a checklist in a text file. The next
time it comes around, you follow the list instead of rebuilding the
steps from memory. That is a kind of automation too, and it costs
nothing.

> **Watch out**
> The chores that are hardest to automate are often the ones you do
> differently every time without noticing. If you cannot write the
> steps down, a program cannot follow them. Write them down first.

## Time Won Is Not Time Filled

Here is the uncomfortable part. When a program takes an hour a week off
your desk, that hour does not appear in your calendar as a free slot.
It dissolves. A meeting runs a little longer, a mail gets a more
careful answer, the afternoon starts a little later. After a month you
are as busy as before and wonder where the hour went.

This is not a failure of the program. It is how work behaves. Tasks
grow to fill the time that is available, and the time a program saves
is the easiest time to fill, because nobody has claimed it yet.

### Decide before you save

The answer is to decide what the time is for before it arrives. When
you set up a recipe, write down, at the same moment, where its minutes
will go. It does not have to be grand. Some ideas that people have
found useful:

- **Leave on time.** The most direct use of saved time is to stop
  working earlier. If the shutdown ritual of recipe E06 runs at the end
  of your day, let it be a real end.
- **Protect a block.** Collect the saved minutes into one block of
  focused time, an hour on Thursday morning with mail closed, and put
  it in your calendar as a meeting with yourself.
- **Take the breaks you skip.** The break reminder of recipe E05 does
  not save time at all; it spends some. If other recipes give you
  minutes, this one turns a few of them into rest.
- **Learn something.** Twenty minutes a week is enough to read a
  chapter, to follow a course or to improve one of the recipes in this
  book.
- **Do the work you postpone.** Every desk has a task that would make a
  real difference and never gets done because the small things come
  first. Give it the saved time.

The ledger at the end of this chapter has a column for exactly this:
what the time went to. Filling it in once a week is the cheapest
protection against the time dissolving.

### Tell people, or do not

There is a question you have to answer for yourself: do you tell your
colleagues and your manager that some of your work is now done by
programs? There are good reasons to tell. A team learns from it, a
manager can see that you improved the way work is done, and the recipes
can help others too. Chapter 6 talks about sharing them without
becoming the help desk.

There are also reasons to be careful. In some workplaces, saved time
is immediately filled with new tasks from outside. If that would happen
to you, it is reasonable to first use the time for the purposes you
chose, and to share once you have seen what the recipes really save.
Either way, do not hide what the programs do. If someone asks, explain.
Hidden automation tends to surprise people at the worst moment.

### Saved time is not a target

A last warning. Once you start counting minutes, it is tempting to
optimise everything, to squeeze another five minutes out of each day
and to feel guilty about the minutes you have not saved yet. That
misses the point of the book. The goal is a better balance, not a
higher score. A recipe that saves ten minutes a week and makes your
Monday morning calmer is a success. A week in which you set up nothing
new and used the time you already won is a success too.

## Small Programs Instead of Big Platforms

When people think about automating work, they often think of large
systems: workflow platforms, enterprise software, a project with a
budget and a steering group. Those systems have their place. This book
is about something smaller: programs that run on your own computer,
that you can read in a few minutes, and that do one chore well.

### Why small works

A small program on your own machine has some clear advantages.

- **It starts today.** You do not wait for a budget, a contract or a
  slot in someone else's plan. You run the wizard and the recipe works.
- **It fits your chore exactly.** A platform has to serve many people,
  so it offers what most of them need. A recipe is set up for your
  folders, your naming and your week.
- **You can read it.** Every program in this book is printed in full.
  When it does something you did not expect, you can look at the lines
  that did it.
- **It costs little.** jdBasic is free, the recipes are free, and they
  run on the computer you already have.
- **It goes away when you want.** If a recipe no longer helps, you
  switch it off and delete the folder. Nothing else depends on it.

There is also a less obvious advantage. Building or adapting a small
program teaches you how your own work is structured. Many readers find
that the most useful part of automating a chore is the moment they had
to describe it clearly enough for a program, because that description
showed them which steps were not needed at all.

### When small is not enough

Small programs have limits, and it is worth knowing them before you
reach them.

- **Shared data.** When several people change the same data, such as a
  customer list or a stock count, a script on one computer is the wrong
  place for it. That data belongs in a system everyone uses.
- **Rules you must follow.** Some work is governed by rules about
  records, retention, approvals or audit trails. A home made script
  rarely meets them, and it should not try. Ask before you automate
  anything in that area.
- **Many users.** A recipe that helps you can help your team, and
  Chapter 5 shows how to hand one out. When it is used by dozens of
  people, it needs support, updates and someone responsible. At that
  point it has become a product and should be treated as one.
- **Work that must not stop.** Your computer is switched off at night,
  updates restart it, and you go on holiday. If a chore must happen
  even when you are away, it needs a server, not a laptop.

None of these limits is a reason not to start. They are signs for when
to talk to the people who run the larger systems. A working recipe is
often the best way to start that conversation, because it shows exactly
what you need.

### Why jdBasic

The programs in this book are written in jdBasic, a language in the
tradition of BASIC that was made for exactly this kind of work. It reads
almost like English, which matters when you want to understand a
program you did not write. It comes with the libraries that office work
needs: Excel and Word files, PDF, mail, calendars, web pages, schedules.
The same program runs in the interpreter while you try it and as a
compiled program when you hand it to someone else. You do not need to
know any of this to use the easy recipes, and Chapter 2 introduces as
much of the language as the later chapters use.

## Ethics and Boundaries

Automating your own work is your business, up to a point. The point is
reached where your programs touch your employer's systems, other
people's data or other people's inboxes. This section is not legal
advice, and it does not replace the rules of your workplace. It names
the questions you should be able to answer before you switch a recipe
on.

### Your employer's rules

Many workplaces have rules about software you may install, about where
company data may be stored and about scripts that run on their
computers. Find out what they are. In some offices installing jdBasic
on your own is fine; in others it needs approval from IT. The recipes
run without administrator rights and store everything in your own
folders, which makes the request easier, but the rules still apply.

If you are not sure, ask. A short mail that explains what the program
does and where its files live is usually enough. Chapter 6 has a
section on talking to IT and security, with the questions they tend to
ask and honest answers to them.

### Personal data

Several recipes handle data about people: contact lists, birthdays,
calendars, mail addresses, hours worked. In Europe the General Data
Protection Regulation sets rules for this, and other regions have
similar laws. In plain words, the ideas behind them are simple:

- keep only the personal data you need for a clear purpose;
- keep it safe, on your own computer or on systems your employer
  approves, and not on random web services;
- do not keep it longer than you need it;
- do not use it for something the people would not expect.

The recipes are written with this in mind. They work on files you
already have, they keep their results in your work folder, and none of
them sends data to a service on the internet unless you configure one.
Recipe E12, the birthday reminder, is a good example to think about. A
list of your colleagues' birthdays, kept for sending them good wishes,
is fine for most people. The same list, used to sort them by age, is
not.

### Mail on behalf of others

Some recipes prepare mail, and later recipes can send it. Be careful
with any program that sends mail in your name, and even more careful
with one that sends in someone else's name. Mail that looks personal
but was produced by a program can feel like a deception when the
receiver finds out. A few rules of thumb:

- prepare, check, then send, at least until you are sure the program
  does the right thing in every case;
- keep templates honest: a thank you that was written once and is sent
  many times is fine, but it should not pretend to be something else;
- never send mail from another person's account without their explicit
  agreement;
- keep a copy of everything a program sends.

### Monitor yourself, not your colleagues

The time tracker of recipe E07, the meeting cost meter of Chapter 4 and
the balance score of Chapter 5 measure work. Used on your own week,
they help you see where your time goes and make better decisions about
it. Used on other people without their knowledge, the same programs
become surveillance, and in many countries that is illegal as well as
corrosive to trust.

This book only ever measures the reader. When a team lead in Chapter 4
builds a summary of the team's hours, it is from data the team enters
itself, for a purpose everyone knows. If you adapt a recipe to measure
others, talk to them first and to whoever handles staff matters in
your workplace.

### Passwords

Some recipes need a password: the mail server for sending, a web page
behind a login, a guest network. The rule is simple: a password does
not belong in a program and not in a settings file that others can
read. The recipes in this book ask for passwords when they need them
and keep them in memory only. Chapter 5 shows the job server that asks
once in the morning, and recipe X14 discusses safer ways to store
secrets when a program must run unattended.

If you ever find a password written into one of your own programs,
take it out and change the password. It may already have been seen by
anyone who received a copy.

> **Watch out**
> A backup or a zip archive made by a recipe contains everything in
> the folders you chose. If those folders hold personal data, the
> archive does too, and it needs the same care as the original.

## How This Book Works

The rest of the book is built around recipes: small programs, each
solving one chore, each with its own pages that explain it. This
section describes how they are organised so that you can find your way
around.

### Three levels

The recipes come in three levels, fifteen each.

**Easy** recipes, in Chapter 3, are switched on by the setup wizard.
You answer a few questions, the wizard writes a settings file, copies
the programs and plans when they run. You can read the programs and
change their settings, but you do not have to touch the code. These
recipes are for anyone who uses a computer at work.

**Medium** recipes, in Chapter 4, are programs of one or two hundred
lines that you set up yourself. They work with documents, mail,
calendars and data: invoices from a spreadsheet, letters from a
template, a weekly status mail, a cleaned up export. They expect you to
read the program, change a few lines and run it. Chapter 2 teaches
enough of the language for that.

**Expert** recipes, in Chapter 5, are small systems: a job server that
runs everything on time, an assistant that drafts replies to mail, a
search over your documents, tools you compile and hand to colleagues.
They show how to write programs that log what they do, retry when the
network is slow, test themselves and fail in a way that tells you what
to fix.

You do not have to work through the levels in order. If an expert
recipe solves the chore that costs you most, start there. Each recipe
says what it needs before you start.

### Three people

Three people appear throughout the book. They are not real, but their
chores are.

**Mia** is a project assistant. She keeps documents, meetings and
hours in order for three projects, and her day is full of small tasks
at the computer. She has never programmed and does not intend to. The
easy recipes are hers, and you have already seen her week.

**Jonas** is a team lead in a sales office. He lives in spreadsheets
and in his inbox, writes the same kind of report every week, and
collects numbers from six colleagues every month. He writes Excel
formulas with confidence and has recorded a macro or two. The medium
recipes start from his chores.

**Priya** is the IT generalist of a small company. She looks after the
computers, the network and the accounts, and people come to her when
something needs to work every day without anyone thinking about it.
She programs when she has to. The expert recipes are the tools she
builds for herself and for the others.

When a recipe begins with one of them, the story is short. It is there
to make the chore concrete, so that you can decide quickly whether it
is yours too.

### The parts of a recipe

Every recipe follows the same order, so that you can skim it and find
what you need.

1. **The chore** describes what the work costs now, usually in a short
   scene with one of the three people.
2. **What you get** shows the result in a picture or one sentence.
3. **Before you start** lists what must be in place.
4. **The program** prints the code, exactly as it was tested.
5. **How it works** walks through the program step by step.
6. **Run it** gives the command and what it prints.
7. **Schedule it** explains how the program runs without you.
8. **Make it yours** lists the settings and the lines you can change.
9. **When it goes wrong** covers the problems you are likely to meet.
10. **Balance dividend** says how much time the recipe gives back.

The programs are not typed into the book by hand. Each listing is taken
from the file that was tested when the book was built, so what you read
is what runs. The tests are in the same folders as the programs, and
you can run them yourself.

### What you need

For the easy recipes you need a Windows computer, the jdBasic release
described in Chapter 2 and about fifteen minutes for the wizard. For the
medium recipes you need a text editor and the patience to read a
program from top to bottom once. For the expert recipes it helps to have
written a program before, in any language. Some of them need access to
a mail server, a web service or an AI model, and they say so at the
start.

## Starting Without Getting Stuck

Many readers will finish this chapter, look at a long audit and feel
that there is too much to do. That feeling is the enemy of the whole
idea. The way around it is to start with one chore, live with its
program for a week, and only then add the next.

### Pick one chore

From your audit, choose the line that meets all three of these
conditions:

- it happens at least a few times a week;
- a mistake would be harmless and easy to undo;
- there is a recipe for it in Chapter 3.

For most people this is the Downloads folder, the renaming of scans or
the morning routine of opening the same programs. Do not pick the
largest chore first. Pick the safest one, so that your first experience
with a program doing your work is a calm one.

### The first week with a recipe

Run the recipe with its dry run for a day or two before you let it act.
The dry run prints what the program would do and changes nothing. Read
the list. You will find one of three things: everything is right, some
files are treated in a way you did not expect, or the program finds
nothing at all because a setting points at the wrong folder. All three
are useful. In the second and third case, change a setting and run the
dry run again.

When the list looks right, let the recipe act and schedule it. For the
rest of the week, notice how often you would have done the chore by
hand. That number is your first real entry for the ledger, and it is
almost always different from the estimate in your audit.

At the end of the week, decide whether to keep the recipe. If yes, pick
the next line. If not, switch it off without regret and write down why.
A recipe that did not help tells you something about your work too.

### Common objections

**"I am not technical."** The easy recipes do not ask you to be. You
answer questions in a window, and the programs run on their own. If you
want to change something, the settings are in one file with plain
names. Chapter 2 shows the few lines of the language you may want to
read, but nothing in Chapter 3 requires them.

**"It is faster to do it by hand."** For a single time, almost always.
The program is faster over a month. If the setup really takes longer
than the chore costs in a year, the automation test has already told
you to keep it by hand, and that is a fine result.

**"IT will not allow it."** Perhaps. Ask with a clear description of
what you want to run, where its files are and what it touches. Many IT
departments are glad when someone asks first. If the answer is no, the
audit and the ledger are still worth keeping, because they help you
make the case for a tool that IT does approve.

**"What if it breaks while I am away?"** The recipes in this book fail
in a visible way: they print a message and write it to a log, and they
leave your files where they were. When you come back, the worst case is
a chore that did not happen and is waiting for you as it used to.
Chapter 6 describes a short monthly check that catches most problems
before they matter.

**"My work is too varied to automate."** Some of it is. The audit
usually shows that the varied part is the valuable part, and that it is
surrounded by repeated chores that are not varied at all. The goal is
to clear those away so that the varied work gets more of your
attention.

**"I will lose the skill."** For chores like sorting files, nobody
misses the skill. For anything that matters, the programs in this book
are printed in full and explained line by line. Reading them is a
better way to understand the chore than repeating it, because the
program has to spell out every step you used to do without thinking.

### How long it takes

A rough picture from the recipes in Chapter 3: running the wizard takes
about fifteen minutes. Each easy recipe needs a day or two of dry runs
and a few minutes of settings. After a month most readers have four to
six recipes running and have stopped thinking about the chores they
took over. The medium recipes take an evening each, the expert ones a
weekend. None of this has to happen at once. The ledger will show you
the pace that suits you.

## The Life Balance Ledger

The last tool of this chapter is the simplest one in the book. It is a
table that you keep for yourself, once a week, for as long as you use
the recipes. It answers two questions that otherwise get lost: how much
time did the programs give back, and what did that time go to?

### The table

Keep the ledger wherever you keep things you look at every week: a
sheet in a spreadsheet, a page in a notebook or a text file in your
work folder. It has four columns.

| Week | Recipe | Minutes saved | What the time went to |
|---|---|---|---|
| | | | |

- **Week**: the date of the Monday, so the rows sort by date.
- **Recipe**: the recipe, such as E01 Downloads Butler, or "all" if you
  prefer one line per week.
- **Minutes saved**: your estimate for that week. Each recipe gives a
  starting value in its Balance dividend box; replace it with your own
  number once you know it.
- **What the time went to**: one honest line. "Left at five on
  Tuesday", "Read the first half of the course", or "I do not know",
  which is also useful to know.

Here is the start of Mia's ledger after her first month with the easy
recipes.

| Week | Recipe | Minutes saved | What the time went to |
|---|---|---|---|
| 2026-10-05 | E01 Downloads Butler | 30 | Nothing planned yet, it disappeared |
| 2026-10-05 | E03 Batch Renamer | 20 | Same |
| 2026-10-12 | E01, E03, E07 | 85 | Left on time on Wednesday and Friday |
| 2026-10-19 | E01, E03, E07, E08 | 110 | Thursday morning hour for the archive cleanup |
| 2026-10-26 | E01, E03, E07, E08, E13 | 125 | Thursday hour again; one long lunch |

The first line is the most common first line of all. The time came
back and went nowhere in particular, because nothing was waiting for
it. Once Mia wrote down in advance that the minutes were for leaving on
time, they stayed hers.

### Adding it up

If you keep the ledger as a text file, a few lines of jdBasic can add it
up for you. Here is a small example with the minutes of four weeks. You
will understand every line of it after Chapter 2; for now, it shows how
little a program needs to be useful.

```basic
DIM weeks = ["10-05", "10-12", "10-19", "10-26"]
DIM minutes = [50, 85, 110, 125]
DIM total = SUM(minutes)
PRINT "Minutes saved in four weeks: "; total
PRINT "That is "; ROUND(total / 60, 1); " hours"
PRINT "Average per week: "; total / LEN(weeks); " minutes"
```

```text
Minutes saved in four weeks: 370
That is 6.2 hours
Average per week: 92.5 minutes
```

### How the book uses the ledger

Every recipe ends with a box that looks like this:

> **Balance dividend**
> About 15 minutes a week of tidying and searching, and the half hour
> once a month that the folder used to cost you.

The number in the box is an estimate for a typical week. Yours will be
different, higher for some recipes and lower for others. Use the box
as the first entry in your ledger and correct it after a few weeks.

Chapter 6 comes back to the ledger after three months. By then you will
have enough rows to see patterns: which recipes really help, which
ones you could switch off, and whether the time you won went where you
wanted it to go. Recipe X15 in Chapter 5 even turns the ledger,
your calendar and your timesheet into a weekly balance score, for those
who like a number. The paper version works just as well.

### Before you turn the page

You now have three things that the rest of the book builds on.

- Your audit, with the chores of one week and the minutes they cost.
- The automation test, which tells you which chores to hand over and
  which to keep.
- An empty ledger, with a line at the top that says what the saved time
  is for.

Chapter 2 installs jdBasic, runs the setup wizard and switches on the
first recipes. Pick the two or three lines of your audit with the most
minutes and look for the matching recipes in the list at the start of
Chapter 3. That is where the first time comes back.
