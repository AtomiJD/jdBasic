#!/usr/bin/env bash
# Push Wunschwelt to the Hostinger box, as a sub-path of vvss.jdbasic.tech.
#   ./deploy.sh            kernels, game, page; restart the world server
#   ./deploy.sh --check    stage the files and run the preflight; publish nothing
#   ./deploy.sh --page    page and client only; the world keeps running
#   ./deploy.sh --wasm     also ship the browser runtime from wasm/ (this game's copy only)
#   ./deploy.sh --setup    once: user, systemd unit "wunschwelt", nginx routes
#   ./deploy.sh --patch <file> <title>   send one patch to the running world
#   ./deploy.sh --wishes   print the wish board of the live world
#   ./deploy.sh --wish <id> <open|building|done|declined>   move a wish along
#   ./deploy.sh --reset-world   forget the saved world and its patches, start fresh
#
# Layout there: /opt/wunschwelt holds the server, the game and its own copy
# of jdBasic; the unit runs as the user "wunschwelt", who owns nothing and
# may not use sudo, on 127.0.0.1:8120. /opt/wunschwelt/env (root only) holds
# the patch key and the address of the model, which is the tunnel from
# cortex on 127.0.0.1:18082. nginx hands /wunschwelt/api/{join,sync,code}
# to the server; the patch entry is reachable on the box alone.
# /var/www/vvss/wunschwelt is the page, .../play the playground with client.jdb.
set -e
cd "$(dirname "$0")"

KEY=~/.ssh/jdtrakr_deploy
# the box as user@address, kept out of the repository
HOST=${WW_HOST:?set WW_HOST to user@address of the box}
SSH="ssh -o BatchMode=yes -i $KEY $HOST"
SCP="scp -q -o BatchMode=yes -i $KEY"
PORT=8120
URL=https://vvss.jdbasic.tech/wunschwelt

push_files() {
    $SSH 'rm -rf ~/ww_stage && mkdir -p ~/ww_stage/game ~/ww_stage/tools ~/ww_stage/web'
    $SCP server.jdb client.jdb "$HOST:~/ww_stage/"
    $SCP game/*.jdb "$HOST:~/ww_stage/game/"
    $SCP tools/patch.jdb tools/nginx_route.jdb tools/preflight.jdb tools/admin.jdb tools/box_preflight.sh "$HOST:~/ww_stage/tools/"
    $SCP web/index.html "$HOST:~/ww_stage/web/"
    # the pictures of the client as raw pixels (the browser runtime reads no
    # PNG) and the list the page loads them by
    $SSH 'mkdir -p ~/ww_stage/assets'
    $SCP assets/*.rgba assets/*.parts "$HOST:~/ww_stage/assets/"
    local mark page listing
    listing=$(mktemp)
    (cd assets && ls *.rgba *.parts) | sed 's|^|assets/|' > "$listing"
    $SCP "$listing" "$HOST:~/ww_stage/assets.txt"
    rm -f "$listing"
    # The playground page belongs to the runtime in wasm/. A browser keeps
    # jdbasic.js and jdbasic.wasm apart in its cache, and an old one beside a
    # new one does not load; the page asks for both under the mark of this
    # runtime, so they change together.
    mark=$(sha256sum ../../wasm/jdbasic.wasm ../../wasm/jdbasic.js | sha256sum | cut -c1-12)
    page=$(mktemp)
    sed -e "s|<script src=\"jdbasic.js\"></script>|<script src=\"jdbasic.js?v=$mark\"></script>|" \
        -e "s|^    createJdBasic({\$|    createJdBasic({\n      locateFile: (file) => file + '?v=$mark',|" \
        ../../wasm/index.html > "$page"
    if [ "$(grep -c "v=$mark" "$page")" != 2 ]; then echo "the playground page did not take the runtime mark"; rm -f "$page"; exit 1; fi
    $SCP "$page" "$HOST:~/ww_stage/playground.html"
    rm -f "$page"
}

# Runs the staged server twice on spare ports with the box's own jdBasic and
# plays a round against each: once without a model, once with a model that
# cannot be reached. Nothing is published when a rule fails there.
preflight() {
    echo "== preflight on the box =="
    $SSH 'bash ~/ww_stage/tools/box_preflight.sh ~/ww_stage'
}

publish_page() {
    $SSH 'sudo -n mkdir -p /var/www/vvss/wunschwelt/play/assets \
      && sudo -n cp ~/ww_stage/web/index.html /var/www/vvss/wunschwelt/ \
      && sudo -n cp ~/ww_stage/client.jdb ~/ww_stage/assets.txt /var/www/vvss/wunschwelt/play/ \
      && sudo -n cp ~/ww_stage/assets/*.rgba ~/ww_stage/assets/*.parts /var/www/vvss/wunschwelt/play/assets/ \
      && sudo -n cp ~/ww_stage/playground.html /var/www/vvss/wunschwelt/play/index.html \
      && sudo -n chown -R www-data:www-data /var/www/vvss/wunschwelt'
}

publish_server() {
    $SSH 'sudo -n mkdir -p /opt/wunschwelt/game /opt/wunschwelt/tools \
      && sudo -n cp ~/ww_stage/server.jdb /opt/wunschwelt/ \
      && sudo -n rm -f /opt/wunschwelt/game/*.jdb \
      && sudo -n cp ~/ww_stage/game/*.jdb /opt/wunschwelt/game/ \
      && sudo -n cp ~/ww_stage/tools/*.jdb ~/ww_stage/tools/*.sh /opt/wunschwelt/tools/ \
      && sudo -n chown -R root:root /opt/wunschwelt/server.jdb /opt/wunschwelt/game /opt/wunschwelt/tools'
}

# The browser runtime of this game, from wasm/ in the repository.
ship_runtime() {
    $SCP ../../wasm/jdbasic.js ../../wasm/jdbasic.wasm "$HOST:~/ww_stage/"
    $SSH 'sudo -n cp ~/ww_stage/jdbasic.js ~/ww_stage/jdbasic.wasm /var/www/vvss/wunschwelt/play/ && sudo -n chown -R www-data:www-data /var/www/vvss/wunschwelt'
}

check() {
    curl -s -m 15 -o /dev/null -w "page %{http_code}  " $URL/
    curl -s -m 15 -o /dev/null -w "client %{http_code}  " $URL/play/client.jdb
    curl -s -m 15 -o /dev/null -w "patch entry from outside %{http_code}  " -X POST -d '{}' $URL/api/patch
    curl -s -m 15 "$URL/api/code?since=999" | head -c 60; echo
    # the client code counts on the runtime in wasm/; another one on the page breaks it in the browser
    local live want
    live=$(curl -s -m 15 -I $URL/play/jdbasic.wasm | tr -d '\r' | awk 'tolower($1)=="content-length:"{print $2}')
    want=$(wc -c < ../../wasm/jdbasic.wasm | tr -d ' ')
    if [ "$live" = "$want" ]; then echo "runtime on the page: the one in wasm/"; else echo "RUNTIME ON THE PAGE DIFFERS from wasm/ ($live bytes there, $want here): run ./deploy.sh --wasm"; exit 1; fi
}

case "$1" in
--patch)
    [ -f "$2" ] && [ -n "$3" ] || { echo "usage: ./deploy.sh --patch <file> <title>"; exit 1; }
    # each run has its own pair of files on the box; the title travels as a
    # file, so no shell on the way reads it as code
    RUN=ww_patch_$$_$(date +%s)
    TITLE_FILE=$(mktemp)
    trap 'rm -f "$TITLE_FILE"' EXIT
    printf '%s' "$3" > "$TITLE_FILE"
    $SCP "$2" "$HOST:~/$RUN.jdb"
    $SCP "$TITLE_FILE" "$HOST:~/$RUN.title"
    echo "== preflight on the box, on a copy of the live world =="
    $SSH "bash /opt/wunschwelt/tools/box_preflight.sh /opt/wunschwelt ~/$RUN.jdb ~/$RUN.title" || { $SSH "rm -f ~/$RUN.jdb ~/$RUN.title"; echo "the patch stays out of the live world"; exit 1; }
    # the key is read on the box by root and never leaves it
    $SSH "sudo -n sh -c 'set -a; . /opt/wunschwelt/env; /opt/wunschwelt/jdBasic /opt/wunschwelt/tools/patch.jdb /home/deploy/$RUN.jdb \"\$(cat /home/deploy/$RUN.title)\" http://127.0.0.1:$PORT'; rc=\$?; rm -f ~/$RUN.jdb ~/$RUN.title; exit \$rc"
    exit 0
    ;;
--setup)
    echo "== files =="
    push_files
    preflight
    echo "== user, runtime, key =="
    $SSH "id wunschwelt >/dev/null 2>&1 || sudo -n useradd --system --no-create-home --shell /usr/sbin/nologin wunschwelt
sudo -n mkdir -p /opt/wunschwelt
sudo -n test -x /opt/wunschwelt/jdBasic || sudo -n cp /home/deploy/jdbasic-1.2/build/jdbasic /opt/wunschwelt/jdBasic
if ! sudo -n test -s /opt/wunschwelt/env; then
  sudo -n install -m 600 -o root -g root /dev/null /opt/wunschwelt/env
  printf 'WW_PATCH_KEY=%s\nWW_LLM_URL=http://127.0.0.1:18082/v1/chat/completions\n' \"\$(openssl rand -hex 24)\" | sudo -n tee /opt/wunschwelt/env > /dev/null
fi
sudo -n chown root:root /opt/wunschwelt/env && sudo -n chmod 600 /opt/wunschwelt/env"
    publish_server
    echo "== systemd unit =="
    $SSH 'sudo -n tee /etc/systemd/system/wunschwelt.service > /dev/null' <<EOF
[Unit]
Description=Wunschwelt world server
After=network.target

[Service]
Type=simple
User=wunschwelt
Group=wunschwelt
WorkingDirectory=/opt/wunschwelt
EnvironmentFile=/opt/wunschwelt/env
Environment=WW_STATE_DIR=/var/lib/wunschwelt
StateDirectory=wunschwelt
StateDirectoryMode=0700
UMask=0077
ExecStart=/opt/wunschwelt/jdBasic /opt/wunschwelt/server.jdb $PORT
Restart=always
RestartSec=3
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
PrivateDevices=true
MemoryMax=1G

[Install]
WantedBy=multi-user.target
EOF
    $SSH 'sudo -n systemctl daemon-reload && sudo -n systemctl enable wunschwelt && sudo -n systemctl restart wunschwelt && sleep 2 && systemctl is-active wunschwelt'
    echo "== nginx routes =="
    $SSH "F=/etc/nginx/sites-available/vvss
R=\$(/opt/wunschwelt/jdBasic /opt/wunschwelt/tools/nginx_route.jdb \$F /home/deploy/ww_stage/vvss.new $PORT | tail -1)
echo \"routes: \$R\"
if [ \"\$R\" = added ]; then
  B=\$F.bak-\$(date +%Y%m%d-%H%M%S)
  sudo -n cp \$F \$B && sudo -n cp /home/deploy/ww_stage/vvss.new \$F
  if sudo -n nginx -t 2>/dev/null; then sudo -n systemctl reload nginx; else sudo -n cp \$B \$F; echo 'nginx refused the routes, site file restored'; exit 1; fi
fi"
    echo "== page =="
    publish_page
    # the editor's bundle is taken from the neighbouring page once; the runtime is this game's own
    $SSH 'sudo -n test -d /var/www/vvss/wunschwelt/play/vendor || sudo -n cp -r /var/www/vvss/tower/play/vendor /var/www/vvss/wunschwelt/play/'
    ship_runtime
    ;;
--wishes)
    # the board is text strangers typed: it is read as data
    $SSH "sudo -n sh -c 'set -a; . /opt/wunschwelt/env; /opt/wunschwelt/jdBasic /opt/wunschwelt/tools/admin.jdb wishes \"\" http://127.0.0.1:$PORT'"
    exit $?
    ;;
--wish)
    case "$2" in ''|*[!0-9]*) echo "usage: ./deploy.sh --wish <id> <open|building|done|declined>"; exit 1;; esac
    case "$3" in open|building|done|declined) ;; *) echo "usage: ./deploy.sh --wish <id> <open|building|done|declined>"; exit 1;; esac
    $SSH "sudo -n sh -c 'set -a; . /opt/wunschwelt/env; /opt/wunschwelt/jdBasic /opt/wunschwelt/tools/admin.jdb wish_status id=$2,status=$3 http://127.0.0.1:$PORT'"
    exit $?
    ;;
--reset-world)
    $SSH 'sudo -n systemctl stop wunschwelt && sudo -n find /var/lib/wunschwelt -mindepth 1 -delete && sudo -n systemctl start wunschwelt && sleep 2 && systemctl is-active wunschwelt'
    ;;
--check)
    push_files
    preflight
    exit 0
    ;;
--page)
    push_files
    preflight
    publish_page
    ;;
*)
    push_files
    preflight
    publish_server
    publish_page
    if [ "$1" = "--wasm" ]; then
        ship_runtime
    fi
    $SSH 'sudo -n systemctl restart wunschwelt && sleep 2 && systemctl is-active wunschwelt'
    ;;
esac

echo "== check =="
check
