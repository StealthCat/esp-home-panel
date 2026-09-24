#include "theme_art.h"

#if __has_include("theme_art_generated.inc")
#include "theme_art_generated.inc"
#else
const lv_image_dsc_t *theme_art_get(theme_art_id_t id)
{
    (void)id;
    return NULL;
}
#endif
