#!/usr/bin/env bash
# Recompute the CSP inline-script sha256 hashes from the DEPLOYED jdTrakr + jdweb
# templates and rewrite the nginx script-src directive to match.
#
# Run this on the server AFTER copying any template whose inline <script> changed.
# If you skip it, the browser silently blocks the changed script via CSP: the page
# renders empty (blank columns / dead combos) with NO server error and curl looks
# fine (curl does not enforce CSP). That is exactly the trap this script removes.
#
# Safe: backs up the nginx conf, runs nginx -t, reloads, auto-rolls-back on failure.
# Usage:  ./update_csp.sh           apply
#         ./update_csp.sh --dry     just print the computed script-src
set -euo pipefail

CONF="${CONF:-/etc/nginx/sites-available/jdtrakr}"
TPLDIRS="${TPLDIRS:-/opt/jdtrakr/jdtrakr_tpl /opt/jdtrakr/jdweb_tpl}"
DRY=0
[ "${1:-}" = "--dry" ] && DRY=1

# byte-exact sha256 (base64) of every inline <script>...</script> across all templates
HASHES=$(for d in $TPLDIRS; do
  for f in "$d"/*.html; do
    [ -f "$f" ] || continue
    perl -MDigest::SHA=sha256 -MMIME::Base64 -0777 -e '
      for my $file (@ARGV) {
        open(my $fh, "<", $file) or next; local $/; my $data = <$fh>;
        while ($data =~ /<script>(.*?)<\/script>/sg) {
          print "sha256-" . encode_base64(sha256($1), "") . "\n";
        }
      }' "$f"
  done
done | sort -u)

[ -z "$HASHES" ] && { echo "no inline scripts found - aborting"; exit 1; }

SRC="'self'"
for h in $HASHES; do SRC="$SRC '$h'"; done
echo "computed script-src: $SRC"

if [ "$DRY" = "1" ]; then
  echo "(dry run - nginx not changed)"
  exit 0
fi

TS=$(date +%s)
sudo cp "$CONF" "$CONF.bak-$TS"
sudo sed -i "s#script-src [^\"]*#script-src $SRC#" "$CONF"
if sudo nginx -t; then
  sudo systemctl reload nginx
  echo "CSP updated ($(echo "$HASHES" | wc -l) hashes) and nginx reloaded."
else
  sudo cp "$CONF.bak-$TS" "$CONF"
  echo "nginx -t FAILED - rolled back, no change applied."
  exit 1
fi
