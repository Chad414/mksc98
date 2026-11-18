/* Minimal SDL3-compatible surface for the Windows 98 port.
 *
 * SDL3 itself cannot run on Win9x. Upstream's classic-game host files
 * (game_host.c, gp_race_host.c, ...) are compiled unchanged against this
 * header; win98/shim.c implements it on top of a small backend
 * (backend_win32.c: Win32 + DirectDraw/GDI + waveOut, or backend_null.c for
 * the headless reference build). Only what those files use is declared.
 * Names, values and signatures follow SDL 3.4 so the upstream sources and
 * saved settings (key bindings are SDL scancodes) keep their meaning. */
#ifndef MKSC_WIN98_SDL_SHIM_H
#define MKSC_WIN98_SDL_SHIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MKSC_WIN98_SHIM 1
#define SDLCALL
#define SDL_min(a, b) ((a) < (b) ? (a) : (b))
#define SDL_max(a, b) ((a) > (b) ? (a) : (b))

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;
typedef Sint64 SDL_Time;

/* ---- init, errors, memory, strings ------------------------------------ */
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_GAMEPAD 0x00002000u
#define SDL_INIT_EVENTS 0x00004000u
bool SDL_Init(Uint32 flags);
bool SDL_InitSubSystem(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
bool SDL_SetError(const char *format, ...);
bool SDL_OutOfMemory(void);
#define SDL_malloc malloc
#define SDL_free free
#define SDL_strcmp strcmp
char *SDL_strdup(const char *text);
size_t SDL_strlcpy(char *target, const char *source, size_t capacity);
int SDL_strcasecmp(const char *left, const char *right);
const char *SDL_getenv(const char *name);

/* ---- time ------------------------------------------------------------- */
Uint64 SDL_GetTicks(void);
Uint64 SDL_GetTicksNS(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 milliseconds);

/* ---- threads (only trivially used by the kept files) -------------------- */
typedef struct SDL_AtomicInt { int value; } SDL_AtomicInt;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_Mutex SDL_Mutex;
typedef int (SDLCALL *SDL_ThreadFunction)(void *data);
int SDL_GetAtomicInt(SDL_AtomicInt *atomic);
int SDL_SetAtomicInt(SDL_AtomicInt *atomic, int value);
bool SDL_CompareAndSwapAtomicInt(SDL_AtomicInt *atomic, int old_value, int new_value);
SDL_Mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_Mutex *mutex);
void SDL_LockMutex(SDL_Mutex *mutex);
void SDL_UnlockMutex(SDL_Mutex *mutex);

/* ---- files ------------------------------------------------------------ */
typedef struct SDL_IOStream SDL_IOStream;
typedef enum { SDL_IO_STATUS_READY, SDL_IO_STATUS_ERROR, SDL_IO_STATUS_EOF } SDL_IOStatus;
typedef enum { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY, SDL_PATHTYPE_OTHER } SDL_PathType;
typedef struct SDL_PathInfo {
    SDL_PathType type;
    Uint64 size;
    SDL_Time create_time, modify_time, access_time;
} SDL_PathInfo;
typedef enum { SDL_ENUM_CONTINUE, SDL_ENUM_SUCCESS, SDL_ENUM_FAILURE } SDL_EnumerationResult;
typedef SDL_EnumerationResult (SDLCALL *SDL_EnumerateDirectoryCallback)(void *userdata, const char *directory,
                                                                        const char *name);
SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode);
size_t SDL_ReadIO(SDL_IOStream *stream, void *bytes, size_t size);
SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *stream);
bool SDL_CloseIO(SDL_IOStream *stream);
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info);
bool SDL_CreateDirectory(const char *path);
bool SDL_RemovePath(const char *path);
bool SDL_RenamePath(const char *old_path, const char *new_path);
bool SDL_EnumerateDirectory(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata);
char *SDL_GetPrefPath(const char *organization, const char *application);
char *SDL_GetCurrentDirectory(void);
char *SDL_GetClipboardText(void);

/* The Win98 port has no file chooser: the ROM is found beside the .exe. */
typedef struct SDL_DialogFileFilter { const char *name, *pattern; } SDL_DialogFileFilter;
typedef void (SDLCALL *SDL_DialogFileCallback)(void *userdata, const char *const *files, int filter);

/* ---- keyboard --------------------------------------------------------- */
typedef enum SDL_Scancode {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D, SDL_SCANCODE_E, SDL_SCANCODE_F,
    SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L,
    SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_Q, SDL_SCANCODE_R,
    SDL_SCANCODE_S, SDL_SCANCODE_T, SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
    SDL_SCANCODE_Y, SDL_SCANCODE_Z,
    SDL_SCANCODE_1 = 30, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
    SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
    SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE,
    SDL_SCANCODE_MINUS = 45, SDL_SCANCODE_EQUALS, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_SEMICOLON = 51, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE,
    SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_CAPSLOCK,
    SDL_SCANCODE_F1 = 58, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5, SDL_SCANCODE_F6,
    SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12,
    SDL_SCANCODE_INSERT = 73, SDL_SCANCODE_HOME, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_DELETE, SDL_SCANCODE_END,
    SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_DOWN, SDL_SCANCODE_UP,
    SDL_SCANCODE_KP_ENTER = 88,
    SDL_SCANCODE_LCTRL = 224, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT, SDL_SCANCODE_LGUI,
    SDL_SCANCODE_RCTRL, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RALT, SDL_SCANCODE_RGUI,
    SDL_SCANCODE_AC_BACK = 282,
    SDL_SCANCODE_COUNT = 512
} SDL_Scancode;
typedef Uint32 SDL_Keycode;
#define SDLK_SCANCODE_MASK (1u << 30)
#define SDL_SCANCODE_TO_KEYCODE(scancode) ((SDL_Keycode)(scancode) | SDLK_SCANCODE_MASK)
#define SDLK_UNKNOWN 0u
#define SDLK_RETURN 0x0du
#define SDLK_ESCAPE 0x1bu
#define SDLK_TAB 0x09u
#define SDLK_SPACE 0x20u
#define SDLK_A 0x61u
#define SDLK_B 0x62u
#define SDLK_P 0x70u
#define SDLK_S 0x73u
#define SDLK_X 0x78u
#define SDLK_Z 0x7au
#define SDLK_F5 SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F5)
#define SDLK_RIGHT SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RIGHT)
#define SDLK_LEFT SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LEFT)
#define SDLK_DOWN SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_DOWN)
#define SDLK_UP SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_UP)
#define SDLK_AC_BACK SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_AC_BACK)
const bool *SDL_GetKeyboardState(int *count);
void SDL_ResetKeyboard(void);
const char *SDL_GetScancodeName(SDL_Scancode scancode);

/* ---- gamepads: declared so shared headers compile; never present -------- */
typedef Uint32 SDL_JoystickID;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef enum SDL_GamepadButton {
    SDL_GAMEPAD_BUTTON_INVALID = -1,
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_COUNT = 26
} SDL_GamepadButton;
typedef enum SDL_GamepadAxis {
    SDL_GAMEPAD_AXIS_INVALID = -1, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
    SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
} SDL_GamepadAxis;
SDL_JoystickID *SDL_GetGamepads(int *count);
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id);
void SDL_CloseGamepad(SDL_Gamepad *gamepad);
bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button);
const char *SDL_GetGamepadStringForButton(SDL_GamepadButton button);

/* ---- events ----------------------------------------------------------- */
typedef enum SDL_EventType {
    SDL_EVENT_QUIT = 0x100,
    SDL_EVENT_WILL_ENTER_BACKGROUND = 0x103, SDL_EVENT_DID_ENTER_BACKGROUND, SDL_EVENT_WILL_ENTER_FOREGROUND,
    SDL_EVENT_DID_ENTER_FOREGROUND,
    SDL_EVENT_WINDOW_FIRST = 0x202,
    SDL_EVENT_WINDOW_EXPOSED = 0x204, SDL_EVENT_WINDOW_RESIZED = 0x206, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED = 0x207,
    SDL_EVENT_WINDOW_FOCUS_GAINED = 0x20e, SDL_EVENT_WINDOW_FOCUS_LOST = 0x20f,
    SDL_EVENT_WINDOW_CLOSE_REQUESTED = 0x210,
    SDL_EVENT_WINDOW_LAST = 0x21f,
    SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_KEY_UP,
    SDL_EVENT_GAMEPAD_AXIS_MOTION = 0x650, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_UP,
    SDL_EVENT_GAMEPAD_ADDED, SDL_EVENT_GAMEPAD_REMOVED, SDL_EVENT_GAMEPAD_REMAPPED,
    SDL_EVENT_FINGER_DOWN = 0x700, SDL_EVENT_FINGER_UP, SDL_EVENT_FINGER_MOTION,
    SDL_EVENT_DROP_FILE = 0x1000,
    SDL_EVENT_RENDER_TARGETS_RESET = 0x2000, SDL_EVENT_RENDER_DEVICE_RESET
} SDL_EventType;
typedef struct SDL_KeyboardEvent {
    Uint32 type, reserved; Uint64 timestamp; Uint32 windowID, which;
    SDL_Scancode scancode; SDL_Keycode key; Uint16 mod, raw; bool down, repeat;
} SDL_KeyboardEvent;
typedef struct SDL_GamepadButtonEvent {
    Uint32 type, reserved; Uint64 timestamp; SDL_JoystickID which; Uint8 button; bool down;
} SDL_GamepadButtonEvent;
typedef struct SDL_GamepadAxisEvent {
    Uint32 type, reserved; Uint64 timestamp; SDL_JoystickID which; Uint8 axis; int16_t value;
} SDL_GamepadAxisEvent;
typedef struct SDL_GamepadDeviceEvent { Uint32 type, reserved; Uint64 timestamp; SDL_JoystickID which; } SDL_GamepadDeviceEvent;
typedef struct SDL_DropEvent { Uint32 type, reserved; Uint64 timestamp; Uint32 windowID; float x, y; const char *source, *data; } SDL_DropEvent;
typedef struct SDL_TouchFingerEvent {
    Uint32 type, reserved; Uint64 timestamp; Uint64 touchID, fingerID; float x, y, dx, dy, pressure; Uint32 windowID;
} SDL_TouchFingerEvent;
typedef struct SDL_WindowEvent { Uint32 type, reserved; Uint64 timestamp; Uint32 windowID; int32_t data1, data2; } SDL_WindowEvent;
typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_GamepadButtonEvent gbutton;
    SDL_GamepadAxisEvent gaxis;
    SDL_GamepadDeviceEvent gdevice;
    SDL_DropEvent drop;
    SDL_TouchFingerEvent tfinger;
    SDL_WindowEvent window;
    Uint8 padding[128];
} SDL_Event;
bool SDL_PollEvent(SDL_Event *event);
bool SDL_PushEvent(SDL_Event *event);
void SDL_PumpEvents(void);

/* ---- video: one window, one 2D renderer, streaming ARGB textures ------- */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_FRect { float x, y, w, h; } SDL_FRect;
typedef struct SDL_FPoint { float x, y; } SDL_FPoint;
typedef struct SDL_FColor { float r, g, b, a; } SDL_FColor;
typedef struct SDL_Vertex { SDL_FPoint position; SDL_FColor color; SDL_FPoint tex_coord; } SDL_Vertex;
typedef enum { SDL_SCALEMODE_NEAREST, SDL_SCALEMODE_LINEAR } SDL_ScaleMode;
typedef enum { SDL_TEXTUREACCESS_STATIC, SDL_TEXTUREACCESS_STREAMING, SDL_TEXTUREACCESS_TARGET } SDL_TextureAccess;
typedef enum { SDL_PIXELFORMAT_UNKNOWN, SDL_PIXELFORMAT_ARGB8888 = 0x16362004, SDL_PIXELFORMAT_RGBA32 = 0x16762004 } SDL_PixelFormat;
typedef Uint32 SDL_BlendMode;
#define SDL_BLENDMODE_NONE 0u
typedef Uint64 SDL_WindowFlags;
#define SDL_WINDOW_FULLSCREEN 0x1u
#define SDL_WINDOW_HIDDEN 0x8u
#define SDL_WINDOW_RESIZABLE 0x20u
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0x2000u
typedef enum { SDL_GL_CONTEXT_MAJOR_VERSION = 17, SDL_GL_CONTEXT_MINOR_VERSION, SDL_GL_CONTEXT_PROFILE_MASK = 21 } SDL_GLAttr;
#define SDL_GL_CONTEXT_PROFILE_ES 0x0004
bool SDL_GL_SetAttribute(SDL_GLAttr attribute, int value);

SDL_Window *SDL_CreateWindow(const char *title, int width, int height, SDL_WindowFlags flags);
void SDL_DestroyWindow(SDL_Window *window);
bool SDL_SetWindowTitle(SDL_Window *window, const char *title);
bool SDL_SetWindowSize(SDL_Window *window, int width, int height);
bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen);
bool SDL_SyncWindow(SDL_Window *window);
bool SDL_ShowWindow(SDL_Window *window);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name);
SDL_Renderer *SDL_CreateGPURenderer(SDL_GPUDevice *device, SDL_Window *window); /* always NULL */
SDL_GPUDevice *SDL_GetGPURendererDevice(SDL_Renderer *renderer);               /* always NULL */
void SDL_DestroyRenderer(SDL_Renderer *renderer);
const char *SDL_GetRendererName(SDL_Renderer *renderer);
SDL_Window *SDL_GetRenderWindow(SDL_Renderer *renderer);
bool SDL_SetRenderVSync(SDL_Renderer *renderer, int vsync);
bool SDL_GetCurrentRenderOutputSize(SDL_Renderer *renderer, int *width, int *height);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, SDL_PixelFormat format, SDL_TextureAccess access,
                               int width, int height);
void SDL_DestroyTexture(SDL_Texture *texture);
bool SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
bool SDL_SetTextureScaleMode(SDL_Texture *texture, SDL_ScaleMode mode);
bool SDL_GetTextureScaleMode(SDL_Texture *texture, SDL_ScaleMode *mode);
bool SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode mode);
bool SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture);
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *renderer);
bool SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
bool SDL_RenderClear(SDL_Renderer *renderer);
bool SDL_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect *source,
                       const SDL_FRect *target);
bool SDL_RenderPresent(SDL_Renderer *renderer);
bool SDL_FlushRenderer(SDL_Renderer *renderer);

/* ---- audio: types only. win98/audio_queue.c replaces sdl_audio_queue.c -- */
typedef Uint32 SDL_AudioDeviceID;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef struct SDL_AudioSpec { int format, channels, freq; } SDL_AudioSpec;
#define SDL_AUDIO_S16 0x8010
bool SDL_SetAudioStreamGain(SDL_AudioStream *stream, float gain);
bool SDL_ClearAudioStream(SDL_AudioStream *stream);

#endif
