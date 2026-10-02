<!-- pagebreak -->

## Your Own Recipe

Sooner or later the chore that costs you the most time is not in this
book. This section takes you from an empty folder to a recipe of your
own that the wizard sets up like any other. It starts from a template
that does a small job of its own, so you begin with something that
works and change it step by step.

### The template

The folder `recipes/template/T00_my_recipe` holds a complete recipe:
it moves files that nobody changed for a number of days into an
archive folder. It has the same five files every recipe in this book
has:

| File | What it holds |
|---|---|
| `my_recipe.jdb` | The program: reads the settings, makes a plan, carries it out |
| `mine.jdb` | The module with the work itself, in functions a test can call |
| `my_recipe_test.jdb` | The test that proves the module does what it says |
| `recipe.toml` | What the wizard needs to know: name, schedule, settings |
| a page of text | How it works, for you in half a year |

The program is short, because it only connects things:

<!-- include recipes/template/T00_my_recipe/my_recipe.jdb -->

The work happens in the module, in two functions. `PLAN` looks and
decides; it changes nothing and answers a list. `APPLY` carries a list
out, or prints it on a dry run:

<!-- include recipes/template/T00_my_recipe/mine.jdb -->

Splitting the work this way is the most useful idea of the whole book.
A plan can be printed, checked and tested before anything happens on
your disk. Every recipe you met in Chapters 3 and 4 works like this.

### From template to recipe, step by step

1. **Copy the folder.** Copy `T00_my_recipe` into `recipes/medium` and
   give it a name with a number of your own, such as
   `M50_quote_tracker`. Rename the three `.jdb` files to match.
2. **Describe the chore in one sentence.** Write it as the first line
   of the program, after the file name. If it does not fit into one
   sentence, it is two recipes.
3. **Write the plan first.** Change `PLAN` so that it answers what your
   recipe would do, as a list of maps, one per action. Print the list
   from a small test program and look at it. Nothing has happened yet,
   so you can try as often as you like.
4. **Write the test.** For every case your plan decides, add one check
   to the test: the old file is in the plan, the new one is not, an
   empty folder gives an empty plan. Use `MKTEMP$` for a folder of
   your own, so the test never touches your real files.
5. **Carry the plan out.** Change `APPLY`. Keep the dry run: print what
   would happen, and only act when `dry_run` is false.
6. **Move the settings out.** Every value you might change later, a
   folder, a number of days, an address, goes into the program's
   `work.conf` section through `WORKCONF.VALUE` or `WORKCONF.FOLDER$`,
   with a sensible default.
7. **Tell the wizard.** Change `recipe.toml`: the id, the name, the
   program, the module, one `[[setting]]` per setting with its label
   and default, and a schedule. From now on the wizard lists your
   recipe with the others.
8. **Run it for a week by hand.** Run it with `--dry-run` every day and
   compare what it would do with what you would have done. When the
   two agree for a week, switch the schedule on.

> **Try this**
> Before you write a single line, write the "Balance dividend" box of
> your recipe: how many minutes a week it gives back. If the number is
> below five, the recipe is probably not worth the time it costs to
> write. If it is above sixty, check whether it should be two recipes.

### The checklist

Run through this list before you schedule a recipe of your own:

- It has a dry run, and the dry run changes nothing.
- It never overwrites a file; a name that is taken gets a number.
- It reads every setting from `work.conf` and has a default for each.
- It needs no password in a file; mail goes through the outbox.
- Its test passes, and the test uses folders of its own.
- `jdbasic --lint` on each of its files finds nothing.
- It prints one line per thing it does, so a log shows what happened.
- Somebody else could read the first comment and know what it is for.

A recipe that passes the list is ready for the Task Scheduler. A
recipe that also runs as a compiled program, as every recipe of this
book does, is ready to be handed to a colleague; Chapter 5 shows how.
