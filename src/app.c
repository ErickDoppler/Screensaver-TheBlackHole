#include "app.h"
#include "platform.h"
#include "gl_loader.h"
#include "render.h"
#include "scene.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

typedef struct App {
    const AppConfig *cfg;
    Settings    s;
    SDL_Window *win;
    SDL_GLContext gl;
    Renderer    r;
    Scene       sc;
    int         width, height;
    float       refresh_hz;
    int         use_vsync;
    int         swap_n;
    int         running;
    float       elapsed;
    float       last_input;
    int         took_control;
    float       mouse_travel;
    int         frames;
    /* Adaptive quality: only when the Quality setting is left on auto. */
    int         auto_quality;
    int         quality;
    float       frame_ms;          /* smoothed */
    float       quality_hold;      /* seconds before the next adjustment */
    int         q_w, q_h;          /* the size the search was measured at */
    int         quality_saved;
} App;

/* ------------------------------------------------------------------------ */
static SDL_Window *create_window(App *a) {
    const AppConfig *cfg = a->cfg;
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_SetStringProperty(p, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "The Black Hole");

    if (cfg->mode == MODE_PREVIEW) {
#ifdef _WIN32
        int w = 0, h = 0;
        void *child = plat_win32_create_preview_child(cfg->parent_hwnd, &w, &h);
        if (!child) { SDL_DestroyProperties(p); return NULL; }
        SDL_SetPointerProperty(p, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, child);
#else
        SDL_DestroyProperties(p);
        return NULL;
#endif
    } else if (cfg->mode == MODE_EMBED) {
#ifdef _WIN32
        SDL_DestroyProperties(p);
        return NULL;
#else
        /* XScreenSaver's own window, fullscreen or the little preview in its
         * settings. SDL adopts it rather than creating one; the window's size
         * is whatever the host made it. */
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,
                              (Sint64)cfg->embed_window);
#endif
    } else if (cfg->mode == MODE_FULLSCREEN) {
        int x = 0, y = 0, w = 1280, h = 720;
#ifdef _WIN32
        plat_win32_virtual_screen(&x, &y, &w, &h);
#else
        SDL_Rect b;
        if (SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &b)) { x = b.x; y = b.y; w = b.w; h = b.h; }
#endif
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, cfg->win_w);
        SDL_SetNumberProperty(p, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, cfg->win_h);
        SDL_SetBooleanProperty(p, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    }
    SDL_Window *w = SDL_CreateWindowWithProperties(p);
    SDL_DestroyProperties(p);
    return w;
}

static int init_gl(App *a) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    a->win = create_window(a);
    if (!a->win) { plat_log("window: %s", SDL_GetError()); return 0; }
    a->gl = SDL_GL_CreateContext(a->win);
    if (!a->gl) { plat_log("GL context: %s", SDL_GetError()); return 0; }
    SDL_GL_MakeCurrent(a->win, a->gl);
    const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(a->win));
    a->refresh_hz = (dm && dm->refresh_rate > 0.f) ? dm->refresh_rate : 60.f;
    float ratio = a->refresh_hz / (float)a->s.target_fps;
    int n = (int)floorf(ratio + 0.5f);
    a->use_vsync = n >= 1 && n <= 4 && fabsf(ratio - (float)n) < 0.05f;
    a->swap_n = n;
    if (a->use_vsync && !SDL_GL_SetSwapInterval(1)) a->use_vsync = 0;
    if (!a->use_vsync) SDL_GL_SetSwapInterval(0);
    const char *missing = gl_load_functions();
    if (missing) { plat_log("OpenGL 3.3 function missing: %s", missing); return 0; }
    SDL_GetWindowSizeInPixels(a->win, &a->width, &a->height);
    return 1;
}

/* ------------------------------------------------------------------------ */
static void mark_input_from(App *a, const char *why) {
    if (!a->took_control && a->cfg->trace)
        plat_log("controls taken at t=%.2f by: %s", a->elapsed, why);
    a->took_control = 1;
    a->last_input = a->elapsed;
}
#define mark_input(a) mark_input_from((a), __func__)

/* While the user is driving, the screensaver keeps its hands off: the idle
 * rotation stops and the view stays where they put it. Both come back after
 * IDLE_RESUME seconds of nothing. */
static int manual_control(const App *a) {
    return a->took_control && a->elapsed - a->last_input < IDLE_RESUME;
}

static void request_exit(App *a) { a->running = 0; }

/* Drawing into someone else's window: the host owns the input and decides
 * when we stop (XScreenSaver sends SIGTERM, which SDL turns into a quit). */
static int passive(const App *a) {
    return a->cfg->mode == MODE_PREVIEW || a->cfg->mode == MODE_EMBED;
}

static void reset_view(App *a) {
    /* Back to facing the hole; Yaw and Tumble carry on turning from there. */
    a->sc.yaw = a->sc.pitch = 0.f;
    a->sc.auto_yaw = a->sc.auto_pitch = a->sc.auto_roll = 0.f;
    a->took_control = 0;
}

static void handle_key(App *a, const SDL_KeyboardEvent *k) {
    if (k->repeat) return;
    if (k->key == SDLK_ESCAPE) { request_exit(a); return; }

    /* The hidden picker and the fall-in summon are chords, so they survive
     * "exit on any button" - they are not something a passer-by will hit. */
    int ctrl_alt = (k->mod & SDL_KMOD_CTRL) && (k->mod & SDL_KMOD_ALT);
    if (ctrl_alt && k->key == SDLK_S) {
        int next = (a->sc.kind + 1) % SCENE_COUNT;
        for (int i = 0; i < SCENE_COUNT && !settings_scene_enabled(&a->s, next); ++i)
            next = (next + 1) % SCENE_COUNT;
        scene_go(&a->sc, &a->s, next);
        plat_log("picker: %s", settings_scene_name(next));
        return;
    }

    if (a->cfg->mode == MODE_FULLSCREEN && a->s.exit_on_any_key) { request_exit(a); return; }
    switch (k->key) {
    case SDLK_RETURN: case SDLK_KP_ENTER:
        scene_warp(&a->sc, &a->s);
        break;
    case SDLK_HOME: case SDLK_R: reset_view(a); break;
    default: break;
    }
}

static void handle_mouse_motion(App *a, const SDL_MouseMotionEvent *m) {
    /* Relative mouse mode delivers one large delta the moment it is turned on.
     * Acting on it swung the view on its own and, worse, counted as the user
     * taking the controls - which stops the idle rotation for twenty seconds
     * before anyone has touched anything. */
    if (a->elapsed < 0.5f) return;
    if (a->s.mouse_rotation) {
        a->sc.yaw   = wrapf(a->sc.yaw + m->xrel * 0.0022f, -BH_PI, BH_PI);
        a->sc.pitch = clampf(a->sc.pitch - m->yrel * 0.0022f, -1.35f, 1.35f);
        /* A deliberate look, not a twitch or a sensor drift: the threshold is
         * on accumulated travel, so a resting mouse never pins the camera. */
        a->mouse_travel += fabsf(m->xrel) + fabsf(m->yrel);
        if (a->mouse_travel > 40.f) { a->mouse_travel = 0.f; mark_input(a); }
        return;
    }
    if (a->cfg->mode != MODE_FULLSCREEN || !a->s.exit_on_mouse_move) return;
    a->mouse_travel += fabsf(m->xrel) + fabsf(m->yrel);
    float threshold = lerpf(200.f, 4.f, a->s.mouse_sensitivity / 100.f);
    if (a->mouse_travel > threshold) request_exit(a);
}

static void handle_mouse_button(App *a) {
    if (a->cfg->mode != MODE_FULLSCREEN) { if (a->s.click_resets_view) reset_view(a); return; }
    if (a->s.mouse_rotation) { request_exit(a); return; }   /* no other way out with the mouse */
    if (a->s.click_resets_view) reset_view(a);
    else if (a->s.exit_on_mouse_move) request_exit(a);
}

static void poll_events(App *a) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: request_exit(a); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            SDL_GetWindowSizeInPixels(a->win, &a->width, &a->height);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!passive(a)) {
                if (!e.key.repeat && e.key.key != SDLK_ESCAPE) mark_input(a);
                handle_key(a, &e.key);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (!passive(a)) handle_mouse_motion(a, &e.motion);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (!passive(a)) handle_mouse_button(a);
            break;
        default: break;
        }
    }
}

/* Held keys fly the camera; J/L and I/K turn it. The two are separate on
 * purpose: the arrows and WASD are for going somewhere, and they had better
 * feel like it, so they drive a real velocity that the dust streams past and
 * the disk shears under.
 *
 * Where they do not take you is any closer to the hole. It is hundreds of
 * light years off; nobody crosses that by holding a key, and swelling the
 * shadow to pretend otherwise would be the one dishonest thing in the picture.
 * So the motion is real and the destination never arrives - which is exactly
 * what being out there is like. */
static void update_input(App *a, float dt) {
    a->sc.move_in = v3(0, 0, 0);
    if (passive(a)) return;
    if (a->cfg->mode == MODE_FULLSCREEN && a->s.exit_on_any_key) return;
    const bool *k = SDL_GetKeyboardState(NULL);

    /* --- flying --------------------------------------------------------- */
    float thrust = 1.f;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) thrust = 3.f;
    if (k[SDL_SCANCODE_LCTRL]  || k[SDL_SCANCODE_RCTRL])  thrust = 0.35f;
    vec3 m = v3(0, 0, 0);
    if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) m.x -= 1.f;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) m.x += 1.f;
    if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) m.z += 1.f;
    if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) m.z -= 1.f;
    if (k[SDL_SCANCODE_PAGEUP]   || k[SDL_SCANCODE_SPACE]) m.y += 1.f;
    if (k[SDL_SCANCODE_PAGEDOWN] || k[SDL_SCANCODE_Z])     m.y -= 1.f;
    float len = sqrtf(v3_dot(m, m));
    if (len > 0.f) {
        a->sc.move_in = v3_scale(m, thrust / len);
        mark_input(a);
    }

    /* --- turning -------------------------------------------------------- */
    float rot = 1.1f * dt * (thrust > 1.f ? 2.2f : 1.f);
    float dy = 0.f, dp = 0.f;
    if (k[SDL_SCANCODE_J]) dy -= rot;
    if (k[SDL_SCANCODE_L]) dy += rot;
    if (k[SDL_SCANCODE_I]) dp += rot;
    if (k[SDL_SCANCODE_K]) dp -= rot;
    if (dy == 0.f && dp == 0.f) return;
    mark_input(a);
    a->sc.yaw   = wrapf(a->sc.yaw + dy, -BH_PI, BH_PI);
    a->sc.pitch = clampf(a->sc.pitch + dp, -1.35f, 1.35f);
}

/* Nudge the quality until the frame budget is met, then leave it alone and
 * remember where it settled. This is the "benchmark on first run" the design
 * asks for, done continuously instead of once, so it also copes with three
 * 4K monitors and with a laptop that drops to its integrated GPU. */
/* The measured value is stored under its own key, never under "quality".
 * Writing it back to the user's own setting turns auto off for good - the
 * setting is only on auto while it reads 0 - and pins every later run to a
 * number measured at whatever window size happened to be up at the time. A
 * value settled in a 1280x720 window is four times too ambitious for the same
 * machine running fullscreen, and the result is a couple of frames a second,
 * which does not look like low quality. It looks like the thing has hung. */
#define QUALITY_AUTO_KEY "quality-auto"

static void adapt_quality(App *a, float dt, float budget_ms) {
    if (!a->auto_quality) return;
    /* The budget is per-pixel work, so a resize invalidates the search. */
    if (a->width != a->q_w || a->height != a->q_h) {
        a->q_w = a->width;
        a->q_h = a->height;
        a->quality_saved = 0;
        a->quality_hold = 1.0f;
        return;
    }
    a->frame_ms = approachf(a->frame_ms, a->r.last_frame_ms, 0.6f, dt);
    a->quality_hold -= dt;
    if (a->quality_hold > 0.f || a->elapsed < 1.5f) return;
    int before = a->quality;
    if (a->frame_ms > budget_ms * 1.10f)      a->quality -= 10;
    else if (a->frame_ms < budget_ms * 0.55f) a->quality += 5;
    a->quality = (int)clampf((float)a->quality, 10.f, 100.f);
    a->quality_hold = 2.f;
    if (a->quality == before) {
        /* Settled. Store it so the next run starts here instead of hunting. */
        if (!a->quality_saved) {
            plat_store_write_int(QUALITY_AUTO_KEY, a->quality);
            a->quality_saved = 1;
            plat_log("quality settled at %d for %dx%d (%.1f ms/frame, budget %.1f)",
                     a->quality, a->width, a->height, a->frame_ms, budget_ms);
        }
        return;
    }
    a->quality_saved = 0;
    a->r.q = render_quality(a->quality);
    render_resize(&a->r, a->width, a->height);
    plat_log("quality -> %d (%.1f ms/frame, budget %.1f)", a->quality, a->frame_ms, budget_ms);
}

/* ------------------------------------------------------------------------ */
int app_run(const AppConfig *cfg) {
    App a;
    memset(&a, 0, sizeof a);
    a.cfg = cfg;
    a.s = cfg->settings;
    settings_clamp(&a.s);
    if (cfg->mode == MODE_PREVIEW) {
        /* The preview is a few hundred pixels wide: keep it light. */
        if (a.s.target_fps > 20) a.s.target_fps = 20;
        if (a.s.star_count > 2500) a.s.star_count = 2500;
        a.s.quality = 25;
        a.s.ghost_tail = 0;
        a.s.damage = 0;
    }

    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (cfg->mode == MODE_EMBED) {
        /* The window id is an X11 one, even inside a Wayland session, and the
         * input on it belongs to XScreenSaver. */
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
        SDL_SetHint(SDL_HINT_VIDEO_X11_EXTERNAL_WINDOW_INPUT, "0");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        plat_log("SDL_Init: %s", SDL_GetError());
        return 2;
    }
    int code = 0;
    if (!init_gl(&a)) { code = 3; goto done; }
    if (cfg->mode == MODE_EMBED && a.width < 640) {
        /* The preview pane in xscreensaver-settings: same diet as the
         * Windows one, decided once the host's window size is known. */
        if (a.s.star_count > 2500) a.s.star_count = 2500;
        a.s.quality = 25;
        a.s.ghost_tail = 0;
        a.s.damage = 0;
    }
    if (!render_init(&a.r, &a.s)) { code = 4; goto done; }
    render_resize(&a.r, a.width, a.height);

    a.auto_quality = a.s.quality == 0 && cfg->mode != MODE_PREVIEW;
    a.quality = a.s.quality > 0 ? a.s.quality : 55;
    if (a.auto_quality) {
        /* Last run's measurement is a starting point, not a verdict: the
         * search keeps running, so a different window size or a different GPU
         * is found again within a few seconds. */
        int stored = 0;
        if (plat_store_read_int(QUALITY_AUTO_KEY, &stored) && stored >= 10 && stored <= 100)
            a.quality = stored;
        a.r.q = render_quality(a.quality);
        render_resize(&a.r, a.width, a.height);
    }
    a.q_w = a.width;
    a.q_h = a.height;
    a.frame_ms = 1000.f / (float)a.s.target_fps;

    unsigned seed = cfg->seed ? cfg->seed : (unsigned)SDL_GetPerformanceCounter();
    scene_init(&a.sc, &a.s, seed);
    if (cfg->start_scene >= 0) scene_go(&a.sc, &a.s, cfg->start_scene);
    if (cfg->start_cover > 0.f) {
        /* Re-frame, lens and all: a coverage forced from the command line
         * with the dealt scene's focal length would test neither. */
        Framing f = cfg->start_cover < 0.35f ? FRAME_FAR
                  : cfg->start_cover > 1.5f ? FRAME_ENGULFED : FRAME_HERO;
        scene_frame(&a.sc, &a.s, f, cfg->start_cover);
        plat_log("forced framing %s: r=%.1f M, fov=%.0f deg", scene_framing_name(f),
                 a.sc.cam_r, a.sc.fov_base * 180.f / BH_PI);
    }
    if (cfg->start_fov > 0.f) {
        a.sc.fov_base = clampf(cfg->start_fov, 8.f, 150.f) * BH_PI / 180.f;
        a.sc.fov = a.sc.fov_base;
        a.sc.r_base = scene_radius_for_coverage(a.sc.coverage, a.sc.fov_base);
        a.sc.cam_r = a.sc.r_base;
    }
    if (cfg->start_theta > 0.f) a.sc.cam_theta = a.sc.theta_base = BH_PI * 0.5f + cfg->start_theta;
    if (cfg->start_outer > 0.f) a.sc.disk_outer = cfg->start_outer;
    if (cfg->start_cover > 0.f || cfg->start_theta > 0.f || cfg->start_outer > 0.f ||
        cfg->start_fov > 0.f)
        plat_log("framing: r=%.1f M, fov=%.0f deg, off-plane=%.2f rad, disk %.1f-%.1f M",
                 a.sc.cam_r, a.sc.fov_base * 180.f / BH_PI,
                 a.sc.cam_theta - BH_PI * 0.5f, a.sc.disk_inner, a.sc.disk_outer);
    if (cfg->no_warp_in) { a.sc.warp = WARP_SETTLED; a.sc.boost = 0.f; a.sc.warp_t = 0.f; }

    if (cfg->mode == MODE_FULLSCREEN) {
        SDL_HideCursor();
        if (a.s.mouse_rotation) SDL_SetWindowRelativeMouseMode(a.win, true);
        SDL_RaiseWindow(a.win);
    }

    plat_log("start: mode=%d %dx%d fps=%d (display %.0f Hz, %s) scene=%s quality=%d%s",
             cfg->mode, a.width, a.height, a.s.target_fps, a.refresh_hz,
             a.use_vsync ? "vsync" : "paced", settings_scene_name(a.sc.kind),
             a.quality, a.auto_quality ? " (auto)" : "");

    const Uint64 frame_ns = 1000000000ull / (Uint64)a.s.target_fps;
    const float budget_ms = 1000.f / (float)a.s.target_fps;
    Uint64 last = SDL_GetTicksNS();
    Uint64 next_deadline = last + frame_ns;
    a.running = 1;
    while (a.running) {
        Uint64 now = SDL_GetTicksNS();
        float dt = (float)(now - last) / 1e9f;
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        a.elapsed += dt;

        poll_events(&a);
#ifdef _WIN32
        if (cfg->mode == MODE_PREVIEW && !plat_win32_window_alive(cfg->parent_hwnd)) break;
#endif
        if (!a.running) break;

        /* The look-threshold budget leaks away, so sensor drift and the odd
         * stray pixel never add up to "the user has taken over". Only a
         * deliberate movement outruns the decay. */
        a.mouse_travel *= expf(-dt / 0.6f);
        update_input(&a, dt);
        scene_update(&a.sc, &a.s, dt, manual_control(&a));

        render_resize(&a.r, a.width, a.height);
        render_frame(&a.r, &a.s, &a.sc, a.elapsed, dt);
        adapt_quality(&a, dt, budget_ms);
        a.frames++;
        if (cfg->trace && a.frames % 30 == 0)
            plat_log("t=%5.1f  phi=%6.3f  theta=%5.2f  r=%6.2f  fov=%5.1f  disk_t=%8.2f  "
                     "speed=%4.2f  manual=%d  q=%d  dt=%.4f",
                     a.elapsed, a.sc.cam_phi, a.sc.cam_theta, a.sc.cam_r,
                     a.sc.fov * 180.f / BH_PI, a.sc.disk_time, a.sc.speed,
                     manual_control(&a), a.quality, dt);

        if (cfg->dump_path && cfg->frame_limit > 0 && a.frames >= cfg->frame_limit) {
            if (render_dump_png(&a.r, cfg->dump_path)) plat_log("wrote %s", cfg->dump_path);
            else { plat_log("failed to write %s", cfg->dump_path); code = 5; }
        }
        SDL_GL_SwapWindow(a.win);
        if (cfg->frame_limit > 0 && a.frames >= cfg->frame_limit) break;

        if (a.use_vsync && a.swap_n == 1) continue;
        now = SDL_GetTicksNS();
        if (next_deadline > now + 200000ull) SDL_DelayPrecise(next_deadline - now);
        now = SDL_GetTicksNS();
        next_deadline += frame_ns;
        if (next_deadline < now) next_deadline = now + frame_ns;
    }
    plat_log("stop: %d frames in %.2f s (%.1f fps avg), quality=%d, %.1f ms/frame",
             a.frames, a.elapsed, a.elapsed > 0.f ? a.frames / a.elapsed : 0.f,
             a.quality, a.frame_ms);

done:
    render_shutdown(&a.r);
    if (a.gl) SDL_GL_DestroyContext(a.gl);
    if (a.win) SDL_DestroyWindow(a.win);
    SDL_Quit();
    return code;
}
