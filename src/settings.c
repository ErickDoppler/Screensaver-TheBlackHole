#include "settings.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_defaults(Settings *s) {
#define X(field, key, def, mn, mx) s->field = def;
    BH_SETTINGS_INT(X)
#undef X
#define X(field, key, def) s->field = def;
    BH_SETTINGS_COLOR(X)
#undef X
}

void settings_clamp(Settings *s) {
#define X(field, key, def, mn, mx) s->field = clampi(s->field, mn, mx);
    BH_SETTINGS_INT(X)
#undef X
#define X(field, key, def) s->field &= 0xFFFFFFu;
    BH_SETTINGS_COLOR(X)
#undef X
    /* Every scene switched off would leave nothing to show. */
    if (s->scene_mask == 0) s->scene_mask = SCENE_ALL;
    /* The post-merger hole is only reachable when mergers are allowed. */
    if (!s->event_merger) s->scene_mask &= ~(1 << SCENE_AFTERMATH);
}

void settings_load(Settings *s) {
    int v;
    settings_defaults(s);
#define X(field, key, def, mn, mx) if (plat_store_read_int(key, &v)) s->field = v;
    BH_SETTINGS_INT(X)
#undef X
#define X(field, key, def) if (plat_store_read_int(key, &v)) s->field = (unsigned)v;
    BH_SETTINGS_COLOR(X)
#undef X
    settings_clamp(s);
}

void settings_save(const Settings *s) {
#define X(field, key, def, mn, mx) plat_store_write_int(key, s->field);
    BH_SETTINGS_INT(X)
#undef X
#define X(field, key, def) plat_store_write_int(key, (int)s->field);
    BH_SETTINGS_COLOR(X)
#undef X
}

int settings_parse_color(const char *text, unsigned *out) {
    if (!text) return 0;
    if (text[0] == '#') text++;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (strlen(text) != 6) return 0;
    for (int k = 0; k < 6; ++k) if (!isxdigit((unsigned char)text[k])) return 0;
    *out = (unsigned)strtoul(text, NULL, 16);
    return 1;
}

const char *settings_rotate_mode_name(int m) {
    switch (m) {
    case ROT_YAW:    return "yaw";
    case ROT_TUMBLE: return "tumble";
    default:         return "orbit";
    }
}

static const char *const scene_names[SCENE_COUNT] = {
    "void", "fed", "feeding", "evaporating", "stardust",
    "binary-void", "binary-fed", "nebula", "core", "aftermath"
};

const char *settings_scene_name(int scene) {
    return (scene >= 0 && scene < SCENE_COUNT) ? scene_names[scene] : "?";
}

int settings_scene_enabled(const Settings *s, int scene) {
    if (scene < 0 || scene >= SCENE_COUNT) return 0;
    return (s->scene_mask >> scene) & 1;
}

void settings_set_scene(Settings *s, int scene, int on) {
    if (scene < 0 || scene >= SCENE_COUNT) return;
    if (on) s->scene_mask |= 1 << scene;
    else    s->scene_mask &= ~(1 << scene);
}

unsigned settings_damage_rgb(const Settings *s) {
    switch (s->damage_palette) {
    case DMG_GREEN_PURPLE: return 0x8CFF6Au;   /* the purple half is mixed in per pixel */
    case DMG_BLACK:        return 0x000000u;
    case DMG_CUSTOM:       return s->damage_color;
    default:               return 0xFFFFFFu;
    }
}

/* Named values for the handful of enum-valued settings. */
static int parse_int_value(const char *key, const char *val, int *out) {
    if (!val) return 0;
    if (!strcmp(key, "rotate-mode")) {
        if (!strcmp(val, "orbit"))  { *out = ROT_ORBIT;  return 1; }
        if (!strcmp(val, "yaw"))    { *out = ROT_YAW;    return 1; }
        if (!strcmp(val, "tumble")) { *out = ROT_TUMBLE; return 1; }
    }
    if (!strcmp(key, "doppler")) {
        if (!strcmp(val, "interstellar")) { *out = DOPPLER_INTERSTELLAR; return 1; }
        if (!strcmp(val, "true"))         { *out = DOPPLER_TRUE; return 1; }
    }
    if (!strcmp(key, "damage-palette")) {
        if (!strcmp(val, "white"))  { *out = DMG_WHITE; return 1; }
        if (!strcmp(val, "green"))  { *out = DMG_GREEN_PURPLE; return 1; }
        if (!strcmp(val, "black"))  { *out = DMG_BLACK; return 1; }
        if (!strcmp(val, "custom")) { *out = DMG_CUSTOM; return 1; }
    }
    if (!strcmp(key, "scene-mask")) {
        /* "--scene-mask void,fed" as well as a raw number */
        int named = 0, mask = 0;
        for (int i = 0; i < SCENE_COUNT; ++i) {
            const char *p = strstr(val, scene_names[i]);
            if (!p) continue;
            size_t n = strlen(scene_names[i]);
            /* whole word only, so "void" does not also match "binary-void" */
            int left_ok = p == val || p[-1] == ',';
            int right_ok = p[n] == 0 || p[n] == ',';
            if (left_ok && right_ok) { mask |= 1 << i; named = 1; }
        }
        if (named) { *out = mask; return 1; }
    }
    if (!strcmp(val, "on") || !strcmp(val, "true") || !strcmp(val, "yes")) { *out = 1; return 1; }
    if (!strcmp(val, "off") || !strcmp(val, "false") || !strcmp(val, "no")) { *out = 0; return 1; }
    char *end;
    long v = strtol(val, &end, 0);   /* base 0: accepts 0x3ff for the scene mask */
    if (end == val || *end) return 0;
    *out = (int)v;
    return 1;
}

/* Splits "--name=value" / "-name value" into name and value pointers. */
static const char *split_arg(const char *arg, char *name, size_t name_cap, int *has_inline_value) {
    while (*arg == '-') arg++;
    const char *eq = strchr(arg, '=');
    size_t n = eq ? (size_t)(eq - arg) : strlen(arg);
    name[0] = 0;
    if (n == 0 || n >= name_cap) return NULL;
    memcpy(name, arg, n);
    name[n] = 0;
    *has_inline_value = eq != NULL;
    return eq ? eq + 1 : NULL;
}

int settings_parse_arg(Settings *s, int argc, char **argv, int *i) {
    const char *arg = argv[*i];
    if (arg[0] != '-') return 0;
    char name[64];
    int inline_val = 0;
    const char *val = split_arg(arg, name, sizeof name, &inline_val);
    if (name[0] == 0) return 0;

    /* "--no-<bool-key>" form */
    int negate = 0;
    const char *key = name;
    if (strncmp(name, "no-", 3) == 0) { negate = 1; key = name + 3; }

#define X(field, key_str, def, mn, mx)                                         \
    if (strcmp(key, key_str) == 0) {                                           \
        if (negate) { s->field = 0; return 1; }                                \
        if ((mx) == 1 && !inline_val &&                                        \
            (*i + 1 >= argc || argv[*i + 1][0] == '-')) { s->field = 1; return 1; } \
        if (!inline_val) { if (*i + 1 >= argc) return 0; val = argv[++*i]; }   \
        int v; if (!parse_int_value(key_str, val, &v)) return 0;               \
        s->field = clampi(v, mn, mx); return 1;                                \
    }
    BH_SETTINGS_INT(X)
#undef X
#define X(field, key_str, def)                                                 \
    if (strcmp(key, key_str) == 0) {                                           \
        if (!inline_val) { if (*i + 1 >= argc) return 0; val = argv[++*i]; }   \
        unsigned c; if (!settings_parse_color(val, &c)) return 0;              \
        s->field = c; return 1;                                                \
    }
    BH_SETTINGS_COLOR(X)
#undef X
    return 0;
}
