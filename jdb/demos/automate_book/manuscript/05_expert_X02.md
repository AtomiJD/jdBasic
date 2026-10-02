<!-- pagebreak -->

## X02 Inbox Assistant

### The chore

Lena starts every day with forty new mails. Most of them need no more
than a glance: a newsletter, a copy of something she already knows, a
meeting that moved. Six or seven need an answer, and those she writes
the same way every time: thank the sender, say what she will do, say
when. It takes her the first hour of the morning, and by the time she
is done, the first questions of the day are already waiting.

### What you get

A language model reads the mails you saved, writes one sentence about
each and drafts a reply where the sender expects one. Every draft is a
plain text file in a folder of drafts. You read it, change what you
want, and approve it by changing one word. Only approved drafts go into
the outbox, and only `--send` sends them, with the password you type.

The model never sends anything, and nothing reaches the outbox without
you. Where the model would have to promise a date, an amount or a
decision, it writes `[CHECK]` instead, and a draft with `[CHECK]` in it
stays in the folder until you have filled it in.

### Before you start

You need three things:

1. **Saved mails.** The recipe reads `.eml` files from a folder, the
   same way the Inbox Unpacker (M07) does. Save the mails you want
   help with into that folder, or let a mail rule do it.
2. **A language model.** Either a service such as OpenAI or Anthropic,
   or a model on your own computer. For a service you need an API key;
   put it into the environment variable `OPENAI_API_KEY` (or
   `ANTHROPIC_API_KEY`), or let the recipe ask for it each time. The
   key is never written into `work.conf`. For a model of your own,
   install a model server such as Ollama and set `base_url` to its
   address; then no key is needed and no mail leaves your computer.
3. **The outbox rule of Chapter 4.** The `[mail]` part of `work.conf`
   names your mail server, as for the other recipes that send mail.

> **Watch out**
> A mail you hand to a service goes to that service. Before you use a
> service for work mail, ask whether your company allows it. Mails with
> personal data or business secrets are a reason to use a model on your
> own computer.

### The program

The program has four modes: drafting, a dry run, approving and
sending. The work is in the module:

<!-- include recipes/expert/X02_inbox_assistant/inbox_assistant.jdb -->

The module talks to the model, writes and reads the draft files, and
turns an approved draft into a mail:

<!-- include recipes/expert/X02_inbox_assistant/inboxai.jdb -->

### How it works

1. **The client.** `CLIENT` builds a client of the LLMAPI library from
   the settings. The library speaks to OpenAI, to Anthropic and to any
   server that answers like OpenAI's chat service, which is what Ollama
   and most local model servers do. The temperature of 0.2 keeps the
   answers sober and close to the mail.
2. **The rules.** `RULES$` is the instruction the model gets with every
   mail. It asks for an answer in a fixed shape and forbids promises.
   This is the text to change when the drafts do not sound like you.
3. **A reply in a fixed shape.** `DRAFT` hands the mail to
   `LLMAPI.JSON` with a shape of three fields: `summary`, `needs_reply`
   and `reply`. The model answers in exactly that shape, so the program
   can read the answer without guessing.
4. **Asking again.** A model service is sometimes busy. When the error
   names a timeout or a busy server, `DRAFT` waits half a second, then
   a second, and asks again, three times in all. A wrong key or a
   broken request is not worth a second try, so `RETRY.TRANSIENT`
   decides which errors count as passing.
5. **Each mail once.** `PENDING` lists the saved mails that are not in
   the file `seen.txt` of the drafts folder, and `MARK_SEEN` adds a
   mail there after the model read it. A mail the model could not read
   stays out of `seen.txt` and comes up again on the next run.
6. **The draft file.** `DRAFT_TEXT$` writes a few header lines, a line
   with `---` and the reply. The first header line is
   `Approve: no`. `READ_DRAFT` reads the file back, and `APPROVED`
   lists the drafts that say `Approve: yes` and hold no `[CHECK]`.
7. **From draft to mail.** `TO_MESSAGE` builds the mail with the
   headers `In-Reply-To` and `References`, so your answer shows up in
   the sender's mail program in the same thread as the question.
8. **The log.** Every mail read, every failure and every approval is a
   line in `logs/inbox_assistant.log` of your work folder.

### Run it

Start with a dry run. It lists the mails the model would get and asks
nothing:

```
jdbasic inbox_assistant.jdb --dry-run
would ask about budget.eml
would ask about news.eml
```

Then let it draft:

```
jdbasic inbox_assistant.jdb
budget.eml: Anna asks for the Q3 figures by Friday.
news.eml: A newsletter with spreadsheet tips.
1 draft(s) in C:\Users\lena\Documents\AutomateWork\drafts
```

The newsletter needs no reply, so it only gets its sentence. The draft
for Anna is the file `budget.txt`:

```
Approve: no
To: Anna Berg <anna@example.com>
Subject: Re: Budget figures
In-Reply-To: <q3@example.com>
Summary: Anna asks for the Q3 figures by Friday.
Source: budget.eml
---
Dear Anna,
you will have them by [CHECK].
Lena
```

Open it in Notepad, write the day instead of `[CHECK]`, change `no` to
`yes` and save. Then:

```
jdbasic inbox_assistant.jdb --approve
outbox  C:\Users\lena\Documents\AutomateWork\outbox\budget.eml
1 approved draft(s) in the outbox
jdbasic inbox_assistant.jdb --send
```

The approved draft moves to `drafts\done`, the mail waits in the
outbox, and `--send` asks for the mail password and sends it.

### Schedule it

The wizard plans this recipe as *manual*, because a run without a key
in the environment stops and asks for one. Once the key is in
`OPENAI_API_KEY`, or once you use a model of your own, set the schedule
to `weekdays 08:00` in the wizard. The drafts are then waiting when you
sit down, and approving them is still your step.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[inbox_assistant]`:

```toml
[inbox_assistant]
inbox = "~/Documents/AutomateWork/saved mail"
drafts = "~/Documents/AutomateWork/drafts"
provider = "openai"
model = "gpt-4o-mini"
base_url = ""
max_mails = 20
```

- **A model on your own computer.** With Ollama running, set
  `base_url = "http://localhost:11434/v1"` and `model` to the name of a
  model you pulled, such as `"llama3.1"`. The provider stays
  `"openai"`, because Ollama answers in OpenAI's shape.
- **Anthropic instead of OpenAI.** Set `provider = "anthropic"` and a
  Claude model name, and put the key into `ANTHROPIC_API_KEY`.
- **Your tone.** Add a sentence to `RULES$`, for example
  `r$ = r$ + " Write in German when the mail is German."` or a line
  about how formal you are with customers.
- **Fewer mails per run.** `max_mails` caps how many mails one run
  hands to the model. A service charges per word, so a low cap keeps a
  full inbox after a holiday from costing much.
- **Keep the summaries.** The summary of every mail is printed and
  also written into the log line of that mail, so
  `logs/inbox_assistant.log` holds a short history of your inbox. Point
  the Log Detective (M09) at the logs folder to see when the model
  failed.

### When it goes wrong

- **"answered 401"**: the key is wrong or has expired. The recipe does
  not ask twice for the same mail; fix the key and run it again.
- **"transport failure"**: the service or your model server cannot be
  reached. For Ollama, check that it runs and that `base_url` ends in
  `/v1`. The mail stays pending and comes up on the next run.
- **"did not answer with JSON"**: a small local model could not keep
  to the shape. Try a larger model, or set the temperature lower in
  `CLIENT`.
- **An approved draft stays in the folder**: it still holds `[CHECK]`
  somewhere. Search for it, fill it in, and approve again.

> **Balance dividend**
> About 60 minutes a week: the morning hour shrinks to reading the
> summaries and correcting a few drafts. Keep the time it frees for
> the mails that need your own thinking.
