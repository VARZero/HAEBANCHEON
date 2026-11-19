#ifndef BOSIO_USER_PUBLISH_METADATA_H
#define BOSIO_USER_PUBLISH_METADATA_H
#include <stdint.h>
/* BS25 stores palette words as RGB24 (rdata[23:0]). A fixed, different upper
 * byte for each scene bank leaves every displayed color unchanged and lets
 * the existing kernel's whole-metadata equality check stop at byte three.
 * This selects a full metadata commit instead of the equal-metadata patch
 * command. It is specific to the existing BS25 palette format, not ARGB32.
 * Keep the tag fixed per bank so it does not add steady-state DDR writes.
 */
static inline uint32_t bosio_bs25_palette_bank_tag(uint32_t rgb, unsigned bank)
{
    return (rgb & 0x00ffffffu) | ((bank + 1u) << 24);
}
#endif
