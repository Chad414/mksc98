/* Entry point of the Windows 98 port.
 *
 * Start-up is fixed: the ROM is "Mario Kart - Super Circuit (USA).gba" in the
 * directory of the executable. It is imported once with upstream's importer
 * into game.pack beside the executable (re-imported when the importer version
 * changes), then the original game starts. There is no prompt or file dialog.
 * Everything printed goes to mksc98.log beside the executable. */
#include "port.h"

#include "asset_pack.h"
#include "game_host_internal.h"
#include "rom_import.h"
#include "rom_import_host.h"
#include "settings.h"
#include "startup_pack.h"

#define ROM_NAME "Mario Kart - Super Circuit (USA).gba"
#define PACK_NAME "game.pack"
#define LOG_NAME "mksc98.log"
#define TALL_ROWS_MAX 96

static void sibling(char *out, size_t capacity, const char *name)
{
    snprintf(out, capacity, "%s%s", backend_base_directory(), name);
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

static void open_log(void)
{
    char path[1100];
    FILE *file;
    sibling(path, sizeof path, LOG_NAME);
    file = fopen(path, "w"); /* truncate the previous run's log */
    if (file != NULL) fclose(file);
    /* Upstream reports through stdout and stderr; both append to the log and
     * are unbuffered, so every line is on disk before the next one runs. */
    if (freopen(path, "a", stdout) != NULL) setvbuf(stdout, NULL, _IONBF, 0);
    if (freopen(path, "a", stderr) != NULL) setvbuf(stderr, NULL, _IONBF, 0);
}

static void usage(void)
{
    port_log("options:");
    port_log("  --scale N          window size as a multiple of 240x160 (1..8); otherwise the saved Options value");
    port_log("  --fullscreen       fill the screen at the desktop's own resolution; the picture is the largest");
    port_log("                     whole multiple of 240x160 that fits, centred on black (5x on 1280x1024)");
    port_log("  --tall N           races get N extra rows of sky on top (1..96) whatever the aspect setting");
    port_log("  --windowed         start in a window even if fullscreen is saved in Options");
    port_log("  --fullscreen-640   switch the display to 640x480 16-bit instead (smallest blit, 2x picture)");
    port_log("  --aspect R         for this launch: original (240x160), fill (the Options setting: fills the screen");
    port_log("                     at a whole scale when fullscreen), or a fixed shape W:H from 3:2 to 32:5");
    port_log("  --sync-flip        fullscreen: wait inside the flip for the card to finish drawing");
    port_log("  --no-flip          fullscreen Direct3D copies the picture to the screen instead of page-flipping");
    port_log("  --ddraw            do not use Direct3D: DirectDraw stretch blit (the card may smooth the picture)");
    port_log("  --gdi              use neither (GDI StretchDIBits)");
    port_log("  --soft-scale       scale on the CPU and blit 1:1 instead of a hardware stretch blit");
    port_log("  --no-audio         do not open the sound device");
    port_log("  --waveout          sound through waveOut instead of a DirectSound looping buffer");
    port_log("  --fps              log frames per second every 5 seconds");
    port_log("test harness:");
    port_log("  --frames N         stop after N frames (with --bench: N race frames)");
    port_log("  --hash             scripted Grand Prix; log a hash of every 60th and of the last frame");
    port_log("  --dump-frame F.bmp write the last frame (needs --frames) as a 24-bit BMP");
    port_log("  --screenshot F.bmp after the last frame, save the screen area around the game window");
    port_log("  --bench            scripted Grand Prix with no vsync or sleep; report ms/frame by phase");
    port_log("  --watch            scripted Grand Prix shown in the window at normal speed with sound");
    port_log("  --mode M           scripted run of another mode: gp (default), quick, battle (C = arena 0..3), trial");
    port_log("  --route C:D:S      scripted run: cup C (0..), driver D (1..8), class S (0..2); default 0:1:0");
}

static int parse_options(int argc, char **argv)
{
    PortOptions *o = &port_options;
    o->route_cup = 0; o->route_character = 1; o->route_class = 0;
    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        const char *value = index + 1 < argc ? argv[index + 1] : NULL;
        if (strcmp(argument, "--scale") == 0 && value != NULL) { o->scale = atoi(value); ++index; }
        else if (strcmp(argument, "--fullscreen") == 0) o->fullscreen = 1;
        else if (strcmp(argument, "--windowed") == 0) o->fullscreen = -1;
        else if (strcmp(argument, "--fullscreen-640") == 0) o->fullscreen640 = 1;
        else if (strcmp(argument, "--aspect") == 0 && value != NULL) {
            int a = 0, b = 0;
            if (strcmp(value, "fill") == 0) o->aspect_width = -1;
            else if (strcmp(value, "original") == 0) o->aspect_width = 240;
            else if (sscanf(value, "%d:%d", &a, &b) == 2 && a > 0 && b > 0) o->aspect_width = (160 * a + b / 2) / b;
            else return 0;
            if (o->aspect_width != -1 && (o->aspect_width < 240 || o->aspect_width > 1024)) {
                port_log("--aspect: the game picture is 160 lines tall and at least 240 wide, so ratios from 3:2 up to 32:5 are possible");
                return 0;
            }
            ++index;
        }
        else if (strcmp(argument, "--gdi") == 0) o->force_gdi = 1;
        else if (strcmp(argument, "--ddraw") == 0) o->force_ddraw = 1;
        else if (strcmp(argument, "--no-flip") == 0) o->no_flip = 1;
        else if (strcmp(argument, "--sync-flip") == 0) o->sync_flip = 1;
        else if (strcmp(argument, "--soft-scale") == 0) o->soft_scale = 1;
        else if (strcmp(argument, "--no-audio") == 0) o->no_audio = 1;
        else if (strcmp(argument, "--waveout") == 0) o->waveout = 1;
        else if (strcmp(argument, "--fps") == 0) o->show_fps = 1;
        else if (strcmp(argument, "--frames") == 0 && value != NULL) { o->frames = atol(value); ++index; }
        else if (strcmp(argument, "--hash") == 0) o->hash = 1;
        else if (strcmp(argument, "--dump-frame") == 0 && value != NULL) { o->dump_frame = value; ++index; }
        else if (strcmp(argument, "--screenshot") == 0 && value != NULL) { o->screenshot = value; ++index; }
        else if (strcmp(argument, "--bench") == 0) o->bench = 1;
        else if (strcmp(argument, "--watch") == 0) o->watch = 1;
        else if (strcmp(argument, "--route") == 0 && value != NULL) {
            if (sscanf(value, "%d:%d:%d", &o->route_cup, &o->route_character, &o->route_class) != 3) return 0;
            o->route = 1; ++index;
        }
        else if (strcmp(argument, "--mode") == 0 && value != NULL) {
            o->route_mode = strcmp(value, "quick") == 0 ? 1 : strcmp(value, "battle") == 0 ? 2 : strcmp(value, "trial") == 0 ? 3 :
                            strcmp(value, "gp") == 0 ? 0 : -1;
            if (o->route_mode < 0) return 0;
            o->route = 1; ++index;
        }
        else if (strcmp(argument, "--tall") == 0 && value != NULL) {
            o->tall_rows = atoi(value);
            if (o->tall_rows < 0 || o->tall_rows > TALL_ROWS_MAX) return 0;
            ++index;
        }
        else if (strcmp(argument, "--help") == 0) { usage(); return -1; }
        else { port_log("unknown option: %s", argument); usage(); return 0; }
    }
    if (o->scale < 0 || o->scale > 8 || o->frames < 0) return 0;
    if (o->hash || o->bench || o->watch) o->route = 1;
    /* A scripted route replaces the keyboard and wall clock with upstream's
     * deterministic input aid: identical frames on every machine. */
    o->headless = o->route;
    if (o->bench && o->frames == 0) o->frames = 1800;
    return 1;
}

/* vmrun and some shells hand the whole option string over as one argument. */
static int split_arguments(int argc, char **argv, char **out, int capacity)
{
    int count = 0;
    for (int index = 0; index < argc && count < capacity - 1; ++index) {
        char *token = argv[index];
        if (index == 0 || strchr(token, ' ') == NULL || strncmp(token, "--", 2u) != 0) { out[count++] = token; continue; }
        for (token = strtok(token, " "); token != NULL && count < capacity - 1; token = strtok(NULL, " "))
            out[count++] = token;
    }
    out[count] = NULL;
    return count;
}

/* The base game only: every optional presentation is pinned off, whatever a
 * settings.json might say. */
static void force(PortableSettingsOverrides *overrides, PortableSetting field, uint32_t value)
{
    if (portable_setting_set(&overrides->values, field, value)) overrides->mask |= UINT64_C(1) << field;
    else port_log("warning: could not pin setting %s=%lu", portable_setting_name(field), (unsigned long)value);
}

static void base_game_overrides(PortableSettingsOverrides *overrides)
{
    memset(overrides, 0, sizeof *overrides);
    portable_settings_defaults(&overrides->values);
    force(overrides, PORTABLE_SETTING_CLASSIC_MENUS, 1u);   /* original 240x160 menus */
    force(overrides, PORTABLE_SETTING_RACE_RENDERER, 0u);   /* original software mode 7 */
    force(overrides, PORTABLE_SETTING_INTERNAL_SCALE, 1u);  /* 240x160, no supersampling */
    force(overrides, PORTABLE_SETTING_FILTER, 0u);          /* nearest */
    force(overrides, PORTABLE_SETTING_INTERPOLATE, 0u);     /* one picture per game tick */
    force(overrides, PORTABLE_SETTING_ARTWORK_LOD, 0u);
    force(overrides, PORTABLE_SETTING_DRIVING_PROFILE, 0u); /* original handling */
    force(overrides, PORTABLE_SETTING_TOUCH_CONTROLS, 0u);
    force(overrides, PORTABLE_SETTING_WIDE_UI_BACKGROUNDS, 0u);
    force(overrides, PORTABLE_SETTING_RUMBLE, 0u);
    /* The aspect setting (Original or Fill) is the player's, in Options; the
     * command line overrides it for one launch. Fill is stored as width 0. */
    if (port_options.aspect_width == -1) force(overrides, PORTABLE_SETTING_ASPECT_WIDTH, 0u);
    if (port_options.aspect_width > 0) force(overrides, PORTABLE_SETTING_ASPECT_WIDTH, (uint32_t)port_options.aspect_width);
    /* Window Scale, Fullscreen and Integer Scaling stay under the player's
     * control in Options; the command line overrides them for one launch. */
    if (port_options.scale > 0) force(overrides, PORTABLE_SETTING_WINDOW_SCALE, (uint32_t)port_options.scale);
    if (port_options.fullscreen != 0 || port_options.fullscreen640)
        force(overrides, PORTABLE_SETTING_FULLSCREEN, port_options.fullscreen > 0 && !port_options.fullscreen640 ? 1u : 0u);
    overrides->no_mods = 1u;
}

/* 1 ready, 0 failed (reason logged). */
static int prepare_pack(const char *rom_path, const char *pack_path)
{
    char error[512];
    if (file_exists(pack_path)) {
        uint32_t version = 0u;
        if (portable_startup_pack_probe(pack_path, 1, &version) == 1 && version >= MKSC_ROM_IMPORT_VERSION) {
            port_log("pack: using %s (importer version %lu)", pack_path, (unsigned long)version);
            return 1;
        }
        port_log("pack: %s is unreadable or from an older importer (version %lu, need %lu); importing again",
                 pack_path, (unsigned long)version, (unsigned long)MKSC_ROM_IMPORT_VERSION);
        portable_startup_pack_discard();
        if (remove(pack_path) != 0) { port_log("ERROR: cannot replace %s", pack_path); return 0; }
    }
    port_log("import: converting the ROM into %s (first start only; this takes a while)", pack_path);
    const uint64_t started = backend_counter();
    if (!portable_rom_import_file(rom_path, pack_path, error, sizeof error)) {
        port_log("ERROR: ROM import failed: %s", error);
        return 0;
    }
    port_log("import: done in %.1f s", (double)(backend_counter() - started) / (double)backend_frequency());
    return 1;
}

int main(int argc, char **argv)
{
    static char *arguments[64];
    static char rom_path[1100], pack_path[1100], progress_path[1200];
    PortableSettingsOverrides *overrides;
    PortableAssetPackView pack;
    uint8_t *bytes = NULL;
    size_t size = 0u;
    int result;

    if (!backend_init()) return 1;
    open_log();
    port_log("MKSC98 for Windows 98 (built %s %s)", __DATE__, __TIME__);
    argc = split_arguments(argc, argv, arguments, 64);
    result = parse_options(argc, arguments);
    if (result <= 0) { if (result == 0) port_log("ERROR: invalid command line"); backend_shutdown(); return result == 0 ? 2 : 0; }

    sibling(rom_path, sizeof rom_path, ROM_NAME);
    sibling(pack_path, sizeof pack_path, PACK_NAME);
    if (!file_exists(rom_path)) {
        port_log("ERROR: the ROM was not found. Put your own \"%s\" in the same directory as this program:", ROM_NAME);
        port_log("ERROR:   %s", rom_path);
        backend_shutdown();
        return 1;
    }
    if (!prepare_pack(rom_path, pack_path)) { backend_shutdown(); return 1; }
    if (portable_startup_pack_take(pack_path, 1, &bytes, &size, &pack) != 1) {
        port_log("ERROR: could not load %s", pack_path);
        backend_shutdown();
        return 1;
    }
    port_log("pack: %lu bytes loaded", (unsigned long)size);

    overrides = calloc(1u, sizeof *overrides);
    if (overrides == NULL) { backend_shutdown(); return 1; }
    base_game_overrides(overrides);
    if (port_options.route) {
        /* Upstream's scripted cup needs a scratch progress save; start clean
         * so every run sees the same state. */
        sibling(progress_path, sizeof progress_path, "harness-progress.bin");
        remove(progress_path + 0);
        memmove(progress_path + 17, progress_path, strlen(progress_path) + 1u);
        memcpy(progress_path, "MKSC_GP_PROGRESS=", 17u);
        putenv(progress_path);
        port_log("route: scripted Grand Prix cup=%d driver=%d class=%d frames=%ld%s%s", port_options.route_cup,
                 port_options.route_character, port_options.route_class, port_options.frames,
                 port_options.hash ? " hash" : "", port_options.bench ? " bench" : "");
        if (port_options.route_mode == 1)
            result = run_quick_run_menu_smoke(&pack, (uint8_t)port_options.route_cup, (uint32_t)port_options.route_character,
                                              (uint8_t)port_options.route_class, 0u, 0u, overrides);
        else if (port_options.route_mode == 2)
            result = run_battle_menu_smoke(&pack, (uint8_t)port_options.route_cup, (uint32_t)port_options.route_character,
                                           (uint8_t)port_options.route_class, overrides);
        else if (port_options.route_mode == 3) {
            static char save_directory[1200];
            sibling(save_directory, sizeof save_directory, "harness-save");
            result = run_game_shell_smoke(&pack, save_directory, (uint32_t)port_options.route_character, overrides);
        } else
            result = run_gp_cup_menu_smoke(&pack, (uint8_t)port_options.route_cup, (uint32_t)port_options.route_character,
                                           (uint8_t)port_options.route_class, 0, 0u, overrides);
    } else {
        port_log("controls: arrows steer/move, Z accelerate/confirm, X back, Down brake, Left Shift hop/drift,");
        port_log("controls: C item, Enter confirm, Space or P pause/start, Esc quits the game");
        result = run_time_trial_sdl_settings(&pack, overrides);
    }
    harness_report();
    if (port_options.frames > 0 && harness_done()) result = 0; /* stopped by --frames, as asked */
    port_log("exit: %d after %d frames", result, shim_frame_count());
    free(overrides);
    free(bytes);
    backend_shutdown();
    return result;
}
