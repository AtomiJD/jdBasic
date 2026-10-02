<!-- pagebreak -->

## X12 Tested Automations

### The chore

In March Lena's Monday report covered the wrong week. The clocks had
changed on Sunday night, the report counted seven days back from
midnight, landed on a Sunday, and reported Sunday to Saturday. Nobody
saw it for two weeks, because the numbers looked like numbers. Another
time she improved a shared module on a Friday afternoon, and on Monday
three recipes stopped with an error that had nothing to do with what
she had changed.

Each recipe of this book comes with a test. The tests only help when
somebody runs them, and after a change, nobody remembers to run all
forty of them.

### What you get

Every Sunday evening the test runner looks through the recipes in your
work folder, runs every test it finds, each in a process and a folder
of its own, and writes a report:

```
Test report 2026-10-04 20:00: 48 tests, 1 failed, 0 flaky

FAILED  expert/X09_report_pipeline/report_pipeline_test.jdb
        Error #99: TESTKIT: 2 of 30 assertions failed at line 306
OK  easy/E01_downloads_butler/downloads_butler_test.jdb  (22 checks)
OK  easy/E02_morning_launcher/morning_launcher_test.jdb  (16 checks)
```

When a test fails, the report goes to you as a mail in the outbox, so
you see it before the Monday report goes out. A test that fails once
and passes on the second run is marked *flaky*: it is worth a look,
but it does not wake you.

The recipe also brings a test of a different kind: properties of
Monday's reports, checked against hundreds of days, amounts and logs
that a generator draws at random. They are the check that would have
caught the week that started on a Sunday.

### Before you start

The recipes live in the `recipes` folder of your work folder, where the
wizard copies them, and each has its `_test.jdb` file next to it. The
runner starts every test with the `jdbasic` program the wizard found,
the one in the `[paths]` part of `work.conf`. Its own part is short:

```toml
[tested_automations]
recipes = "~/Documents/AutomateWork/recipes"
reports = "~/Documents/AutomateWork/test reports"
to = ""
```

An empty `to` sends the mail to you, the sender of the `[mail]` part.

### The program

The program finds the tests, runs them and writes the report:

<!-- include recipes/expert/X12_tested_automations/test_runner.jdb -->

The module does the finding and the running:

<!-- include recipes/expert/X12_tested_automations/testrun.jdb -->

And these are the properties of Monday's reports, in the folder
`props` of the recipe:

<!-- include recipes/expert/X12_tested_automations/props/monday_props.jdb -->

### How it works

1. **Finding the tests.** `FOLDERS` walks the recipes folder three
   levels deep and leaves out folders whose name starts with a dot.
   `FIND` keeps every file that ends in `_test.jdb` or `_props.jdb`.
2. **One search path for all.** A test imports the module of its
   recipe, and a property test may import the modules of other recipes
   as well. `SEARCH_PATH$` puts every folder that holds a `.jdb` file
   on the module search path, after the folders that were there
   already, and `RUN_ONE` sets it as `JDBASIC_PATH` for the test.
3. **A process and a folder of its own.** `RunOnce` changes into the
   folder of the test with `CD`, starts `jdbasic` on it with `OS.EXEC`
   and changes back. A test that crashes takes only its own process
   down, and a test that reads a sample file next to itself finds it.
   Every path is made absolute with `ABSOLUTE$` first, because after
   the change of folder a relative path points somewhere else.
4. **Reading the result.** TESTKIT ends with a line like *22 passed, 0
   failed*. `PLAIN$` takes out the colour codes of the console, and
   `COUNTS` reads the two numbers. A test is fine when its process
   ended with 0 and no check failed.
5. **Flaky or failed.** A test that fails is run once more. When the
   second run passes, the result says `flaky`; a test that depends on
   the clock or on a port that was taken behaves like that.
6. **The report.** `REPORT$` writes the failed tests first, each with
   the last line it printed, then the flaky ones, then the rest. The
   program writes it to a file per day, prints it, puts it into a mail
   when something failed, and ends with an error itself, so the Task
   Scheduler or the X01 job server marks the run as failed too.
7. **Properties.** A property is a function that answers TRUE or FALSE
   for one input. `PROPTEST.FORALL` draws 200 inputs from a generator,
   `"int:0..1500"` for a whole number in that range, or
   `"list(int:0..5000,0..40)"` for a list of up to 40 amounts, and
   calls the function with each. `WeekIsBefore` turns a number into a
   day of the years 2026 to 2030 and checks that the week X09 reports
   starts on a Monday, ends six days later and lies before the day.
   `RecipesAgree` checks that X09 and the timesheet of M15 agree on the
   Monday of a week. The seed in `gSeed` makes every run draw the same
   inputs, so a failure can be repeated.

When a property fails, PROPTEST looks for the smallest input that
still fails and names it with the seed. This is what the properties
said about the version of X09 that counted from midnight:

```
Monday's report (X09)
 FAIL 1   the week reported is the week before: falsified by 802
          (seed 20261005, case 3, shrunk from 804)
  ok  2   an amount reads back as written (200 cases)
  ok  3   no booking is lost on the way to the report (200 cases)

two recipes, one calendar (X09 and M15)
 FAIL 4   X09 and M15 agree on the Monday: falsified by 802
          (seed 20261005, case 3, shrunk from 804)
  ok  5   the weeks of a timesheet add up to its month (200 cases)
```

Day 802 after 1 January 2026 is Monday 13 March 2028, the first Monday
after the clocks change in the United States. Within a second the test
names a day that a person would have had to wait two years for.

### Run it

With the long lines wrapped:

```
jdbasic test_runner.jdb --dry-run
48 tests below C:/Users/lena/Documents/AutomateWork/recipes
  C:/Users/lena/Documents/AutomateWork/recipes/easy/
      E01_downloads_butler/downloads_butler_test.jdb
  ...

jdbasic test_runner.jdb --only M15
Test report 2026-10-02 06:44: 1 tests, 0 failed, 0 flaky

OK  C:/Users/lena/Documents/AutomateWork/recipes/medium/
    M15_timesheet/timesheet_test.jdb  (31 checks)
```

All 48 tests of this book take about half a minute. `--only` takes any
part of a path, so `--only expert` runs the tests of one level and
`--only X09` those of one recipe.

### Schedule it

The wizard plans the runner for Sunday at 20:00, so the report is
there before the first recipe of the week starts. In the X01 job
server it is one more job:

```toml
[[job]]
name = "tests"
when = "weekly sun 20:00"
program = "X12_tested_automations/test_runner.jdb"
```

Run it by hand as well, every time you have changed a recipe or a
shared module. That is the moment it pays most.

### Make it yours

- **A property of your own.** Add a function to `monday_props.jdb` and
  one `FORALL` line. This one checks that every month of the timesheet
  has four to six weeks:

  ```basic
  FUNC FourToSixWeeks(offset)
      DIM month$ = LEFT$(Day$(offset), 7)
      DIM n = LEN(TIMESHEET.WEEKS(month$))
      RETURN n >= 4 ANDALSO n <= 6
  ENDFUNC

  PROPTEST.FORALL("a month has four to six weeks", _
      "int:0..1500", FourToSixWeeks@, gSeed)
  ```

- **Search harder on the weekend.** Raise `runs` in `gSeed` to 2000.
  The properties then take a few seconds instead of a fraction of one.
- **A properties file for another recipe.** Any file that ends in
  `_props.jdb` below the recipes folder is run, and it may import the
  module of any recipe. Keep such files in a folder of their own, such
  as `props`, as this recipe does, and check them through the runner:
  `jdbasic --lint` on its own does not know where the other recipes
  keep their modules.

### When it goes wrong

- **"No recipes folder at"**: set `recipes` to the folder the wizard
  copied the recipes to.
- **Every test fails with "cannot load module"**: the module search
  path misses the libraries. Start the runner from a command prompt
  where `jdbasic` itself finds its libraries, or add their folder to
  `JDBASIC_PATH` before the runner starts.
- **A test passes alone and fails in the runner**: it expects to be
  started from a particular folder, or it uses a port or a file that
  another test also uses. The runner starts every test in its own
  folder; a test should find its samples there and make its own
  temporary folders with `MKTEMP$`.
- **The runner does not finish**: a test waits for input or for a
  server that does not answer. The runner has no time limit; find the
  test with `--only` and the levels, and give it one.

> **Balance dividend**
> About 30 minutes a week: the reports that would have gone out wrong,
> the Monday mornings spent finding out why a recipe stopped, and the
> courage to change a shared module on a Friday.
