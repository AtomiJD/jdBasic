<!-- pagebreak -->

# Appendix D: The Settings File

Every setting of the recipes lives in one file, `work.conf`, in the `config` folder of your work folder. This appendix lists every key a recipe reads. It is made from the recipes themselves each time the book is built, so it cannot fall behind them.

Each part of the file starts with its name in brackets. Text goes in double quotes, numbers and `true` or `false` do not, and a list is written in square brackets with commas between its items. Inside double quotes a backslash starts a special character, so write folders with forward slashes. A key you leave out takes the default shown here.

## The shared parts

The setup wizard writes `[user]` and `[paths]`, and `[mail]` when you give a mail server. Several recipes read them. The last column names the recipes and shared modules that read a key.

### `[user]`

| Key | What it is | Read by |
|---|---|---|
| `name` | your name, for mail and documents | M15, X02, OUTBOX |
| `email` | your mail address | X11, X13, OUTBOX |
| `day_start` | when your working day starts, as HH:MM | X15 |
| `day_end` | when it ends | X15 |

### `[paths]`

| Key | What it is | Read by |
|---|---|---|
| `work` | the work folder | X01, X02, X04, X06, X10, X14, X16 |
| `jdbasic` | the jdbasic program the scheduled tasks start | X01, X06, X07, X10, X12 |

### `[mail]`

| Key | What it is | Read by |
|---|---|---|
| `server` | the mail server, such as `smtps://mail.example.com:465` | OUTBOX |
| `user` | your user name on the mail server | X14, OUTBOX |
| `from` | the sender of every message; without it your name and address from `[user]` | OUTBOX |
| `outbox` | the folder the messages wait in | OUTBOX |
| `starttls` | `true` when the server wants STARTTLS on port 587 | OUTBOX |

The mail password is never part of `work.conf`. The recipes ask for it when you send, or take it from the Windows Credential Manager under `AutomateWork/mail` (Chapter 4 and X14).

## Chapter 3: the easy recipes

### E01 Downloads Butler: `[downloads_butler]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `source` | Folder to tidy | folder | `"~/Downloads"` |
| `target` | Folder to sort into | folder | `"~/Downloads/Sorted"` |
| `min_age_hours` | Leave files younger than (hours) | number | `24` |
| `rules` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[downloads_butler]` yourself to change it; the default is the program's.

### E02 Morning Launcher: `[morning_launcher]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `every_day` | Open every workday (programs, folders, web pages) | list | `[]` |
| `mon` | Open on Mondays as well | list | `[]` |
| `fri` | Open on Fridays as well | list | `[]` |

### E03 Batch Renamer: `[batch_renamer]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder whose files get dated names | folder | `"~/Documents/Scans"` |
| `pattern` | Pattern for the new name | text | `"{date} {name}{ext}"` |
| `extensions` | Only files with these extensions (empty: all) | list | `["pdf", "jpg", "png"]` |

### E04 One-Click Backup: `[one_click_backup]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folders` | Folders to back up | list | `["~/Documents"]` |
| `target` | Folder for the archives (best on another drive) | folder | `"~/Documents/AutomateWork/backups"` |
| `keep` | Archives to keep | number | `10` |
| `max_file_mb` | Leave out files larger than (MB) | number | `200` |

### E05 Break Reminder: `[break_reminder]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `start` | Working hours start | time | `"08:30"` |
| `end` | Working hours end | time | `"17:30"` |
| `every_minutes` | Remind me every (minutes) | number | `50` |
| `workdays_only` | Only Monday to Friday | bool | `true` |
| `message` | The reminder text | text | `"Time for a break: stand up, stretch, drink some water."` |
| `state_file` | read by the program, not asked by the wizard \* |  | `"~/Documents/AutomateWork/logs/break_reminder.txt"` |

\* Add the key to `[break_reminder]` yourself to change it; the default is the program's.

### E06 Shutdown Ritual: `[shutdown_ritual]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `journal` | Journal file | file | `"~/Documents/AutomateWork/journal/journal.md"` |
| `summary_weekday` | Day of the weekly look back (1 Monday to 5 Friday) | number | `5` |

### E07 Time Tracker: `[time_tracker]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder for the time log and the weekly sheets | folder | `"~/Documents/AutomateWork/time"` |

### E08 Mail Templates: `[mail_templates]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder with your templates | folder | `"~/Documents/AutomateWork/templates"` |
| `me` | Your name under the mail | text | `""` |
| `signature` | The line before your name | text | `"Kind regards"` |
| `values` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[mail_templates]` yourself to change it; the default is the program's.

### E09 Meeting Notes Starter: `[meeting_notes]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `calendar` | Calendar file (.ics) | file | `"~/Documents/AutomateWork/calendar.ics"` |
| `folder` | Folder for the notes | folder | `"~/Documents/Meeting notes"` |
| `lookahead_days` | Look ahead (days) | number | `7` |
| `utc_offset` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[meeting_notes]` yourself to change it; the default is the program's.

### E10 Duplicate Finder: `[duplicate_finder]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folders` | Folders to look through | list | `["~/Documents", "~/Downloads"]` |
| `report` | Excel report | file | `"~/Documents/AutomateWork/duplicates.xlsx"` |
| `min_size_kb` | Leave out files smaller than (KB) | number | `1` |

### E11 Disk Space Report: `[disk_space]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `root` | Folder to measure | folder | `"~"` |
| `warn_gb` | Warn above (GB) | number | `50` |
| `top` | How many folders and files to list | number | `10` |
| `report` | File for the report | file | `"~/Documents/AutomateWork/disk_space.txt"` |

### E12 Birthday Reminder: `[birthday_reminder]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `contacts` | Contact list (CSV) | file | `"~/Documents/AutomateWork/contacts.csv"` |
| `calendar` | Calendar file to write (.ics) | file | `"~/Documents/AutomateWork/birthdays.ics"` |
| `days` | Look ahead (days) | number | `14` |

### E13 Printable Week: `[printable_week]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `todo_file` | Your to-do list (a text file) | file | `"~/Documents/AutomateWork/todo.txt"` |
| `out_folder` | Folder for the printable pages | folder | `"~/Documents/AutomateWork/print"` |
| `days` | Days on the page (5 or 7) | number | `5` |
| `weeks_ahead` | Which week (0 this week, 1 next week) | number | `1` |

### E14 Page Watcher: `[page_watcher]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `store` | Folder for the copies of the watched pages | folder | `"~/Documents/AutomateWork/watch"` |
| `page` | read by the program, not asked by the wizard \* |  | `[]` |

\* Add the key to `[page_watcher]` yourself to change it; the default is the program's.

### E15 Guest Wi-Fi Card: `[guest_wifi]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `ssid` | Name of the guest network | text | `""` |
| `password` | Its password | text | `""` |
| `security` | Security (WPA, WEP or nopass) | text | `"WPA"` |
| `cards` | Cards on the page (1 to 8) | number | `8` |
| `hidden` | The network is hidden | bool | `false` |
| `out_file` | File for the cards | file | `"~/Documents/AutomateWork/print/guest_wifi.pdf"` |

### E16 Form Filler: `[form_filler]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `values` | The values, one per field, in the order of the form | list | `[]` |
| `window` | Title of the form's window (empty: the window in front) | text | `""` |
| `countdown` | Seconds to wait before typing | number | `5` |
| `submit` | Press Enter after the last field | bool | `false` |

## Chapter 4: the medium recipes

### M01 Invoice Generator: `[invoice_generator]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `sheet` | The Excel file with the invoice rows | file | `"~/Documents/AutomateWork/invoices.xlsx"` |
| `folder` | Folder for the invoices | folder | `"~/Documents/AutomateWork/invoices"` |
| `company` | Your company's name | text | `""` |
| `address` | Your company's address (one line) | text | `""` |
| `tab` | Sheet tab with the orders (empty: the first) | text | `""` |
| `prefix` | Start of every invoice number | text | `"RE-"` |
| `format` | Invoice file: pdf or docx | text | `"pdf"` |
| `vat_percent` | VAT in percent | number | `19` |

### M02 Report Merger: `[report_merger]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder with one subfolder per month | folder | `"~/Documents/AutomateWork/reports"` |
| `tab` | Sheet to read in each report (empty: the first) | text | `""` |
| `team` | Names of the people who send a report | list | `[]` |

### M03 Mail Merge: `[mail_merge]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `template` | Word template with {{placeholders}} | file | `"~/Documents/AutomateWork/templates/letter.docx"` |
| `contacts` | Excel file with the contacts | file | `"~/Documents/AutomateWork/contacts.xlsx"` |
| `folder` | Folder for the letters | folder | `"~/Documents/AutomateWork/letters"` |
| `subject` | Subject of the mails | text | `"Invitation to our customer day"` |
| `mail` | Put a mail into the outbox for each letter | bool | `true` |
| `tab` | Sheet tab with the contacts (empty: the first) | text | `""` |
| `file_name` | Name of each letter's file | text | `"{{name}}"` |
| `body` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[mail_merge]` yourself to change it; the default is the program's.

### M04 Receipt Sorter: `[receipt_sorter]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `inbox` | Folder where new receipts land | folder | `"~/Documents/AutomateWork/receipts/inbox"` |
| `folder` | Folder to file the receipts in | folder | `"~/Documents/AutomateWork/receipts"` |
| `rules` | Rules: word in the file name = category | list | `["fuel = Travel", "rail = Travel", "hotel = Travel", "taxi = Travel", "office = Office", "lunch = Meals"]` |
| `kinds` | read by the program, not asked by the wizard \* |  | `["pdf", "jpg", "jpeg", "png"]` |

\* Add the key to `[receipt_sorter]` yourself to change it; the default is the program's.

### M05 Shift and Vacation Planner: `[shift_planner]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `plan_file` | The Excel file with the team plan | file | `"~/Documents/Team/plan.xlsx"` |
| `calendar_file` | Where to write the calendar (a shared folder) | file | `"~/Documents/Team/team.ics"` |
| `max_away` | Warn when more people than this are away | number | `2` |
| `away_kinds` | Kinds that mean away | list | `["Vacation", "Sick", "Training"]` |
| `sheet` | Name of the sheet in the workbook | text | `"Plan"` |
| `title` | Title of the plan | text | `"Team plan"` |

### M06 Friday Status Mail: `[friday_status]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `notes_folder` | Folder with one notes file per person | folder | `"~/Documents/Team/status"` |
| `to` | Who gets the status mail | list | `[]` |
| `subject` | Subject ({week} becomes the week) | text | `"Status {week}"` |
| `clear_done` | Remove done notes after the mail (true or false) | bool | `false` |
| `template` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[friday_status]` yourself to change it; the default is the program's.

### M07 Inbox Unpacker: `[inbox_unpacker]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `inbox_folder` | Folder where you save mails (.eml) | folder | `"~/Documents/AutomateWork/saved mail"` |
| `target_folder` | Folder for the attachments | folder | `"~/Documents/AutomateWork/attachments"` |
| `keep_inline` | Also save pictures inside the text (true or false) | bool | `false` |
| `done_folder` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[inbox_unpacker]` yourself to change it; the default is the program's.

### M08 Personal Dashboard: `[dashboard]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `todo_file` | Your notes file with open items (- [ ] lines) | file | `"~/Documents/AutomateWork/todo.txt"` |
| `downloads` | Downloads folder | folder | `"~/Downloads"` |
| `port` | Port of the page on this computer | number | `8765` |
| `folder` | set in `[time_tracker]`, read here too |  | `"~/Documents/AutomateWork/time"` |
| `template` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[dashboard]` yourself to change it; the default is the program's.

### M09 Log Detective: `[log_detective]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder with the log files | folder | `""` |
| `pattern` | Which files (such as *.log) | text | `"*.log"` |
| `top` | How many frequent errors to list | number | `5` |
| `out_folder` | Folder for the report and chart | folder | `"~/Documents/AutomateWork/logs"` |

### M10 Hot Folder: `[hot_folder]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder to watch | folder | `"~/Documents/AutomateWork/hot"` |
| `done_folder` | Folder for handled files | folder | `"~/Documents/AutomateWork/hot/done"` |
| `rename` | New names ({date}, {time}, {name}) | text | `"{date} {name}"` |
| `separator` | Separator in CSV files | text | `";"` |
| `settle_seconds` | Seconds a file must rest before it is handled | number | `60` |
| `rule` | read by the program, not asked by the wizard \* |  | `[]` |

\* Add the key to `[hot_folder]` yourself to change it; the default is the program's.

### M11 Data Cleaner: `[data_cleaner]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `input` | Export to clean (.csv or .xlsx) | file | `"~/Documents/AutomateWork/export.csv"` |
| `separator` | Separator in CSV files | text | `";"` |
| `day_first` | Dates like 05/10/2026 mean the 5th of October | bool | `true` |
| `name_column` | Column with customer names to compare | text | `"Customer"` |
| `similar` | How alike two names must be (0 to 100) | number | `85` |
| `decimal` | Decimal mark in the files (auto, or , or .) | text | `"auto"` |
| `columns` | read by the program, not asked by the wizard \* |  | *worked out by the program* |
| `output` | read by the program, not asked by the wizard \* |  | `""` |

\* Add the key to `[data_cleaner]` yourself to change it; the default is the program's.

### M12 Meeting Cost Meter: `[meeting_cost]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `calendar` | Calendar file (.ics) to read | file | `"~/Documents/AutomateWork/calendar.ics"` |
| `hourly_rate` | Cost of one hour of work | number | `60` |
| `currency` | Currency | text | `"EUR"` |
| `weeks` | Weeks to look back | number | `4` |
| `top` | How many meetings the report lists | number | `5` |
| `workbook` | Workbook for the report | file | `"~/Documents/AutomateWork/meeting_cost.xlsx"` |

### M13 Snippet Tool: `[snippet_tool]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder for the snippets | folder | `"~/Documents/AutomateWork/snippets"` |
| `values` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[snippet_tool]` yourself to change it; the default is the program's.

### M14 Contract Diff: `[contract_diff]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `reports` | Folder for the change reports | folder | `"~/Documents/AutomateWork/reports"` |
| `similar_percent` | How alike (in percent) a changed paragraph must stay | number | `60` |

### M15 Timesheet for the Boss: `[timesheet]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `log` | Another time log (empty: the one E07 writes) | file | `""` |
| `folder` | Folder for the timesheets | folder | `"~/Documents/AutomateWork/timesheets"` |
| `boss` | Who gets the timesheet (name and address) | text | `""` |
| `folder` | set in `[time_tracker]`, read here too |  | `"~/Documents/AutomateWork/time"` |

### M16 Macro Player: `[macro_player]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder of your macros | folder | `"~/Documents/AutomateWork/macros"` |
| `countdown` | Seconds to wait before a macro starts | number | `5` |

## Chapter 5: the expert recipes

### X01 Personal Job Server: `[job_server]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `jobs` | The jobs file | file | `"~/Documents/AutomateWork/config/jobs.toml"` |
| `folder` | Folder for the state, the log and the lock | folder | `"~/Documents/AutomateWork/logs"` |

### X02 Inbox Assistant: `[inbox_assistant]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `inbox` | Folder where you save mails (.eml) | folder | `"~/Documents/AutomateWork/saved mail"` |
| `drafts` | Folder for the drafts you approve | folder | `"~/Documents/AutomateWork/drafts"` |
| `provider` | Language model service (openai or anthropic) | text | `"openai"` |
| `model` | Model name | text | `"gpt-4o-mini"` |
| `base_url` | Own model server, such as http://localhost:11434/v1 (empty: the service) | text | `""` |
| `max_mails` | Most mails per run | number | `20` |

### X03 Find Anything: `[find_anything]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folders` | Folders to search in | list | `["~/Documents"]` |
| `kinds` | File kinds to read | list | `["txt", "md", "csv", "docx", "pdf"]` |
| `index` | Folder for the search index | folder | `"~/Documents/AutomateWork/find"` |
| `language` | Language of the documents (en, de or none) | text | `"en"` |
| `port` | Port of the search page on this computer | number | `8766` |

### X04 Ticket Sync: `[ticket_sync]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `tracker` | Address of the ticket tracker, such as https://tickets.example.com | text | `""` |
| `user` | Your user name in the tracker | text | `""` |
| `todo` | Your to-do file (- [ ] lines) | file | `"~/Documents/AutomateWork/todo.txt"` |
| `token_env` | Environment variable that holds the token | text | `"TICKETS_TOKEN"` |

### X05 Calendar Bridge: `[calendar_bridge]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `client_id` | Application id of the calendar app (from your IT) | text | `""` |
| `tenant` | Microsoft 365 tenant (organizations, or its id) | text | `"organizations"` |
| `days` | Days ahead to copy | number | `14` |
| `label` | What a busy block is called | text | `"Busy (work)"` |
| `show_titles` | Show meeting subjects (never for private ones) | bool | `false` |
| `output` | Calendar file your personal calendar subscribes to | file | `"~/Documents/AutomateWork/calendar/work-busy.ics"` |
| `authority` | read by the program, not asked by the wizard \* |  | *worked out by the program* |
| `api` | read by the program, not asked by the wizard \* |  | `"https://graph.microsoft.com/v1.0"` |
| `token_file` | read by the program, not asked by the wizard \* |  | `"~/.automatework/calendar_token.json"` |

\* Add the key to `[calendar_bridge]` yourself to change it; the default is the program's.

### X06 Work Cockpit: `[work_cockpit]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `tries` | Starts of a failing automation before it counts as failed | number | `1` |

### X07 Tools for Colleagues: `[tools_for_colleagues]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `release` | Release folder or web address colleagues update from | text | `"~/Documents/AutomateWork/releases"` |
| `builds` | Folder for the builds | folder | `"~/Documents/AutomateWork/builds"` |

### X08 Parallel Crunching: `[parallel_crunching]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder with the exports (CSV) | folder | `"~/Documents/AutomateWork/exports"` |
| `reports` | Folder for the reports | folder | `"~/Documents/AutomateWork/reports"` |
| `separator` | Separator in the CSV files | text | `";"` |
| `workers` | Workers (0: one per core) | number | `0` |
| `timeout_seconds` | Seconds one file may take | number | `30` |

### X09 Report Pipeline: `[report_pipeline]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `database` | The bookings database (SQLite) | file | `"~/Documents/AutomateWork/reports/bookings.db"` |
| `folder` | Folder for the reports and the log | folder | `"~/Documents/AutomateWork/reports"` |
| `to` | Who gets the report (name and address) | text | `""` |

### X10 Let an AI Drive: `[ai_drive]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `allow` | Recipes the agent may run for real (ids, such as E11) | list | `[]` |
| `module` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[ai_drive]` yourself to change it; the default is the program's.

### X11 Web Harvester: `[web_harvester]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `folder` | Folder for the targets, the CSV file and the cache | folder | `"~/Documents/AutomateWork/harvest"` |
| `delay_seconds` | Seconds between two requests to the same site | number | `5` |
| `cache_hours` | Hours a page read once is kept | number | `12` |
| `targets` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[web_harvester]` yourself to change it; the default is the program's.

### X12 Tested Automations: `[tested_automations]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `recipes` | Folder with the recipes to test | folder | `"~/Documents/AutomateWork/recipes"` |
| `reports` | Folder for the test reports and the log | folder | `"~/Documents/AutomateWork/test reports"` |
| `to` | Who gets the mail when a test fails (empty: you) | text | `""` |

### X13 Health Checks and Alerts: `[health_checks]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `alert_to` | Who gets the alerts (empty: your own address) | text | `""` |
| `late_minutes` | Minutes a job may be late before it counts | number | `30` |
| `server_minutes` | Minutes the job server may stay silent | number | `10` |
| `outbox_hours` | Hours a mail may wait in the outbox | number | `24` |
| `send_alerts` | Send alerts at once (needs the mail password from X14) | bool | `false` |
| `notify` | Show new problems in a window (window or none) | text | `"window"` |

### X14 Secrets and Audit Trail: `[secrets_audit]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `summary_days` | Days the audit summary looks back | number | `7` |
| `names` | read by the program, not asked by the wizard \* |  | `[]` |

\* Add the key to `[secrets_audit]` yourself to change it; the default is the program's.

### X15 Balance Score: `[balance_score]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `calendar` | Your calendar exported as an .ics file | file | `"~/Documents/AutomateWork/calendar.ics"` |
| `target_hours` | Hours of a normal week | number | `40` |
| `meeting_share` | Percent of the week in meetings that is still fine | number | `40` |
| `weight_overtime` | Points lost per hour of overtime | number | `2` |
| `weight_meetings` | Points lost per percent of meetings above that | number | `1` |
| `weight_late` | Points lost per hour worked outside your working hours | number | `3` |
| `weight_mail` | Points lost per mail written outside your working hours | number | `1` |
| `port` | Port of the page on this computer | number | `8766` |
| `weeks` | Weeks in the trend | number | `8` |
| `folder` | set in `[time_tracker]`, read here too |  | `"~/Documents/AutomateWork/time"` |
| `template` | read by the program, not asked by the wizard \* |  | *worked out by the program* |

\* Add the key to `[balance_score]` yourself to change it; the default is the program's.

### X16 Desktop Robot: `[desktop_robot]`

| Key | What it is | Kind | Default |
|---|---|---|---|
| `job` | The job file of the robot | file | `"~/Documents/AutomateWork/robot/order_entry.toml"` |
| `countdown` | Seconds to wait before the robot starts | number | `5` |
