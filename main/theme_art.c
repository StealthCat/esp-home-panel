#include "theme_art.h"

/*
 * main/CMakeLists.txt generates this include for every build path. Keep this
 * include unconditional: if sprite generation ever breaks, the build must fail
 * rather than producing firmware that quietly omits all theme artwork.
 */
#include "theme_art_generated.inc"
