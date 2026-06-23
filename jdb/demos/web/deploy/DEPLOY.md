# Deploying jdTrakr on Ubuntu (Hostinger VPS)

Target architecture:

```
Internet --443/TLS--> nginx (TLS) --proxy--> 127.0.0.1:8080  jdTrakr (systemd)
                                                                  |
                                                             jdtrakr.db
```

jdTrakr listens only on loopback. nginx terminates TLS and proxies to it.
The app has its own login, so no extra nginx auth is needed. One SQLite file
(`jdtrakr.db`) holds all data - back that file up and you have everything.

Replace `trakr.example.com` with your real domain throughout. Point an A record
(and AAAA if you use IPv6) at the VPS before requesting a certificate.

---

## 1. Build jdBasic on the box (HTTP + SQLite, headless)

```bash
sudo apt update
sudo apt install -y build-essential libssl-dev git curl

git clone https://github.com/AtomiJD/jdBasic.git
cd jdBasic

# The SQLite amalgamation is not in the repo - fetch it once.
curl -L -o /tmp/sqlite.zip https://sqlite.org/2024/sqlite-amalgamation-3460100.zip
unzip -j /tmp/sqlite.zip 'sqlite-amalgamation-*/sqlite3.c' 'sqlite-amalgamation-*/sqlite3.h' -d bridges/sqlitebridge/

# Minimal headless build: HTTP + SQLite, no GFX/IMGUI/native-compiler.
HTTP=1 SQLITE=1 GFX=0 IMGUI=0 NATIVEC=0 MCPSERVER=0 ./build.sh

./build/jdBasic --version    # expect: Features: HTTP, SQLite
```

Eigen ships inside the repo (`libs/eigen`), so the only system dependency is
OpenSSL (`libssl-dev` to build, `libssl3` at runtime - already on Ubuntu).

If `unzip` is missing: `sudo apt install -y unzip`. The amalgamation URL/version
can be any recent one from https://sqlite.org/download.html.

---

## 2. Lay out the app

```bash
sudo useradd --system --home /opt/jdtrakr --shell /usr/sbin/nologin jdtrakr
sudo mkdir -p /opt/jdtrakr
sudo cp build/jdBasic /opt/jdtrakr/
```

jdTrakr is three files that must sit together: the app `jdtrakr.jdb`, the shared
framework module `jdweb.jdb`, and the config `jdtrakr.json`. Upload all three
(from your machine):

```bash
scp jdb/demos/web/jdtrakr.jdb jdb/demos/web/jdweb.jdb jdb/demos/web/jdtrakr.json you@your-vps:/tmp/
```

On the box, turn on secure cookies (served over HTTPS) in the JSON config, then
install all three:

```bash
sudo sed -i 's/"secure": false/"secure": true/' /tmp/jdtrakr.json
sudo cp /tmp/jdtrakr.jdb /tmp/jdweb.jdb /tmp/jdtrakr.json /opt/jdtrakr/
sudo chown -R jdtrakr:jdtrakr /opt/jdtrakr
```

`IMPORT JDWEB` and the config load resolve relative to the app file, so all
three living in `/opt/jdtrakr` is all it takes. To restyle every future app at
once, edit `jdweb.jdb` (the `THEME$` design tokens) and restart.

---

## 3. systemd service

```bash
sudo cp jdb/demos/web/deploy/jdtrakr.service /etc/systemd/system/jdtrakr.service
sudo systemctl daemon-reload
sudo systemctl enable --now jdtrakr
systemctl status jdtrakr --no-pager
curl -s -o /dev/null -w "local app = %{http_code}\n" http://127.0.0.1:8080/login   # expect 200
```

---

## 4. nginx + TLS

```bash
sudo apt install -y nginx certbot python3-certbot-nginx

sudo cp jdb/demos/web/deploy/nginx-jdtrakr.conf /etc/nginx/sites-available/jdtrakr
sudo sed -i 's/trakr.example.com/YOUR.DOMAIN/' /etc/nginx/sites-available/jdtrakr
sudo ln -s /etc/nginx/sites-available/jdtrakr /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx

sudo certbot --nginx -d YOUR.DOMAIN      # issues the cert and adds the 443 block
```

certbot sets up auto-renewal via a systemd timer. Done.

---

## 5. Firewall

```bash
sudo ufw allow OpenSSH
sudo ufw allow 'Nginx Full'
sudo ufw enable
```

---

## 6. First login

Open `https://YOUR.DOMAIN`. The board is empty and there are no users yet.

- The **first sign-in on an empty board creates the owner account**: type your
  name and a password, and that account is created with that password.
- Then open **Users** and add the rest of the team. Each teammate's **first
  sign-in sets their own password**; after that it is verified.
- Only known user names can sign in (no open self-registration after the first).

---

## Operating notes

- **Backup:** `sudo cp /opt/jdtrakr/jdtrakr.db ~/jdtrakr-$(date +%F).db`
- **Update the app:** upload the changed file(s) (`jdtrakr.jdb`, `jdweb.jdb`
  and/or `jdtrakr.json`) into `/opt/jdtrakr/`, then `sudo systemctl restart
  jdtrakr`. The `.db` is untouched by a restart. `"secure": true` already lives
  in the installed `jdtrakr.json`, so no per-update edit is needed.
- **Logs:** `journalctl -u jdtrakr -f`
- **Passwords** are stored as salted SHA-256 (per-user random salt). To reset a
  user's password, clear it and let them re-claim:
  `sqlite3 /opt/jdtrakr/jdtrakr.db "UPDATE users SET pass_hash=NULL, salt=NULL WHERE name='Name';"`
- **Sessions** live in the `sessions` table; deleting a row logs that token out.
