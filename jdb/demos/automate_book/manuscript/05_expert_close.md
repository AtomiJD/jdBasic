<!-- pagebreak -->

## The Whole Picture

Lena's team now runs on about twenty programs. None of them is large,
and each can be read in an afternoon. What makes them a system is that
they share a few files and agree on who writes which one. This page
shows those files and the programs around them.

```text
                 work folder (AutomateWork)
 +-----------------------------------------------------------+
 | config/work.conf      every setting, one part per recipe |
 | config/jobs.toml      what the job server runs, and when |
 | recipes/              the programs, copied by the wizard  |
 | logs/                 one log per program, the job state, |
 |                       the audit trail, the metrics        |
 | outbox/               mail waiting for you to send it     |
 +-----------------------------------------------------------+
        ^                    ^                     ^
        | reads, writes      | reads               | reads
        |                    |                     |
  X01 job server      X13 health checks     X06 cockpit window
  starts each job     mails you when a      a button per job,
  on time, retries,   job failed or the     its last result
  logs, signs runs    server went quiet
        |
        +--> E01 to E15, M01 to M15, X02 to X15, run as jobs

  Windows Credential Manager <-- X14 keeps passwords and keys
  localhost pages            <-- M08 dashboard, X03 search, X15 score
  X12 tests                  --> every recipe, before a change
  X07 releases               --> programs for colleagues
```

Four rules hold the system together. They are the same habits this
chapter started with, now written as who may do what:

1. **One program writes each file.** The job server writes the job
   state, each recipe writes its own log, the wizard writes
   `work.conf`, and you write `jobs.toml`. Everything else only reads.
   Two writers of one file is how a system loses data at three in the
   morning.
2. **Settings live in `work.conf`, secrets in the Credential Manager.**
   A setting can be read by anyone who can read the folder, so a
   password never becomes one.
3. **Mail waits in the outbox.** Programs prepare messages; a person,
   or a health check with a fixed address of your own, sends them.
4. **Nothing changes without a test.** X12 runs every recipe's test.
   Run it before you copy a changed recipe into the work folder, and
   the Monday report keeps arriving.

### Where to go from here

Start with the job server and the health checks, then move recipes
over to the server one at a time, each after a week of dry runs. Add
the cockpit when a colleague asks how to start something, and the
release tool of X07 when they ask for a copy. Chapter 6 is about the
months after that: what breaks, how to notice, and how to talk to IT
about all of it.
