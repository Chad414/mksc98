/* Replaces upstream's platform/load_job.c and loading_screen.c. Upstream
 * runs content loading on a worker thread behind an animated loading screen;
 * this port runs the work inline on the calling thread (no extra window, no
 * thread with an 8 MiB stack) and reports the phases in the log. */
#include "loading_screen.h"
#include "port.h"

int portable_load_start(PortableLoadJob *job, PortableLoadWork work, void *user)
{
    if (job == NULL || work == NULL) return 0;
    job->work = work;
    job->user = user;
    SDL_SetAtomicInt(&job->phase, 0);
    SDL_SetAtomicInt(&job->state, PORTABLE_LOAD_RUNNING);
    SDL_SetAtomicInt(&job->state, work(user, job) ? PORTABLE_LOAD_READY : PORTABLE_LOAD_FAILED);
    return 1;
}
PortableLoadState portable_load_state(PortableLoadJob *job) { return (PortableLoadState)SDL_GetAtomicInt(&job->state); }
void portable_load_cancel(PortableLoadJob *job) { (void)job; }
int portable_load_cancelled(PortableLoadJob *job) { (void)job; return 0; }
void portable_load_phase(PortableLoadJob *job, int phase) { SDL_SetAtomicInt(&job->phase, phase); }
PortableLoadState portable_load_finish(PortableLoadJob *job)
{
    const PortableLoadState state = portable_load_state(job);
    if (state < PORTABLE_LOAD_READY) return PORTABLE_LOAD_IDLE;
    job->work = NULL;
    job->user = NULL;
    SDL_SetAtomicInt(&job->state, PORTABLE_LOAD_IDLE);
    return state;
}

int portable_loading_begin(int hidden) { (void)hidden; return 1; }
void portable_loading_close(void) {}
int portable_loading_take(SDL_Window **window, SDL_Renderer **renderer) { (void)window; (void)renderer; return 0; }
int portable_loading_run_detail(SDL_Window *window, SDL_Renderer *renderer, const char *title,
    const char *detail, const char *const *phases, unsigned phase_count, PortableLoadWork work, void *user)
{
    PortableLoadJob job;
    (void)window; (void)renderer; (void)detail; (void)phases; (void)phase_count;
    memset(&job, 0, sizeof job);
    port_log("loading: %s", title != NULL ? title : "");
    if (!portable_load_start(&job, work, user)) return 0;
    return portable_load_finish(&job) == PORTABLE_LOAD_READY;
}
int portable_loading_run(SDL_Window *window, SDL_Renderer *renderer, const char *title,
    const char *const *phases, unsigned phase_count, PortableLoadWork work, void *user)
{
    return portable_loading_run_detail(window, renderer, title, NULL, phases, phase_count, work, user);
}
