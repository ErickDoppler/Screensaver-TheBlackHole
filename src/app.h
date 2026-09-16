/* Application shell: window creation, input rules, frame pacing. */
#ifndef BH_APP_H
#define BH_APP_H
#include "settings.h"

typedef enum RunMode {
    MODE_FULLSCREEN,   /* real screensaver run (/s): covers all monitors */
    MODE_PREVIEW,      /* inside the Windows screensaver dialog (/p hwnd) */
    MODE_WINDOW,       /* developer window (/w) */
    MODE_EMBED         /* inside a window another program owns: XScreenSaver */
} RunMode;

typedef struct AppConfig {
    RunMode     mode;
    void       *parent_hwnd;    /* MODE_PREVIEW only */
    unsigned long embed_window; /* MODE_EMBED only: the X11 window to draw into */
    Settings    settings;
    int         win_w, win_h;   /* MODE_WINDOW size */
    const char *dump_path;      /* write a PNG of the frame after frame_limit */
    int         frame_limit;    /* 0 = run until exit */
    int         start_scene;    /* -1 = deal one from the deck */
    float       start_cover;    /* >0 = force the shadow's share of the screen */
    float       start_theta;    /* >0 = force radians out of the disk plane */
    float       start_outer;    /* >0 = force the disk's outer radius, in M */
    float       start_fov;      /* >0 = force the vertical field of view, degrees */
    unsigned    seed;           /* 0 = time-seeded */
    int         no_warp_in;     /* start settled instead of arriving */
    int         trace;          /* log the camera and the clocks each half second */
} AppConfig;

/* Returns a process exit code. */
int app_run(const AppConfig *cfg);
#endif
