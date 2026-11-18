/* Implementation of the SDL3 subset in sdl3/SDL3/SDL.h. Platform specifics
 * are delegated to the backend; everything here is portable C. */
#include "port.h"

#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* ---- init, errors, strings -------------------------------------------- */
static char error_text[256];
static Uint32 initialized;

bool SDL_Init(Uint32 flags) { initialized |= flags; return true; }
bool SDL_InitSubSystem(Uint32 flags) { initialized |= flags; return true; }
void SDL_QuitSubSystem(Uint32 flags) { initialized &= ~flags; }
Uint32 SDL_WasInit(Uint32 flags) { return initialized & flags; }
void SDL_Quit(void) { initialized = 0u; }
const char *SDL_GetError(void) { return error_text; }
bool SDL_SetError(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error_text, sizeof error_text, format, arguments);
    va_end(arguments);
    return false;
}
bool SDL_OutOfMemory(void) { return SDL_SetError("out of memory"); }
char *SDL_strdup(const char *text)
{
    const size_t size = strlen(text) + 1u;
    char *copy = malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}
size_t SDL_strlcpy(char *target, const char *source, size_t capacity)
{
    const size_t length = strlen(source);
    if (capacity != 0u) {
        const size_t count = length < capacity - 1u ? length : capacity - 1u;
        memcpy(target, source, count);
        target[count] = '\0';
    }
    return length;
}
int SDL_strcasecmp(const char *left, const char *right)
{
    for (;; ++left, ++right) {
        int a = (unsigned char)*left, b = (unsigned char)*right;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b || a == 0) return a - b;
    }
}
const char *SDL_getenv(const char *name) { return getenv(name); }

/* ---- time ------------------------------------------------------------- */
/* Scripted (headless) runs must not depend on wall time: every clock then
 * advances by one GBA frame per presented frame. */
static uint64_t virtual_ns(void) { return (uint64_t)shim_frame_count() * UINT64_C(16742707); }
Uint64 SDL_GetPerformanceFrequency(void) { return port_options.headless ? UINT64_C(1000000000) : backend_frequency(); }
Uint64 SDL_GetPerformanceCounter(void) { return port_options.headless ? virtual_ns() : backend_counter(); }
Uint64 SDL_GetTicksNS(void)
{
    if (port_options.headless) return virtual_ns();
    const uint64_t frequency = backend_frequency(), counter = backend_counter();
    return counter / frequency * UINT64_C(1000000000) + counter % frequency * UINT64_C(1000000000) / frequency;
}
Uint64 SDL_GetTicks(void) { return SDL_GetTicksNS() / UINT64_C(1000000); }
void SDL_Delay(Uint32 milliseconds)
{
    if (!port_options.headless) backend_sleep(milliseconds);
}

/* ---- threads: the kept code only needs these to exist ------------------ */
int SDL_GetAtomicInt(SDL_AtomicInt *atomic) { return atomic->value; }
int SDL_SetAtomicInt(SDL_AtomicInt *atomic, int value)
{
    const int previous = atomic->value;
    atomic->value = value;
    return previous;
}
bool SDL_CompareAndSwapAtomicInt(SDL_AtomicInt *atomic, int old_value, int new_value)
{
    if (atomic->value != old_value) return false;
    atomic->value = new_value;
    return true;
}
struct SDL_Mutex { int unused; };
SDL_Mutex *SDL_CreateMutex(void) { return calloc(1u, sizeof(SDL_Mutex)); }
void SDL_DestroyMutex(SDL_Mutex *mutex) { free(mutex); }
void SDL_LockMutex(SDL_Mutex *mutex) { (void)mutex; }
void SDL_UnlockMutex(SDL_Mutex *mutex) { (void)mutex; }

/* ---- files (ANSI C runtime only) ---------------------------------------- */
struct SDL_IOStream { FILE *file; SDL_IOStatus status; };
SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode)
{
    FILE *file = fopen(path, mode);
    SDL_IOStream *stream;
    if (file == NULL) { SDL_SetError("cannot open %.200s", path); return NULL; }
    stream = calloc(1u, sizeof *stream);
    if (stream == NULL) { fclose(file); return NULL; }
    stream->file = file;
    return stream;
}
size_t SDL_ReadIO(SDL_IOStream *stream, void *bytes, size_t size)
{
    const size_t count = fread(bytes, 1u, size, stream->file);
    stream->status = ferror(stream->file) ? SDL_IO_STATUS_ERROR
                     : (count < size && feof(stream->file)) ? SDL_IO_STATUS_EOF : SDL_IO_STATUS_READY;
    return count;
}
SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *stream) { return stream->status; }
bool SDL_CloseIO(SDL_IOStream *stream)
{
    const int ok = fclose(stream->file) == 0;
    free(stream);
    return ok;
}
/* Paths from upstream may end in a separator; stat() on Win9x rejects that. */
static const char *trimmed_path(const char *path, char *buffer, size_t capacity)
{
    size_t length = strlen(path);
    if (length == 0u || length >= capacity) return path;
    memcpy(buffer, path, length + 1u);
    while (length > 1u && (buffer[length - 1u] == '/' || buffer[length - 1u] == '\\') && buffer[length - 2u] != ':')
        buffer[--length] = '\0';
    return buffer;
}
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
    char buffer[1024];
    struct stat status;
    if (path == NULL || stat(trimmed_path(path, buffer, sizeof buffer), &status) != 0)
        return SDL_SetError("no such path");
    if (info != NULL) {
        memset(info, 0, sizeof *info);
        info->type = (status.st_mode & S_IFMT) == S_IFDIR ? SDL_PATHTYPE_DIRECTORY
                     : (status.st_mode & S_IFMT) == S_IFREG ? SDL_PATHTYPE_FILE : SDL_PATHTYPE_OTHER;
        info->size = (Uint64)status.st_size;
        info->modify_time = (SDL_Time)status.st_mtime * INT64_C(1000000000);
        info->create_time = info->access_time = info->modify_time;
    }
    return true;
}
static int make_directory(const char *path)
{
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}
bool SDL_CreateDirectory(const char *path)
{
    char buffer[1024];
    SDL_PathInfo info;
    size_t length;
    if (path == NULL || strlen(path) >= sizeof buffer) return false;
    path = trimmed_path(path, buffer, sizeof buffer);
    if (path != buffer) memcpy(buffer, path, strlen(path) + 1u);
    if (SDL_GetPathInfo(buffer, &info)) return info.type == SDL_PATHTYPE_DIRECTORY;
    length = strlen(buffer);
    /* Create missing parents, shallowest first. */
    for (size_t index = 1u; index < length; ++index) {
        if ((buffer[index] != '/' && buffer[index] != '\\') || buffer[index - 1u] == ':') continue;
        const char separator = buffer[index];
        buffer[index] = '\0';
        (void)make_directory(buffer);
        buffer[index] = separator;
    }
    return make_directory(buffer) != 0;
}
bool SDL_RemovePath(const char *path) { return remove(path) == 0 || errno == ENOENT; }
bool SDL_RenamePath(const char *old_path, const char *new_path)
{
    (void)remove(new_path); /* Win9x rename() refuses an existing target. */
    return rename(old_path, new_path) == 0;
}
bool SDL_EnumerateDirectory(const char *path, SDL_EnumerateDirectoryCallback callback, void *userdata)
{
    char directory[1024];
    const size_t length = strlen(path);
    SDL_EnumerationResult result = SDL_ENUM_CONTINUE;
    if (length + 6u >= sizeof directory) return false;
    memcpy(directory, path, length + 1u);
    if (length != 0u && directory[length - 1u] != '/' && directory[length - 1u] != '\\') strcat(directory, "/");
#ifdef _WIN32
    char pattern[1030];
    WIN32_FIND_DATAA found;
    HANDLE handle;
    snprintf(pattern, sizeof pattern, "%s*.*", directory);
    handle = FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        if (strcmp(found.cFileName, ".") == 0 || strcmp(found.cFileName, "..") == 0) continue;
        result = callback(userdata, directory, found.cFileName);
    } while (result == SDL_ENUM_CONTINUE && FindNextFileA(handle, &found));
    FindClose(handle);
#else
    DIR *handle = opendir(directory);
    struct dirent *entry;
    if (handle == NULL) return false;
    while (result == SDL_ENUM_CONTINUE && (entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        result = callback(userdata, directory, entry->d_name);
    }
    closedir(handle);
#endif
    return result != SDL_ENUM_FAILURE;
}
/* Saves, settings and the imported pack live beside the executable: Win98
 * has no per-user application-data convention worth depending on. */
char *SDL_GetPrefPath(const char *organization, const char *application)
{
    (void)organization; (void)application;
    return SDL_strdup(backend_base_directory());
}
char *SDL_GetCurrentDirectory(void) { return SDL_strdup(backend_base_directory()); }
char *SDL_GetClipboardText(void) { return SDL_strdup(""); }

/* ---- keyboard and events ----------------------------------------------- */
static bool key_state[SDL_SCANCODE_COUNT];
enum { EVENT_CAPACITY = 64 };
static SDL_Event event_queue[EVENT_CAPACITY];
static unsigned event_head, event_count;

const bool *SDL_GetKeyboardState(int *count)
{
    if (count != NULL) *count = SDL_SCANCODE_COUNT;
    return key_state;
}
void SDL_ResetKeyboard(void) { memset(key_state, 0, sizeof key_state); }
const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
    static char letter[2];
    static const struct { SDL_Scancode code; const char *name; } names[] = {
        {SDL_SCANCODE_RETURN, "Return"}, {SDL_SCANCODE_ESCAPE, "Escape"}, {SDL_SCANCODE_BACKSPACE, "Backspace"},
        {SDL_SCANCODE_TAB, "Tab"}, {SDL_SCANCODE_SPACE, "Space"}, {SDL_SCANCODE_RIGHT, "Right"},
        {SDL_SCANCODE_LEFT, "Left"}, {SDL_SCANCODE_DOWN, "Down"}, {SDL_SCANCODE_UP, "Up"},
        {SDL_SCANCODE_LCTRL, "Left Ctrl"}, {SDL_SCANCODE_LSHIFT, "Left Shift"}, {SDL_SCANCODE_LALT, "Left Alt"},
        {SDL_SCANCODE_RCTRL, "Right Ctrl"}, {SDL_SCANCODE_RSHIFT, "Right Shift"}, {SDL_SCANCODE_RALT, "Right Alt"},
        {SDL_SCANCODE_COMMA, ","}, {SDL_SCANCODE_PERIOD, "."}, {SDL_SCANCODE_SLASH, "/"},
        {SDL_SCANCODE_SEMICOLON, ";"}, {SDL_SCANCODE_KP_ENTER, "Keypad Enter"}};
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        letter[0] = (char)('A' + (scancode - SDL_SCANCODE_A));
        return letter;
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
        letter[0] = (char)(scancode == SDL_SCANCODE_0 ? '0' : '1' + (scancode - SDL_SCANCODE_1));
        return letter;
    }
    for (size_t index = 0u; index < sizeof names / sizeof names[0]; ++index)
        if (names[index].code == scancode) return names[index].name;
    return "";
}
bool SDL_PushEvent(SDL_Event *event)
{
    if (event_count == EVENT_CAPACITY) return false;
    event_queue[(event_head + event_count++) % EVENT_CAPACITY] = *event;
    return true;
}
void SDL_PumpEvents(void) { backend_pump(); }
bool SDL_PollEvent(SDL_Event *event)
{
    if (event_count == 0u) backend_pump();
    if (event_count == 0u) return false;
    if (event != NULL) *event = event_queue[event_head];
    event_head = (event_head + 1u) % EVENT_CAPACITY;
    --event_count;
    return true;
}
static SDL_Keycode keycode_of(SDL_Scancode scancode)
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) return (SDL_Keycode)('a' + (scancode - SDL_SCANCODE_A));
    switch (scancode) {
    case SDL_SCANCODE_RETURN: return SDLK_RETURN;
    case SDL_SCANCODE_ESCAPE: return SDLK_ESCAPE;
    case SDL_SCANCODE_TAB: return SDLK_TAB;
    case SDL_SCANCODE_SPACE: return SDLK_SPACE;
    default: return SDL_SCANCODE_TO_KEYCODE(scancode);
    }
}
void shim_post_key(SDL_Scancode scancode, int down, int repeat)
{
    SDL_Event event;
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_SCANCODE_COUNT) return;
    key_state[scancode] = down != 0;
    memset(&event, 0, sizeof event);
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.key = keycode_of(scancode);
    event.key.down = down != 0;
    event.key.repeat = repeat != 0;
    (void)SDL_PushEvent(&event);
}
void shim_post_simple(SDL_EventType type)
{
    SDL_Event event;
    memset(&event, 0, sizeof event);
    event.type = type;
    if (type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_ResetKeyboard();
    (void)SDL_PushEvent(&event);
}

/* ---- gamepads: none ------------------------------------------------------ */
SDL_JoystickID *SDL_GetGamepads(int *count)
{
    if (count != NULL) *count = 0;
    return calloc(1u, sizeof(SDL_JoystickID));
}
SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) { (void)id; return NULL; }
void SDL_CloseGamepad(SDL_Gamepad *gamepad) { (void)gamepad; }
bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button) { (void)gamepad; (void)button; return false; }
const char *SDL_GetGamepadStringForButton(SDL_GamepadButton button) { (void)button; return ""; }

/* ---- video ---------------------------------------------------------------- */
struct SDL_Window { int width, height, hidden; };
struct SDL_Renderer { SDL_Window *window; };
/* Room kept free above every texture's pixels, so the extra sky rows of a
 * taller race picture (tall.c) can be placed directly above them. */
enum { TEXTURE_HEADROOM_ROWS = 96 };
struct SDL_Texture {
    int width, height;
    uint32_t *storage; /* allocation: headroom rows, then the pixels */
    uint32_t *pixels;
    SDL_ScaleMode scale_mode;
};
static SDL_Window the_window;
static SDL_Renderer the_renderer;
static int window_open;
static int presented_frames;
/* The picture handed to SDL_RenderTexture since the last present. */
static const SDL_Texture *pending_texture;
static SDL_Rect pending_target;

int shim_frame_count(void) { return presented_frames; }

bool SDL_GL_SetAttribute(SDL_GLAttr attribute, int value) { (void)attribute; (void)value; return true; }
SDL_Window *SDL_CreateWindow(const char *title, int width, int height, SDL_WindowFlags flags)
{
    if (window_open) return &the_window;
    const int hidden = port_options.headless && !port_options.bench && !port_options.watch;
    int fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if (port_options.headless) {
        /* Scripted runs ask for a hidden 1x window; the command line decides. */
        const int scale = port_options.scale > 0 ? port_options.scale : 2;
        width = (width >= 240 ? width : 240) * scale; /* upstream passes the logical width at 1x */
        height = 160 * scale;
        fullscreen = port_options.fullscreen > 0;
    }
    if (port_options.fullscreen640) fullscreen = 2;
    the_window.width = width;
    the_window.height = height;
    the_window.hidden = hidden;
    if (!backend_window_create(title, width, height, hidden, fullscreen)) {
        SDL_SetError("could not create the window");
        return NULL;
    }
    window_open = 1;
    return &the_window;
}
void SDL_DestroyWindow(SDL_Window *window)
{
    if (window == NULL || !window_open) return;
    backend_window_destroy();
    window_open = 0;
}
bool SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    static char current[160];
    (void)window;
    /* Upstream sets the title every frame; only touch the OS on a change. */
    if (strncmp(current, title, sizeof current - 1u) == 0) return true;
    SDL_strlcpy(current, title, sizeof current);
    /* Upstream's titles describe its own key layout (Esc = back); here Esc quits. */
    backend_window_title("MKSC98 | arrows move | Z/Enter choose | X back | Space pause | Esc quit");
    return true;
}
/* The Options menu's Window Scale and Fullscreen entries arrive here. */
bool SDL_SetWindowSize(SDL_Window *window, int width, int height)
{
    (void)window;
    if (!port_options.headless) backend_window_size(width, height);
    return true;
}
bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen)
{
    (void)window;
    if (!port_options.headless && !port_options.fullscreen640) backend_window_fullscreen(fullscreen);
    return true;
}
bool SDL_SyncWindow(SDL_Window *window) { (void)window; return true; }
bool SDL_ShowWindow(SDL_Window *window) { (void)window; return true; }
SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name)
{
    (void)name;
    if (window == NULL) return NULL;
    the_renderer.window = window;
    return &the_renderer;
}
SDL_Renderer *SDL_CreateGPURenderer(SDL_GPUDevice *device, SDL_Window *window) { (void)device; (void)window; return NULL; }
SDL_GPUDevice *SDL_GetGPURendererDevice(SDL_Renderer *renderer) { (void)renderer; return NULL; }
void SDL_DestroyRenderer(SDL_Renderer *renderer) { (void)renderer; pending_texture = NULL; }
const char *SDL_GetRendererName(SDL_Renderer *renderer) { (void)renderer; return backend_video_name(); }
SDL_Window *SDL_GetRenderWindow(SDL_Renderer *renderer) { return renderer != NULL ? renderer->window : NULL; }
bool SDL_SetRenderVSync(SDL_Renderer *renderer, int vsync) { (void)renderer; (void)vsync; return true; }
bool SDL_GetCurrentRenderOutputSize(SDL_Renderer *renderer, int *width, int *height)
{
    (void)renderer;
    backend_output_size(width, height);
    return *width > 0 && *height > 0;
}
SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, SDL_PixelFormat format, SDL_TextureAccess access,
                               int width, int height)
{
    SDL_Texture *texture;
    (void)renderer; (void)format; (void)access;
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048) return NULL;
    texture = calloc(1u, sizeof *texture);
    if (texture == NULL) return NULL;
    texture->storage = calloc((size_t)width * (size_t)(height + TEXTURE_HEADROOM_ROWS), sizeof *texture->storage);
    if (texture->storage == NULL) { free(texture); return NULL; }
    texture->pixels = texture->storage + (size_t)width * TEXTURE_HEADROOM_ROWS;
    texture->width = width;
    texture->height = height;
    return texture;
}
void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (texture == NULL) return;
    if (pending_texture == texture) pending_texture = NULL;
    free(texture->storage);
    free(texture);
}
bool SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch)
{
    if (texture == NULL || pixels == NULL) return false;
    const int x = rect != NULL ? rect->x : 0, y = rect != NULL ? rect->y : 0;
    const int width = rect != NULL ? rect->w : texture->width, height = rect != NULL ? rect->h : texture->height;
    if (x < 0 || y < 0 || width > texture->width - x || height > texture->height - y) return false;
    for (int row = 0; row < height; ++row)
        memcpy(texture->pixels + (size_t)(y + row) * (size_t)texture->width + (size_t)x,
               (const uint8_t *)pixels + (size_t)row * (size_t)pitch, (size_t)width * sizeof(uint32_t));
    return true;
}
bool SDL_SetTextureScaleMode(SDL_Texture *texture, SDL_ScaleMode mode)
{
    if (texture == NULL) return false;
    texture->scale_mode = mode;
    return true;
}
bool SDL_GetTextureScaleMode(SDL_Texture *texture, SDL_ScaleMode *mode)
{
    if (texture == NULL) return false;
    *mode = texture->scale_mode;
    return true;
}
bool SDL_SetTextureBlendMode(SDL_Texture *texture, SDL_BlendMode mode) { (void)texture; (void)mode; return true; }
bool SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture) { (void)renderer; return texture == NULL; }
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *renderer) { (void)renderer; return NULL; }
bool SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    (void)renderer; (void)r; (void)g; (void)b; (void)a;
    return true;
}
bool SDL_RenderClear(SDL_Renderer *renderer) { (void)renderer; pending_texture = NULL; return true; }
bool SDL_RenderTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect *source,
                       const SDL_FRect *target)
{
    (void)renderer; (void)source;
    if (texture == NULL) return false;
    pending_texture = texture;
    if (target != NULL) {
        pending_target.x = (int)target->x; pending_target.y = (int)target->y;
        pending_target.w = (int)target->w; pending_target.h = (int)target->h;
    } else {
        pending_target.x = pending_target.y = 0;
        backend_output_size(&pending_target.w, &pending_target.h);
    }
    return true;
}
bool SDL_RenderPresent(SDL_Renderer *renderer)
{
    int ok = 1;
    (void)renderer;
    if (pending_texture != NULL) {
        const SDL_Texture *texture = pending_texture;
        const uint32_t *pixels = texture->pixels;
        int height = texture->height, band_rows = 0;
        SDL_Rect target = pending_target;
        /* A race frame with extra sky rows (tall.c): put them above the
         * 160-line picture and re-centre the taller result at the same
         * whole-number scale. Anything else is shown as it came. */
        const uint32_t *band = texture->height == 160 ? tall_take_band(texture->width, &band_rows) : NULL;
        if (band != NULL && band_rows <= TEXTURE_HEADROOM_ROWS && target.w % texture->width == 0 &&
            target.h == 160 * (target.w / texture->width)) {
            const int scale = target.w / texture->width, tall = band_rows + 160;
            int output_width, output_height;
            backend_output_size(&output_width, &output_height);
            if (tall * scale <= output_height) {
                /* The band goes into the headroom right above the picture. */
                uint32_t *first = texture->pixels - (size_t)texture->width * (size_t)band_rows;
                memcpy(first, band, (size_t)texture->width * (size_t)band_rows * sizeof *first);
                pixels = first;
                height = tall;
                target.h = tall * scale;
                target.y = (output_height - target.h) / 2;
            }
        }
        harness_frame(pixels, texture->width, height, texture->width);
        harness_phase_begin(PHASE_BLIT);
        ok = backend_present(pixels, texture->width, height, texture->width, target.x, target.y, target.w, target.h);
        harness_phase_end(PHASE_BLIT);
    }
    ++presented_frames;
    if (port_options.watch) {
        /* The scripted clock is virtual; hold each picture for one GBA frame. */
        static uint64_t due;
        const uint64_t frequency = backend_frequency(), step = frequency * 4389u / 262144u;
        uint64_t now = backend_counter();
        if (due == 0u || now > due + step * 8u) due = now;
        due += step;
        while ((now = backend_counter()) < due) {
            const uint64_t left_ms = (due - now) * 1000u / frequency;
            if (left_ms >= 2u) backend_sleep((unsigned)(left_ms - 1u));
        }
    }
    if (harness_done()) {
        static int captured;
        if (port_options.screenshot != NULL && !captured) {
            captured = 1;
            port_log("screenshot: %s %s", port_options.screenshot,
                     backend_screenshot(port_options.screenshot) ? "written" : "FAILED");
        }
        shim_post_simple(SDL_EVENT_QUIT);
    }
    return ok != 0;
}
bool SDL_FlushRenderer(SDL_Renderer *renderer) { (void)renderer; return true; }
