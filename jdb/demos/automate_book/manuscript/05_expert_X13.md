<!-- pagebreak -->

## X13 Health Checks and Alerts

### The chore

The job server runs Lena's jobs and writes down what happened. Nobody
reads it. She learns that the Friday backup failed when she needs the
backup, and that the server was not running when a colleague asks on
Monday where the report is. The other way round is just as tiring: a
check that mails her every fifteen minutes about the same failed job
gets filtered away within a week. What she wants is one message when
something goes wrong, one when it is over, and silence in between.

### What you get

Every fifteen minutes the health check looks at the job server and the
outbox. It finds four kinds of trouble:

| Problem | When it counts |
|---|---|
| The server | no lock file, or no sign of life for 10 minutes |
| A failed job | its last run ended with an error |
| A late job | it was due more than 30 minutes ago and has not run |
| A waiting mail | it has sat in the outbox for more than a day |

Each new problem is announced once, in a window on the screen and in a
mail. When it goes away, a second mail says so. Each run also writes
the numbers of every job into a file that a monitoring system can read.

```
jdbasic health_check.jdb --dry-run
4 jobs, 2 problems
  backup failed at 2026-10-09 16:31: exit 1: the drive E: is gone
  invoice 0413.eml has waited in the outbox for 27 hours.
would alert lena@example.com
```

### Before you start

The job server (X01) runs, and the `[mail]` part of `work.conf` holds
your mail server, as in Chapter 4. Without a jobs file the check still
watches the outbox.

The alerts go into the outbox like every other mail of this book, and
you send them with the `--send` of any medium recipe. To have them go
out at once, store the mail password with X14 and switch on
`send_alerts`. The check then sends without asking you first, so it
sends only to the address in `alert_to`, which is your own unless you
change it.

### The program

The main program gathers what is on disk, lets the module find the
problems, and sends what is new:

<!-- include recipes/expert/X13_health_checks/health_check.jdb -->

The module holds the checks, the memory of what was already
announced, the mail, the numbers and the sending:

<!-- include recipes/expert/X13_health_checks/health.jdb -->

### How it works

1. **The facts.** The main program reads the jobs and the state file
   of the job server through `JOBSTATE`, the age of the server's lock
   mark, and the age of every mail in the outbox. It passes them to
   `HEALTH.CHECK` as plain values, so the test can hand in any
   situation it likes.
2. **The server.** `ServerProblems` complains when there is no lock
   file or when the server has not marked it for `server_minutes`.
   Without any jobs the server is not needed, and there is no
   complaint.
3. **Failed and late jobs.** `JobProblems` looks at the last result of
   each job. For lateness it asks SCHED for the last run time before
   *now minus late_minutes*. If the job's last run was for an earlier
   time, the job missed that run.
4. **The same key for the same trouble.** Every problem has a key such
   as `failed:backup`. `DIFF` compares the keys with the open alerts of
   the last run, kept in `alerts.json`: a key that is new is a new
   problem, a key that is gone is over, and a key that stays keeps the
   time it was first seen. That is what keeps the check quiet between
   the two messages.
5. **The mail.** `MESSAGE` builds one mail with the new problems, the
   ones that are over and the number still open. With `send_alerts`
   and a password from the Credential Manager, `SEND` delivers it at
   once through `RETRY`: three tries, with a pause of five seconds that
   doubles. In every other case, and when sending fails, it goes into
   the outbox.
6. **The numbers.** `METRICS_TEXT$` sets one gauge per job and value
   with the METRICS library and writes them in the text format of
   Prometheus to `metrics.prom` in the logs folder.

### Run it

```
jdbasic health_check.jdb
4 jobs, 1 problem
  backup failed at 2026-10-09 16:31: exit 1: the drive E: is gone
In the outbox: alert 2026-10-09 164501.eml
```

A window lists the new problem. Fifteen minutes later the problem is
still there, and the check prints it but stays silent. After the next
backup works, one more alert follows: *AutomateWork: all clear*, with
the backup under *Over now*.

The numbers file starts like this:

```
# HELP automatework_job_ok 1 when the last run worked.
# TYPE automatework_job_ok gauge
automatework_job_ok{job="downloads"} 1.0
automatework_job_ok{job="backup"} 0.0
```

### Schedule it

The wizard plans the check every 15 minutes in the Windows Task
Scheduler, on its own. Do not hand it to the job server: a check that
the server starts cannot report that the server is gone.

### Make it yours

The settings are in the part of `work.conf` that starts with
`[health_checks]`:

```toml
[health_checks]
alert_to = ""
late_minutes = 30
server_minutes = 10
outbox_hours = 24
send_alerts = false
notify = "window"
```

An empty `alert_to` sends the alerts to your own address from the
`[user]` part.

- **Long jobs.** The server marks its lock between jobs. A backup that
  runs for forty minutes keeps it from marking, so raise
  `server_minutes` above your longest job, for example to 60.
- **No window.** On a computer where nobody sits, set `notify =
  "none"`; the mail is enough.
- **A file that must be fresh.** Lena's Monday report must be younger
  than a week. One more check in `CHECK`, before `RETURN out`:

  ```basic
  DIM report$ = "C:/Reports/monday.xlsx"
  DIM info = FILE.STAT(report$)
  DIM age = DATEDIFF("d", CDATE(info{"mtime"}), NOW())
  IF age > 7 THEN
      Add(out, "old:monday", "old", "The Monday report is old.")
  ENDIF
  ```

- **Your monitoring system.** If your IT department runs Prometheus
  with the Windows exporter, its text file collector reads
  `metrics.prom` from the folder it is told to watch. Point
  `[job_server] folder` there, or copy the file in a job.

### When it goes wrong

- **"The job server is not running" right after logon**: the server
  starts with the logon and needs a moment. The next run of the check
  closes the alert again. To avoid the pair of mails, raise
  `server_minutes`.
- **The same alert again and again**: the problem comes and goes, for
  example a job that fails on every second run. Each failure after a
  good run is new. The log of the job server shows the pattern; fix
  the job.
- **"No mail password in the Credential Manager"**: `send_alerts` is
  on, but X14 has not stored the password. Run `jdbasic
  secrets_audit.jdb --set mail`, or switch `send_alerts` off.
- **"Not sent: curl 7"**: the mail server did not answer. The alert is
  in the outbox and goes out with the next `--send`.

> **Balance dividend**
> About 20 minutes a week of looking at logs, and no more Mondays that
> start with a phone call about a report that never came.
