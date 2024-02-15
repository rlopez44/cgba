#ifndef CGBA_GAMEPAD_H
#define CGBA_GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>
#include "cgba/memory.h"
#include "SDL_events.h"

enum GBA_GAMEPAD_BUTTONS {
    BUTTON_A,
    BUTTON_B,
    BUTTON_SELECT,
    BUTTON_START,
    BUTTON_RIGHT,
    BUTTON_LEFT,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_R,
    BUTTON_L,
};

typedef struct gba_gamepad {
    uint32_t state; // only bits 0-9 are used
    gba_mem *mem;
} gba_gamepad;

gba_gamepad *init_gamepad(void);
void deinit_gamepad(gba_gamepad *gamepad);

void on_keypress(gba_gamepad *pad, SDL_KeyboardEvent *key_event);

#endif /* CGBA_GAMEPAD_H */
