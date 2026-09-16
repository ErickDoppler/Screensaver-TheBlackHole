/* Entry point. Decodes the screensaver command line and dispatches.
 *
 * Windows launches a .scr with one of:
 *   /s            run the screensaver (fullscreen)
 *   /p <hwnd>     draw a live preview inside the given window
 *   /c[:<hwnd>]   show the settings dialog          (also: no arguments)
 *   /a <hwnd>     change password (legacy, ignored)
 *
 * XScreenSaver (Linux) runs a hack with -root and puts the window to draw
 * into in $XSCREENSAVER_WINDOW (older versions: -window-id <id>).
 *
 * Developer extras (any platform):
 *   /w | --window [WxH]     run in a resizable window
 *   --dump <file.png> --frames <n>   render n frames, save the last one, exit
 *   --scene <name>          start on one scene (void, fed, feeding, ...)
 *   --cover <fraction>      force the shadow's share of the screen height
 *   --theta <radians>       force how far out of the disk plane the camera sits
 *   --disk-outer <M>        force the disk's outer radius
 *   --lens <degrees>        force the vertical field of view
 *   --seed <n>              fix the run's random seed
 *   --settled               skip the arrival warp
 *   --log <file>            append diagnostics to a file
 *   --<setting> <value>     override any setting (see settings.h)
 */
#include "app.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

static void lower_ascii(char *s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

static void *parse_hwnd(const char *text) {
    if (!text) return NULL;
    unsigned long long v = strtoull(text, NULL, 10);
    if (v == 0) v = strtoull(text, NULL, 16);
    return (void *)(uintptr_t)v;
}

static int scene_by_name(const char *name) {
    for (int i = 0; i < SCENE_COUNT; ++i)
        if (!strcmp(name, settings_scene_name(i))) return i;
    return -1;
}

int main(int argc, char **argv) {
    AppConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.mode = MODE_FULLSCREEN;
    cfg.win_w = 1280;
    cfg.win_h = 720;
    cfg.start_scene = -1;
    settings_load(&cfg.settings);

    int mode_given = 0, want_config = 0, want_root = 0;
    unsigned long embed_window = 0;   /* XScreenSaver hands us its window */
    void *config_parent = NULL;

    for (int i = 1; i < argc; ++i) {
        char opt[64];
        const char *raw = argv[i];
        if (raw[0] != '/' && raw[0] != '-') continue;
        snprintf(opt, sizeof opt, "%s", raw + (raw[0] == '/' ? 1 : (raw[1] == '-' ? 2 : 1)));
        lower_ascii(opt);

        /* Screensaver switches (single letter, optional ":hwnd") */
        if (opt[0] && (opt[1] == 0 || opt[1] == ':') && strchr("scpaw", opt[0])) {
            const char *inline_val = opt[1] == ':' ? opt + 2 : NULL;
            switch (opt[0]) {
            case 's': cfg.mode = MODE_FULLSCREEN; mode_given = 1; break;
            case 'w': cfg.mode = MODE_WINDOW; mode_given = 1; break;
            case 'p':
                cfg.mode = MODE_PREVIEW; mode_given = 1;
                cfg.parent_hwnd = parse_hwnd(inline_val ? inline_val : (i + 1 < argc ? argv[++i] : NULL));
                break;
            case 'c':
                want_config = 1; mode_given = 1;
                config_parent = parse_hwnd(inline_val ? inline_val :
                    (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0]) ? argv[++i] : NULL));
                break;
            case 'a':
                if (!inline_val && i + 1 < argc) ++i;
                return 0;
            }
            continue;
        }
        /* XScreenSaver conventions (Linux) */
        if (!strcmp(opt, "root")) { want_root = 1; mode_given = 1; continue; }
        if (!strcmp(opt, "window-id") && i + 1 < argc) {
            embed_window = strtoul(argv[++i], NULL, 0);
            mode_given = 1;
            continue;
        }
        if (!strcmp(opt, "window")) {
            cfg.mode = MODE_WINDOW; mode_given = 1;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) {
                int w = 0, h = 0;
                if (sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { cfg.win_w = w; cfg.win_h = h; }
            }
            continue;
        }
        if (!strcmp(opt, "dump") && i + 1 < argc)   { cfg.dump_path = argv[++i]; continue; }
        if (!strcmp(opt, "frames") && i + 1 < argc) { cfg.frame_limit = atoi(argv[++i]); continue; }
        if (!strcmp(opt, "log") && i + 1 < argc)    { plat_log_set_file(argv[++i]); continue; }
        if (!strcmp(opt, "cover") && i + 1 < argc)  { cfg.start_cover = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "theta") && i + 1 < argc)  { cfg.start_theta = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "disk-outer") && i + 1 < argc) { cfg.start_outer = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "lens") && i + 1 < argc)   { cfg.start_fov = (float)atof(argv[++i]); continue; }
        if (!strcmp(opt, "seed") && i + 1 < argc)   { cfg.seed = (unsigned)strtoul(argv[++i], NULL, 0); continue; }
        if (!strcmp(opt, "settled")) { cfg.no_warp_in = 1; continue; }
        if (!strcmp(opt, "trace")) { cfg.trace = 1; continue; }
        if (!strcmp(opt, "scene") && i + 1 < argc) {
            int sc = scene_by_name(argv[++i]);
            if (sc < 0) plat_log("unknown scene: %s", argv[i]);
            else cfg.start_scene = sc;
            continue;
        }
        if (settings_parse_arg(&cfg.settings, argc, argv, &i)) continue;
        plat_log("unknown argument: %s", raw);
    }
    if (cfg.dump_path && cfg.frame_limit <= 0) cfg.frame_limit = 60;
    /* A scene asked for on the command line is meant to be seen, whatever the
     * scene mask says. */
    if (cfg.start_scene >= 0) settings_set_scene(&cfg.settings, cfg.start_scene, 1);

#ifdef _WIN32
    if (want_config || !mode_given) {
        if (!plat_win32_config_dialog(config_parent)) return 0;
        settings_load(&cfg.settings);
        cfg.mode = MODE_WINDOW;
    }
    if (cfg.mode == MODE_PREVIEW && !cfg.parent_hwnd) return 1;
    (void)want_root; (void)embed_window;
#else
    (void)config_parent;
    if (want_config) {
        printf("The Black Hole has no settings window of its own on Linux.\n"
               "Pick it in xscreensaver-settings, or pass --<setting> <value>.\n");
        return 0;
    }
    /* XScreenSaver hands over its window in the environment (and older
     * versions also as -window-id). Started with -root but no window to draw
     * into, it covers the screen with a window of its own. */
    if (!embed_window) {
        const char *env = getenv("XSCREENSAVER_WINDOW");
        if (env && *env) embed_window = strtoul(env, NULL, 0);
    }
    if (embed_window) {
        cfg.mode = MODE_EMBED;
        cfg.embed_window = embed_window;
    } else if (want_root) {
        cfg.mode = MODE_FULLSCREEN;
    } else if (!mode_given) {
        cfg.mode = MODE_WINDOW;
    }
#endif
    return app_run(&cfg);
}
