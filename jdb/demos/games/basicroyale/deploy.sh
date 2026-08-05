#!/usr/bin/env bash
# Push BASIC Royale to the Hostinger box.
#   ./deploy.sh          game files + client, restart the match server
#   ./deploy.sh --wasm   also ship the 10 MB runtime and the vendor bundle
#
# Layout there: ~/royale holds the server (systemd unit "royale", bound to
# 127.0.0.1:8081), /var/www/vvss holds the page nginx serves as
# vvss.jdbasic.tech with /api/ proxied to the match server.
set -e
cd "$(dirname "$0")"

KEY=~/.ssh/jdtrakr_deploy
HOST=deploy@72.60.176.59
SSH="ssh -o BatchMode=yes -i $KEY $HOST"
SCP="scp -q -o BatchMode=yes -i $KEY"

echo "== server =="
$SCP server.jdb arena.jdb cards.json lang.json "$HOST:~/royale/"

echo "== landing page =="
# the site root is the poster page; the playground lives under /play
$SSH 'mkdir -p ~/royale/web/play'
$SCP web/index.html web/admin.html web/players.html web/cards.html web/balance.html \
     web/balance.json web/hero.png web/icon.png web/social.png "$HOST:~/royale/web/"
# card art, rendered by makecards.jdb from the same figures the game draws
$SSH 'mkdir -p ~/royale/web/cards'
$SCP web/cards/*.png "$HOST:~/royale/web/cards/"

echo "== render card figures =="
# a card edited in cards.json must not reach the sheet with yesterday's
# picture, so the PNGs are rendered from the file that is about to ship
JDB=../../../../build/jdBasic.exe
[ -x "$JDB" ] || JDB=../../../../build/jdbasic
if [ -x "$JDB" ]; then
    "$JDB" makecards.jdb | tail -1
else
    echo "no local jdbasic - shipping web/cards as it stands"
fi

echo "== card figures =="
# makecards.jdb renders one PNG per card; a new card is invisible on the
# sheet until its figure travels with the page
$SSH 'mkdir -p ~/royale/web/cards'
$SCP web/cards/*.png "$HOST:~/royale/web/cards/"

echo "== client =="
$SCP royale.jdb art.jdb ../../../../wasm/index.html "$HOST:~/royale/web/play/"

if [ "$1" = "--wasm" ]; then
    echo "== runtime + vendor (large) =="
    $SCP ../../../../wasm/jdbasic.js ../../../../wasm/jdbasic.wasm "$HOST:~/royale/web/play/"
    tar -czf /tmp/vvss_vendor.tgz -C ../../../../wasm vendor
    $SCP /tmp/vvss_vendor.tgz "$HOST:/tmp/"
    $SSH 'cd ~/royale/web/play && tar -xzf /tmp/vvss_vendor.tgz && rm /tmp/vvss_vendor.tgz'
    rm -f /tmp/vvss_vendor.tgz
fi

echo "== publish + restart =="
$SSH 'sudo -n cp -r ~/royale/web/. /var/www/vvss/ && sudo -n chown -R www-data:www-data /var/www/vvss && sudo -n systemctl restart royale && sleep 2 && systemctl is-active royale'

echo "== check =="
# nginx redirects port 80 to https since certbot ran, so check the real URL
curl -s -m 15 -o /dev/null -w "page %{http_code}  client %{time_total}s\n" https://vvss.jdbasic.tech/
curl -s -m 15 -o /dev/null -w "royale.jdb %{http_code}  " https://vvss.jdbasic.tech/play/royale.jdb
curl -s -m 15 https://vvss.jdbasic.tech/api/ | head -c 60; echo
