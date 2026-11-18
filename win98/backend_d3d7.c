/* Sharp scaling for the Windows 98 backend: the picture is uploaded as a
 * texture and drawn by Direct3D 7 as one point-sampled quad.
 *
 * Why this exists: DirectDraw's Blt has no filter control, and the Radeon's
 * driver smooths a stretched blit, which blurs the 240x160 picture at 5x.
 * Scaling on the CPU instead means writing megabytes of video memory per
 * frame. A textured quad with point filtering gives exact whole-pixel
 * scaling done by the card: the CPU only uploads the small source picture.
 *
 * Direct3D is used for this one quad only; the game is still drawn entirely
 * by the software renderer. Needs DirectX 7 (DirectDrawCreateEx) and a 3D
 * card; when either is missing the caller falls back to DirectDraw's Blt. */
#include "port.h"

#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

#include "backend_d3d7.h"

/* Defined here so no dxguid import library is needed. */
static const GUID guid_directdraw7 = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};
static const GUID guid_direct3d7 = {0xf5049e77, 0x4861, 0x11d2, {0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8}};
static const GUID guid_hal_device = {0x84e63de0, 0x46aa, 0x11cf, {0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e}};

typedef HRESULT (WINAPI *CreateExFunction)(GUID *, void **, REFIID, IUnknown *);
typedef struct { float x, y, z, rhw, u, v; } Vertex;

static HMODULE library;
static LPDIRECTDRAW7 draw;
static LPDIRECT3D7 direct3d;
static LPDIRECT3DDEVICE7 device;
static LPDIRECTDRAWSURFACE7 primary, target, texture;
static LPDIRECTDRAWCLIPPER clipper;
static int target_width, target_height;   /* render target: the whole screen */
static int texture_width, texture_height; /* powers of two */
static int texture_bits;                  /* 32 (X8R8G8B8) or 16 (R5G6B5) */
static int exclusive;                     /* own the screen and page-flip instead of copying to it */
static int clear_frames;                  /* flip-chain buffers still to be cleared to black */
static int last_x = -1, last_y, last_w, last_h;
static DWORD flip_flags;
static int flip_pending;                  /* drawn, flip still to be done */
static LONGLONG stage_ticks[4];           /* waiting for the previous flip, upload, draw, flip */
static long stage_frames, flips_deferred;
static char name[96];

const char *d3d7_name(void) { return name; }

void d3d7_close(void)
{
    if (texture != NULL) { IDirectDrawSurface7_Release(texture); texture = NULL; }
    if (device != NULL) { IDirect3DDevice7_Release(device); device = NULL; }
    if (direct3d != NULL) { IDirect3D7_Release(direct3d); direct3d = NULL; }
    if (target != NULL) { IDirectDrawSurface7_Release(target); target = NULL; }
    if (clipper != NULL) { IDirectDrawClipper_Release(clipper); clipper = NULL; }
    if (primary != NULL) { IDirectDrawSurface7_Release(primary); primary = NULL; }
    if (draw != NULL) {
        if (exclusive) IDirectDraw7_SetCooperativeLevel(draw, NULL, DDSCL_NORMAL);
        IDirectDraw7_Release(draw);
        draw = NULL;
    }
    last_x = -1;
    flip_pending = 0;
    if (library != NULL) { FreeLibrary(library); library = NULL; }
    texture_width = texture_height = 0;
}

static int fail(const char *step, HRESULT result)
{
    port_log("video: Direct3D 7 not used (%s, %08lx)", step, (unsigned long)result);
    d3d7_close();
    return 0;
}

int d3d7_open(HWND window, int own_screen)
{
    DDSCAPS2 back = {DDSCAPS_BACKBUFFER, 0, 0, {0}};
    DDCAPS driver;
    CreateExFunction create;
    DDSURFACEDESC2 description;
    DDPIXELFORMAT format;
    HRESULT result;
    library = LoadLibraryA("ddraw.dll");
    if (library == NULL) return 0;
    create = (CreateExFunction)(void *)GetProcAddress(library, "DirectDrawCreateEx");
    if (create == NULL) return fail("DirectX 7 is not installed", 0);
    if ((result = create(NULL, (void **)&draw, &guid_directdraw7, NULL)) != DD_OK) { draw = NULL; return fail("DirectDrawCreateEx", result); }
    /* DDSCL_FPUPRESERVE: Direct3D must leave the x87 control word alone. */
    exclusive = own_screen;
    target_width = GetSystemMetrics(SM_CXSCREEN);
    target_height = GetSystemMetrics(SM_CYSCREEN);
    if ((result = IDirectDraw7_SetCooperativeLevel(draw, window, (exclusive ? DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN : DDSCL_NORMAL) |
                                                        DDSCL_FPUPRESERVE)) != DD_OK)
        return fail("cooperative level", result);
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    if (exclusive) {
        /* Fullscreen in the desktop's own mode: the quad is drawn straight
         * into the back buffer of a flip chain and shown by a page flip, so
         * nothing has to be copied to the screen. */
        description.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
        description.dwBackBufferCount = 1;
        description.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE;
        if ((result = IDirectDraw7_CreateSurface(draw, &description, &primary, NULL)) != DD_OK) { primary = NULL; return fail("flipping primary surface", result); }
        if ((result = IDirectDrawSurface7_GetAttachedSurface(primary, &back, &target)) != DD_OK) { target = NULL; return fail("back buffer", result); }
        memset(&driver, 0, sizeof driver);
        driver.dwSize = sizeof driver;
        IDirectDraw7_GetCaps(draw, &driver, NULL);
        /* The game paces itself; a flip that waits for the monitor would turn
         * one late frame into a halved frame rate. */
        flip_flags = (driver.dwCaps2 & DDCAPS2_FLIPNOVSYNC) ? DDFLIP_NOVSYNC : 0u;
        clear_frames = 2;
    } else {
        description.dwFlags = DDSD_CAPS;
        description.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if ((result = IDirectDraw7_CreateSurface(draw, &description, &primary, NULL)) != DD_OK) { primary = NULL; return fail("primary surface", result); }
        if ((result = IDirectDraw7_CreateClipper(draw, 0, &clipper, NULL)) != DD_OK) { clipper = NULL; return fail("clipper", result); }
        IDirectDrawClipper_SetHWnd(clipper, 0, window);
        IDirectDrawSurface7_SetClipper(primary, clipper);
    }
    memset(&format, 0, sizeof format);
    format.dwSize = sizeof format;
    IDirectDrawSurface7_GetPixelFormat(primary, &format);
    if ((format.dwFlags & DDPF_RGB) == 0u || format.dwRGBBitCount < 16u) return fail("desktop is not 16/32-bit colour", 0);
    /* An off-screen 3D render target the size of the screen: the quad is
     * drawn there, then copied 1:1 to the window (video memory to video
     * memory, done by the card). */
    if (!exclusive) {
        memset(&description, 0, sizeof description);
        description.dwSize = sizeof description;
        description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        description.dwWidth = (DWORD)target_width;
        description.dwHeight = (DWORD)target_height;
        description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
        if ((result = IDirectDraw7_CreateSurface(draw, &description, &target, NULL)) != DD_OK) { target = NULL; return fail("3D render target", result); }
    }
    if ((result = IDirectDraw7_QueryInterface(draw, &guid_direct3d7, (void **)&direct3d)) != DD_OK) { direct3d = NULL; return fail("Direct3D 7 interface", result); }
    if ((result = IDirect3D7_CreateDevice(direct3d, &guid_hal_device, target, &device)) != DD_OK) { device = NULL; return fail("hardware 3D device", result); }
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ZENABLE, FALSE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_LIGHTING, FALSE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_DITHERENABLE, FALSE);
    IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_FOGENABLE, FALSE);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    /* The whole point: nearest-texel sampling, no smoothing. */
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_MAGFILTER, D3DTFG_POINT);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_MINFILTER, D3DTFN_POINT);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_MIPFILTER, D3DTFP_NONE);
    IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_ADDRESS, D3DTADDRESS_CLAMP);
    snprintf(name, sizeof name, "Direct3D 7 point-sampled quad, %lu-bit %s", (unsigned long)format.dwRGBBitCount,
             exclusive ? (flip_flags ? "fullscreen, page flip" : "fullscreen, page flip (waits for the monitor)") : "desktop");
    return 1;
}

static int make_texture(int width, int height)
{
    DDSURFACEDESC2 description;
    int wanted_width = 256, wanted_height = 256;
    while (wanted_width < width) wanted_width *= 2;
    while (wanted_height < height) wanted_height *= 2;
    if (texture != NULL && wanted_width == texture_width && wanted_height == texture_height) return 1;
    if (texture != NULL) { IDirectDrawSurface7_Release(texture); texture = NULL; }
    for (int bits = 32; bits >= 16 && texture == NULL; bits -= 16) {
        memset(&description, 0, sizeof description);
        description.dwSize = sizeof description;
        description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        description.dwWidth = (DWORD)wanted_width;
        description.dwHeight = (DWORD)wanted_height;
        description.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
        description.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE; /* system copy; the driver uploads it */
        description.ddpfPixelFormat.dwSize = sizeof description.ddpfPixelFormat;
        description.ddpfPixelFormat.dwFlags = DDPF_RGB;
        description.ddpfPixelFormat.dwRGBBitCount = (DWORD)bits;
        description.ddpfPixelFormat.dwRBitMask = bits == 32 ? 0x00ff0000u : 0xf800u;
        description.ddpfPixelFormat.dwGBitMask = bits == 32 ? 0x0000ff00u : 0x07e0u;
        description.ddpfPixelFormat.dwBBitMask = bits == 32 ? 0x000000ffu : 0x001fu;
        if (IDirectDraw7_CreateSurface(draw, &description, &texture, NULL) == DD_OK) texture_bits = bits;
        else texture = NULL;
    }
    if (texture == NULL) return 0;
    texture_width = wanted_width;
    texture_height = wanted_height;
    port_log("video: Direct3D texture %dx%d, %d-bit", texture_width, texture_height, texture_bits);
    return 1;
}

int d3d7_present(HWND window, const uint32_t *pixels, int width, int height, int pitch_pixels,
                 int x, int y, int output_width, int output_height)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        DDSURFACEDESC2 locked;
        D3DVIEWPORT7 viewport;
        Vertex quad[4];
        RECT source, destination;
        POINT origin = {0, 0};
        HRESULT result;
        LARGE_INTEGER t0, t1, t2, t3;
        if (!make_texture(width, height)) return 0;
        QueryPerformanceCounter(&t0);
        /* The previous picture's flip may still be waiting for the card to
         * finish drawing it (see below); it has to happen before this one
         * is drawn into the next buffer. */
        if (flip_pending) {
            (void)IDirectDrawSurface7_Flip(primary, NULL, DDFLIP_WAIT | flip_flags);
            flip_pending = 0;
        }
        QueryPerformanceCounter(&t1);
        stage_ticks[0] += t1.QuadPart - t0.QuadPart;
        memset(&locked, 0, sizeof locked);
        locked.dwSize = sizeof locked;
        result = IDirectDrawSurface7_Lock(texture, NULL, &locked, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
        if (result == DD_OK) {
            for (int row = 0; row < height; ++row) {
                const uint32_t *in = pixels + (size_t)row * (size_t)pitch_pixels;
                uint8_t *line = (uint8_t *)locked.lpSurface + (size_t)row * (size_t)locked.lPitch;
                if (texture_bits == 32) memcpy(line, in, (size_t)width * 4u);
                else {
                    uint16_t *out = (uint16_t *)(void *)line;
                    for (int column = 0; column < width; ++column) {
                        const uint32_t c = in[column];
                        out[column] = (uint16_t)(((c >> 8) & 0xf800u) | ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu));
                    }
                }
            }
            IDirectDrawSurface7_Unlock(texture, NULL);
            QueryPerformanceCounter(&t2);
            stage_ticks[1] += t2.QuadPart - t1.QuadPart;
            /* Clip to the render target; the picture is normally well inside. */
            if (x < 0 || y < 0 || x + output_width > target_width || y + output_height > target_height) return 0;
            memset(&viewport, 0, sizeof viewport);
            viewport.dwWidth = (DWORD)target_width;
            viewport.dwHeight = (DWORD)target_height;
            viewport.dvMaxZ = 1.0f;
            IDirect3DDevice7_SetViewport(device, &viewport);
            /* Pixel centres sit at whole coordinates, so the quad's edges go
             * half a pixel out: each texel then covers exactly scale x scale. */
            const float left = (float)x - 0.5f, top = (float)y - 0.5f;
            const float right = left + (float)output_width, bottom = top + (float)output_height;
            const float u = (float)width / (float)texture_width, v = (float)height / (float)texture_height;
            quad[0] = (Vertex){left, top, 0.0f, 1.0f, 0.0f, 0.0f};
            quad[1] = (Vertex){right, top, 0.0f, 1.0f, u, 0.0f};
            quad[2] = (Vertex){left, bottom, 0.0f, 1.0f, 0.0f, v};
            quad[3] = (Vertex){right, bottom, 0.0f, 1.0f, u, v};
            if (exclusive) {
                /* Black around the picture: both buffers of the chain, once each
                 * after the picture's place changes. */
                if (x != last_x || y != last_y || output_width != last_w || output_height != last_h) {
                    last_x = x; last_y = y; last_w = output_width; last_h = output_height;
                    clear_frames = 2;
                }
                if (clear_frames > 0) {
                    IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
                    --clear_frames;
                }
            }
            result = IDirect3DDevice7_BeginScene(device);
            if (result == DD_OK) {
                IDirect3DDevice7_SetTexture(device, 0, texture);
                result = IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | D3DFVF_TEX1,
                                                        quad, 4, 0);
                IDirect3DDevice7_EndScene(device);
            }
            QueryPerformanceCounter(&t3);
            stage_ticks[2] += t3.QuadPart - t2.QuadPart;
            ++stage_frames;
            if (result == DD_OK && exclusive) {
                /* Filling 1.3 million pixels takes the card a few milliseconds.
                 * Rather than wait here, ask for the flip without waiting; if
                 * the card is still drawing, the flip is done from d3d7_pump()
                 * (the game's loop calls it every pass) or, at the latest,
                 * before the next picture. The game computes its next frame
                 * meanwhile. */
                result = IDirectDrawSurface7_Flip(primary, NULL, (port_options.sync_flip ? DDFLIP_WAIT : DDFLIP_DONOTWAIT) | flip_flags);
                if (result == DDERR_WASSTILLDRAWING) { flip_pending = 1; ++flips_deferred; result = DD_OK; }
                QueryPerformanceCounter(&t0);
                stage_ticks[3] += t0.QuadPart - t3.QuadPart;
                if (result == DD_OK) return 1;
            } else if (result == DD_OK) {
                ClientToScreen(window, &origin);
                source.left = x; source.top = y; source.right = x + output_width; source.bottom = y + output_height;
                destination.left = origin.x + x; destination.top = origin.y + y;
                destination.right = destination.left + output_width;
                destination.bottom = destination.top + output_height;
                result = IDirectDrawSurface7_Blt(primary, &destination, target, &source, DDBLT_WAIT, NULL);
                if (result == DD_OK) return 1;
            }
        }
        if (result != DDERR_SURFACELOST) return 0;
        if (IDirectDraw7_RestoreAllSurfaces(draw) != DD_OK) return 1; /* still away: skip this frame */
        clear_frames = 2;
    }
    return 1;
}

int d3d7_owns_screen(void) { return draw != NULL && exclusive; }

/* Called from the message pump: finish a flip the card was not ready for. */
void d3d7_pump(void)
{
    if (!flip_pending || primary == NULL) return;
    if (IDirectDrawSurface7_Flip(primary, NULL, DDFLIP_DONOTWAIT | flip_flags) != DDERR_WASSTILLDRAWING) flip_pending = 0;
}

/* Bench: where the presenting time went, per picture. */
void d3d7_report(void)
{
    LARGE_INTEGER frequency;
    if (stage_frames == 0 || !QueryPerformanceFrequency(&frequency)) return;
    const double to_ms = 1000.0 / (double)frequency.QuadPart / (double)stage_frames;
    port_log("video: per picture: wait for previous flip %.3f ms, texture upload %.3f ms, draw %.3f ms, flip %.3f ms; "
             "%ld of %ld flips finished after the call returned", (double)stage_ticks[0] * to_ms,
             (double)stage_ticks[1] * to_ms, (double)stage_ticks[2] * to_ms, (double)stage_ticks[3] * to_ms,
             flips_deferred, stage_frames);
}
