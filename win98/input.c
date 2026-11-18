/* Replaces upstream's platform/sdl_gamepad.c. The Win98 port is keyboard
 * only: the keyboard half is the same as upstream's (bindings come from the
 * settings as SDL scancodes), the controller half reports "no controller". */
#include "sdl_gamepad.h"
#include "controller_input.h"
#include "driver_input.h"

static PortableSettings *input_settings; /* heap: PortableSettings is ~50 KiB */

void portable_sdl_input_configure(const PortableSettings *settings)
{
    if (input_settings == NULL) input_settings = malloc(sizeof *input_settings);
    if (input_settings == NULL) return;
    if (settings != NULL) *input_settings = *settings;
    else portable_settings_defaults(input_settings);
}
static const PortableSettings *configured(void)
{
    if (input_settings == NULL) portable_sdl_input_configure(NULL);
    return input_settings;
}
uint16_t portable_sdl_keyboard_driving(void)
{
    const PortableSettings *settings = configured();
    return settings == NULL ? 0u
        : portable_keyboard_driving_buttons(SDL_GetKeyboardState(NULL), SDL_SCANCODE_COUNT, settings);
}
uint16_t portable_sdl_keyboard2_driving(void) { return 0u; } /* no split-screen */
int portable_sdl_gamepad_set_deadzone(uint16_t deadzone) { return deadzone >= 1u && deadzone <= 30000u; }
SDL_Gamepad *portable_sdl_gamepad_open_first(void) { return NULL; }
SDL_JoystickID portable_sdl_gamepad_id(SDL_Gamepad *gamepad) { (void)gamepad; return 0u; }
void portable_sdl_gamepad_device_event(SDL_Gamepad **gamepad, const SDL_Event *event) { (void)gamepad; (void)event; }
uint16_t portable_sdl_gamepad_driving(SDL_Gamepad *gamepad) { (void)gamepad; return 0u; }
uint16_t portable_sdl_gamepad_frontend_button(Uint8 button) { (void)button; return 0u; }
uint16_t portable_sdl_gamepad_frontend(SDL_Gamepad *gamepad) { (void)gamepad; return 0u; }
void portable_sdl_gamepad_race_rumble(SDL_Gamepad *gamepad, PortableRumbleTracker *tracker,
    const PortableDriverEngineSoundInput *engine, const PortableDriverContactAudio *contact,
    const PortableDriverContactAudio *boost_voice, int racing)
{
    /* Upstream always steps the tracker; only the pulse itself needs a pad. */
    const PortableRumbleView view = {engine, contact, boost_voice};
    (void)gamepad; (void)racing;
    (void)portable_rumble_step(tracker, &view);
}
void portable_sdl_gamepad_stick(SDL_Gamepad *gamepad, int16_t *x, int16_t *y) { (void)gamepad; *x = *y = 0; }
