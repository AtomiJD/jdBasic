#!/usr/bin/env bash
# Push jdTower to the Hostinger box, next to BASIC Royale on vvss.jdbasic.tech.
#   ./deploy.sh            game files + pages, restart the score server
#   ./deploy.sh --wasm     also ship the runtime (jdbasic.js/.wasm) and vendor bundle
#   ./deploy.sh --setup    once: systemd unit "jdtower" and the nginx route /tower/api/
#
# Layout there: ~/tower holds server.jdb and its players.json (systemd unit
# "jdtower", bound to 127.0.0.1:8082); /var/www/vvss/tower is the landing
# page, /var/www/vvss/tower/play the playground running jdtower.jdb.
set -e
cd "$(dirname "$0")"

KEY=~/.ssh/jdtrakr_deploy
HOST=deploy@72.60.176.59
SSH="ssh -o BatchMode=yes -i $KEY $HOST"
SCP="scp -q -o BatchMode=yes -i $KEY"
WASM=../../../../wasm

if [ "$1" = "--setup" ]; then
    echo "== systemd unit =="
    $SSH 'mkdir -p ~/tower && sudo -n tee /etc/systemd/system/jdtower.service > /dev/null' <<'EOF'
[Unit]
Description=jdTower score server
After=network.target

[Service]
Type=simple
User=deploy
WorkingDirectory=/home/deploy/tower
ExecStart=/home/deploy/jdbasic-1.2/build/jdbasic /home/deploy/tower/server.jdb 127.0.0.1 8082
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
    $SCP server.jdb "$HOST:~/tower/"
    $SSH 'sudo -n systemctl daemon-reload && sudo -n systemctl enable --now jdtower && sleep 2 && systemctl is-active jdtower'

    echo "== nginx route /tower/api/ =="
    # inserted in front of the royale /api/ block, once; nginx -t guards the reload
    $SSH 'F=/etc/nginx/sites-enabled/vvss
if grep -q "location /tower/api/" $F; then echo "route already there"; else
sudo -n cp $F /home/deploy/vvss.nginx.bak
sudo -n python3 - "$F" <<"PY"
import sys
p = sys.argv[1]
s = open(p).read()
block = """    location /tower/api/ {
        proxy_pass http://127.0.0.1:8082/;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 30s;
    }

"""
anchor = "    location /api/ {"
assert anchor in s
open(p, "w").write(s.replace(anchor, block + anchor, 1))
PY
fi
sudo -n nginx -t && sudo -n systemctl reload nginx && echo "nginx reloaded"'
    exit 0
fi

echo "== score server =="
$SCP server.jdb "$HOST:~/tower/"

echo "== pages and game =="
$SSH 'mkdir -p ~/tower/web/play/data/levels'
$SCP web/index.html web/manifest.json web/sw.js web/icon-180.png web/icon-192.png web/icon-512.png web/icon-mask.png "$HOST:~/tower/web/"
# the client asks for data files with ?v=<hash of data/>, so browsers drop the old ones
DATA_VER=$(cat data/*.json data/levels/*.json | md5sum | cut -c1-10)
sed "s/^DIM DATA_VER$ = \"dev\"/DIM DATA_VER$ = \"$DATA_VER\"/" jdtower.jdb > /tmp/jdtower_deploy.jdb
grep -q "DATA_VER$ = \"$DATA_VER\"" /tmp/jdtower_deploy.jdb || { echo "DATA_VER stamp failed"; exit 1; }
$SCP /tmp/jdtower_deploy.jdb "$HOST:~/tower/web/play/jdtower.jdb"
rm -f /tmp/jdtower_deploy.jdb
$SCP sim.jdb $WASM/index.html "$HOST:~/tower/web/play/"
$SCP data/towers.json data/enemies.json data/lang.json data/achievements.json "$HOST:~/tower/web/play/data/"
$SCP data/levels/*.json "$HOST:~/tower/web/play/data/levels/"

if [ "$1" = "--wasm" ]; then
    echo "== runtime + vendor (large) =="
    $SCP $WASM/jdbasic.js $WASM/jdbasic.wasm "$HOST:~/tower/web/play/"
    tar -czf /tmp/tower_vendor.tgz -C $WASM vendor
    $SCP /tmp/tower_vendor.tgz "$HOST:/tmp/"
    $SSH 'cd ~/tower/web/play && tar -xzf /tmp/tower_vendor.tgz && rm /tmp/tower_vendor.tgz'
    rm -f /tmp/tower_vendor.tgz
fi

echo "== publish + restart =="
$SSH 'sudo -n mkdir -p /var/www/vvss/tower && sudo -n cp -r ~/tower/web/. /var/www/vvss/tower/ && sudo -n chown -R www-data:www-data /var/www/vvss/tower && sudo -n systemctl restart jdtower && sleep 2 && systemctl is-active jdtower'

echo "== check =="
curl -s -m 15 -o /dev/null -w "page %{http_code}  " https://vvss.jdbasic.tech/tower/
curl -s -m 15 -o /dev/null -w "jdtower.jdb %{http_code}  " https://vvss.jdbasic.tech/tower/play/jdtower.jdb
curl -s -m 15 https://vvss.jdbasic.tech/tower/api/ | head -c 80; echo
