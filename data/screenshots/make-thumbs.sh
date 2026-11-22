#!/bin/sh
# Makes the menu thumbnails (<NAME>.png, see thumbs.lst) of the screenshots <NAME>.bmp in the directory SRC
# (a screenshot of the game made with PMD_SCREENSHOT, the whole screen of the PMD 85 stretched to any size).
# Usage: ./make-thumbs.sh SRC [NAME...]    needs ImageMagick
set -eu
SRC=${1:?directory with the screenshots NAME.bmp}
shift || true
cd "$(dirname "$0")"
grep -v '^#' thumbs.lst | while read -r NAME X Y SCALE; do
	[ -n "$NAME" ] || continue
	if [ $# -gt 0 ]; then case " $* " in *" $NAME "*) ;; *) continue ;; esac; fi
	SIZE=$((174 / SCALE))
	# The screen of the PMD 85 is 288x256 pixels: the box filter and the threshold give back the pixels of the game.
	magick "$SRC/$NAME.bmp" -colorspace Gray -filter box -resize 288x256! -threshold 40% \
		-crop "${SIZE}x${SIZE}+${X}+${Y}" +repage -filter point -resize "$((SCALE * 100))%" \
		-colors 2 -define png:compression-level=9 PNG8:"$NAME.png"
done
