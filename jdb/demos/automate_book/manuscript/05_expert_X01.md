<!-- pagebreak -->

## X01 Personal Job Server

### The chore

Lena's programs started out as entries in the Windows Task Scheduler,
one per program, each set up by hand on the day she wrote it. By now
there are fourteen of them. When one fails, the Task Scheduler notes a
result code that nobody reads. When her laptop is closed at half past
six, the evening job does not run, and nothing tells her. Twice the
nightly import was still running when the morning report started, and
the report showed half a day. Keeping an eye on all of it costs her
half an hour a week, and the eye is not always open.

### What you get

One program that starts when you log on and runs all your jobs from
one file. Each job runs on time, a failed job is tried again with a
growing pause, two runs of the same server never happen at once, and
a job whose time passed while the computer was off runs once when the
server starts. Every run lands in a log and in a state file that other
programs read, such as the health check of X13, and with the key of
X14 every run is also signed into an audit trail.

```
jdbasic job_server.jdb --list
downloads (daily 18:30)
  next 2026-10-05 18:30, last 2026-10-02 18:30 ok
backup (weekly fri 16:30)
  next 2026-10-09 16:30, last 2026-10-02 16:31 FAILED
status mail (weekly fri 15:00)
  next 2026-10-09 15:00
hot folder (every 5 minutes)
  switched off
```

### Before you start

The wizard has set up the recipes the server will run, so they are in
the `recipes` folder of your work folder. Copy the file `jobs.toml`
from this recipe's folder into the `config` folder next to
`work.conf`, and write one block per job into it.

Each job the server takes over must stop running on its own. Run the
wizard again and set the schedule of those recipes to `manual`;
otherwise they run twice, once from the Task Scheduler and once from
the server.

### The program

The jobs file names each job, when it runs and what it runs. A
`program` is a recipe, given relative to the recipes folder; a
`command` is any other program:

<!-- include recipes/expert/X01_job_server/jobs.toml -->

The main program reads the settings and the jobs, and then loops: it
runs what is due, writes the state, and sleeps until the next job is
due, at most a minute at a time:

<!-- include recipes/expert/X01_job_server/job_server.jdb -->

The module decides what is due and runs it. `RUN` runs one job with
its retries, `RUN_NOW` records the result, and `TICK` goes through all
jobs once:

<!-- include recipes/expert/X01_job_server/jobserver.jdb -->

Reading the jobs file and the state file is in a small module of its
own, `jobstate.jdb` in `recipes/lib`, because the health check and the
cockpit read the same files.

### How it works

1. **The jobs file.** `JOBSTATE.JOBS` reads `jobs.toml` and fills in
   every field a job leaves out: one attempt, a minute before the
   second try, `catch_up` and `enabled` on. A file with mistakes, such
   as a name used twice or a schedule SCHED cannot read, is an error
   that lists every mistake at once, so you fix them in one go.
2. **When a job is due.** For each job, `TickOne` asks SCHED for the
   last run time before now. If that time is later than the run time
   the job last ran for, the job is due. The schedules are the same
   words the wizard uses: `daily 18:30`, `weekdays 08:00`, `weekly fri
   16:00`, `every 15 minutes`, and a cron line such as `30 8 1 * *`
   for half past eight on the first of each month.
3. **A new job waits.** A job the server sees for the first time gets
   its last run time as if it had run, and waits for the next one.
   Adding a job at ten in the morning does not start every daily job
   at once.
4. **Missed runs.** When the server starts in the morning, a job that
   was due at half past six last evening is due now: its last run time
   is later than the time it last ran for. With `catch_up = false` a
   run that is more than five minutes late is skipped and logged as
   skipped. That suits the status mail, which nobody wants on Saturday.
5. **Retries.** `RUN` hands the job to `RETRY.ATTEMPT`. The function
   `Execute$` runs the program with `OS.EXEC` and turns an exit code
   other than 0 into an error with the last line the program printed.
   RETRY catches it, waits `retry_seconds`, doubles the wait for the
   next attempt and gives up after `attempts`.
6. **The record.** `RUN_NOW` writes start, end, result, exit code and
   error into the job's entry of the state, counts runs and failures,
   logs one line and, when X14 has made its signing key, adds an entry
   to the audit trail.
7. **One server at a time.** The server writes the time into a lock
   file each time it wakes, at least once a minute. A second server
   finds a lock younger than three minutes and ends with *A job server
   is running already*. The health check reads the same time to see
   whether the server is alive.
8. **Changes while it runs.** After each round the server looks at the
   time `jobs.toml` was saved. A new time means it reads the file
   again; a file with mistakes is logged and the old jobs stay in use.
   `--stop` leaves a stop file that the server finds within a minute.

Jobs run one after another. A backup that takes twenty minutes holds
up the job after it by twenty minutes, which is fine for office work.
A program that never ends holds up all of them, so give your own
recipes a clear end.

### Run it

Look at the plan first. `--list` shows every job with its next run
and its last result, and `--dry-run` names what is due right now
without running it:

```
jdbasic job_server.jdb --dry-run
would run downloads for 2026-10-05 18:30
```

`--run` runs one job at once, which is the way to try a new job:

```
jdbasic job_server.jdb --run backup
2026-10-05 10:12:40 INFO  [jobs] job ran job=backup attempts=1
```

Started without a switch, the server keeps running and prints its log
lines:

```
jdbasic job_server.jdb
2026-10-05 08:31:02 INFO  [jobs] server started jobs=4
2026-10-05 08:31:02 INFO  [jobs] job ran job=downloads attempts=1
```

A job that fails after all its attempts writes an `ERROR` line with
*job failed*, the number of attempts and the error.

The log is `job_server.log` in the `logs` folder of your work folder,
next to `job_state.json`. When it reaches a megabyte it becomes
`job_server.log.1`; the server keeps three such older files and drops
the oldest.

### Schedule it

The wizard plans the server *at logon*: the Task Scheduler starts it
when you log on to Windows, and it runs until you log off. That is the
one entry the Task Scheduler keeps; everything else is in
`jobs.toml`.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[job_server]`:

```toml
[job_server]
jobs = "~/Documents/AutomateWork/config/jobs.toml"
folder = "~/Documents/AutomateWork/logs"
```

- **Once a month.** The wizard can plan a recipe for one day a month
  (Chapter 6 shows the change), but the Task Scheduler skips a day the
  computer was off. The server runs a missed job once when it starts.
  The timesheet of M15 at half past eight on the first of each month:

  ```toml
  [[job]]
  name = "timesheet"
  when = "30 8 1 * *"
  program = "M15_timesheet/monthly_timesheet.jdb"
  ```

- **Any program.** A `command` runs anything with its full path, for
  example a mirror of a folder to a network drive:

  ```toml
  [[job]]
  name = "mirror"
  when = "weekdays 12:30"
  command = "C:/Windows/System32/robocopy.exe"
  args = ["C:/Reports", "R:/Reports", "/MIR"]
  ```

  robocopy answers 1 when it copied files, which the server would
  count as a failure. For such a program, run a small batch file
  instead, `mirror.bat`, that turns every code below 8 into 0:

  ```bat
  robocopy C:Reports R:Reports /MIR
  if %ERRORLEVEL% LSS 8 exit /b 0
  exit /b %ERRORLEVEL%
  ```

  and give the job `command = "C:/Work/mirror.bat"` and no `args`.

- **What a job said.** To keep the last line every job printed in the
  log, add one line to the success branch of `RUN_NOW`:

  ```basic
  fields{"said"} = res{"output"}
  ```

- **Longer pauses.** A web service that is down is often down for
  longer than a minute. `attempts = 4` with `retry_seconds = 300`
  tries again after 5, 10 and 20 minutes. The other jobs wait during
  those pauses, so keep long pauses for jobs that run at night.

### When it goes wrong

- **"A job server is running already", with none running**: the server
  ended without removing its lock, for example when Windows shut down.
  The lock counts for three minutes; after that a new server starts.
  You may also delete `job_server.lock` in the logs folder.
- **"The jobs file has mistakes"**: the message lists each one with the
  name of the job. A running server keeps its old jobs and writes *jobs
  file not used* into the log until the file is right.
- **A job fails with "exit 1"**: run it alone with `--run NAME`. The
  error has the last line the program printed; run the recipe itself
  with `--dry-run` to see more.
- **A job runs twice**: its own entry in the Task Scheduler is still
  there. Set the recipe to `manual` in the wizard.

> **Balance dividend**
> About 30 minutes a week of checking whether things ran, plus the
> mornings that start with a report that is complete.
