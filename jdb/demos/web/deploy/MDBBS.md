# Deploying jdBBS

```
board / browser --80--> nginx --proxy--> 127.0.0.1:8100  mdbbs (systemd)
                                                              |
                                                        mdbbs_site/*.md
```

Plain HTTP on purpose. The clients are small boards and the content is a
public page of text; a handshake they have to pay for buys nothing here.
Add a certificate when you like, but do not add a redirect from 80, or
the boards lose the site.

## Lay it out

```bash
sudo mkdir -p /opt/mdbbs
sudo tar xzf mdbbs.tgz -C /opt/mdbbs          # mdbbs.jdb + mdbbs_site/
sudo cp /opt/jdtrakr/jdBasic /opt/mdbbs/      # any HTTP-capable build
sudo useradd --system --home /opt/mdbbs --shell /usr/sbin/nologin mdbbs
sudo chown -R mdbbs:mdbbs /opt/mdbbs
```

## Run it

```bash
sudo cp mdbbs.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now mdbbs
curl -s http://127.0.0.1:8100/index.md | head -3
```

## Publish it

```bash
sudo cp nginx-mdbbs.conf /etc/nginx/sites-available/mdbbs
sudo ln -sf /etc/nginx/sites-available/mdbbs /etc/nginx/sites-enabled/mdbbs
sudo nginx -t && sudo systemctl reload nginx
```

The vhost answers to the name and to the bare address, so a board can
reach it before DNS has caught up. Once the A record for the name is in
place, the address can come out of `server_name`.

## Adding a page

Drop a forty column `.md` into `mdbbs_site/`, link it from `index.md`,
and it is served. No restart: the file is read per request. A program in
`mdbbs_site/progs/` is offered as a download by any page that links to
it.

## Colour

The pages are markdown with one addition, because a board with eight
colours should be allowed to use them:

```
{cyan} ---- MAIN MENU ----------------------{/}
 A {orange}little dirty language{/}, running on
```

`{name}` starts a colour and `{/}` ends it; the end of a line ends it
too, so a whole line can be coloured by opening at the front and never
closing. The names are `red green yellow blue magenta cyan white gray
orange`, which is what a terminal has and therefore what every reader of
these pages can show. An unknown name is left alone as text.

Two renderers honour it. The HTML one wraps the run in a span; the board
one emits the terminal escape and, importantly, does not count it
towards the forty columns - the wrap measures what is visible and a line
broken mid-colour carries that colour onto the next.

Blue is the one to go easy on: the Fruit Jam's framebuffer has two bits
of it against three of everything else.
