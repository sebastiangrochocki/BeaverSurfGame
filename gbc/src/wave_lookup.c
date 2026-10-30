/* Fixed-bank reader: switching ROM inside the banked wave engine would unmap
 * its own instructions. Each page contains 512 exact refreshes of 16 samples. */
#include <gb/gb.h>
#include "rules.h"

void wave_lookup(uint16_t tick, uint8_t tier) NONBANKED {
    uint8_t saved=CURRENT_BANK;
    const uint8_t *source=(const uint8_t *)(0x4000u+((tick&511u)<<5));
    int16_t *dest=world.heights+(tick&3);
    uint8_t n=16;
    SWITCH_ROM((uint8_t)(8+(tier>8?8:tier)*2+((tick>>9)&1)));
    do {
        uint8_t lo=*source++;
        *dest=(int16_t)((uint16_t)lo|((uint16_t)*source++<<8));
        dest+=4;
    } while(--n);
    SWITCH_ROM(saved);
}
