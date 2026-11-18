#!/usr/bin/env bash
# Push the story cards to the Hostinger box, as a sub-path of vvss.jdbasic.tech.
#   XMAS_HOST=user@address ./deploy.sh spike     the spike under /xmas/spike/
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

deploy_spike() {
    local dest=/var/www/vvss/xmas/spike stage=xmas_stage mark page
    $SSH "rm -rf ~/$stage && mkdir -p ~/$stage/art"
    $SCP spike.jdb assets.txt "$HOST:~/$stage/"
    $SCP art/*.rgbz "$HOST:~/$stage/art/"
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
    $SSH "sudo -n mkdir -p $dest/art \
      && sudo -n cp ~/$stage/*.jdb ~/$stage/assets.txt ~/$stage/index.html ~/$stage/jdbasic.js ~/$stage/jdbasic.wasm $dest/ \
      && sudo -n cp ~/$stage/art/*.rgbz $dest/art/ \
      && (sudo -n test -d $dest/vendor || sudo -n cp -r /var/www/vvss/tower/play/vendor $dest/) \
      && sudo -n chown -R www-data:www-data /var/www/vvss/xmas \
      && rm -rf ~/$stage"
    local url=$SITE/xmas/spike/
    curl -s -m 15 -o /dev/null -w "page %{http_code}  " "$url"
    curl -s -m 15 -o /dev/null -w "program %{http_code}  " "${url}spike.jdb"
    curl -s -m 15 -o /dev/null -w "art %{http_code}  " "${url}art/santa_head.rgbz"
    curl -s -m 15 -o /dev/null -w "vendor %{http_code}\n" "${url}vendor/xterm/xterm.js"
    echo "${url}?prog=spike&fs=1&assets=assets.txt"
}

case "$1" in
spike) deploy_spike ;;
*) echo "usage: XMAS_HOST=user@address ./deploy.sh spike"; exit 1 ;;
esac
