// The music: an original 16-bar tune on the YM2612 (bass, lead, arpeggio, kick) and the PSG's noise channel (snare, hats).
#pragma once
#include "md.h"

void music_init(void);
void music_play(void);
void music_stop(void);
void music_update(void);  // once a frame
