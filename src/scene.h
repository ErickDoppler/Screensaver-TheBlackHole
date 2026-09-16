/* The simulation: which hole we are looking at, where the camera sits on its
 * orbit, and the warp that carries us from one to the next.
 *
 * Coordinates are geometric, in units of the gravitational radius M (G = c = 1).
 * The horizon is at r = 2, the photon sphere at r = 3, the shadow's edge at an
 * impact parameter of 3*sqrt(3) = 5.196. The hole sits at the origin with its
 * spin axis along +Y, so the accretion disk lies in the XZ plane; the camera
 * does the moving. Working in units of M is also what keeps the arithmetic in
 * float range - a supermassive horizon is 10^10 metres across, and the same
 * scene in metres would lose all its precision. */
#ifndef BH_SCENE_H
#define BH_SCENE_H
#include "mathx.h"
#include "settings.h"

/* How a scene is framed. Coverage and focal length are chosen together, not
 * slid along one axis: the three shots want opposite lenses, and picking one
 * of them outright is what stops every arrival looking the same.
 *
 *   FAR       a small shadow on a long lens. The compressed star field is what
 *             sells it - the hole looms at a distance instead of sitting there
 *             as a dot pasted on the sky.
 *   HERO      the shot. Big, round, still unmistakably an object, with the
 *             disk arcing over the top of it. Most arrivals are this.
 *   ENGULFED  the shadow past the edges of the frame on a very wide lens, so
 *             the view has to be turned to find anything at all. Rare, because
 *             past about 150% coverage the hole stops reading as a sphere and
 *             starts reading as a wall. */
typedef enum Framing { FRAME_FAR = 0, FRAME_HERO, FRAME_ENGULFED } Framing;

/* What the camera is doing between one hole and the next. */
typedef enum WarpPhase {
    WARP_SETTLED = 0,   /* parked by a hole, watching */
    WARP_LEAVING,       /* accelerating away, sky streaking forward */
    WARP_ARRIVING       /* decelerating into the next one */
} WarpPhase;

#define WARP_LEAVE_S  1.25f
#define WARP_ARRIVE_S 1.80f
/* Seconds of no input before the screensaver takes the camera back. */
#define IDLE_RESUME   20.f

typedef struct Scene {
    int    kind;              /* SCENE_* */
    float  time;              /* seconds since this scene began */
    rng_t  rng;

    int    deck[SCENE_COUNT]; /* shuffled order: every scene before any repeat */
    int    deck_n, deck_pos;
    int    last_kind;
    /* What this scene looked like the last time it came round, so the next
     * time can be made to look different. Without this the deck guarantees
     * only that you see all ten before any repeats - it says nothing about the
     * repeat being worth watching, and the same hole from the same sort of
     * distance twice in a row is the thing that makes a rotation feel small. */
    signed char last_framing_of[SCENE_COUNT];
    signed char last_side_of[SCENE_COUNT];    /* +1 above the disk, -1 below */

    /* the hole */
    float  spin;              /* 0..0.999 */
    vec3   axis, disk_x, disk_y;

    /* What is orbiting in the plane. Matches uDiskMode in the shader:
     * 0 nothing, 1 an accretion disk, 2 scattered flashes, 3 disk + stream. */
    int    disk_mode;
    float  outer_reach;       /* furthest crossing radius worth testing */
    /* The tidal stream and the star being pulled apart at the end of it. */
    float  star_r, star_az, stream_wind;
    /* Two holes winding in on a common centre. binary = 0 when there is one. */
    int    binary;
    float  sep;               /* their separation, in M */
    float  bin_phase;         /* where they are in the orbit */
    float  bin_rate;          /* radians per second, as seen from here */
    float  mass_a, mass_b;
    vec3   hole_a, hole_b;
    /* kept so existing code reads naturally */
    int    disk_on;
    float  disk_inner;        /* the ISCO, which moves in as the hole spins up */
    float  disk_outer;
    float  disk_bright;
    float  disk_time;         /* the disk's own clock, so it can be slowed */

    /* the camera on its orbit */
    float  cam_r;             /* distance from the hole, units of M */
    float  r_base;            /* the arrival distance, before the dolly */
    float  cam_theta;         /* polar angle from the spin axis */
    float  cam_phi;           /* azimuth */
    float  coverage;          /* the shadow's share of the screen on arrival */
    Framing framing;
    float  yaw, pitch;        /* look-around, relative to facing the hole */
    float  drift;             /* Side movement: a slow lateral slide */

    /* The lens. Scenes choose their own focal length: a wide one for the
     * close encounters, where the distortion should sweep the whole frame and
     * the hole should feel like something you are inside; a long one for the
     * distant ones, which compresses the star field and makes a small shadow
     * loom instead of sitting there as a dot. */
    float  fov;               /* radians, vertical, as rendered this frame */
    float  fov_base;          /* the scene's framing, before the breathing */

    /* Movement. The keys drive a real velocity, and that velocity is real:
     * the dust streams, the disk shears. What it cannot do is arrive. The hole
     * is hundreds of light years off, so the distance to it does not change,
     * and pretending otherwise would be the one lie in the picture. */
    vec3   move_in;           /* this frame's key input, in the camera's frame */
    vec3   vel;               /* world velocity, units of M per second */
    vec3   travel;            /* integrated: what the dust field rides on */
    float  speed;             /* smoothed |vel|, drives the dust and the shear */
    float  disk_shear;        /* travel folded into the disk's own phase */

    /* the warp */
    WarpPhase warp;
    float  warp_t;            /* seconds inside the current warp phase */
    float  boost;             /* 0..~0.97, drives sky aberration */
    float  until_warp;        /* seconds left on the warp timer (<0 = never) */

    /* artistic deflection multiplier, per scene, on top of the setting */
    float  lensing;
    float  ring_glow;

    /* Camera damage. The session clock is deliberately not reset by a warp:
     * the wear is meant to read as time spent out there, so a long session
     * keeps degrading instead of starting over at every hole. */
    float  dmg_session;
    float  dmg_glass, dmg_matrix, dmg_bloom, dmg_tear;
    float  dmg_impact_x, dmg_impact_y;
    float  dmg_next;          /* seconds to the next random hit */
    /* Damage runs in cycles: clean and resting, wearing, healing, resting
     * again. A hit does not land in one frame; it goes into the pending pool
     * and spills into the visible level over the following seconds, so
     * cracks creep and pixels die one at a time. */
    int    dmg_phase;         /* DMG_REST, DMG_WEAR, DMG_HEAL */
    float  dmg_phase_t;       /* seconds left in rest or wear */
    float  dmg_pend_glass, dmg_pend_matrix;
    float  dmg_heal_glass, dmg_heal_matrix;   /* heal rates, per second */
} Scene;

void scene_init(Scene *sc, const Settings *s, unsigned seed);
void scene_update(Scene *sc, const Settings *s, float dt, int manual);
/* Warp to another hole at another distance. */
void scene_warp(Scene *sc, const Settings *s);
/* Jump straight to one scene (the Ctrl+Alt+S picker, and --scene). */
void scene_go(Scene *sc, const Settings *s, int kind);

/* Where the camera is, and the basis it looks along. */
vec3 scene_cam_pos(const Scene *sc);
void scene_cam_basis(const Scene *sc, vec3 *right, vec3 *up, vec3 *fwd);
/* The direction the warp is carrying us, for sky aberration. */
vec3 scene_boost_dir(const Scene *sc);

/* Distance from the hole that makes the shadow cover `coverage` of the screen
 * height, for a camera with the given vertical field of view. */
float scene_radius_for_coverage(float coverage, float fov_y);
/* Frames the shot: picks the focal length that goes with this framing, then
 * the distance that gives `coverage` through it. Pass coverage <= 0 to let the
 * framing choose that too. */
void scene_frame(Scene *sc, const Settings *s, Framing f, float coverage);
const char *scene_framing_name(Framing f);
/* How fast the camera may fly, in units of M per second. */
#define MOVE_SPEED_MAX 9.0f
/* The side of the wrapping cube the near-field dust lives in. */
#define DUST_CELL 34.0f
/* The innermost stable circular orbit for a hole of this spin (prograde). */
float scene_isco(float spin);
#endif
