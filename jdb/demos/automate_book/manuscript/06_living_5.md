<!-- pagebreak -->

## When an Automation Should Be Retired

Every recipe in this book was worth its time when somebody wrote it.
Not every one stays worth it. Work changes, a chore disappears, a
better tool arrives. An automation that no longer pays still costs a
little: it runs, it can fail, it reads files and sometimes sends mail,
and somebody has to keep it in the monthly check.

### Signs that it is time

- **The ledger shows almost nothing.** Mia's Printable Week (E13) gave
  her 25 minutes in three months, because she plans her week in a
  calendar now. The recipe still worked; it was no longer needed.
- **You check its work by hand anyway.** If you open every file it
  sorted to see whether it sorted right, the recipe saves you nothing
  and adds a step. Either fix what makes you doubt it, or retire it.
- **It needs fixing more often than it saves.** A Page Watcher whose
  page changes every month costs more time in repairs than it gives
  back.
- **Nobody reads what it makes.** A report that goes to five people
  and is opened by none is a chore for the computer instead of for you,
  and it still fills five inboxes.
- **The chore moved.** The team changed its ticket system, the reports
  go to a new platform, the colleague who needed the sheet left.

### How to retire it

1. **Tell the people who use its results.** If a recipe sends a mail
   or writes a report that others read, they should hear it from you
   before it stops.
2. **Switch it off in the wizard.** Untick *Use this recipe* on the
   third page and set up again. The wizard removes the recipe's tasks
   from the Task Scheduler and says so for each one. For a job under
   the server of X01, delete its block from `jobs.toml`.
3. **Keep the folder for a month.** Leave the recipe and its part of
   `work.conf` where they are. If you miss it, switching it on again
   costs one click.
4. **Then clean up.** Delete the recipe's folder from
   `AutomateWork\recipes`, its part of `work.conf`, and any secrets it
   kept in the Credential Manager, which X14 removes with
   `jdbasic secrets_audit.jdb --delete` and the secret's name.
5. **Write one line in the ledger.** The date and the reason. The next
   time you are tempted to write a similar recipe, that line saves you
   an evening.

Retiring is not a failure of the recipe or of you. It means the recipe
did its job long enough for the work around it to change, and that you
noticed.