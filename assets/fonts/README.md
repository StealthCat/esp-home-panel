# Header font

Outfit SemiBold, weight 600, from the Outfit variable font distributed by
Google Fonts: https://github.com/google/fonts/tree/main/ofl/outfit
Copyright and SIL Open Font License are included in `OFL-Outfit.txt`.

`Outfit-SemiBold.ttf` is a static weight-600 instance created with fontTools
`instantiateVariableFont`. The firmware uses the committed generated C font;
normal firmware builds do not require Node, fontTools or font downloads.

Regenerate from the repository root using lv_font_conv 1.5.3:

```sh
npx lv_font_conv@1.5.3 --size 48 --bpp 4 --format lvgl \
  --font assets/fonts/Outfit-SemiBold.ttf -r 0x20-0x7e,0xa0-0xff \
  --symbols '‘’“”–—…' --no-compress --lv-include lvgl.h \
  --lv-font-name font_outfit_semibold_48 -o main/font_outfit_semibold_48.c
```

The subset includes printable ASCII, Latin-1 and common typographic punctuation.
It uses grayscale antialiasing and kerning. The title preserves user-entered
capitalization and scrolls when it exceeds the header width.
