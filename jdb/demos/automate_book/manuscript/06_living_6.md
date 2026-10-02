<!-- pagebreak -->

## The Ledger After Three Months

Chapter 1 asked you to keep a ledger: one line a week, with the
minutes the recipes gave back and what the time went to. After three
months it has about thirteen weeks of lines, enough to answer the
questions that matter. This section reads Mia's ledger as an example.

### Adding up by recipe

Mia added up her minutes per recipe for the thirteen weeks from
October to December. A few lines of jdBasic give the share of each
recipe in the total:

```basic
DIM recipes = ["E01", "E03", "E07", "E08", "E13"]
DIM minutes = [390, 160, 420, 300, 25]
DIM total = SUM(minutes)
PRINT "Minutes in thirteen weeks: "; total
PRINT "Hours: "; ROUND(total / 60, 1)
DIM k = 0
FOR k = 0 TO LEN(recipes) - 1
    DIM share = ROUND(100 * minutes[k] / total)
    PRINT recipes[k]; ": "; minutes[k]; " minutes, "; share; "%"
NEXT k
```

```text
Minutes in thirteen weeks: 1295
Hours: 21.6
E01: 390 minutes, 30%
E03: 160 minutes, 12%
E07: 420 minutes, 32%
E08: 300 minutes, 23%
E13: 25 minutes, 2%
```

Almost 22 hours in a quarter is more than half a working week. Two
recipes, the Time Tracker (E07) and the Downloads Butler (E01), give
almost two thirds of it. The Printable Week (E13) gives almost
nothing, and the previous section retired it.

### Four questions for the numbers

**Were the estimates right?** Every recipe states its minutes per week
in its Balance dividend box. Mia's Downloads Butler gave her 30
minutes a week against the 15 of the box, because her downloads folder
was worse than the typical one. The Time Tracker gave her a little
more than its box says. The difference matters less than the habit: a
number you measured is worth more than one you were promised.

**Which recipe would you miss?** Switch one off for two weeks and see.
Mia did it with the Mail Templates (E08) and switched them back on
after three days.

**Where did the time go?** This is the column that tells the real
story. In Mia's first weeks it said *nothing planned, it disappeared*.
From the third week on it named things: leaving on time on two days,
an hour on Thursday morning for a project that had waited since
spring. Time that had a name in advance stayed hers; time without one
was taken by the next request.

**What is still on the audit?** The chores of Chapter 1 that no recipe
covered are still there. Some of them now look like candidates for the
template of Chapter 4, because three months of recipes made the
pattern visible.

### Other ledgers

Jonas keeps his ledger for the team, not for himself: the invoices,
the merged reports and the Friday status save the team about two hours
a week, and he writes down what the team did with them. His most
useful line was the one that showed the status mail saving him most of
an hour every Friday afternoon, which he now spends on the one-to-one
talks he used to postpone.

Lena lets the Balance Score of X15 do the adding up. It reads her
calendar, her time log and the mail in her outbox, and turns the week
into one number with a trend. She still writes the last column by
hand, because no program knows what the time was for.

### Keep going, or stop

If the ledger shows a steady number and a clear last column, keep it
for another quarter and then decide whether a line a month is enough.
If it shows that the time disappears every week, the ledger has done
its job too: the next step is not another recipe, but a decision about
what the time is for, written down before the week starts.