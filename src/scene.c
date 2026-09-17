#include "scene.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

/* The shadow's edge, in impact parameter: b = 3*sqrt(3) M. */
#define SHADOW_B 5.196152423f
/* Never closer than just outside the photon sphere: from inside it there is
 * no orbit to sit on, only a fall. */
#define CAM_R_MIN 3.4f
#define CAM_R_MAX 400.f

/* Kerr's innermost stable circular orbit, prograde (Bardeen, Press & Teukolsky).
 * 6 M for a still hole, down to ~1.24 M at a = 0.999: the disk's inner edge
 * moves in and its gas runs faster as the hole spins up. */
float scene_isco(float a) {
    a = clampf(a, 0.f, 0.999f);
    float z1 = 1.f + cbrtf(1.f - a * a) * (cbrtf(1.f + a) + cbrtf(1.f - a));
    float z2 = sqrtf(3.f * a * a + z1 * z1);
    return 3.f + z2 - sqrtf((3.f - z1) * (3.f + z1 + 2.f * z2));
}

/* The shadow's angular radius seen from r is sin(t) = 3*sqrt(3)*sqrt(1-2/r)/r.
 * Inverting it for r: start from the far-field guess and iterate, which
 * converges in a handful of rounds anywhere the camera is allowed to be. */
float scene_radius_for_coverage(float coverage, float fov_y) {
    float target = clampf(coverage * fov_y * 0.5f, 0.004f, 1.45f);
    float sin_t = sinf(target);
    float r = SHADOW_B / sin_t;              /* ignoring the redshift factor */
    for (int i = 0; i < 24; ++i) {
        float rr = fmaxf(r, CAM_R_MIN);
        float next = SHADOW_B * sqrtf(fmaxf(1.f - 2.f / rr, 0.01f)) / sin_t;
        r = 0.5f * r + 0.5f * next;          /* damped, so it cannot oscillate */
    }
    return clampf(r, CAM_R_MIN, CAM_R_MAX);
}

/* --- scene selection ------------------------------------------------------ */
/* A shuffled deck, so every enabled scene is seen once before any repeats and
 * a fresh deck never opens with the one just shown. */
static void reshuffle(Scene *sc, const Settings *s) {
    sc->deck_n = 0;
    for (int i = 0; i < SCENE_COUNT; ++i)
        if (settings_scene_enabled(s, i)) sc->deck[sc->deck_n++] = i;
    if (sc->deck_n == 0) sc->deck[sc->deck_n++] = SCENE_VOID;
    for (int i = sc->deck_n - 1; i > 0; --i) {
        int j = (int)(rng_f(&sc->rng) * (float)(i + 1));
        if (j > i) j = i;
        int t = sc->deck[i]; sc->deck[i] = sc->deck[j]; sc->deck[j] = t;
    }
    if (sc->deck_n > 1 && sc->deck[0] == sc->last_kind) {
        int t = sc->deck[0]; sc->deck[0] = sc->deck[1]; sc->deck[1] = t;
    }
    sc->deck_pos = 0;

    /* Worth logging: this is the guarantee that every scene is seen before any
     * of them comes round twice, and that a new deck never opens on the one
     * just shown. */
    char order[256];
    int n = 0;
    for (int i = 0; i < sc->deck_n && n < (int)sizeof order - 20; ++i)
        n += snprintf(order + n, sizeof order - (size_t)n, "%s%s",
                      i ? " " : "", settings_scene_name(sc->deck[i]));
    plat_log("deck: %s", order);
}

static int deal_scene(Scene *sc, const Settings *s) {
    if (sc->deck_pos >= sc->deck_n) reshuffle(sc, s);
    return sc->deck[sc->deck_pos++];
}

static void update_binary(Scene *sc, float dt);

/* --- framing --------------------------------------------------------------
 * The lens is chosen with the shot, not fixed once for the whole screensaver.
 * A long lens on a distant hole compresses the star field and makes the thing
 * loom; a wide one up close puts the viewer inside it. A single fixed 60
 * degrees gave the worst of both: distant holes read as dots, near ones as
 * flat walls with no sense of scale at all. */
const char *scene_framing_name(Framing f) {
    switch (f) {
    case FRAME_FAR:      return "far";
    case FRAME_ENGULFED: return "engulfed";
    default:             return "hero";
    }
}

void scene_frame(Scene *sc, const Settings *s, Framing f, float coverage) {
    float cov, fov_deg;
    switch (f) {
    case FRAME_FAR:
        cov = coverage > 0.f ? coverage
                             : (sc->disk_on ? rng_range(&sc->rng, 0.09f, 0.20f)
                                            : rng_range(&sc->rng, 0.10f, 0.30f));
        fov_deg = rng_range(&sc->rng, 26.f, 42.f);
        break;
    case FRAME_ENGULFED:
        cov = coverage > 0.f ? coverage : rng_range(&sc->rng, 1.7f, 2.8f);
        fov_deg = rng_range(&sc->rng, 92.f, 112.f);
        break;
    default:
        cov = coverage > 0.f ? coverage
                             : (sc->disk_on ? rng_range(&sc->rng, 0.24f, 0.48f)
                                            : rng_range(&sc->rng, 0.50f, 1.15f));
        fov_deg = rng_range(&sc->rng, 50.f, 72.f);
        break;
    }
    /* The user's setting shifts the whole range rather than overriding it, so
     * a preference for long or short glass still gets all three shots. */
    fov_deg *= clampf((float)s->fov_deg / 55.f, 0.5f, 1.8f);
    sc->framing = f;
    sc->coverage = cov;
    sc->fov_base = clampf(fov_deg, 16.f, 118.f) * BH_PI / 180.f;
    sc->fov = sc->fov_base;
    sc->r_base = scene_radius_for_coverage(cov, sc->fov_base);
    sc->cam_r = sc->r_base;

    /* The disk is sized against the camera rather than picked blind, so the
     * two viewpoints in pick_theta both get enough gas to work with: the rim
     * runs past the viewer, which is what lets the shallow shot lie across the
     * sky and the steep one still fill the lower frame. Pulling it in to the
     * camera's own distance collapses the whole disk to a bright wire, because
     * the emission profile piles its light just outside the inner edge and
     * that edge hides behind the shadow. */
    if (sc->disk_on)
        sc->disk_outer = clampf(sc->cam_r * rng_range(&sc->rng, 1.4f, 2.2f),
                                sc->disk_inner + 10.f, 90.f);
    /* The plane test runs out to whatever the scene actually put there - the
     * disk's rim normally, but far past it when a tidal stream reaches out to
     * a star still coming apart hundreds of radii away. */
    sc->outer_reach = sc->disk_outer;
}

/* Mostly the hero shot, with enough of the other two that the rotation never
 * settles into one look - and never the same framing this scene had last time
 * it came up, so a second visit to the same hole is a genuinely different
 * picture rather than the same one from a slightly different angle. */
static Framing pick_framing(Scene *sc, int allow_engulfed, int avoid) {
    Framing choice[3];
    int n = 0;
    choice[n++] = FRAME_FAR;
    choice[n++] = FRAME_HERO;
    if (allow_engulfed) choice[n++] = FRAME_ENGULFED;

    /* Drop the one it had last time, unless that would leave nothing. */
    if (avoid >= 0 && n > 1) {
        int w = 0;
        for (int i = 0; i < n; ++i) if ((int)choice[i] != avoid) choice[w++] = choice[i];
        n = w;
    }
    /* Weighted toward the hero shot among whatever is left. */
    float u = rng_f(&sc->rng);
    for (int i = 0; i < n; ++i)
        if (choice[i] == FRAME_HERO && u < 0.6f) return FRAME_HERO;
    return choice[(int)(rng_f(&sc->rng) * (float)n) % n];
}

/* --- setting up one scene ------------------------------------------------- */
/* Feeding holes are entered above or below the disk, never edge-on: that is
 * the view where the gas shears past and the far side arcs over the top. */
static float pick_theta(Scene *sc, int off_plane, int side) {
    /* off_plane: 1 = the usual disk scenes, 2 = looking down on the plane */
    if (off_plane == 2) {
        float off = rng_range(&sc->rng, 0.50f, 0.88f);
        return BH_PI * 0.5f + (float)side * off;
    }
    /* Skimming the sheet, 3 to 13 degrees out of the plane, is the shot.
     * From just above or below the gas the near side of the disk is edge-on -
     * a thin bright bar across the middle - while the far side is lensed up
     * over the top of the shadow and down under the bottom of it, closing a
     * ring of fire around a clean black circle.
     *
     * Climb much higher and the near gas stops being a bar and becomes a
     * ceiling: it covers everything above the plane, the shadow goes black on
     * black against it, and the whole frame turns into a lit sky over a dark
     * floor. So most arrivals skim, and a quarter of them sit a little higher,
     * 16 to 29 degrees, for a more three-quarter view of the same thing. */
    float off;
    if (off_plane)
        off = rng_f(&sc->rng) < 0.72f ? rng_range(&sc->rng, 0.06f, 0.22f)
                                      : rng_range(&sc->rng, 0.28f, 0.50f);
    else
        off = rng_range(&sc->rng, 0.f, 1.35f);
    /* The side is handed in, so a repeat visit is seen from the other face of
     * the disk: under it if you were over it last time. */
    return BH_PI * 0.5f + (float)side * off;
}

static void configure(Scene *sc, const Settings *s, int kind) {
    sc->kind = kind;
    sc->time = 0.f;
    sc->spin = clampf(s->spin / 1000.f, 0.f, 0.999f);
    sc->axis = v3(0, 1, 0);
    sc->disk_x = v3(1, 0, 0);
    sc->disk_y = v3(0, 0, 1);
    sc->lensing = s->lensing / 100.f;
    sc->ring_glow = 0.09f;   /* raised below for the scenes with nothing else in them */
    sc->disk_on = 0;
    sc->disk_mode = 0;
    sc->binary = 0;
    sc->mass_a = sc->mass_b = 1.f;
    sc->star_r = 0.f;
    sc->disk_inner = scene_isco(sc->spin);
    sc->disk_outer = 22.f;
    sc->outer_reach = 22.f;
    sc->disk_bright = 1.f;
    sc->yaw = sc->pitch = 0.f;

    /* Whatever this scene was last time, it is not that again. */
    int avoid = (kind >= 0 && kind < SCENE_COUNT) ? sc->last_framing_of[kind] : -1;
    int side  = (kind >= 0 && kind < SCENE_COUNT && sc->last_side_of[kind]) ?
                -sc->last_side_of[kind] : (rng_f(&sc->rng) < 0.5f ? -1 : 1);

    int off_plane = 0;
    Framing f;
    switch (kind) {
    case SCENE_FED:
    case SCENE_FEEDING:
    case SCENE_AFTERMATH:
        sc->disk_on = 1;
        sc->disk_mode = 1;
        off_plane = 1;
        if (kind == SCENE_FEEDING) {
            /* Feeding in progress: a star is coming apart out there and the
             * thread of it reaches all the way in. The star is put hundreds of
             * gravitational radii away on purpose - the length of the thread
             * is the only thing in the picture that carries the real scale of
             * the place. */
            sc->disk_bright = 1.35f;
            sc->disk_mode = 3;
            /* Looking down on the plane, not along it. The thread is a spiral,
             * and a spiral seen edge-on is a line - all of that length, which
             * is the entire point of the scene, folds into the bright band the
             * disk already makes. From above it unwinds across the frame. */
            off_plane = 2;
            sc->star_r = rng_range(&sc->rng, 180.f, 460.f);
            sc->stream_wind = rng_range(&sc->rng, 0.7f, 1.5f);
            /* Bearing is set after the camera is placed - a star dropped at a
             * random angle is off the edge of the frame nine times in ten, and
             * a thread with nothing on the end of it is just a scratch. */
            sc->star_az = 0.f;
        }
        if (kind == SCENE_AFTERMATH) sc->disk_bright = 1.6f;
        /* Never engulfed: to fill the frame with the shadow you have to be in
         * among the gas, and from in there the disk stops being a disk and
         * becomes a horizon across the sky. The engulfing shot belongs to the
         * scenes with nothing but bent starlight in them. */
        f = pick_framing(sc, 0, avoid);
        scene_frame(sc, s, f, 0.f);
        break;
    case SCENE_EVAPORATING:
        /* A tiny horizon inside an enormous field of distortion. The deflection
         * is pushed well past the truth on purpose - that is what the Lensing
         * strength multiplier is for, and this is the scene it exists for. The
         * long lens is what makes the distortion field fill the frame while
         * the horizon itself stays a speck. */
        scene_frame(sc, s, FRAME_FAR, rng_range(&sc->rng, 0.04f, 0.11f));
        sc->lensing *= 2.8f;
        sc->ring_glow = 0.22f;
        break;
    case SCENE_CORE:
        sc->disk_on = 1;
        sc->disk_bright = 0.55f;
        off_plane = 1;
        scene_frame(sc, s, avoid == FRAME_HERO ? FRAME_FAR : FRAME_HERO, 0.f);
        break;
    case SCENE_STARDUST:
        /* The clean void, but the hole has been scooping up rubble and the
         * debris lights as it is compressed. The flashes are put in the
         * orbital plane rather than scattered over the screen precisely so
         * they bend with the light and drift with the gravity: what makes
         * them convincing is that they obey the same rules the disk does. */
        sc->disk_mode = 2;
        sc->disk_on = 1;                 /* shares the plane-crossing test */
        /* Looking down on the plane, so the flashes spread through the
         * bending region instead of collapsing into the edge-on line. */
        off_plane = 2;
        scene_frame(sc, s, pick_framing(sc, 1, avoid), 0.f);
        sc->disk_inner = fmaxf(sc->disk_inner, 3.6f);
        sc->outer_reach = clampf(sc->cam_r * 1.4f, 26.f, 90.f);
        sc->ring_glow = 0.26f;
        break;

    case SCENE_BINARY_VOID:
    case SCENE_BINARY_FED: {
        /* Two holes on a common centre, close enough that the merger is not
         * far off - so the orbit is fast and the light between them bends
         * through one shared figure rather than two separate ones. */
        sc->binary = 1;
        sc->mass_a = 1.f;
        sc->mass_b = rng_range(&sc->rng, 0.45f, 1.0f);
        sc->sep = rng_range(&sc->rng, 13.f, 24.f);
        sc->bin_phase = rng_range(&sc->rng, 0.f, 2.f * BH_PI);
        /* A real pair this close orbits in milliseconds. Shown at that rate it
         * is a strobe and a seizure risk, so it runs on the Time dilation
         * setting - which is not a cheat, it is what a distant observer's
         * clock does to it anyway. */
        sc->bin_rate = lerpf(0.35f, 3.2f, s->time_dilation / 100.f);
        if (kind == SCENE_BINARY_FED) {
            sc->disk_mode = 1;
            sc->disk_on = 1;
            sc->disk_bright = 1.5f;
        }
        off_plane = 1;
        scene_frame(sc, s, rng_f(&sc->rng) < 0.35f ? FRAME_FAR : FRAME_HERO, 0.f);
        /* Far enough out that both fit in frame with room for the shared
         * figure between them. */
        sc->r_base = fmaxf(sc->r_base, sc->sep * 3.2f);
        sc->cam_r = sc->r_base;
        if (sc->disk_mode == 1) {
            /* circumbinary: it can only survive outside the pair */
            sc->disk_inner = sc->sep * 1.5f;
            sc->disk_outer = sc->disk_inner + rng_range(&sc->rng, 30.f, 70.f);
        }
        sc->outer_reach = sc->disk_outer;
        sc->ring_glow = 0.20f;
        break;
    }

    default:
        scene_frame(sc, s, pick_framing(sc, 1, avoid), 0.f);
        break;
    }
    /* A hole in an empty sky has only its ring to show; one inside a bright
     * disk does not need the help and would only look haloed. */
    if (!sc->disk_on) sc->ring_glow = 0.30f;
    sc->cam_theta = pick_theta(sc, off_plane, side);
    sc->theta_base = sc->cam_theta;
    sc->tumble_t = 0.f;
    sc->cam_phi = rng_range(&sc->rng, 0.f, 2.f * BH_PI);
    sc->drift = 0.f;
    sc->disk_time = rng_range(&sc->rng, 0.f, 500.f);
    sc->vel = v3(0, 0, 0);
    sc->travel = v3(0, 0, 0);
    sc->speed = 0.f;
    sc->disk_shear = 0.f;
    /* The thread runs out to the star, so the plane test has to follow it
     * there - the whole point of the scene is being able to trace it the
     * entire way. */
    if (sc->disk_mode == 3) {
        sc->outer_reach = sc->star_r * 1.12f;
        /* Put the star on the far side of the hole and a little to one side,
         * so the shot holds both: the hole at the centre, the star out near
         * the frame edge, and the thread sweeping between them. */
        /* Beyond the hole and a little to one side. Past the hole it is very
         * nearly along the same line of sight, so a small bearing offset is
         * all it takes to clear the shadow - and much more than this puts it
         * outside the frame entirely. */
        float off = rng_range(&sc->rng, 0.18f, 0.38f) * (rng_f(&sc->rng) < 0.5f ? -1.f : 1.f);
        sc->star_az = sc->cam_phi + BH_PI + off;
        /* Far in proportion to where the camera is, so the thread is long
         * whatever distance the shot was framed at. */
        sc->star_r = clampf(sc->cam_r * rng_range(&sc->rng, 6.f, 13.f), 120.f, 700.f);
        sc->outer_reach = sc->star_r * 1.12f;
    }
    update_binary(sc, 0.f);
    if (kind >= 0 && kind < SCENE_COUNT) {
        sc->last_framing_of[kind] = (signed char)sc->framing;
        sc->last_side_of[kind] = (signed char)side;
    }

    plat_store_write_int("last-scene", kind);
    plat_log("scene %s (%s, %s the disk): r=%.1f M, %.0f%% cover, fov=%.0f deg, "
             "theta=%.2f, spin=%.3f, isco=%.2f",
             settings_scene_name(kind), scene_framing_name(sc->framing),
             side > 0 ? "over" : "under", sc->cam_r,
             sc->coverage * 100.f, sc->fov_base * 180.f / BH_PI, sc->cam_theta,
             sc->spin, sc->disk_inner);
}

/* --- public --------------------------------------------------------------- */
void scene_init(Scene *sc, const Settings *s, unsigned seed) {
    memset(sc, 0, sizeof *sc);
    sc->rng.s = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < SCENE_COUNT; ++i) sc->last_framing_of[i] = -1;
    /* Remembered across runs, so the screensaver never opens twice running on
     * the same hole - the one scene a user is guaranteed to see every time. */
    sc->last_kind = -1;
    plat_store_read_int("last-scene", &sc->last_kind);
    reshuffle(sc, s);
    configure(sc, s, deal_scene(sc, s));
    sc->warp = WARP_ARRIVING;          /* the screensaver opens on an arrival */
    sc->warp_t = 0.f;
    sc->until_warp = s->warp_minutes > 0 ? (float)s->warp_minutes * 60.f : -1.f;
}

void scene_go(Scene *sc, const Settings *s, int kind) {
    sc->last_kind = sc->kind;
    configure(sc, s, kind);
    sc->warp = WARP_ARRIVING;
    sc->warp_t = 0.f;
    sc->until_warp = s->warp_minutes > 0 ? (float)s->warp_minutes * 60.f : -1.f;
}

void scene_warp(Scene *sc, const Settings *s) {
    if (sc->warp != WARP_SETTLED) return;
    sc->warp = WARP_LEAVING;
    sc->warp_t = 0.f;
}

vec3 scene_cam_pos(const Scene *sc) {
    float st = sinf(sc->cam_theta), ct = cosf(sc->cam_theta);
    return v3(sc->cam_r * st * cosf(sc->cam_phi),
              sc->cam_r * ct,
              sc->cam_r * st * sinf(sc->cam_phi));
}

void scene_cam_basis(const Scene *sc, vec3 *right, vec3 *up, vec3 *fwd) {
    vec3 p = scene_cam_pos(sc);
    /* Looking at the hole, then turned by the idle motion and the user's own
     * yaw and pitch together. */
    vec3 f = v3_norm(v3_scale(p, -1.f));
    vec3 world_up = v3(0, 1, 0);
    if (fabsf(v3_dot(f, world_up)) > 0.995f) world_up = v3(0, 0, 1);
    vec3 rt = v3_norm(v3_cross(f, world_up));
    vec3 u  = v3_cross(rt, f);
    /* yaw about the camera's up, then pitch about its right */
    float yaw = sc->yaw + sc->auto_yaw, pitch = sc->pitch + sc->auto_pitch;
    float cy = cosf(yaw), sy = sinf(yaw);
    vec3 f1 = v3_add(v3_scale(f, cy), v3_scale(rt, sy));
    vec3 r1 = v3_sub(v3_scale(rt, cy), v3_scale(f, sy));
    float cp = cosf(pitch), sp = sinf(pitch);
    vec3 f2 = v3_norm(v3_add(v3_scale(f1, cp), v3_scale(u, sp)));
    vec3 r2 = v3_norm(r1);
    vec3 u2 = v3_norm(v3_cross(r2, f2));
    /* and roll about the view direction, for Tumble */
    float cr = cosf(sc->auto_roll), sr = sinf(sc->auto_roll);
    *right = v3_add(v3_scale(r2, cr), v3_scale(u2, sr));
    *up = v3_sub(v3_scale(u2, cr), v3_scale(r2, sr));
    *fwd = f2;
}

vec3 scene_boost_dir(const Scene *sc) {
    vec3 r, u, f;
    scene_cam_basis(sc, &r, &u, &f);
    /* Leaving, we accelerate away from the hole; arriving, into it. */
    return sc->warp == WARP_LEAVING ? v3_scale(f, -1.f) : f;
}

/* --- the pair --------------------------------------------------------------
 * Two holes on a common centre of gravity, placed by mass: the lighter one
 * swings wider. They are only ever a few tens of gravitational radii apart
 * here, which is the last few orbits before a merger, so they move fast enough
 * to watch - and the disk, when there is one, has to sit well outside them
 * because nothing survives between two holes at this range. */
static void update_binary(Scene *sc, float dt) {
    if (!sc->binary) return;
    sc->bin_phase += sc->bin_rate * dt;
    if (sc->bin_phase > 2.f * BH_PI) sc->bin_phase -= 2.f * BH_PI;

    float total = sc->mass_a + sc->mass_b;
    float ra = sc->sep * sc->mass_b / total;   /* heavier hole, tighter circle */
    float rb = sc->sep * sc->mass_a / total;
    float c = cosf(sc->bin_phase), sn = sinf(sc->bin_phase);
    /* They orbit in the plane the disk uses, which is the XZ plane. */
    sc->hole_a = v3( ra * c, 0.f,  ra * sn);
    sc->hole_b = v3(-rb * c, 0.f, -rb * sn);
}

/* --- movement without arrival ---------------------------------------------
 * The keys give the camera a genuine velocity, and everything that velocity
 * should do, it does: the near-field dust streams past at the right rate and
 * in the right direction, and the disk shears under it. What it does not do is
 * close the distance. The hole is hundreds of light years away; nobody flies
 * that in a lifetime of holding a key down, and letting the shadow swell as
 * you "approach" would be the one dishonest thing in the picture.
 *
 * So the velocity is integrated into `travel`, which the dust rides on, and
 * the camera's distance from the hole never hears about it. */
static void update_movement(Scene *sc, const Settings *s, float dt) {
    vec3 right, up, fwd;
    scene_cam_basis(sc, &right, &up, &fwd);
    float top = MOVE_SPEED_MAX * lerpf(0.35f, 1.f, s->movement_speed / 100.f);
    vec3 want = v3_add(v3_add(v3_scale(right, sc->move_in.x * top),
                              v3_scale(up, sc->move_in.y * top)),
                       v3_scale(fwd, sc->move_in.z * top));
    /* Mass: it takes a moment to get going and a moment to stop, so a tap on
     * the key reads as a nudge rather than a teleport. */
    sc->vel.x = approachf(sc->vel.x, want.x, 0.30f, dt);
    sc->vel.y = approachf(sc->vel.y, want.y, 0.30f, dt);
    sc->vel.z = approachf(sc->vel.z, want.z, 0.30f, dt);
    sc->travel = v3_add(sc->travel, v3_scale(sc->vel, dt));
    /* Keep the accumulator inside the dust field's wrapping period, or it
     * loses its precision after an hour of someone leaning on a key. */
    sc->travel.x = fmodf(sc->travel.x, DUST_CELL);
    sc->travel.y = fmodf(sc->travel.y, DUST_CELL);
    sc->travel.z = fmodf(sc->travel.z, DUST_CELL);

    float sp = sqrtf(v3_dot(sc->vel, sc->vel));
    sc->speed = approachf(sc->speed, sp, 0.15f, dt);
    /* Sideways motion drags the disk's pattern with it: a small thing, but it
     * is the second cue that the camera is moving, and it works even when the
     * dust happens to be sparse in front of you. */
    sc->disk_shear += v3_dot(sc->vel, right) * dt * 0.012f;
}

/* --- camera damage -------------------------------------------------------- */
/* Damage comes from two places at once: a slow random baseline, so the quiet
 * void scenes wear too, and whatever is actually on screen - a bright disk
 * close by, a flare, a jet, the deceleration at the end of a warp. Cause
 * visibly precedes effect, which is what makes it read as a story rather than
 * as noise laid over the picture.
 *
 * Nothing happens at all until the grace period is up. Whoever glances at the
 * screen in the first minutes sees an intact camera; the wear arrives only
 * once the thing has been left running. */
enum { DMG_REST = 0, DMG_WEAR = 1, DMG_HEAL = 2 };

/* How fast pending damage becomes visible, in level per second. A crack runs
 * across the glass in seconds; a sensor dies pixel by pixel over tens. */
#define DMG_GLASS_SPREAD   0.05f
#define DMG_MATRIX_SPREAD  0.012f
/* Never quite a ruined camera, so there is always a heal left to watch. */
#define DMG_PEAK           0.85f

/* Move cur toward target by at most rate*dt. */
static float stepf(float cur, float target, float rate, float dt) {
    float d = target - cur, m = rate * dt;
    return d > m ? cur + m : d < -m ? cur - m : target;
}

/* The rest between cycles is the grace period again, so the picture spends a
 * while clean before the next round of wear. With no grace period set there
 * is still a short breather, or the camera would never be seen intact. */
static float dmg_rest_s(const Settings *s) {
    return fmaxf((float)s->damage_grace * 60.f, 20.f);
}

static void update_damage(Scene *sc, const Settings *s, float dt) {
    sc->dmg_session += dt;

    /* Rolling shutter only while the scene is actually sweeping past. */
    sc->dmg_tear  = approachf(sc->dmg_tear, s->damage ? sc->boost : 0.f, 0.15f, dt);
    sc->dmg_bloom = approachf(sc->dmg_bloom, 0.f, 0.45f, dt);

    if (!s->damage || sc->dmg_session < (float)s->damage_grace * 60.f) {
        /* Not just "no new damage": anything already there fades out fast, so
         * turning the setting off cleans the picture up rather than freezing
         * it mid-crack. */
        sc->dmg_glass  = approachf(sc->dmg_glass,  0.f, 0.8f, dt);
        sc->dmg_matrix = approachf(sc->dmg_matrix, 0.f, 0.8f, dt);
        sc->dmg_pend_glass = sc->dmg_pend_matrix = 0.f;
        sc->dmg_heal_glass = sc->dmg_heal_matrix = 0.f;
        /* The grace period is up the moment it is up: start wearing then. */
        sc->dmg_phase = DMG_REST;
        sc->dmg_phase_t = 0.f;
        return;
    }

    switch (sc->dmg_phase) {
    case DMG_REST:
        sc->dmg_phase_t -= dt;
        if (sc->dmg_phase_t <= 0.f) {
            sc->dmg_phase = DMG_WEAR;
            sc->dmg_phase_t = rng_range(&sc->rng, 45.f, 120.f);
            sc->dmg_next = rng_range(&sc->rng, 2.f, 8.f);   /* first hit soon */
            plat_log("damage: wearing for %.0f s", sc->dmg_phase_t);
        }
        break;

    case DMG_WEAR: {
        /* How hard this scene is on a camera. A disk close by is radiation;
         * the deceleration at the end of a warp shakes the mounting. */
        float exposure = 1.f;
        if (sc->disk_on) exposure += sc->disk_bright * clampf(14.f / sc->cam_r, 0.1f, 2.2f);
        if (sc->warp != WARP_SETTLED) exposure += 1.4f * sc->boost;

        sc->dmg_next -= dt * exposure;
        if (sc->dmg_next <= 0.f) {
            /* One hit. Glass or matrix, not both: a camera that took
             * everything at once would just look broken. */
            sc->dmg_next = rng_range(&sc->rng, 12.f, 40.f);
            if (s->damage_glass && (!s->damage_matrix || rng_f(&sc->rng) < 0.45f)) {
                /* A fresh pane gets a new impact point; a cracked one keeps
                 * its web and the web grows, rather than jumping elsewhere. */
                if (sc->dmg_glass + sc->dmg_pend_glass < 0.01f) {
                    sc->dmg_impact_x = rng_range(&sc->rng, 0.15f, 0.85f);
                    sc->dmg_impact_y = rng_range(&sc->rng, 0.15f, 0.85f);
                }
                sc->dmg_pend_glass = fmaxf(0.f, fminf(sc->dmg_pend_glass + rng_range(&sc->rng, 0.15f, 0.4f),
                                           DMG_PEAK - sc->dmg_glass));
            } else if (s->damage_matrix) {
                sc->dmg_pend_matrix = fmaxf(0.f, fminf(sc->dmg_pend_matrix + rng_range(&sc->rng, 0.1f, 0.3f),
                                            DMG_PEAK - sc->dmg_matrix));
            }
            /* Whatever hit it also saturated the sensor for a moment. */
            sc->dmg_bloom = 1.f;
        }
        sc->dmg_phase_t -= dt;
        if (sc->dmg_phase_t <= 0.f) {
            /* Wear is over: whatever had not spread yet never will. Letting a
             * backlog finish first kept the damage growing for minutes after
             * the wear ended, so the heal never seemed to come. */
            sc->dmg_pend_glass = sc->dmg_pend_matrix = 0.f;
            float heal = (float)s->damage_heal;
            sc->dmg_heal_glass  = fmaxf(sc->dmg_glass,  0.02f) / heal;
            sc->dmg_heal_matrix = fmaxf(sc->dmg_matrix, 0.02f) / heal;
            sc->dmg_phase = DMG_HEAL;
            plat_log("damage: healing from glass %.2f, matrix %.2f over %.0f s",
                     sc->dmg_glass, sc->dmg_matrix, heal);
        }
        break;
    }

    case DMG_HEAL:
        /* The rates were set as the wear ended, so the damage there was then
         * goes away in exactly the heal time. */
        sc->dmg_glass  = stepf(sc->dmg_glass,  0.f, sc->dmg_heal_glass,  dt);
        sc->dmg_matrix = stepf(sc->dmg_matrix, 0.f, sc->dmg_heal_matrix, dt);
        if (sc->dmg_glass <= 0.f && sc->dmg_matrix <= 0.f) {
            sc->dmg_heal_glass = sc->dmg_heal_matrix = 0.f;
            sc->dmg_phase = DMG_REST;
            sc->dmg_phase_t = dmg_rest_s(s);
            plat_log("damage: healed, resting for %.0f s", sc->dmg_phase_t);
        }
        break;
    }

    /* Pending damage spills into the picture gradually. */
    float g = fminf(sc->dmg_pend_glass, DMG_GLASS_SPREAD * dt);
    g = fminf(g, 1.f - sc->dmg_glass);
    sc->dmg_glass += g;
    sc->dmg_pend_glass = sc->dmg_glass >= 1.f ? 0.f : sc->dmg_pend_glass - g;
    float m = fminf(sc->dmg_pend_matrix, DMG_MATRIX_SPREAD * dt);
    m = fminf(m, 1.f - sc->dmg_matrix);
    sc->dmg_matrix += m;
    sc->dmg_pend_matrix = sc->dmg_matrix >= 1.f ? 0.f : sc->dmg_pend_matrix - m;
}

/* Eased 0..1 ramp, so the warp has no corners in it. */
static float ease(float t) {
    t = clampf(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

void scene_update(Scene *sc, const Settings *s, float dt, int manual) {
    sc->time += dt;

    /* The disk runs on its own clock, so the Time dilation setting can slow a
     * process that is genuinely violent down to something watchable. */
    float slow = lerpf(0.12f, 1.6f, s->time_dilation / 100.f);
    sc->disk_time += dt * slow;
    update_binary(sc, dt);
    update_movement(sc, s, dt);
    update_damage(sc, s, dt);

    /* The framing is never quite still. A very slow dolly on the orbit and a
     * matching breath in the focal length keep the shot alive without ever
     * being caught moving: over a minute the shadow swells and settles by a
     * few per cent, which is the difference between a photograph and a place.
     * Both run off the scene clock, not the warp, so they survive the user
     * taking the controls. */
    float dolly = 1.f + 0.11f * sinf(sc->time * 0.043f + 1.7f);
    sc->cam_r = clampf(sc->r_base * dolly, 3.4f, 400.f);
    sc->fov = sc->fov_base * (1.f + 0.055f * sinf(sc->time * 0.029f));

    /* --- the warp --------------------------------------------------------- */
    sc->warp_t += dt;
    switch (sc->warp) {
    case WARP_LEAVING:
        sc->boost = 0.97f * ease(sc->warp_t / WARP_LEAVE_S);
        if (sc->warp_t >= WARP_LEAVE_S) {
            sc->last_kind = sc->kind;
            configure(sc, s, deal_scene(sc, s));
            sc->warp = WARP_ARRIVING;
            sc->warp_t = 0.f;
            sc->until_warp = s->warp_minutes > 0 ? (float)s->warp_minutes * 60.f : -1.f;
        }
        break;
    case WARP_ARRIVING:
        sc->boost = 0.97f * (1.f - ease(sc->warp_t / WARP_ARRIVE_S));
        if (sc->warp_t >= WARP_ARRIVE_S) {
            sc->boost = 0.f;
            sc->warp = WARP_SETTLED;
            sc->warp_t = 0.f;
        }
        break;
    default:
        sc->boost = 0.f;
        if (sc->until_warp > 0.f) {
            sc->until_warp -= dt;
            if (sc->until_warp <= 0.f) scene_warp(sc, s);
        }
        break;
    }

    /* --- the camera ------------------------------------------------------- */
    /* Side movement: a slow lateral slide, on top of whatever the idle motion
     * is doing. It is small - enough to keep the frame alive, not enough to
     * re-frame the shot. */
    if (s->side_movement && !manual) {
        float v = lerpf(0.004f, 0.06f, s->movement_speed / 100.f);
        sc->drift += v * dt;
        sc->cam_phi += v * dt * 0.35f;
    }

    /* Once they have been idle a while, the view they aimed drifts back to
     * facing the hole - slowly enough that it is never caught doing it. This
     * touches only the user's aim; the idle motion below has its own. */
    if (!manual) {
        sc->yaw   = approachf(sc->yaw,   0.f, 6.f, dt);
        sc->pitch = approachf(sc->pitch, 0.f, 6.f, dt);
    }

    int mode = s->rotate_360 ? s->rotate_mode : -1;
    /* Whatever the current mode does not drive settles back to level. The yaw
     * is taken the short way round, from wherever the last turn left it. */
    if (mode != ROT_YAW && mode != ROT_TUMBLE)
        sc->auto_yaw = approachf(wrapf(sc->auto_yaw, -BH_PI, BH_PI), 0.f, 4.f, dt);
    if (mode != ROT_TUMBLE) {
        sc->auto_pitch = approachf(sc->auto_pitch, 0.f, 4.f, dt);
        sc->auto_roll  = approachf(wrapf(sc->auto_roll, -BH_PI, BH_PI), 0.f, 4.f, dt);
    }
    if (mode < 0) return;

    /* The turn does NOT stop because somebody touched the mouse.
     *
     * The Black Wall stopped its drift on any input, and rightly: there the
     * drift carried you along the wall, so steering and drifting fought each
     * other for the same degree of freedom. Here they are separate things -
     * the turn moves the camera around the hole, the mouse only aims it - and
     * freezing the sky for twenty seconds because a mouse got nudged on the
     * desk makes the whole screensaver look like it has crashed. It is the
     * single most alarming thing it can do, and it buys nothing.
     *
     * What the user's aim does get is the right of way: their yaw and pitch
     * stand until they stop, then ease back to centre (see below), so the
     * framing recovers without the universe ever holding still. */

    /* A full turn in the time the slider asks for. */
    float rate = 2.f * BH_PI / (float)(s->rotate_seconds > 0 ? s->rotate_seconds : 120);
    switch (mode) {
    case ROT_YAW:
        /* The camera holds its place and turns on the spot: the whole sky and
         * the hole pass through the frame, with no parallax. */
        sc->auto_yaw = wrapf(sc->auto_yaw + rate * dt, -BH_PI, BH_PI);
        break;
    case ROT_TUMBLE: {
        /* The view itself tumbles: a full turn, a nod up and down, and a slow
         * roll, at rates that never line up into a repeating figure - so the
         * hole sweeps through the frame from ever-changing directions. Under
         * that, the camera drifts round the hole and swings from over the disk
         * to under it and back, starting from the elevation the scene chose
         * rather than jumping to the plane. */
        sc->tumble_t += dt;
        float t = sc->tumble_t * rate;
        sc->auto_yaw   = wrapf(sc->auto_yaw + rate * dt, -BH_PI, BH_PI);
        sc->auto_pitch = approachf(sc->auto_pitch, 0.75f * sinf(t * 0.61f), 1.5f, dt);
        sc->auto_roll  = wrapf(sc->auto_roll + rate * 0.43f * dt, -BH_PI, BH_PI);
        sc->cam_phi += rate * 0.5f * dt;
        float off = sc->theta_base - BH_PI * 0.5f;
        if (fabsf(off) < 0.15f) off = off < 0.f ? -0.15f : 0.15f;
        sc->cam_theta = approachf(sc->cam_theta, BH_PI * 0.5f + off * cosf(t * 0.29f), 1.5f, dt);
        break;
    }
    default:
        /* Orbit: the camera actually travels, so near and far lensed images of
         * the same star shift against each other. That parallax is what makes
         * the bending read as depth instead of as a filter over the picture. */
        sc->cam_phi += rate * dt;
        break;
    }
    if (sc->cam_phi > 2.f * BH_PI) sc->cam_phi -= 2.f * BH_PI;
}
