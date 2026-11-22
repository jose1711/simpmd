#!/bin/bash
# Tests of the menus without a display: the games list (games.lst), the favorites and their popup menu, the PNG pictures
# and the exit while the popup menu is open. Drives ../bin/simpmd with PMD_MENUSCRIPT in a scratch share directory.
# The binary has to be built with the default (empty) PMD_SHARE, otherwise the test is skipped.
set -u
BIN=$(cd "$(dirname "$0")/.." && pwd)/bin/simpmd
DATA=$(cd "$(dirname "$0")/.." && pwd)/data
[ -x "$BIN" ] || { echo "SKIP menu test: $BIN is not built"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/share/screenshots"
cp "$DATA"/roms/M* "$DATA"/font/* "$DATA"/logo/* "$T/share/"
for g in AUTO FLAPPY FRED ZED; do cp "$DATA"/tapes/*/$g "$T/share/"; done
cp "$DATA/screenshots/AUTO.png" "$T/share/screenshots/"
# Not sorted, with a category, a comment and a line that is not a game.
cat > "$T/share/games.lst" <<'LST'
# test list
#CATEGORY Kat
ZED;1;MGLD 00, JUMP 1000
FRED;2;MGLD 03;;;;;Kat
not a game
AUTO;1;MGLD 00, JUMP 2000
FLAPPY;1;MGLD 03
LST
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy PMD_OK_MS=300 PMD_STATEDIR="$T/states"
cd "$T/share" || exit 1
FAIL=0
ok() { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; FAIL=1; }
run() { PMD_FAVORITES="$T/fav.lst" PMD_MENUSCRIPT="$1" timeout 60 "$BIN" >"$T/out.txt" 2>&1; }

echo "Games list"
# The first line of the output is the message of the start of SDL.
LIST=$("$BIN" --list 2>/dev/null | grep -v '^SDL ' | tr '\n' ' ')
if [ "$LIST" != "AUTO FLAPPY FRED ZED " ]; then
	if [ -z "$LIST" ] || ! "$BIN" --list 2>/dev/null | grep -qx ZED; then
		echo "SKIP menu test: simpmd reads another share directory (built with PMD_SHARE)"; exit 0
	fi
	bad "--list prints '$LIST'"
else
	ok "games.lst is parsed and sorted, lines that are not games are skipped"
fi

echo "Favorites"
rm -f "$T/fav.lst"
run "Right Return:hold Down Return quit"
[ "$(tr '\n' ' ' < "$T/fav.lst" 2>/dev/null)" = "FLAPPY " ] && ok "default favorites (those of games.lst), the second one removed" \
	|| bad "after the removal: '$(cat "$T/fav.lst" 2>/dev/null)'"
[ ! -e "$T/fav.lst.tmp" ] && ok "no temporary file is left" || bad "fav.lst.tmp is left"
run "Up Down Return:hold Down Return quit"
[ "$(tr '\n' ' ' < "$T/fav.lst")" = "FLAPPY AUTO " ] && ok "a game of the list added" || bad "after the add: '$(cat "$T/fav.lst")'"
run "Up Down Return:hold Escape Return:hold Down Return quit"
[ "$(tr '\n' ' ' < "$T/fav.lst")" = "FLAPPY " ] && ok "Esc closes the popup menu, the game removed in the list" \
	|| bad "after Esc and the removal: '$(cat "$T/fav.lst")'"

echo "Pictures"
printf 'AUTO\n' > "$T/fav.lst"
run "shot=$T/png.bmp quit"
printf 'not a png' > "$T/share/screenshots/AUTO.png"
run "shot=$T/broken.bmp quit"
RC=$?
rm "$T/share/screenshots/AUTO.png"
run "shot=$T/none.bmp quit"
[ $RC -eq 0 ] && [ -s "$T/broken.bmp" ] && ok "a broken PNG does not stop the menu" || bad "broken PNG: exit code $RC"
cmp -s "$T/broken.bmp" "$T/none.bmp" && ok "a broken PNG is shown as no picture" || bad "a broken PNG is not shown as no picture"
[ -s "$T/png.bmp" ] && ! cmp -s "$T/png.bmp" "$T/none.bmp" && ok "the PNG is decoded and drawn" || bad "the PNG is not drawn"

echo "Exit with the popup menu open"
printf 'AUTO\n' > "$T/fav.lst"
PMD_FAVORITES="$T/fav.lst" PMD_MENUSCRIPT="Return:hold" "$BIN" >/dev/null 2>&1 &
PID=$!
sleep 3
kill -TERM $PID
for i in $(seq 1 50); do kill -0 $PID 2>/dev/null || break; sleep 0.1; done
if kill -0 $PID 2>/dev/null; then kill -KILL $PID; bad "SIGTERM is ignored while the popup menu is open"; else ok "SIGTERM ends the emulator"; fi

[ $FAIL -eq 0 ] && echo "RESULT: all passed" || echo "RESULT: FAILED"
exit $FAIL
