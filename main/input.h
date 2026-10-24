#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INPUT_KEY_UP = 0,
    INPUT_KEY_DOWN,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_OK,      /* joystick click (GPIO32) */
    INPUT_KEY_BACK,    /* GPIO25 */
    INPUT_KEY_SHIFT,   /* GPIO26 */
    INPUT_KEY_COUNT,
    INPUT_KEY_NONE = -1
} input_key_t;

typedef enum {
    INPUT_EVENT_PRESS = 0,
    INPUT_EVENT_RELEASE,
} input_event_type_t;

typedef struct {
    input_key_t key;
    input_event_type_t type;
} input_event_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Init GPIO debounce polling + joystick ADC. Spawns an internal task. */
void input_init(void);

/* Pull one input event from the queue. Returns false on timeout. */
bool input_get_event(input_event_t *ev, uint32_t timeout_ms);

/* Logical pressed state: physical buttons + joystick directions + click. */
bool input_is_pressed(input_key_t key);

/* Raw analog joystick axes, normalized to -100..100 (0 = rest/center).
 * x: right positive, y: up positive. */
void input_get_joystick(int *x, int *y);

#ifdef __cplusplus
}
#endif