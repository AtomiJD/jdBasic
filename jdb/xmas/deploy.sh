#!/usr/bin/env bash
# Push the story cards to the Hostinger box, as a sub-path of vvss.jdbasic.tech.
#   XMAS_HOST=user@address ./deploy.sh site
#
# /xmas/       the landing page (web/index.html) with its preview picture: a
#              link with ?s= goes on to the player, a wide screen to the
#              editor, a phone sees a short page with the demo
# /xmas/play/  the runtime page, player.jdb, editor.jdb, the module and its art
#
# Static files only: nginx serves /var/www/vvss as it is. The runtime page is
# wasm/index.html with one mark on jdbasic.js and jdbasic.wasm, so a browser
# never pairs an old one with a new one; its vendor/ (terminal, editor) is
# taken once from the tower page on the box.
set -e
cd "$(dirname "$0")"

KEY=~/.ssh/jdtrakr_deploy
HOST=${XMAS_HOST:?set XMAS_HOST to user@address of the box}
SSH="ssh -o BatchMode=yes -i $KEY $HOST"
SCP="scp -q -o BatchMode=yes -i $KEY"
SITE=https://vvss.jdbasic.tech
ROOT=/var/www/vvss/xmas

deploy_site() {
    local play=$ROOT/play stage=xmas_stage mark page
    $SSH "rm -rf ~/$stage && mkdir -p ~/$stage/art ~/$stage/web"
    $SCP player.jdb editor.jdb xmasplay.jdb assets.txt "$HOST:~/$stage/"
    $SCP art/*.rgbz art/bg_*.jpg "$HOST:~/$stage/art/"
    $SCP web/index.html web/og.jpg "$HOST:~/$stage/web/"
    $SCP ../../lib/raster.jdb ../../lib/xml.jdb ../../lib/img.jdb ../../lib/ttf.jdb "$HOST:~/$stage/"
    $SCP ../../wasm/jdbasic.js ../../wasm/jdbasic.wasm "$HOST:~/$stage/"
    mark=$(sha256sum ../../wasm/jdbasic.wasm ../../wasm/jdbasic.js | sha256sum | cut -c1-12)
    page=$(mktemp)
    trap 'rm -f "$page"' EXIT
    sed -e "s|<script src=\"jdbasic.js\"></script>|<script src=\"jdbasic.js?v=$mark\"></script>|" \
        -e "s|^    createJdBasic({\$|    createJdBasic({\n      locateFile: (file) => file + '?v=$mark',|" \
        ../../wasm/index.html > "$page"
    if [ "$(grep -c "v=$mark" "$page")" != 2 ]; then echo "the runtime page did not take the runtime mark"; rm -f "$page"; exit 1; fi
    $SCP "$page" "$HOST:~/$stage/index.html"
    rm -f "$page"
    $SSH "sudo -n mkdir -p $play/art \
      && sudo -n cp ~/$stage/*.jdb ~/$stage/assets.txt ~/$stage/index.html ~/$stage/jdbasic.js ~/$stage/jdbasic.wasm $play/ \
      && sudo -n cp ~/$stage/art/*.rgbz ~/$stage/art/bg_*.jpg $play/art/ \
      && (sudo -n test -d $play/vendor || sudo -n cp -r /var/www/vvss/tower/play/vendor $play/) \
      && sudo -n cp ~/$stage/web/index.html ~/$stage/web/og.jpg $ROOT/ \
      && sudo -n chown -R www-data:www-data $ROOT \
      && rm -rf ~/$stage"
    curl -s -m 15 -o /dev/null -w "landing %{http_code}  " "$SITE/xmas/"
    curl -s -m 15 -o /dev/null -w "preview %{http_code}  " "$SITE/xmas/og.jpg"
    curl -s -m 15 -o /dev/null -w "runtime %{http_code}  " "$SITE/xmas/play/"
    curl -s -m 15 -o /dev/null -w "player %{http_code}  " "$SITE/xmas/play/player.jdb"
    curl -s -m 15 -o /dev/null -w "editor %{http_code}  " "$SITE/xmas/play/editor.jdb"
    curl -s -m 15 -o /dev/null -w "vendor %{http_code}\n" "$SITE/xmas/play/vendor/xterm/xterm.js"
    echo "$SITE/xmas/"
}

case "$1" in
site) deploy_site ;;
*) echo "usage: XMAS_HOST=user@address ./deploy.sh site"; exit 1 ;;
esac
