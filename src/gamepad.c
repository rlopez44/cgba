#include "cgba/gamepad.h"
#include "SDL_events.h"

gba_gamepad *init_gamepad(void)
{
    gba_gamepad *pad = malloc(sizeof(gba_gamepad));
    if (!pad)
        return NULL;

    memset(pad, -1, sizeof(gba_gamepad));
    return pad;
}

void deinit_gamepad(gba_gamepad *gamepad)
{
    free(gamepad);
}

void on_keypress(gba_gamepad *pad, SDL_KeyboardEvent *key_event)
{
    bool key_pressed = key_event->type == SDL_KEYDOWN;
    bool bitval = !key_pressed;
    SDL_Keycode keycode = key_event->keysym.sym;

    uint32_t shift;
    switch (keycode)
    {
        case SDLK_w:      shift = BUTTON_UP; break;
        case SDLK_a:      shift = BUTTON_LEFT; break;
        case SDLK_s:      shift = BUTTON_DOWN; break;
        case SDLK_j:      shift = BUTTON_B; break;
        case SDLK_k:      shift = BUTTON_A; break;
        case SDLK_u:      shift = BUTTON_L; break;
        case SDLK_i:      shift = BUTTON_R; break;
        case SDLK_RETURN: shift = BUTTON_START; break;
        case SDLK_SPACE:  shift = BUTTON_SELECT; break;
        default: return;
    }

    uint32_t mask = 1 << shift;
    pad->state = (pad->state & ~mask) | (bitval << shift);

    // TODO: implement keypad interrupt
}
