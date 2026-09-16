/* OpenGL 3.3 core renderer.
 *
 * Two stages. The star field is drawn as real point sprites into the six faces
 * of a sky cube map - that is where Particle size, twinkle and the star colour
 * live, and it is one glDrawArrays per face with no vertex work on the CPU.
 * Then a single fullscreen pass integrates a null geodesic per pixel and reads
 * that cube map along the direction the light actually came from, picking up
 * the accretion disk on the way. Ghost tails, blur and camera damage run after,
 * inherited from The Black Wall.
 *
 * Everything offscreen is rendered at `scale` of the window and upscaled in the
 * final blit: the picture is nearly all smooth gradient, so half resolution is
 * almost free to look at and is the difference between a screensaver and a
 * space heater. */
#ifndef BH_RENDER_H
#define BH_RENDER_H
#include "mathx.h"
#include "settings.h"
#include "scene.h"

/* Quality tiers, resolved from the Quality setting (see render_quality). */
typedef struct Quality {
    int   steps;        /* geodesic integration steps per pixel */
    float scale;        /* offscreen resolution, as a fraction of the window */
    int   cube_face;    /* sky cube map face size, in texels */
} Quality;

typedef struct Renderer {
    unsigned prog_star, prog_lens, prog_ghost, prog_blur, prog_blit, prog_damage;
    unsigned prog_dust;              /* the near field, which is what shows motion */
    unsigned prog_bright;            /* bright-pass for the glow */
    unsigned prog_binary;            /* the two-centre march */
    unsigned vao;                    /* empty VAO for the fullscreen passes */
    unsigned star_vao, star_vbo;
    int      star_count;
    unsigned sky_tex, sky_fbo;       /* the cube map and a reusable FBO */
    int      sky_face;               /* face size the cube was built for */
    int      sky_valid;              /* 0 = redraw the stars this frame */
    float    sky_time;               /* when it was last redrawn */

    unsigned fbo_scene, tex_scene;
    unsigned fbo_acc[2], tex_acc[2];
    unsigned fbo_tmp, tex_tmp;
    unsigned fbo_bloom[2], tex_bloom[2];   /* half resolution */
    int      bloom_w, bloom_h;
    int      acc_index, acc_valid;

    int   width, height;             /* the window, in pixels */
    int   low_w, low_h;              /* the offscreen targets */
    int   fbo_w, fbo_h;              /* what they were built for */
    float max_point_px;
    Quality q;
    /* True GPU milliseconds for the last completed frame, from a timer
     * query. Timing the CPU side of render_frame measures how long it took
     * to QUEUE the work, which on a healthy driver is almost nothing - and
     * an adaptive loop fed that number climbs to maximum quality no matter
     * how badly the GPU is actually drowning. */
    unsigned gpu_query[3];
    int      query_slot;
    int      query_live[3];
    int      query_ok;               /* 0 = driver gave us nothing usable */
    float last_frame_ms;
} Renderer;

int  render_init(Renderer *r, const Settings *s);
void render_resize(Renderer *r, int w, int h);
void render_frame(Renderer *r, const Settings *s, const Scene *sc, float time, float dt);
void render_shutdown(Renderer *r);
/* Reads back the default framebuffer and writes a PNG. 1 on success. */
int  render_dump_png(const Renderer *r, const char *path);
/* Rebuilds the star field (count or colour changed). */
void render_rebuild_stars(Renderer *r, const Settings *s);

Quality render_quality(int setting);
#endif
