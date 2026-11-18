/* Taller race pictures (--tall N, or --aspect fill on a screen with rows to
 * spare): N extra rows of sky above the game's 160 lines.
 *
 * The extra rows go on top so that the road, the karts and everything else
 * keep their exact place relative to the bottom edge. Upstream's renderer is
 * not told about the taller picture: it keeps drawing its 160 lines into the
 * game's own frame, and this file keeps a separate "band" of N rows that the
 * shim (shim.c) puts above that frame on screen. Per race frame:
 *   1. the scene is rendered as usual, but with its HUD switched off;
 *   2. the band is filled from the course's sky tile maps, which hold
 *      artwork above the part the game normally shows;
 *   3. the HUD is drawn once into a keyed layer and laid over band + frame so
 *      that its top row and the portrait column sit at the new top edge,
 *      while the rank and the map stay at the bottom.
 * A few things the game draws over the scene afterwards are caught the same
 * way (link-time wrappers below): the cup introduction's title bars and its
 * sliding HUD, and pictures such as Lakitu that enter from above.
 * Screens that render no race scene (menus) have no band and are shown as
 * the usual 160 lines, centred.
 *
 * This file also holds the bench timer around the scene render. */
#include "port.h"

#include "frontend_image_renderer.h"
#include "gp_opening_renderer.h"
#include "gp_results_renderer.h"
#include "gp_standings_renderer.h"
#include "hud_renderer.h"
#include "race_scene_renderer.h"
#include "sky_renderer.h"

int __real_portable_race_scene_render(const PortableRaceSceneView *scene,
                                      const PortableRacePresentationState *state, PortableSoftwareFrame *frame);
int __real_portable_hud_render_entry_bands(const PortableSoftwareFrame *hud, uint8_t progress,
                                           uint32_t transparent, PortableSoftwareFrame *frame);
int __real_portable_gp_opening_render(const PortableGpOpeningAssetView *asset, const PortableGpEntrySnapshot *entry,
                                      uint8_t engine_class, PortableSoftwareFrame *frame);
PortableFrontendPresentationStatus __real_portable_frontend_image_draw_scaled(
    const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame,
    int x, int y, int left, int top, int right, int bottom, unsigned scale);

/* HUD regions in the game's 160-line layout. Top group: the coins / lap /
 * item / time row, and the portrait column under its left end. Everything
 * else (rank, course map, speed gauge) belongs to the bottom edge. */
enum { HUD_TOP_ROWS = 32, HUD_COLUMN_WIDTH = 40, HUD_COLUMN_BOTTOM = 115, TALL_MAX_WIDTH = 1024 };
#define LAYER_KEY PORTABLE_LAP_BOARD_HUD_TRANSPARENT /* upstream's own "nothing drawn here" colour */

static uint32_t *band_pixels;   /* band_width x band_rows, above the game's frame */
static size_t band_capacity;
static uint32_t *layer_pixels;  /* width x 160 scratch layer */
static size_t layer_capacity;
static PortableRacePresentationState *scene_state; /* copy with the HUD switched off */
static int band_width, band_rows, band_fresh;
/* The scene was rendered without its HUD for a screen the game lays out
 * over it (result boards, retry menu): pictures it adds are centred too. */
static int overlays_centred;
/* The game frame the band belongs to, to recognise later draws onto it. */
static uint32_t *race_pixels;
static size_t race_stride;

/* ---- the Fill picture shape -----------------------------------------------
 * Options > Aspect Ratio has two values in this port: Original (stored as a
 * width of 240) and Fill (stored as 0, upstream's "auto"). Fill means: at the
 * largest whole scale that shows 240x160 on this screen, use every column
 * and every row that fits - a wider picture, and extra sky rows in races.
 * It applies while the game covers the whole screen; in a window Fill is the
 * original picture. Upstream asks for the picture width through
 * portable_software_frame_window_width(), wrapped here. */
size_t __real_portable_software_frame_window_width(void *renderer, uint16_t aspect_width);
static int fill_active, fill_logged_width;

static void fill_geometry(int *width, int *rows)
{
    int screen_width, screen_height, scale;
    backend_screen_size(&screen_width, &screen_height);
    scale = screen_height / 160 < screen_width / 240 ? screen_height / 160 : screen_width / 240;
    if (scale < 1) scale = 1;
    *width = screen_width / scale;
    if (*width < 240) *width = 240;
    if (*width > TALL_MAX_WIDTH) *width = TALL_MAX_WIDTH;
    *rows = screen_height / scale - 160;
    if (*rows < 0) *rows = 0;
    if (*rows > 96) *rows = 96;
    if (fill_logged_width != *width * 1000 + *rows) {
        fill_logged_width = *width * 1000 + *rows;
        port_log("aspect: fill -> %dx%d picture at %dx on the %dx%d screen", *width, 160 + *rows, scale,
                 screen_width, screen_height);
    }
}

size_t __wrap_portable_software_frame_window_width(void *renderer, uint16_t aspect_width)
{
    int width, rows;
    if (aspect_width != 0u) {
        fill_active = 0;
        return __real_portable_software_frame_window_width(renderer, aspect_width);
    }
    fill_active = backend_is_fullscreen();
    if (!fill_active) return 240u;
    fill_geometry(&width, &rows);
    return (size_t)width;
}

int tall_rows_now(void)
{
    int width, rows;
    if (port_options.tall_rows > 0) return port_options.tall_rows;
    if (!fill_active) return 0;
    fill_geometry(&width, &rows);
    return rows;
}

static int ensure(uint32_t **pixels, size_t *capacity, size_t count)
{
    if (*capacity >= count) return 1;
    free(*pixels);
    *pixels = malloc(count * sizeof **pixels);
    *capacity = *pixels != NULL ? count : 0u;
    return *pixels != NULL;
}

/* Is `frame` the top-left-anchored race frame (or a column window of it, as
 * the centred 240-wide views are) that the current band sits above? If so,
 * reports the window's first column. */
static int on_race_frame(const PortableSoftwareFrame *frame, size_t *column)
{
    if (!band_fresh || frame == NULL || frame->pixels == NULL || frame->scale > 1u || frame->height != 160u ||
        frame->pixels < race_pixels || (size_t)(frame->pixels - race_pixels) >= (size_t)band_width)
        return 0;
    if (frame->pixels != race_pixels && frame->stride != race_stride) return 0;
    *column = (size_t)(frame->pixels - race_pixels);
    return *column + frame->width <= (size_t)band_width;
}

/* A row of the tall picture: band rows first, then the game's frame. */
static uint32_t *tall_row(size_t y)
{
    return y < (size_t)band_rows ? band_pixels + y * (size_t)band_width
                                 : race_pixels + (y - (size_t)band_rows) * race_stride;
}

static void fill_layer(size_t width)
{
    for (size_t index = 0u; index < width * 160u; ++index) layer_pixels[index] = LAYER_KEY;
}

/* Lays the HUD layer over the tall picture: top group at the same row
 * counted from the new top, everything else `band_rows` lower (its usual
 * place in the game's frame). */
static void composite_hud(size_t width)
{
    const size_t column = width < HUD_COLUMN_WIDTH ? width : HUD_COLUMN_WIDTH;
    for (size_t y = 0u; y < 160u; ++y) {
        const uint32_t *source = layer_pixels + y * width;
        uint32_t *raised = tall_row(y), *usual = tall_row(y + (size_t)band_rows);
        const size_t raised_columns = y < HUD_TOP_ROWS ? width : y < HUD_COLUMN_BOTTOM ? column : 0u;
        for (size_t x = 0u; x < raised_columns; ++x)
            if (source[x] != LAYER_KEY) raised[x] = source[x];
        for (size_t x = raised_columns; x < width; ++x)
            if (source[x] != LAYER_KEY) usual[x] = source[x];
    }
}

/* The band: the sky continued upward. */
static void render_sky_band(const PortableRaceSceneView *scene, const PortableRacePresentationState *state,
                            const PortableSoftwareFrame *frame, size_t width, size_t rows)
{
    PortableSoftwareFrame band = *frame;
    size_t valid = 0u; /* band rows, counted from the bottom, that hold real sky art */
    band.pixels = band_pixels;
    band.height = rows;
    band.stride = 0u;
    if (state->sky_motion.initialized && scene->sky != NULL) {
        PortableSkyMotionSnapshot motion = state->sky_motion;
        valid = rows;
        for (unsigned layer = 0u; layer < 3u; ++layer) {
            /* A layer scrolled s >= 0 rows into its map has s rows of art
             * above the view; past that the map would wrap round to its
             * bottom. Layers already at a negative scroll wrap by design. */
            if ((motion.layer_mask & (1u << layer)) != 0u && motion.scroll_y[layer] >= 0 &&
                (size_t)motion.scroll_y[layer] < valid)
                valid = (size_t)motion.scroll_y[layer];
            motion.scroll_y[layer] -= (int32_t)rows;
            motion.scroll_y_q16[layer] -= (int32_t)rows * 65536;
        }
        motion.split_y = -1;
        motion.road_line = 159; /* every band row is above the road */
        if (!portable_sky_render_motion_back(scene->sky, &band, &motion) ||
            !portable_sky_render_motion_front(scene->sky, &band, &motion))
            valid = 0u;
    }
    /* Above the last row of real art, repeat the nearest valid row (the top
     * of a sky map is open sky); with no scrolled sky at all, repeat the top
     * row of the game's frame. */
    const uint32_t *repeat = valid != 0u ? band_pixels + (rows - valid) * width : frame->pixels;
    for (size_t y = 0u; y + valid < rows; ++y) memcpy(band_pixels + y * width, repeat, width * sizeof *band_pixels);
}

static int tall_render(const PortableRaceSceneView *scene, const PortableRacePresentationState *state,
                       PortableSoftwareFrame *frame)
{
    const size_t rows = (size_t)tall_rows_now(), width = frame->width;
    PortableSoftwareFrame layer = *frame;
    band_fresh = 0;
    if (rows == 0u) return __real_portable_race_scene_render(scene, state, frame);
    /* Only the plain full-width race picture is extended. */
    if (frame->height != 160u || frame->scale > 1u || frame->depth != NULL || width < 240u ||
        width > TALL_MAX_WIDTH || !ensure(&band_pixels, &band_capacity, width * rows) ||
        !ensure(&layer_pixels, &layer_capacity, width * 160u))
        return __real_portable_race_scene_render(scene, state, frame);
    if (state->suppress_native_hud) {
        if (!__real_portable_race_scene_render(scene, state, frame)) return 0;
    } else {
        if (scene_state == NULL) scene_state = malloc(sizeof *scene_state);
        if (scene_state == NULL) return __real_portable_race_scene_render(scene, state, frame);
        *scene_state = *state;
        scene_state->suppress_native_hud = 1u;
        if (!__real_portable_race_scene_render(scene, scene_state, frame)) return 0;
    }
    render_sky_band(scene, state, frame, width, rows);
    band_width = (int)width;
    band_rows = (int)rows;
    overlays_centred = state->suppress_native_hud != 0u;
    race_pixels = frame->pixels;
    race_stride = frame->stride != 0u ? frame->stride : frame->width;
    band_fresh = 1;
    if (!state->suppress_native_hud) {
        layer.pixels = layer_pixels;
        layer.stride = 0u;
        fill_layer(width);
        if (!portable_race_scene_render_hud(scene, state, &layer)) return 0;
        composite_hud(width);
    }
    return 1;
}

/* For the shim: the rows to show above the 160-line picture it is about to
 * present, if the last scene render produced them for a picture this wide.
 * They are handed out once. */
const uint32_t *tall_take_band(int width, int *rows)
{
    if (!band_fresh || width != band_width) { band_fresh = 0; return NULL; }
    {
        /* A screen closed to black (the retry and exit wipes) takes the sky
         * rows with it. */
        size_t y = 0u, x = 0u;
        for (y = 0u; y < 160u; ++y) {
            const uint32_t *row = race_pixels + y * race_stride;
            for (x = 0u; x < (size_t)band_width && (row[x] & UINT32_C(0x00ffffff)) == 0u; ++x) {}
            if (x < (size_t)band_width) break;
        }
        if (y == 160u) { band_fresh = 0; return NULL; }
    }
    band_fresh = 0;
    *rows = band_rows;
    return band_pixels;
}

int __wrap_portable_race_scene_render(const PortableRaceSceneView *scene,
                                      const PortableRacePresentationState *state, PortableSoftwareFrame *frame)
{
    int result;
    harness_phase_begin(PHASE_RENDER);
    result = tall_render(scene, state, frame);
    harness_phase_end(PHASE_RENDER);
    return result;
}

/* The cup introduction slides the HUD in as three bands. Take what it
 * would lay onto the frame and place it like the HUD above. */
int __wrap_portable_hud_render_entry_bands(const PortableSoftwareFrame *hud, uint8_t progress,
                                           uint32_t transparent, PortableSoftwareFrame *frame)
{
    size_t column;
    PortableSoftwareFrame layer;
    if (!on_race_frame(frame, &column) || column != 0u || frame->width != (size_t)band_width ||
        !ensure(&layer_pixels, &layer_capacity, frame->width * 160u))
        return __real_portable_hud_render_entry_bands(hud, progress, transparent, frame);
    layer = *frame;
    layer.pixels = layer_pixels;
    layer.stride = 0u;
    fill_layer(frame->width);
    if (!__real_portable_hud_render_entry_bands(hud, progress, transparent, &layer)) return 0;
    composite_hud(frame->width);
    return 1;
}

/* The cup introduction's title bars, panel and reveal are laid out for 160
 * lines: a bar on top, a bar at the bottom. Keep the top half at the new top
 * edge and the bottom half at the bottom; the rows in between repeat the
 * overlay's middle row, which carries only the reveal effect. */
int __wrap_portable_gp_opening_render(const PortableGpOpeningAssetView *asset, const PortableGpEntrySnapshot *entry,
                                      uint8_t engine_class, PortableSoftwareFrame *frame)
{
    size_t column;
    PortableSoftwareFrame layer;
    if (!on_race_frame(frame, &column) || column != 0u || frame->width != (size_t)band_width ||
        !ensure(&layer_pixels, &layer_capacity, frame->width * 160u))
        return __real_portable_gp_opening_render(asset, entry, engine_class, frame);
    const size_t width = frame->width, rows = (size_t)band_rows;
    layer = *frame;
    layer.pixels = layer_pixels;
    layer.stride = 0u;
    fill_layer(width);
    if (!__real_portable_gp_opening_render(asset, entry, engine_class, &layer)) return 0;
    for (size_t y = 0u; y < 160u + rows; ++y) {
        const size_t from = y < 80u ? y : y < 80u + rows ? 80u : y - rows;
        const uint32_t *source = layer_pixels + from * width;
        uint32_t *target = tall_row(y);
        for (size_t x = 0u; x < width; ++x)
            if (source[x] != LAYER_KEY) target[x] = source[x];
    }
    return 1;
}

/* Pictures the game lays over the race frame can start above its top edge
 * (Lakitu flying in). Draw the part that falls in the band as well: the
 * same call, moved down by the band's height and clipped to the band. */
PortableFrontendPresentationStatus __wrap_portable_frontend_image_draw_scaled(
    const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame,
    int x, int y, int left, int top, int right, int bottom, unsigned scale)
{
    size_t column;
    const PortableFrontendPresentationStatus status =
        __real_portable_frontend_image_draw_scaled(pack, id, frame, x, y, left, top, right, bottom, scale);
    if (status == PORTABLE_FRONTEND_PRESENTATION_OK && top <= 0 && on_race_frame(frame, &column)) {
        PortableSoftwareFrame band = *frame;
        band.pixels = band_pixels + column;
        band.height = (size_t)band_rows;
        band.stride = (size_t)band_width;
        (void)__real_portable_frontend_image_draw_scaled(pack, id, &band, x, y + band_rows, left, 0, right,
                                                         band_rows, scale);
    }
    return status;
}

/* ---- result screens ---------------------------------------------------------
 * After a race the game lays its result boards over the race picture: the lap
 * times board (with the HUD it froze), the race results / Grand Prix points
 * board and the finish portraits. They are laid out for 160 lines. In the
 * taller picture each is drawn into the keyed layer and then placed so the
 * composition is centred: half the extra rows above it, half below. Parts of
 * the frozen HUD go where the live HUD goes (top group up, rank and map at the
 * bottom); the coin counter the game adds goes to the top as well. */
enum { PLACE_CENTRED, PLACE_TOP, PLACE_LAP_BOARD };

int __real_portable_gp_results_render_laps_with_hud(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow, const uint16_t *times_centiseconds, uint8_t lap_count,
    const PortableLapBoardHud *hud, PortableSoftwareFrame *frame);
int __real_portable_gp_results_render_coins(const PortableGpResultsAssetView *asset, uint8_t coins,
                                            PortableSoftwareFrame *frame);

/* Points `layer` at the keyed scratch with the same geometry as `frame` (a
 * full-width race frame or a column window of it). 0 = not a tall race frame. */
static int layer_for(const PortableSoftwareFrame *frame, PortableSoftwareFrame *layer)
{
    size_t column;
    if (!on_race_frame(frame, &column) || !ensure(&layer_pixels, &layer_capacity, (size_t)band_width * 160u))
        return 0;
    fill_layer((size_t)band_width);
    *layer = *frame;
    layer->pixels = layer_pixels + column;
    layer->stride = (size_t)band_width;
    if (frame->pixels == race_pixels && frame->stride == 0u) layer->stride = 0u;
    return 1;
}

static void place_layer(int mode)
{
    const size_t width = (size_t)band_width, rows = (size_t)band_rows;
    for (size_t y = 0u; y < 160u; ++y) {
        const uint32_t *source = layer_pixels + y * width;
        for (size_t x = 0u; x < width; ++x) {
            size_t shift = rows / 2u;
            if (source[x] == LAYER_KEY) continue;
            if (mode == PLACE_TOP) shift = 0u;
            else if (mode == PLACE_LAP_BOARD) {
                /* The frozen HUD regions the lap board copies in (801E4CC):
                 * left portrait column and top row go up, the course map at
                 * the bottom right and the rank at the bottom left go down. */
                if (y < 32u || (x < 40u && y < 120u)) shift = 0u;
                else if ((y >= 96u && x + 64u >= width) || (y >= 128u && x < 48u)) shift = rows;
            }
            tall_row(y + shift)[x] = source[x];
        }
    }
}

/* The lap board copies frozen HUD pieces over itself, which slide out while
 * the board appears. Drawing it once with an empty HUD tells the board's own
 * pixels apart from the HUD's: the board is centred, the HUD pieces go where
 * the live HUD goes (top row and portrait column up, map and rank down). */
static uint32_t *board_pixels;
static size_t board_capacity;
static PortableLapBoardHud *empty_hud;

int __wrap_portable_gp_results_render_laps_with_hud(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow, const uint16_t *times_centiseconds, uint8_t lap_count,
    const PortableLapBoardHud *hud, PortableSoftwareFrame *frame)
{
    PortableSoftwareFrame layer;
    if (empty_hud == NULL && (empty_hud = malloc(sizeof *empty_hud)) != NULL)
        for (size_t index = 0u; index < 240u * 160u; ++index) empty_hud->pixels[index] = PORTABLE_LAP_BOARD_HUD_TRANSPARENT;
    if (hud == NULL || empty_hud == NULL || !layer_for(frame, &layer) ||
        !ensure(&board_pixels, &board_capacity, (size_t)band_width * 160u))
        return __real_portable_gp_results_render_laps_with_hud(asset, flow, times_centiseconds, lap_count, hud, frame);
    const size_t width = (size_t)band_width, rows = (size_t)band_rows;
    if (!__real_portable_gp_results_render_laps_with_hud(asset, flow, times_centiseconds, lap_count, empty_hud, &layer))
        return 0;
    memcpy(board_pixels, layer_pixels, width * 160u * sizeof *board_pixels);
    fill_layer(width);
    if (!__real_portable_gp_results_render_laps_with_hud(asset, flow, times_centiseconds, lap_count, hud, &layer))
        return 0;
    for (size_t y = 0u; y < 160u; ++y)
        for (size_t x = 0u; x < width; ++x) {
            const uint32_t pixel = layer_pixels[y * width + x];
            size_t shift;
            if (pixel == LAYER_KEY) continue;
            if (board_pixels[y * width + x] == pixel) shift = rows / 2u;
            else shift = y < 120u && (y < 32u || x < 40u) ? 0u : rows;
            tall_row(y + shift)[x] = pixel;
        }
    return 1;
}

/* Boards drawn over the race (or over the race background of the standings
 * screen), centred as a whole. */
#define CENTRED(name, params, args)                                         \
    int __real_##name params;                                               \
    int __wrap_##name params                                                \
    {                                                                       \
        PortableSoftwareFrame layer, *target = frame;                       \
        if (!layer_for(frame, &layer)) return __real_##name args;           \
        target = &layer;                                                    \
        if (!__real_##name args) return 0;                                  \
        place_layer(PLACE_CENTRED);                                         \
        return 1;                                                           \
    }

CENTRED(portable_gp_time_board_render,
        (const PortableGpStandingsAssetView *asset, const PortableTimeBoardRow rows[8], const uint8_t points[8],
         const uint8_t awards[8], const PortableGpTimeBoardFlow *flow, uint8_t engine_class,
         PortableSoftwareFrame *frame),
        (asset, rows, points, awards, flow, engine_class, target))
CENTRED(portable_time_board_render_slide,
        (const PortableGpStandingsAssetView *asset, const PortableGpResultsAssetView *results,
         const PortableTimeBoardRow *rows, unsigned count, uint8_t engine_class, const uint8_t row_x[8],
         PortableSoftwareFrame *frame),
        (asset, results, rows, count, engine_class, row_x, target))
CENTRED(portable_battle_board_render,
        (const PortableGpStandingsAssetView *asset, const PortableGpResultsAssetView *results_asset,
         const PortableBattleResults *results, uint8_t engine_class, PortableSoftwareFrame *frame),
        (asset, results_asset, results, engine_class, target))
CENTRED(portable_gp_standings_render_over,
        (const PortableGpStandingsAssetView *asset, const PortableGpStandingsSession *session,
         PortableSoftwareFrame *frame),
        (asset, session, target))
CENTRED(portable_gp_standings_render,
        (const PortableGpStandingsAssetView *asset, const PortableGpStandingsSession *session,
         PortableSoftwareFrame *frame),
        (asset, session, target))
CENTRED(portable_gp_results_render_action_window_open,
        (const PortableGpResultsAssetView *asset, uint8_t progress, uint8_t replay_disabled,
         PortableSoftwareFrame *frame),
        (asset, progress, replay_disabled, target))
CENTRED(portable_gp_results_render_action_window,
        (const PortableGpResultsAssetView *asset, uint8_t selection, uint8_t replay_disabled,
         uint32_t frame_counter, PortableSoftwareFrame *frame),
        (asset, selection, replay_disabled, frame_counter, target))
CENTRED(portable_hud_render_gp_finish_portraits,
        (const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud, const PortableGpFinishPortraits *state,
         const PortableNativeImage *const own_portraits[8], uint32_t clock, PortableSoftwareFrame *frame),
        (gp, hud, state, own_portraits, clock, target))
CENTRED(portable_hud_render_quick_finish_portraits,
        (const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud, const PortableGpFinishPortraits *state,
         const PortableNativeImage *const own_portraits[8], PortableSoftwareFrame *frame),
        (gp, hud, state, own_portraits, target))

int __wrap_portable_gp_results_render_coins(const PortableGpResultsAssetView *asset, uint8_t coins,
                                            PortableSoftwareFrame *frame)
{
    PortableSoftwareFrame layer;
    if (!layer_for(frame, &layer)) return __real_portable_gp_results_render_coins(asset, coins, frame);
    if (!__real_portable_gp_results_render_coins(asset, coins, &layer)) return 0;
    place_layer(PLACE_TOP);
    return 1;
}
/* Pictures the result screens add (the retry / quit window, the quick-run
 * menu): centred with the boards while the scene is shown without its HUD. */
#define CENTRED_IMAGE(name, params, args)                                   \
    PortableFrontendPresentationStatus __real_##name params;                \
    PortableFrontendPresentationStatus __wrap_##name params                 \
    {                                                                       \
        PortableSoftwareFrame layer, *target = frame;                       \
        PortableFrontendPresentationStatus status;                          \
        if (!overlays_centred || !layer_for(frame, &layer)) return __real_##name args; \
        target = &layer;                                                    \
        status = __real_##name args;                                        \
        place_layer(PLACE_CENTRED);                                         \
        return status;                                                      \
    }

CENTRED_IMAGE(portable_frontend_image_draw,
              (const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame, int x, int y,
               int left, int top, int right, int bottom),
              (pack, id, target, x, y, left, top, right, bottom))
CENTRED_IMAGE(portable_frontend_image_draw_palette,
              (const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame, int x, int y,
               int left, int top, int right, int bottom, const uint16_t *palette, size_t palette_count,
               const uint8_t *palette_mask),
              (pack, id, target, x, y, left, top, right, bottom, palette, palette_count, palette_mask))