<!-- pagebreak -->

## X14 Secrets and Audit Trail

### The chore

When Lena took over the team's scripts, the mail password of the
department was written into three of them, in plain text, and one of
those files sat in a folder the whole floor could read. Changing the
password meant finding every copy. A month later the auditors asked a
different question: can she show that the backup really ran every
Friday this quarter, and that nobody edited the log afterwards? She
could show a log, but not that it was untouched.

### What you get

Three tools in one program:

- **A safe place for secrets.** Passwords and keys go into the Windows
  Credential Manager, where Windows keeps them encrypted for your
  login. A recipe reads them from there; no file of yours holds them.
- **A search for written down secrets.** One command reads every
  script and settings file under your work folder and lists each line
  that sets a password, a token or a key, with the value masked.
- **An audit trail that shows changes.** The job server (X01) writes
  one signed line per run. One command checks every signature and the
  chain between the lines, and sums up what the jobs did.

```
jdbasic secrets_audit.jdb --verify
The audit trail is whole: 212 entries.
  backup: 13 runs, 1 failed, last 2026-10-09 16:31:07
  downloads: 7 runs, last 2026-10-09 18:30:02
```

### Before you start

Until now no recipe wrote the mail password anywhere, and you typed it
for every `--send`. That stays the default. This recipe is for the
moment when a program has to work without you, such as the health
check sending an alert at night. Then the password has to live
somewhere, and the Credential Manager is the place Windows itself
offers for that.

Check with your IT department whether stored passwords are allowed on
your work computer. Some companies forbid it; then keep typing.

### The program

The main program is a set of commands, one block each:

<!-- include recipes/expert/X14_secrets_audit/secrets_audit.jdb -->

The module names the secrets, searches for written down ones and sums
up the trail:

<!-- include recipes/expert/X14_secrets_audit/trail.jdb -->

Two shared modules in `recipes/lib` do the rest. `vault.jdb` talks to
the Credential Manager. jdBasic has no command for it, so the module
declares the Windows functions it needs and calls them directly:

<!-- include recipes/lib/vault.jdb lines=1-35 -->

Windows expects the entry as a block of 80 bytes with pointers to the
name, the user and the secret. `PUT` builds that block byte by byte:

<!-- include recipes/lib/vault.jdb lines=185-209 -->

`FIND$` is what a recipe calls when it needs a secret. It looks in the
Credential Manager first, then in an environment variable, and asks
only when both are empty:

<!-- include recipes/lib/vault.jdb lines=261-273 -->

`audit.jdb` keeps the trail:

<!-- include recipes/lib/audit.jdb -->

### How it works

1. **Names.** Every secret of the book has a name that starts with
   `AutomateWork/`: `AutomateWork/mail` for the mail password,
   `AutomateWork/audit-key` for the key that signs the trail. You find
   them in the Control Panel under *Credential Manager*, *Windows
   Credentials*, as generic credentials, and you can change or delete
   them there too.
2. **Storing.** `--set mail` asks twice, without showing what you type,
   and stores the secret only when both answers match. `VAULT.PUT`
   converts name and secret to UTF-16, the form Windows uses, copies
   them into memory Windows can read, fills in the 80 bytes of the
   entry and calls `CredWriteW`. Windows encrypts the secret with a key
   that belongs to your login.
3. **Reading.** `VAULT.GET$` calls `CredReadW`, which answers with a
   pointer to an entry. The module reads the size and the address of
   the secret out of it, copies the bytes and gives the memory back to
   Windows with `CredFree`.
4. **The search.** `TRAIL.SCAN` walks through the folder and its sub
   folders and reads every file that ends in `.jdb`, `.toml`, `.conf`,
   `.ini`, `.json`, `.txt`, `.bat`, `.cmd` or `.ps1`. A line counts when
   a word like *password*, *pwd*, *secret*, *token* or *api_key* is
   followed by `=` or `:` and a value in quotes. The report shows the
   line with every quoted value replaced by stars.
5. **Signing.** `--init-audit` makes a random key of 32 bytes and
   stores it as `AutomateWork/audit-key`. From then on the job server
   finds it and calls `AUDIT.APPEND` after each run. Every line is a
   JSON Web Token signed with that key, and it carries a number and the
   SHA-256 of the line before it.
6. **Checking.** `AUDIT.VERIFY` checks each signature with the key. A
   changed line fails its signature. A removed line leaves a gap in the
   numbers and a hash that points to the wrong line before it.
   `TRAIL.SUMMARY` then counts runs and failures per job for the last
   `summary_days` days.

Be clear about what this protects. The Credential Manager keeps your
secrets out of files, backups and shared folders, and away from other
people who log on to the computer. A program that runs under your own
login can read them, as the recipes do. The trail shows any change made
by someone without the key. The key is in your Credential Manager, so
the trail cannot prove anything against you yourself, and lines cut
off at the end leave no gap. Compare the number of entries with what
you expect, for example with the runs in the state file of the job
server.

### Run it

Without a switch the program lists the secrets the recipes know and
whether each one is stored:

```
jdbasic secrets_audit.jdb
  stored   AutomateWork/mail (lena)
  missing  AutomateWork/audit-key
```

Storing the mail password, then the signing key:

```
jdbasic secrets_audit.jdb --set mail
Secret for mail: ***********
The same once more: ***********
Stored as AutomateWork/mail
jdbasic secrets_audit.jdb --init-audit
Made a signing key; the job server signs from now on.
```

The search over the work folder:

```
jdbasic secrets_audit.jdb --scan
1 line looks like a written down secret
  C:/Users/lena/Documents/AutomateWork\config\old.ini line 4
    password = "****"
```

`--folder` searches another folder, for example the share where the
team keeps its scripts.

### Schedule it

The program needs no schedule; you run it when you set up a secret
and when you want to see the trail. To have the trail checked every
week, add the check to the jobs of the job server. A trail that does
not check out ends the program with an error, so the job counts as
failed and the health check (X13) tells you:

```toml
[[job]]
name = "audit check"
when = "weekly mon 07:00"
program = "X14_secrets_audit/secrets_audit.jdb"
args = ["--verify"]
```

### Make it yours

The settings are in the part of `work.conf` that starts with
`[secrets_audit]`:

```toml
[secrets_audit]
summary_days = 7
names = ["vpn", "ticket-token"]
```

- **Secrets of your own.** Every name in `names` shows up in the list
  and can be stored with `--set`. In your own recipe, read it with one
  line:

  ```basic
  DIM token$ = VAULT.FIND$("AutomateWork/ticket-token", "", "Token: ")
  ```

- **An environment variable instead.** `FIND$` takes the variable you
  name when the Credential Manager has nothing. The health check reads
  `AUTOMATEWORK_MAIL`, which helps on a computer where the job runs
  under an account that has no stored credentials.
- **More words for the search.** The list of words is the text
  `names$` in `LEAKY`. To find connection strings as well, add
  `|connstr` before its closing bracket.

### When it goes wrong

- **"The two did not match; nothing was stored"**: type it again. The
  prompt is made for the keys of an English keyboard. For a password
  with umlauts or other letters, add it in the Control Panel instead:
  *Credential Manager*, *Windows Credentials*, *Add a generic
  credential*, with the name `AutomateWork/mail`.
- **"VAULT: Windows did not store"**: Windows refused the entry, for
  example in a task that runs while nobody is logged on. Run `--set`
  in your own session at the keyboard. The error number in the message
  tells IT what Windows objected to.
- **"The audit trail was changed" with no one to blame**: the file was
  opened in an editor and saved. Some editors add marks at the start of
  a file or change characters, and every changed line fails its
  signature. Restore `audit.log` from the backup, and from then on only
  read it.
- **The search finds a line that is fine**: an example in a comment,
  for instance. Change the example to an empty value or to a call of
  `VAULT.FIND$`, and the line is no longer reported.

> **Balance dividend**
> About 10 minutes a week, and the hour it takes to find and change a
> password that is written into scripts, every time it changes.
