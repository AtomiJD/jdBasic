# TMPL - HTML templates with layouts, loops and filters

`lib/tmpl.jdb` renders a template against a model map. A hole is escaped
for HTML unless it asks to be raw; conditions, loops, includes and layouts
with blocks structure the page; filters format what goes in. A template
file is parsed once and served from a cache until the file changes.

jdweb renders its page shell through it, and so does every jdTrakr page.

Stands in for: Jinja2.

## Quick start

```basic
IMPORT TMPL

DIM kat AS MAP
kat{"name"} = "<b>Kat</b>"
kat{"tickets"} = ["one", "two"]
PRINT TMPL.RENDERSTR$("Hi {{ name }}, {{ tickets | length }} open", kat)
' Hi &lt;b&gt;Kat&lt;/b&gt;, 2 open

DIM html$ = TMPL.RENDER$("views/page.html", kat)
```

`views/page.html`:

```html
{% extends 'layout.html' %}
{% block title %}Tickets{% endblock %}
{% block content %}
<ul>{% for t in tickets %}<li>{{ t | upper }}</li>{% endfor %}</ul>
{% endblock %}
```

## API

| Call | What it does |
|------|--------------|
| `RENDER$(path$, model)` | Renders a template file. Includes and layouts are found next to it. The parsed file is cached and parsed again only when its modification time changes. |
| `RENDERSTR$(src$, model)` | Renders a template held in a string, parsed on every call. Includes and layouts are found relative to the working directory. |
| `ADDFILTER(name$, fn)` | Registers a custom filter: `fn` is a funcref called as `fn(text$, arg$)` that answers the new text. |
| `COMPILES()` | How many times a template has been parsed, for tests of the cache. |

## Syntax

### Holes

| Form | Writes |
|------|--------|
| `{{ path }}` | the value, escaped for HTML: `& < > " '` |
| `{{{ path }}}` | the value as it is |
| `{{ path \| filter \| filter:'arg' }}` | the value run through filters, left to right |

A path walks the model: `user.name`, `items[0].title`, `items.0.title`.
A missing key, an index out of range, or a map or an array written on its
own gives an empty text. A number is written as it prints.

### Conditions

```
{% if state == 'todo' %} ... {% elseif state != "done" %} ... {% else %} ... {% endif %}
```

A condition is a path (true when the value is there and not empty, not
zero and not FALSE; an array when it has items; a map always), `not path`,
or a path compared with `==` or `!=` to a quoted text or to another path.

### Loops

```
{% for t in tickets %} {{ t.title }} {% endfor %}
```

The loop name is visible inside the loop, next to every name of the
model, and is gone after it. A missing or empty array writes nothing.

### Includes and layouts

| Tag | What it does |
|-----|--------------|
| `{% include 'row.html' %}` | renders another file here, with the same names in view |
| `{% extends 'layout.html' %}` | this file fills the blocks of a layout instead of standing on its own |
| `{% block name %} ... {% endblock %}` | an override point in a layout, and the content that fills it in a page |

A layout block the page does not fill keeps its own content. A file that
cannot be found leaves `<!-- tmpl: not found: path -->` in the page.

### Filters

| Filter | What it does |
|--------|--------------|
| `upper`, `lower`, `trim` | the text in capitals, in small letters, without surrounding spaces |
| `length` | the number of items of an array, or of characters of a text; 0 for anything else |
| `default:'x'` | `x` when the value is missing, empty, zero or FALSE; the value itself otherwise, still an array if it was one |
| `date:'DD.MM.YYYY HH:mm'` | a date written `YYYY-MM-DD`, with a time after a space or a `T`, rearranged; the default form is `DD.MM.YYYY`. The fields are used as written, so no time zone shifts them; any other text is left as it is |
| `money:'EUR'` | two decimals with a decimal comma and the currency after it, `EUR` by default |

An unknown filter leaves the value unchanged. A custom filter receives the
text produced so far:

```basic
FUNC Stars(text$, arg$)
    RETURN REPEAT$("*", VAL(text$))
ENDFUNC
TMPL.ADDFILTER("stars", Stars@)
' {{ rating | stars }}
```

Give the filter function string parameters, as above; that is also what
makes it work compiled with `-c`.

## What stays as written

- An unclosed `{{` or `{%` is text.
- A tag that is not one of the above is written back as it was.

## Notes

- Under `HTTP.SERVER`, render each template once before `START`, so the
  handlers only read the warm cache.
- On a server, `tmpl.jdb` has to be where `IMPORT` finds it: next to the
  script that imports it (next to `jdweb.jdb` for jdweb apps), in a
  `modules/` folder beside it, or on `JDBASIC_PATH`. The jdTrakr runbook
  copies it next to `jdweb.jdb`.
- Everything works compiled with `-c`.

Self test: `tests/jdlibs/tmpl_selftest.jdb`.
Demo: `jdb/demos/jdlibs/tmpl_demo.jdb`; a web server rendering through it:
`jdb/demos/web/tmpl_server.jdb`.
