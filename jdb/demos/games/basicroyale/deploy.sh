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
$SCP server.jdb arena.jdb cards.json "$HOST:~/royale/"

echo "== client =="
$SCP royale.jdb art.jdb ../../../../wasm/index.html "$HOST:~/royale/web/"

if [ "$1" = "--wasm" ]; then
    echo "== runtime + vendor (large) =="
    $SCP ../../../../wasm/jdbasic.js ../../../../wasm/jdbasic.wasm "$HOST:~/royale/web/"
    tar -czf /tmp/vvss_vendor.tgz -C ../../../../wasm vendor
    $SCP /tmp/vvss_vendor.tgz "$HOST:/tmp/"
    $SSH 'cd ~/royale/web && tar -xzf /tmp/vvss_vendor.tgz && rm /tmp/vvss_vendor.tgz'
    rm -f /tmp/vvss_vendor.tgz
fi

echo "== publish + restart =="
$SSH 'sudo -n cp -r ~/royale/web/. /var/www/vvss/ && sudo -n chown -R www-data:www-data /var/www/vvss && sudo -n systemctl restart royale && sleep 2 && systemctl is-active royale'

echo "== check =="
$SSH 'curl -s -m 5 -o /dev/null -w "page %{http_code}  " -H "Host: vvss.jdbasic.tech" http://127.0.0.1/ ; curl -s -m 5 -H "Host: vvss.jdbasic.tech" http://127.0.0.1/api/ | head -c 50; echo'
