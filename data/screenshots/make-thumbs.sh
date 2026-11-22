#!/bin/sh
# Makes the menu thumbnails (<NAME>.png, see thumbs.lst) of the screenshots <NAME>.bmp in the directory SRC
# (a screenshot of the game made with PMD_SCREENSHOT, the whole screen of the PMD 85 stretched to any size).
# Usage: ./make-thumbs.sh SRC [NAME...]    needs ImageMagick (THUMBS_DEBUG=1 prints the operations)
set -eu
SRC=${1:?directory with the screenshots NAME.bmp}
shift || true
cd "$(dirname "$0")"

# rectangle WxH+X+Y -> "x1,y1 x2,y2" (for -draw)
rect() {
	echo "$1" | sed -E 's/^([0-9]+)x([0-9]+)\+([0-9]+)\+([0-9]+)$/\1 \2 \3 \4/' | {
		read -r W H X Y
		echo "$X,$Y $((X + W - 1)),$((Y + H - 1))"
	}
}

grep -v '^#' thumbs.lst | while read -r NAME X Y SCALE OPS; do
	[ -n "$NAME" ] || continue
	if [ $# -gt 0 ]; then case " $* " in *" $NAME "*) ;; *) continue ;; esac; fi
	SIZE=$((174 / SCALE))
	# The operations on the 288x256 screen before the crop (see thumbs.lst): the erased rectangles first, then the copies (always of
	# the screen with the erases), then everything outside the kept rectangles is erased.
	COPY="" ERASE="" KEEP=""
	for OP in $OPS; do
		case "$OP" in
			-*) ERASE="$ERASE -draw \"rectangle $(rect "${OP#-}")\"" ;;
			=*) KEEP="$KEEP -draw \"rectangle $(rect "${OP#=}")\"" ;;
			*\>*) G=${OP%>*} TO=${OP#*>}
			      COPY="$COPY \\( mpr:orig -crop $G +repage \\) -geometry +${TO%,*}+${TO#*,} -composite +geometry" ;;
		esac
	done
	[ -z "$KEEP" ] || KEEP="\\( +clone -fill black -colorize 100 -fill white $KEEP \\) -compose multiply -composite -compose over"
	# The screen of the PMD 85 is 288x256 pixels: the box filter and the threshold give back the pixels of the game.
	[ -z "${THUMBS_DEBUG:-}" ] || eval echo magick "\"$SRC/$NAME.bmp\"" $COPY $ERASE $KEEP
	eval magick "\"$SRC/$NAME.bmp\"" -alpha off -colorspace Gray -filter box -resize 288x256! -threshold 40% -fill black $ERASE -write mpr:orig \
		$COPY $KEEP \
		-crop "${SIZE}x${SIZE}+${X}+${Y}" +repage -filter point -resize "$((SCALE * 100))%" \
		-colors 2 -define png:compression-level=9 PNG8:"$NAME.png"
done
