/* User settings. One X-macro table drives defaults, clamping, persistence
 * (registry on Windows) and the command-line parser, so a setting is added in
 * exactly one place. Sliders are stored as 0..100 integers unless noted. */
#ifndef BH_SETTINGS_H
#define BH_SETTINGS_H

/* How the camera moves once it has been idle long enough. */
enum { ROT_ORBIT = 0, ROT_YAW = 1, ROT_TUMBLE = 2 };
/* Broken-matrix colour palettes. CUSTOM takes damage_color. */
enum { DMG_WHITE = 0, DMG_GREEN_PURPLE = 1, DMG_BLACK = 2, DMG_CUSTOM = 3 };
/* Accretion disk colouring: the movie's warm even glow, or the real thing
 * with the approaching side beamed blue-white and the receding side dim. */
enum { DOPPLER_INTERSTELLAR = 0, DOPPLER_TRUE = 1 };

/* The scenes, one bit each in scene_mask. */
enum {
    SCENE_VOID = 0,        /* clean vacuum: shadow, photon ring, bent stars */
    SCENE_FED,             /* feeding ended: settled accretion disk */
    SCENE_FEEDING,         /* feeding: a star torn into a long stream */
    SCENE_EVAPORATING,     /* near the end: point horizon, view-filling lensing */
    SCENE_STARDUST,        /* void plus flashes on real orbital paths */
    SCENE_BINARY_VOID,     /* two holes, one shared gravitational figure */
    SCENE_BINARY_FED,      /* two holes inside a circumbinary disk */
    SCENE_NEBULA,          /* crossing a nebula; it lenses into arcs */
    SCENE_CORE,            /* galactic core: supermassive, dense field */
    SCENE_AFTERMATH,       /* post-merger hole, disk still settling */
    SCENE_COUNT
};
#define SCENE_ALL ((1 << SCENE_COUNT) - 1)

/*  field               key                   default min  max */
#define BH_SETTINGS_INT(X) \
    /* motion and camera */ \
    X(side_movement,      "side-movement",      1,      0,   1)    \
    X(movement_speed,     "movement-speed",     30,     0,   100)  \
    X(mouse_rotation,     "mouse-rotation",     1,      0,   1)    \
    X(exit_on_mouse_move, "exit-on-mouse-move", 1,      0,   1)    \
    X(mouse_sensitivity,  "mouse-sensitivity",  50,     0,   100)  \
    X(click_resets_view,  "click-resets-view",  0,      0,   1)    \
    X(exit_on_any_key,    "exit-on-any-key",    0,      0,   1)    \
    X(rotate_360,         "rotate-360",         1,      0,   1)    \
    X(rotate_mode,        "rotate-mode",        0,      0,   2)    \
    X(rotate_seconds,     "rotate-seconds",     120,    3,   999)  \
    X(warp_minutes,       "warp-minutes",       15,     0,   120)  \
    /* The lens. A short focal length puts the viewer inside the thing; a long
     * one makes a distant hole loom. Each scene frames itself around this. */ \
    X(fov_deg,            "fov",                55,     20,  110)  \
    /* particles */ \
    X(star_count,         "star-count",         5500,   1000, 20000) \
    X(particle_size,      "particle-size",      2,      1,   50)   \
    X(ghost_tail,         "ghost-tail",         12,     0,   100)  \
    X(blur,               "blur",               10,     0,   100)  \
    X(twinkle,            "twinkle",            15,     0,   100)  \
    X(natural_star_color, "natural-star-color", 0,      0,   1)    \
    /* How far the disk's light floods out past its own outline. Light that
     * bright does not stay inside its edges - it swamps the lens and the
     * sensor, and without it a furnace reads as a picture of a furnace. */ \
    X(bloom,              "bloom",              42,     0,   100)  \
    /* physics; spin and lensing are thousandths / percent */ \
    X(spin,               "spin",               700,    0,   999)  \
    X(lensing,            "lensing",            100,    50,  300)  \
    X(doppler,            "doppler",            0,      0,   1)    \
    X(redshift,           "redshift",           1,      0,   1)    \
    X(higher_order,       "higher-order",       1,      0,   1)    \
    X(time_dilation,      "time-dilation",      50,     0,   100)  \
    /* events */ \
    X(jets,               "jets",               1,      0,   1)    \
    X(event_merger,       "event-merger",       1,      0,   1)    \
    X(event_fallin,       "event-fallin",       1,      0,   1)    \
    X(event_flare,        "event-flare",        1,      0,   1)    \
    X(scene_mask,         "scene-mask",         SCENE_ALL, 0, SCENE_ALL) \
    /* camera damage */ \
    X(damage,             "damage",             1,      0,   1)    \
    X(damage_grace,       "damage-grace",       2,      0,   30)   \
    X(damage_heal,        "damage-heal",        45,     5,   300)  \
    X(damage_glass,       "damage-glass",       1,      0,   1)    \
    X(damage_matrix,      "damage-matrix",      1,      0,   1)    \
    X(damage_palette,     "damage-palette",     0,      0,   3)    \
    /* power. quality 0 means "benchmark on first run and store the result" */ \
    X(quality,            "quality",            0,      0,   100)  \
    X(target_fps,         "fps",                60,     10,  120)

/*  field         key            default (0xRRGGBB) */
#define BH_SETTINGS_COLOR(X) \
    X(star_color,   "star-color",   0xBFD4FFu) \
    X(space_color,  "space-color",  0x02030Au) \
    X(disk_color,   "disk-color",   0xFFB14Au) \
    X(ring_color,   "ring-color",   0xFFF0C0u) \
    X(shadow_color, "shadow-color", 0x000000u) \
    X(nebula_color, "nebula-color", 0x6A3FB5u) \
    X(damage_color, "damage-color", 0xFFFFFFu)

typedef struct Settings {
#define X(field, key, def, mn, mx) int field;
    BH_SETTINGS_INT(X)
#undef X
#define X(field, key, def) unsigned field;
    BH_SETTINGS_COLOR(X)
#undef X
} Settings;

void settings_defaults(Settings *s);
void settings_clamp(Settings *s);
void settings_load(Settings *s);        /* defaults, then platform store */
void settings_save(const Settings *s);
/* Tries to consume argv[*i] (and possibly argv[*i+1]) as a setting override
 * such as "--star-count 9000", "--spin=900", "--no-jets",
 * "--rotate-mode tumble", "--disk-color #ff8800". Returns 1 if consumed. */
int settings_parse_arg(Settings *s, int argc, char **argv, int *i);
/* Parses "#RRGGBB", "RRGGBB" or "0xRRGGBB". Returns 1 on success. */
int settings_parse_color(const char *text, unsigned *out);

const char *settings_rotate_mode_name(int m);
const char *settings_scene_name(int scene);
int  settings_scene_enabled(const Settings *s, int scene);
void settings_set_scene(Settings *s, int scene, int on);
/* The matrix-damage palette resolved to a concrete colour. */
unsigned settings_damage_rgb(const Settings *s);
#endif
