/* Windows 98 SE backend: plain Win32 (ANSI APIs only), DirectDraw for the
 * picture with a GDI fallback, DirectSound (backend_dsound.c) or waveOut for
 * sound. Nothing here needs more
 * than DirectX 3-level DirectDraw; ddraw.dll is loaded at run time so the
 * program still starts (on GDI) where DirectDraw is missing or refuses.
 *
 * Why not SDL 1.2: it would add a second abstraction under the SDL3-shaped
 * shim and its own DirectX 5 back ends, for a program that needs one window,
 * one stretched blit and one PCM stream. This file is the whole platform. */
#include "port.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>

#include "backend_d3d7.h"
#include "backend_dsound.h"

static int d3d_active; /* presenting through backend_d3d7.c */
static int window_borderless_now(void);

/* Direct3D for the current window mode: page-flipping when the game covers
 * the whole screen, windowed otherwise (and as the fallback). */
static int open_direct3d(HWND handle)
{
    if (port_options.force_gdi || port_options.force_ddraw) return 0;
    if (window_borderless_now() && !port_options.no_flip && d3d7_open(handle, 1)) return 1;
    return d3d7_open(handle, 0);
}

/* ---- time and paths ---------------------------------------------------------- */
static char base_directory[MAX_PATH + 2];
static uint64_t counter_frequency;

uint64_t backend_counter(void)
{
    LARGE_INTEGER value;
    if (counter_frequency == 1000u) return (uint64_t)timeGetTime();
    QueryPerformanceCounter(&value);
    return (uint64_t)value.QuadPart;
}
uint64_t backend_frequency(void) { return counter_frequency; }
void backend_sleep(unsigned milliseconds) { Sleep(milliseconds); }
const char *backend_base_directory(void) { return base_directory; }

/* ---- window and keyboard ----------------------------------------------------- */
static HWND window;
static int window_hidden, window_fullscreen; /* window_fullscreen: exclusive 640x480x16 */
static int window_borderless;              /* fullscreen at the desktop's own resolution */
static int window_borderless_now(void) { return window_borderless; }
static int windowed_width, windowed_height; /* client size to return to */
#define WINDOWED_STYLE (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)
static int client_width, client_height;
static const char class_name[] = "MKSC98";

static SDL_Scancode scancode_of(WPARAM key, LPARAM data)
{
    const int extended = (data & 0x01000000L) != 0;
    if (key >= 'A' && key <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (key - 'A'));
    if (key >= '1' && key <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (key - '1'));
    if (key >= VK_F1 && key <= VK_F12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (key - VK_F1));
    switch (key) {
    case '0': return SDL_SCANCODE_0;
    case VK_RETURN: return extended ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
    case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE;
    case VK_TAB: return SDL_SCANCODE_TAB;
    case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT;
    case VK_UP: return SDL_SCANCODE_UP;
    case VK_DOWN: return SDL_SCANCODE_DOWN;
    /* Win9x reports both Shift keys as VK_SHIFT; the hardware scan code in
     * bits 16..23 tells them apart (0x36 is the right one). */
    case VK_SHIFT: return ((data >> 16) & 0xff) == 0x36 ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_LSHIFT;
    case VK_CONTROL: return extended ? SDL_SCANCODE_RCTRL : SDL_SCANCODE_LCTRL;
    case VK_MENU: return extended ? SDL_SCANCODE_RALT : SDL_SCANCODE_LALT;
    case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE;
    case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_END: return SDL_SCANCODE_END;
    case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
    case 0xBA: return SDL_SCANCODE_SEMICOLON;
    case 0xBC: return SDL_SCANCODE_COMMA;
    case 0xBE: return SDL_SCANCODE_PERIOD;
    case 0xBF: return SDL_SCANCODE_SLASH;
    case 0xBD: return SDL_SCANCODE_MINUS;
    case 0xBB: return SDL_SCANCODE_EQUALS;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

static void video_borders_dirty(void);

static LRESULT CALLBACK window_procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CLOSE:
        shim_post_simple(SDL_EVENT_QUIT);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* Esc quits the game outright (the menus' "back" is X). */
        if (wparam == VK_ESCAPE) { shim_post_simple(SDL_EVENT_QUIT); return 0; }
        shim_post_key(scancode_of(wparam, lparam), 1, (lparam & 0x40000000L) != 0);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wparam != VK_ESCAPE) shim_post_key(scancode_of(wparam, lparam), 0, 0);
        return 0;
    case WM_ACTIVATE:
        shim_post_simple(LOWORD(wparam) != WA_INACTIVE ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST);
        video_borders_dirty();
        return 0;
    case WM_PAINT:
        ValidateRect(handle, NULL);
        video_borders_dirty();
        shim_post_simple(SDL_EVENT_WINDOW_EXPOSED);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SYSCOMMAND:
        /* No Alt menu beeps, no screen saver or monitor power-down mid-race. */
        if ((wparam & 0xfff0) == SC_KEYMENU || (wparam & 0xfff0) == SC_SCREENSAVE ||
            (wparam & 0xfff0) == SC_MONITORPOWER) return 0;
        break;
    case WM_SETCURSOR:
        if ((window_fullscreen || window_borderless) && LOWORD(lparam) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        break;
    default:
        break;
    }
    return DefWindowProcA(handle, message, wparam, lparam);
}

void backend_pump(void)
{
    MSG message;
    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
    if (d3d_active) d3d7_pump();
}

/* ---- video ---------------------------------------------------------------------- */
typedef HRESULT (WINAPI *DirectDrawCreateFunction)(GUID *, LPDIRECTDRAW *, IUnknown *);
static HMODULE ddraw_library;
static LPDIRECTDRAW ddraw;
static LPDIRECTDRAWSURFACE primary_surface, frame_surface;
static LPDIRECTDRAWCLIPPER clipper;
static int frame_width, frame_height;   /* size of frame_surface */
static int surface_bits;                 /* 16, 24 or 32 */
static uint32_t mask_r, mask_g, mask_b;
static int shift_r, shift_g, shift_b;    /* left shift of each field */
static int loss_r, loss_g, loss_b;       /* 8 - field width */
static int borders_dirty = 1;
static char video_name[96] = "none";
static unsigned fps_frames;
static DWORD fps_started;

static void video_borders_dirty(void) { borders_dirty = 1; }

static void mask_layout(uint32_t mask, int *shift, int *loss)
{
    int position = 0, width = 0;
    while (mask != 0u && (mask & 1u) == 0u) { mask >>= 1; ++position; }
    while ((mask & 1u) != 0u) { mask >>= 1; ++width; }
    *shift = position;
    *loss = 8 - width;
}

static void directdraw_release(void)
{
    if (frame_surface != NULL) { IDirectDrawSurface_Release(frame_surface); frame_surface = NULL; }
    if (clipper != NULL) { IDirectDrawClipper_Release(clipper); clipper = NULL; }
    if (primary_surface != NULL) { IDirectDrawSurface_Release(primary_surface); primary_surface = NULL; }
    if (ddraw != NULL) {
        if (window_fullscreen) IDirectDraw_RestoreDisplayMode(ddraw);
        IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
        IDirectDraw_Release(ddraw);
        ddraw = NULL;
    }
    frame_width = frame_height = 0;
}

/* The staging surface the CPU writes each frame into; Blt stretches it. */
static int directdraw_frame_surface(int width, int height)
{
    DDSURFACEDESC description;
    HRESULT result;
    if (frame_surface != NULL && frame_width == width && frame_height == height) return 1;
    if (frame_surface != NULL) { IDirectDrawSurface_Release(frame_surface); frame_surface = NULL; }
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    description.dwWidth = (DWORD)width;
    description.dwHeight = (DWORD)height;
    /* Video memory first: the card can then stretch-blit it itself. */
    description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    result = IDirectDraw_CreateSurface(ddraw, &description, &frame_surface, NULL);
    if (result != DD_OK) {
        description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        result = IDirectDraw_CreateSurface(ddraw, &description, &frame_surface, NULL);
        if (result != DD_OK) { frame_surface = NULL; return 0; }
        port_log("video: staging surface %dx%d is in system memory", width, height);
    } else
        port_log("video: staging surface %dx%d is in video memory", width, height);
    frame_width = width;
    frame_height = height;
    return 1;
}

static int directdraw_open(void)
{
    DirectDrawCreateFunction create;
    DDSURFACEDESC description;
    DDPIXELFORMAT format;
    DDCAPS driver, emulation;
    HRESULT result;
    if (port_options.force_gdi) return 0;
    ddraw_library = LoadLibraryA("ddraw.dll");
    if (ddraw_library == NULL) { port_log("video: ddraw.dll is not available"); return 0; }
    create = (DirectDrawCreateFunction)(void *)GetProcAddress(ddraw_library, "DirectDrawCreate");
    if (create == NULL || (result = create(NULL, &ddraw, NULL)) != DD_OK) {
        ddraw = NULL;
        port_log("video: DirectDrawCreate failed (%08lx)", create != NULL ? (unsigned long)result : 0ul);
        return 0;
    }
    if (window_fullscreen) {
        HRESULT level = IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
        HRESULT mode = level == DD_OK ? IDirectDraw_SetDisplayMode(ddraw, 640, 480, 16) : level;
        if (level != DD_OK || mode != DD_OK) {
            port_log("video: 640x480x16 exclusive mode refused (cooperative level %08lx, mode %08lx); using a window",
                     (unsigned long)level, (unsigned long)mode);
            IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL);
            window_fullscreen = 0;
            SetWindowLongA(window, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE);
            backend_window_size(client_width, client_height);
        } else {
            client_width = 640;
            client_height = 480;
        }
    }
    if (!window_fullscreen && (result = IDirectDraw_SetCooperativeLevel(ddraw, window, DDSCL_NORMAL)) != DD_OK) {
        port_log("video: DirectDraw SetCooperativeLevel failed (%08lx)", (unsigned long)result);
        goto failed;
    }
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    description.dwFlags = DDSD_CAPS;
    description.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if ((result = IDirectDraw_CreateSurface(ddraw, &description, &primary_surface, NULL)) != DD_OK) {
        port_log("video: DirectDraw primary surface failed (%08lx)", (unsigned long)result);
        primary_surface = NULL;
        goto failed;
    }
    if (!window_fullscreen) {
        if (IDirectDraw_CreateClipper(ddraw, 0, &clipper, NULL) != DD_OK ||
            IDirectDrawClipper_SetHWnd(clipper, 0, window) != DD_OK ||
            IDirectDrawSurface_SetClipper(primary_surface, clipper) != DD_OK) {
            port_log("video: DirectDraw clipper failed");
            goto failed;
        }
    }
    memset(&format, 0, sizeof format);
    format.dwSize = sizeof format;
    if (IDirectDrawSurface_GetPixelFormat(primary_surface, &format) != DD_OK || (format.dwFlags & DDPF_RGB) == 0u ||
        (format.dwRGBBitCount != 16u && format.dwRGBBitCount != 24u && format.dwRGBBitCount != 32u)) {
        port_log("video: the desktop is not 16/24/32-bit colour (%lu bits); DirectDraw not used",
                 (unsigned long)format.dwRGBBitCount);
        goto failed;
    }
    surface_bits = (int)format.dwRGBBitCount;
    mask_r = format.dwRBitMask; mask_g = format.dwGBitMask; mask_b = format.dwBBitMask;
    mask_layout(mask_r, &shift_r, &loss_r);
    mask_layout(mask_g, &shift_g, &loss_g);
    mask_layout(mask_b, &shift_b, &loss_b);
    memset(&driver, 0, sizeof driver); driver.dwSize = sizeof driver;
    memset(&emulation, 0, sizeof emulation); emulation.dwSize = sizeof emulation;
    IDirectDraw_GetCaps(ddraw, &driver, &emulation);
    snprintf(video_name, sizeof video_name, "DirectDraw %s %d-bit (%lu%lu%lu), hardware stretch blit %s, scaling %s",
             window_fullscreen ? "fullscreen 640x480" : window_borderless ? "desktop fullscreen" : "windowed", surface_bits,
             (unsigned long)(8 - loss_r), (unsigned long)(8 - loss_g), (unsigned long)(8 - loss_b),
             (driver.dwCaps & DDCAPS_BLTSTRETCH) ? "yes" : "no", port_options.soft_scale ? "software" : "Blt");
    port_log("video: %s", video_name);
    if (surface_bits != 16) port_log("video: note: a 16-bit desktop halves the blit bandwidth");
    return 1;
failed:
    directdraw_release();
    return 0;
}

enum { SOFT_SCALE_MAX_WIDTH = 2048 };

/* In a window or in desktop-resolution fullscreen the picture is an integer
 * multiple of 240x160 centred in the client area; whatever is left around it
 * is painted black once, and again whenever Windows asks for a repaint. */
static void paint_borders(void)
{
    RECT client;
    HDC context;
    if (!borders_dirty || window == NULL) return;
    borders_dirty = 0;
    context = GetDC(window);
    if (context == NULL) return;
    GetClientRect(window, &client);
    FillRect(context, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    ReleaseDC(window, context);
}

/* Converts XRGB8888 into the staging surface's format, replicating each
 * source pixel `scale` times in both directions (1 for hardware scaling). */
static void convert_rows(uint8_t *target, long pitch, const uint32_t *pixels, int width, int height,
                         int pitch_pixels, int scale)
{
    /* Scaled rows are built in ordinary memory and then written to each
     * target row: reading a row back out of video memory to duplicate it is
     * what made software scaling cost 44 ms a frame on the Radeon. */
    static uint8_t line[SOFT_SCALE_MAX_WIDTH * 4];
    for (int y = 0; y < height; ++y) {
        const uint32_t *source = pixels + (size_t)y * (size_t)pitch_pixels;
        uint8_t *row = scale > 1 ? line : target + (size_t)y * (size_t)pitch;
        if (surface_bits == 16 && scale == 1 && mask_r == 0xf800u && mask_g == 0x07e0u && mask_b == 0x001fu) {
            uint16_t *out = (uint16_t *)(void *)row; /* RGB565: the common, fast case */
            for (int x = 0; x < width; ++x) {
                const uint32_t c = source[x];
                out[x] = (uint16_t)(((c >> 8) & 0xf800u) | ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu));
            }
        } else if (surface_bits == 16) {
            uint16_t *out = (uint16_t *)(void *)row;
            for (int x = 0; x < width; ++x) {
                const uint32_t c = source[x];
                const uint16_t value = (uint16_t)(((((c >> 16) & 0xffu) >> loss_r) << shift_r) |
                    ((((c >> 8) & 0xffu) >> loss_g) << shift_g) | (((c & 0xffu) >> loss_b) << shift_b));
                for (int repeat = 0; repeat < scale; ++repeat) *out++ = value;
            }
        } else if (surface_bits == 32) {
            uint32_t *out = (uint32_t *)(void *)row;
            for (int x = 0; x < width; ++x) {
                const uint32_t c = source[x];
                const uint32_t value = (((c >> 16) & 0xffu) << shift_r) | (((c >> 8) & 0xffu) << shift_g) |
                    ((c & 0xffu) << shift_b);
                for (int repeat = 0; repeat < scale; ++repeat) *out++ = value;
            }
        } else {
            uint8_t *out = row;
            for (int x = 0; x < width; ++x) {
                const uint32_t c = source[x];
                const uint32_t value = (((c >> 16) & 0xffu) << shift_r) | (((c >> 8) & 0xffu) << shift_g) |
                    ((c & 0xffu) << shift_b);
                for (int repeat = 0; repeat < scale; ++repeat) {
                    *out++ = (uint8_t)value; *out++ = (uint8_t)(value >> 8); *out++ = (uint8_t)(value >> 16);
                }
            }
        }
        if (scale > 1)
            for (int repeat = 0; repeat < scale; ++repeat)
                memcpy(target + (size_t)(y * scale + repeat) * (size_t)pitch, line,
                       (size_t)(width * scale) * (size_t)(surface_bits / 8));
    }
}

static int directdraw_present(const uint32_t *pixels, int width, int height, int pitch_pixels,
                              int x, int y, int output_width, int output_height)
{
    DDSURFACEDESC locked;
    RECT target;
    POINT origin = {0, 0};
    HRESULT result;
    int scale = 1;
    if (port_options.soft_scale && output_width % width == 0 && output_height % height == 0 &&
        output_width / width == output_height / height && output_width <= SOFT_SCALE_MAX_WIDTH)
        scale = output_width / width;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!directdraw_frame_surface(width * scale, height * scale)) return 0;
        memset(&locked, 0, sizeof locked);
        locked.dwSize = sizeof locked;
        result = IDirectDrawSurface_Lock(frame_surface, NULL, &locked, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
        if (result == DD_OK) {
            convert_rows(locked.lpSurface, locked.lPitch, pixels, width, height, pitch_pixels, scale);
            IDirectDrawSurface_Unlock(frame_surface, NULL);
            if (!window_fullscreen) ClientToScreen(window, &origin);
            if (!window_fullscreen) paint_borders();
            if (window_fullscreen && borders_dirty) {
                DDBLTFX fill;
                memset(&fill, 0, sizeof fill);
                fill.dwSize = sizeof fill;
                IDirectDrawSurface_Blt(primary_surface, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fill);
                borders_dirty = 0;
            }
            target.left = origin.x + x; target.top = origin.y + y;
            target.right = target.left + output_width; target.bottom = target.top + output_height;
            result = IDirectDrawSurface_Blt(primary_surface, &target, frame_surface, NULL, DDBLT_WAIT, NULL);
            if (result == DD_OK) return 1;
        }
        if (result != DDERR_SURFACELOST) return 0;
        /* Alt+Tab or a mode change took the surfaces away; get them back. */
        if (IDirectDrawSurface_Restore(primary_surface) != DD_OK) return 1; /* still away: skip this frame */
        if (IDirectDrawSurface_Restore(frame_surface) != DD_OK) {
            IDirectDrawSurface_Release(frame_surface);
            frame_surface = NULL;
        }
        borders_dirty = 1;
    }
    return 1;
}

static int gdi_present(const uint32_t *pixels, int width, int height, int pitch_pixels,
                       int x, int y, int output_width, int output_height)
{
    BITMAPINFO info;
    HDC context;
    int copied;
    if (pitch_pixels != width) return 0;
    paint_borders();
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; /* top-down */
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    context = GetDC(window);
    if (context == NULL) return 0;
    SetStretchBltMode(context, COLORONCOLOR);
    copied = StretchDIBits(context, x, y, output_width, output_height, 0, 0, width, height, pixels, &info,
                           DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(window, context);
    return copied != 0 && copied != (int)GDI_ERROR;
}

int backend_present(const uint32_t *pixels, int width, int height, int pitch_pixels,
                    int x, int y, int output_width, int output_height)
{
    int ok;
    static int last_x = -1, last_y = -1, last_width, last_height;
    if (window == NULL || window_hidden) return 1;
    if (IsIconic(window)) return 1;
    /* The picture moved or changed size (menus are 160 lines, races may be
     * taller): what it no longer covers must go back to black. */
    if (x != last_x || y != last_y || output_width != last_width || output_height != last_height) {
        last_x = x; last_y = y; last_width = output_width; last_height = output_height;
        borders_dirty = 1;
    }
    if (d3d_active) {
        if (!d3d7_owns_screen()) paint_borders();
        if (d3d7_present(window, pixels, width, height, pitch_pixels, x, y, output_width, output_height)) ok = 1;
        else {
            port_log("video: Direct3D 7 presenting failed; switching to DirectDraw/GDI");
            d3d7_close();
            d3d_active = 0;
            if (!directdraw_open()) snprintf(video_name, sizeof video_name, "GDI StretchDIBits (Direct3D failed)");
        }
    }
    if (d3d_active) { /* presented above */ }
    else if (ddraw != NULL) {
        ok = directdraw_present(pixels, width, height, pitch_pixels, x, y, output_width, output_height);
        if (!ok) {
            port_log("video: DirectDraw blit failed; switching to GDI");
            directdraw_release();
            snprintf(video_name, sizeof video_name, "GDI StretchDIBits (DirectDraw failed)");
        }
    }
    if (!d3d_active && ddraw == NULL) ok = gdi_present(pixels, width, height, pitch_pixels, x, y, output_width, output_height);
    if (port_options.show_fps) {
        const DWORD now = timeGetTime();
        if (fps_started == 0u) fps_started = now;
        ++fps_frames;
        if (now - fps_started >= 5000u) {
            port_log("fps: %.1f pictures per second", (double)fps_frames * 1000.0 / (double)(now - fps_started));
            fps_frames = 0u;
            fps_started = now;
        }
    }
    return ok;
}
const char *backend_video_name(void) { return video_name; }

/* Test harness: copies what the screen really shows around the game window
 * (frame, title bar and a margin of desktop) into a 24-bit BMP. This checks
 * the visible result of the blit, not just the game's own frame buffer. */
int backend_screenshot(const char *path)
{
    RECT area;
    BITMAPINFO info;
    BITMAPFILEHEADER file_header;
    HDC screen, memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    uint8_t *bits;
    FILE *file;
    int width, height, ok = 0;
    DWORD row_bytes;
    if (window == NULL || window_hidden) return 0;
    GdiFlush();
    GetWindowRect(window, &area);
    area.left -= 24; area.top -= 24; area.right += 24; area.bottom += 24;
    if (area.left < 0) area.left = 0;
    if (area.top < 0) area.top = 0;
    if (area.right > GetSystemMetrics(SM_CXSCREEN)) area.right = GetSystemMetrics(SM_CXSCREEN);
    if (area.bottom > GetSystemMetrics(SM_CYSCREEN)) area.bottom = GetSystemMetrics(SM_CYSCREEN);
    width = area.right - area.left;
    height = area.bottom - area.top;
    if (width <= 0 || height <= 0) return 0;
    row_bytes = ((DWORD)width * 3u + 3u) & ~3u;
    /* Screen -> device bitmap -> 32-bit DIB, then packed to 24-bit here: the
     * plainest path, which every Win9x display driver has to get right. */
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height; /* bottom-up, as BMP files are */
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bits = malloc((size_t)width * (size_t)height * 4u);
    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateCompatibleBitmap(screen, width, height);
    if (bits != NULL && screen != NULL && memory != NULL && bitmap != NULL) {
        int copied, lines;
        previous = SelectObject(memory, bitmap);
        copied = BitBlt(memory, 0, 0, width, height, screen, area.left, area.top, SRCCOPY);
        SelectObject(memory, previous);
        lines = GetDIBits(screen, bitmap, 0, (UINT)height, bits, &info, DIB_RGB_COLORS);
        port_log("screenshot: %dx%d at %ld,%ld BitBlt=%d GetDIBits=%d lines", width, height,
                 (long)area.left, (long)area.top, copied, lines);
        if (lines == height && (file = fopen(path, "wb")) != NULL) {
            uint8_t *row = malloc(row_bytes);
            memset(&file_header, 0, sizeof file_header);
            file_header.bfType = 0x4d42;
            file_header.bfOffBits = sizeof file_header + sizeof info.bmiHeader;
            file_header.bfSize = file_header.bfOffBits + row_bytes * (DWORD)height;
            info.bmiHeader.biBitCount = 24;
            info.bmiHeader.biSizeImage = 0;
            ok = row != NULL && fwrite(&file_header, sizeof file_header, 1u, file) == 1u &&
                 fwrite(&info.bmiHeader, sizeof info.bmiHeader, 1u, file) == 1u;
            for (int y = 0; ok && y < height; ++y) {
                const uint8_t *source = bits + (size_t)y * (size_t)width * 4u;
                memset(row, 0, row_bytes);
                for (int x = 0; x < width; ++x) {
                    row[x * 3] = source[x * 4]; row[x * 3 + 1] = source[x * 4 + 1]; row[x * 3 + 2] = source[x * 4 + 2];
                }
                ok = fwrite(row, 1u, row_bytes, file) == row_bytes;
            }
            free(row);
            if (fclose(file) != 0) ok = 0;
        }
    }
    {
        /* Independent of the capture: what one screen pixel in the middle of
         * the picture reads back as. */
        POINT centre = {client_width / 2, client_height / 2};
        if (!window_fullscreen) ClientToScreen(window, &centre);
        if (screen != NULL) {
            const COLORREF pixel = GetPixel(screen, centre.x, centre.y);
            const COLORREF desktop = GetPixel(screen, 300, 5), corner = GetPixel(screen, centre.x - client_width / 2 + 2, centre.y - client_height / 2 + 2);
            port_log("screenshot: desktop pixel (300,5) is %02x%02x%02x, picture corner pixel is %02x%02x%02x", GetRValue(desktop),
                     GetGValue(desktop), GetBValue(desktop), GetRValue(corner), GetGValue(corner), GetBValue(corner));
            port_log("screenshot: screen pixel at picture centre is %02x%02x%02x (visible=%d iconic=%d foreground=%d)",
                     GetRValue(pixel), GetGValue(pixel), GetBValue(pixel), IsWindowVisible(window) != 0,
                     IsIconic(window) != 0, GetForegroundWindow() == window);
        }
    }
    if (bitmap != NULL) DeleteObject(bitmap);
    if (memory != NULL) DeleteDC(memory);
    if (screen != NULL) ReleaseDC(NULL, screen);
    free(bits);
    return ok;
}

int backend_window_create(const char *title, int width, int height, int hidden, int fullscreen)
{
    WNDCLASSA class_description;
    memset(&class_description, 0, sizeof class_description);
    class_description.lpfnWndProc = window_procedure;
    class_description.hInstance = GetModuleHandleA(NULL);
    class_description.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    class_description.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    class_description.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    class_description.lpszClassName = class_name;
    if (!RegisterClassA(&class_description)) return 0;
    window_hidden = hidden;
    /* fullscreen: 0 window, 1 the desktop's own resolution, 2 exclusive 640x480x16. */
    window_fullscreen = fullscreen == 2 && !hidden;
    client_width = windowed_width = width;
    client_height = windowed_height = height;
    window = CreateWindowExA(0, class_name, title,
        window_fullscreen ? WS_POPUP : WINDOWED_STYLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (window == NULL) return 0;
    if (!window_fullscreen) backend_window_size(width, height);
    if (fullscreen == 1 && !hidden) backend_window_fullscreen(1);
    if (!hidden) {
        HDC screen = GetDC(NULL);
        port_log("video: desktop %dx%d, %d bits per pixel", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                 GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES));
        ReleaseDC(NULL, screen);
        /* The first ShowWindow call of a process obeys the launcher's STARTUPINFO
         * (vmrun and some shells start programs hidden); the second is ours. */
        ShowWindow(window, SW_SHOWNORMAL);
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        SetFocus(window);
        /* Preferred: Direct3D 7 with point sampling (exact pixels, scaled by
         * the card). It keeps the desktop's mode, so not for --fullscreen-640. */
        if (!window_fullscreen && open_direct3d(window)) {
            d3d_active = 1;
            snprintf(video_name, sizeof video_name, "%s", d3d7_name());
            port_log("video: %s", video_name);
        } else if (!directdraw_open()) {
            snprintf(video_name, sizeof video_name, "GDI StretchDIBits");
            port_log("video: %s", video_name);
            if (window_fullscreen) { /* no mode switch without DirectDraw */
                window_fullscreen = 0;
                SetWindowLongA(window, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE);
                backend_window_size(width, height);
            }
        }
    } else
        snprintf(video_name, sizeof video_name, "hidden window (no blit)");
    return 1;
}

void backend_window_destroy(void)
{
    if (d3d_active) {
        if (port_options.bench) d3d7_report();
        d3d7_close();
        d3d_active = 0;
    }
    directdraw_release();
    if (window != NULL) DestroyWindow(window);
    window = NULL;
    UnregisterClassA(class_name, GetModuleHandleA(NULL));
}
void backend_window_title(const char *title) { if (window != NULL) SetWindowTextA(window, title); }
/* Desktop-resolution fullscreen: a borderless topmost window covering the
 * screen, in the desktop's own mode and colour depth. No mode switch, so it
 * works the same with DirectDraw and GDI and survives Alt+Tab. */
void backend_window_fullscreen(int fullscreen)
{
    if (window == NULL || window_hidden || window_fullscreen || window_borderless == (fullscreen != 0)) return;
    window_borderless = fullscreen != 0;
    if (window_borderless) {
        SetWindowLongA(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        client_width = GetSystemMetrics(SM_CXSCREEN);
        client_height = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(window, HWND_TOPMOST, 0, 0, client_width, client_height, SWP_FRAMECHANGED);
        port_log("video: fullscreen at the desktop's %dx%d", client_width, client_height);
    } else {
        SetWindowLongA(window, GWL_STYLE, WINDOWED_STYLE | WS_VISIBLE);
        SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
        backend_window_size(windowed_width, windowed_height);
    }
    if (d3d_active) {
        /* Windowed and page-flipping Direct3D are set up differently. */
        d3d7_close();
        d3d_active = open_direct3d(window);
        snprintf(video_name, sizeof video_name, "%s", d3d_active ? d3d7_name() : "GDI StretchDIBits");
        port_log("video: %s", video_name);
    }
    borders_dirty = 1;
    shim_post_simple(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
}

void backend_window_size(int width, int height)
{
    RECT area = {0, 0, width, height};
    int x, y;
    if (window == NULL) return;
    windowed_width = width;
    windowed_height = height;
    if (window_borderless || window_fullscreen) return; /* applies when leaving fullscreen */
    client_width = width;
    client_height = height;
    borders_dirty = 1;
    AdjustWindowRect(&area, (DWORD)GetWindowLongA(window, GWL_STYLE), FALSE);
    x = (GetSystemMetrics(SM_CXSCREEN) - (area.right - area.left)) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - (area.bottom - area.top)) / 2;
    SetWindowPos(window, NULL, x < 0 ? 0 : x, y < 0 ? 0 : y, area.right - area.left, area.bottom - area.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
void backend_output_size(int *width, int *height) { *width = client_width; *height = client_height; }
int backend_is_fullscreen(void) { return window_borderless || window_fullscreen; }
void backend_screen_size(int *width, int *height)
{
    *width = GetSystemMetrics(SM_CXSCREEN);
    *height = GetSystemMetrics(SM_CYSCREEN);
}

/* ---- audio: waveOut ---------------------------------------------------------------
 * The game mixes 10512 Hz stereo. Sound Blaster-class Win9x drivers are only
 * dependable at the standard rates, so blocks are resampled (linear) to
 * 22050 Hz into a ring of fixed buffers handed to waveOutWrite. */
enum { OUT_RATE = 22050, BUFFER_FRAMES = 512, BUFFER_COUNT = 24 };
static HWAVEOUT wave_out;
static WAVEHDR wave_headers[BUFFER_COUNT];
static int16_t *wave_memory;              /* BUFFER_COUNT * BUFFER_FRAMES stereo frames */
static unsigned wave_next;                /* header being filled */
static unsigned wave_fill;                /* frames already in it */
static uint32_t resample_phase;           /* 16.16 position between previous and next source frame */
static uint32_t resample_step;
static int16_t previous_frame[2];
static int source_rate;
static int gain_q8 = 256;
static int wave_paused;

static unsigned wave_pending_frames(void)
{
    unsigned frames = wave_fill;
    for (unsigned index = 0u; index < BUFFER_COUNT; ++index)
        if ((wave_headers[index].dwFlags & WHDR_INQUEUE) != 0u) frames += BUFFER_FRAMES;
    return frames;
}

static int wave_submit_current(void)
{
    WAVEHDR *header = &wave_headers[wave_next];
    header->dwBufferLength = BUFFER_FRAMES * 4u;
    if (waveOutWrite(wave_out, header, sizeof *header) != MMSYSERR_NOERROR) return 0;
    wave_next = (wave_next + 1u) % BUFFER_COUNT;
    wave_fill = 0u;
    return 1;
}

static int wave_open(void)
{
    WAVEFORMATEX format;
    const int rate = source_rate;
    if (waveOutGetNumDevs() == 0u) return 0;
    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = OUT_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = OUT_RATE * 4;
    if (waveOutOpen(&wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { wave_out = NULL; return 0; }
    wave_memory = calloc((size_t)BUFFER_COUNT * BUFFER_FRAMES * 2u, sizeof *wave_memory);
    if (wave_memory == NULL) { waveOutClose(wave_out); wave_out = NULL; return 0; }
    memset(wave_headers, 0, sizeof wave_headers);
    for (unsigned index = 0u; index < BUFFER_COUNT; ++index) {
        wave_headers[index].lpData = (LPSTR)(wave_memory + (size_t)index * BUFFER_FRAMES * 2u);
        wave_headers[index].dwBufferLength = BUFFER_FRAMES * 4u;
        waveOutPrepareHeader(wave_out, &wave_headers[index], sizeof wave_headers[index]);
    }
    wave_next = wave_fill = 0u;
    wave_paused = 0;
    {
        WAVEOUTCAPSA caps;
        UINT device = 0u;
        /* The mapper reports itself; name the first real card instead. */
        if (waveOutGetID(wave_out, &device) != MMSYSERR_NOERROR || device == WAVE_MAPPER) device = 0u;
        if (waveOutGetDevCapsA(device, &caps, sizeof caps) != MMSYSERR_NOERROR) strcpy(caps.szPname, "unknown");
        port_log("audio: waveOut device \"%s\"", caps.szPname);
    }
    port_log("audio: waveOut %d Hz stereo 16-bit (game mix %d Hz, resampled), never paused or reset while playing",
             OUT_RATE, rate);
    return 1;
}

/* Silence whatever is still queued without asking the driver to stop.
 * waveOutReset/waveOutPause/waveOutRestart on a live stream are what the
 * Sound Blaster PCI (sbpci.vxd) driver mishandled on the real PC (page fault
 * blue screens), and the game pauses and flushes the sound on every music
 * change. So the device is opened once, never stopped while the game runs,
 * and a flush just zeroes the buffers it has not played yet (the driver only
 * reads them; buffers it already copied play out within a few milliseconds). */
static void wave_silence_queued(void)
{
    for (unsigned index = 0u; index < BUFFER_COUNT; ++index)
        if ((wave_headers[index].dwFlags & WHDR_INQUEUE) != 0u)
            memset(wave_headers[index].lpData, 0, BUFFER_FRAMES * 4u);
}

static void wave_close(void)
{
    if (wave_out == NULL) return;
    /* Let the queue run dry (at most BUFFER_COUNT * 23 ms) instead of
     * waveOutReset; reset only if the driver never finishes. */
    wave_silence_queued();
    for (int waited = 0; waited < 1000 && wave_pending_frames() > wave_fill; waited += 10) Sleep(10);
    if (wave_pending_frames() > wave_fill) waveOutReset(wave_out);
    for (unsigned index = 0u; index < BUFFER_COUNT; ++index)
        waveOutUnprepareHeader(wave_out, &wave_headers[index], sizeof wave_headers[index]);
    waveOutClose(wave_out);
    wave_out = NULL;
    free(wave_memory);
    wave_memory = NULL;
}

/* Linear resampling of game frames to OUT_RATE, with the volume applied.
 * Returns the number of output frames (at most max_out; any beyond are lost). */
static unsigned resample(const int16_t *samples, unsigned frames, int16_t *out, unsigned max_out)
{
    unsigned produced = 0u;
    for (unsigned frame = 0u; frame < frames; ++frame) {
        const int16_t left = samples[frame * 2u], right = samples[frame * 2u + 1u];
        /* Emit every output frame that falls between the previous source
         * frame and this one. */
        while (resample_phase < 65536u) {
            const int32_t weight = (int32_t)resample_phase;
            int32_t a = previous_frame[0] + (((left - previous_frame[0]) * weight) >> 16);
            int32_t b = previous_frame[1] + (((right - previous_frame[1]) * weight) >> 16);
            a = (a * gain_q8) >> 8;
            b = (b * gain_q8) >> 8;
            if (produced < max_out) {
                out[produced * 2u] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
                out[produced * 2u + 1u] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
                ++produced;
            }
            resample_phase += resample_step;
        }
        resample_phase -= 65536u;
        previous_frame[0] = left;
        previous_frame[1] = right;
    }
    return produced;
}

static int wave_write(const int16_t *out, unsigned count)
{
    /* After an underrun (or at the start) lead with silence so the device
     * does not run dry again while the game produces the next block. */
    if (wave_pending_frames() == 0u) {
        for (int cushion = 0; cushion < 2; ++cushion) {
            if ((wave_headers[wave_next].dwFlags & WHDR_INQUEUE) != 0u) break;
            memset(wave_headers[wave_next].lpData, 0, BUFFER_FRAMES * 4u);
            if (!wave_submit_current()) return 0;
        }
    }
    for (unsigned index = 0u; index < count; ++index) {
        int16_t *slot;
        if ((wave_headers[wave_next].dwFlags & WHDR_INQUEUE) != 0u) return 1; /* ring full: drop the rest */
        slot = (int16_t *)(void *)wave_headers[wave_next].lpData + wave_fill * 2u;
        slot[0] = out[index * 2u];
        slot[1] = out[index * 2u + 1u];
        if (++wave_fill == BUFFER_FRAMES && !wave_submit_current()) return 0;
    }
    return 1;
}

static void wave_pause(int paused)
{
    if (wave_paused == paused) return;
    wave_paused = paused;
    /* No waveOutPause: while paused the queue gets no new blocks, so the
     * device simply runs out of buffers; silence what is still waiting. */
    if (paused) wave_silence_queued();
}
static void wave_clear(void)
{
    wave_silence_queued();
    wave_fill = 0u;
}

/* ---- audio: which device ----------------------------------------------------------
 * DirectSound by default: one looping buffer that the card plays round and
 * round from the moment it opens until the game quits, the way most games of
 * the time drive it. waveOut (--waveout) hands the driver a new 23 ms buffer
 * about 43 times a second and makes it stop and start its DMA whenever the
 * game falls behind; the real PC's Sound Blaster PCI driver page-faulted in
 * that path (sbpci.vxd, exception 0E, at the same place every time).
 * DirectSound needs the game window, which is created after the sound is
 * opened, so it is started on first use. */
enum { AUDIO_NONE, AUDIO_PENDING, AUDIO_WAVEOUT, AUDIO_DSOUND };
static int audio_kind;

static void resampler_reset(void)
{
    resample_phase = 0u;
    previous_frame[0] = previous_frame[1] = 0;
}

static int audio_ready(void)
{
    if (audio_kind != AUDIO_PENDING) return audio_kind != AUDIO_NONE;
    if (window == NULL) return 0;
    if (dsound_open(window, OUT_RATE)) {
        audio_kind = AUDIO_DSOUND;
        port_log("audio: DirectSound %d Hz stereo 16-bit looping buffer (game mix %d Hz, resampled)", OUT_RATE, source_rate);
    } else {
        audio_kind = wave_open() ? AUDIO_WAVEOUT : AUDIO_NONE;
        if (audio_kind == AUDIO_NONE) port_log("audio: no usable sound device; continuing without sound");
    }
    return audio_kind != AUDIO_NONE;
}

int backend_audio_open(int rate)
{
    source_rate = rate;
    resample_step = (uint32_t)(((uint64_t)rate << 16) / OUT_RATE);
    resampler_reset();
    if (!port_options.waveout && dsound_available()) { audio_kind = AUDIO_PENDING; return 1; }
    audio_kind = wave_open() ? AUDIO_WAVEOUT : AUDIO_NONE;
    return audio_kind != AUDIO_NONE;
}

void backend_audio_close(void)
{
    if (audio_kind == AUDIO_DSOUND) dsound_close();
    else if (audio_kind == AUDIO_WAVEOUT) wave_close();
    audio_kind = AUDIO_NONE;
}

unsigned backend_audio_queued(void)
{
    unsigned frames;
    if (!audio_ready()) return 0u;
    frames = audio_kind == AUDIO_DSOUND ? dsound_queued_frames() : wave_pending_frames();
    return (unsigned)((uint64_t)frames * (unsigned)source_rate / OUT_RATE) * 4u;
}

int backend_audio_write(const int16_t *samples, unsigned bytes)
{
    int16_t out[2u * 1024u];
    unsigned frames = bytes / 4u;
    if (!audio_ready()) return audio_kind == AUDIO_PENDING; /* no window yet: drop */
    while (frames > 0u) {
        /* 256 source frames make at most 538 output frames at 10512 -> 22050 Hz. */
        const unsigned chunk = frames < 256u ? frames : 256u;
        const unsigned count = resample(samples, chunk, out, 1024u);
        if (!(audio_kind == AUDIO_DSOUND ? dsound_write(out, count) : wave_write(out, count))) return 0;
        samples += chunk * 2u;
        frames -= chunk;
    }
    return 1;
}

void backend_audio_pause(int paused)
{
    if (!audio_ready()) return;
    if (audio_kind == AUDIO_DSOUND) { if (paused) dsound_silence(); }
    else wave_pause(paused);
}
void backend_audio_clear(void)
{
    if (!audio_ready()) return;
    if (audio_kind == AUDIO_DSOUND) dsound_silence(); else wave_clear();
    resampler_reset();
}
void backend_audio_gain(float gain)
{
    gain_q8 = (int)(gain * 256.0f);
    if (gain_q8 < 0) gain_q8 = 0;
    if (gain_q8 > 256) gain_q8 = 256;
}

/* ---- lifetime ------------------------------------------------------------------------ */
int backend_init(void)
{
    LARGE_INTEGER frequency;
    char *slash;
    GetModuleFileNameA(NULL, base_directory, MAX_PATH);
    slash = strrchr(base_directory, '\\');
    if (slash != NULL) slash[1] = '\0'; else strcpy(base_directory, ".\\");
    timeBeginPeriod(1); /* 1 ms Sleep and timeGetTime resolution */
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) counter_frequency = (uint64_t)frequency.QuadPart;
    else counter_frequency = 1000u; /* no performance counter: timeGetTime */
    return 1;
}
void backend_shutdown(void)
{
    backend_audio_close();
    if (ddraw_library != NULL) { FreeLibrary(ddraw_library); ddraw_library = NULL; }
    timeEndPeriod(1);
}
