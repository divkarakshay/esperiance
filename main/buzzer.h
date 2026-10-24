#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Init PWM tone generator on the buzzer pin (GPIO33). Non-blocking:
 * tones are queued and played by a background task. If the queue is
 * full, new tones are dropped. */
void buzzer_init(void);

/* Play freq_hz for dur_ms. freq 0 = silence. */
void buzzer_tone(uint16_t freq_hz, uint16_t dur_ms);

#ifdef __cplusplus
}
#endif