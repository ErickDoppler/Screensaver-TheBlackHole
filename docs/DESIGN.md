# The Black Hole - design notes

**Stage: building.** The engine, the renderer, the settings and the dialog are
in and running; four of the ten scenes and none of the four events are. See
**Status** at the end for exactly what is and is not there.

A direct simulation of a black hole, seen from a camera that warps in beside
it, holds still while the universe bends around it, and warps away again. The
3D engine is inherited from **The Black Wall**; the scene is entirely new.

---

## 1. What comes from The Black Wall

The Black Wall (`../Screensaver-TheBlackWall`) is the engine source. Kept:

| Piece | Role here |
|---|---|
| C11 + SDL3 (static) + OpenGL 3.3 core, single-file `.scr` | unchanged |
| `gl_VertexID` point sprites, no vertex buffers | the star field: 5500 stars in one `glDrawArrays` |
| Anti-moire sprite sizing (the 4 px / 2 px rules in `wall.frag`) | stars are sub-pixel; without this they crawl and beat against the display grid |
| X-macro settings table -> defaults, clamping, registry, CLI, dialog | every setting below costs one line |
| Ghost tail `acc = max(frame, acc * decay)`, separable 9-tap blur | the *Particle ghost tail* and *Particle blur* sliders, unchanged |
| `glitch.frag` (static, torn rows, displaced blocks, channel split, TV-off) | roughly 70 % of **Camera damage** already exists |
| Win32 shell: `/s`, `/p <hwnd>`, `/c`, multi-monitor span, preview child window | renamed only |
| Frame pacing, vsync-aligned sleep, FPS cap | a raymarched screensaver needs this more than the wall did |
| `double` world coordinate + origin snapping | becomes the camera radius; a supermassive horizon is ~10^10 m and `float` dies |
| `--dump out.png --frames N`, `--log`, `--<setting> <value>` | how progress gets shown during development |

Dropped: `res/models/*.glb`, `models.c`, `man.vert`, `mask.*`, `ember.vert`,
`wall.*`, `floor.*`, `city.vert`, the wave/spike/ghost logic in `sim.c`, and
the CPU/network system counters.

**No git repository.** Plain folder.

---

## 2. Renderer: the hybrid

A point sprite cannot be bent. Lensing asks a per-pixel question - *where did
the light arriving here actually come from* - so the hole is a fullscreen
fragment pass. But the star settings (size, tail, blur, twinkle) only mean
something if stars are real sprites. Hence the hybrid:

1. **Star panorama.** The star field is drawn as point sprites into a cube map
   (or equirectangular panorama), reusing The Black Wall's sprite sizing and
   twinkle. Redrawn only when the camera moves far enough to matter, not every
   frame.
2. **Geodesic pass.** A fullscreen shader integrates null geodesics in a Kerr
   metric outward from each pixel, and samples the panorama by the *bent* exit
   direction. Rays that cross the horizon return black.
3. **Volumetrics in the same march.** The accretion disk, the tidal stream, the
   scooped stardust, the jets and the nebula are intersected as the ray is
   integrated, so they are lensed correctly - including the disk's far side
   wrapped over the top and under the bottom of the shadow.
4. **Post.** Ghost tail, blur, camera damage, exactly as the engine does today.

**Deliberate consequences.** The Einstein ring, the photon sphere, the
secondary and tertiary disk images and the whole-sky compression at 400 %
coverage all fall out of the integration rather than being faked.

### Physics choices

* **Shadow geometry is correct.** The shadow radius is ~2.6x the horizon
  radius, not 0.5x the diameter as first sketched; noticeable distortion runs
  several diameters out. A **Lensing strength** multiplier (0.5x - 3.0x) lets
  the drama be dialled above or below truth on purpose.
* **Spin (Kerr), 0 -> 0.999.** Frame dragging, an off-centre asymmetric
  shadow, a flattened ergosphere. One slider; makes every warp-in different.
* **Doppler beaming and relativistic aberration** on the disk - the approaching
  side blazing blue-white, the receding side dim and red. *Interstellar*
  omitted this on purpose because it looked lopsided, so it is a toggle:
  **Interstellar look** vs **True relativistic**.
* **Gravitational redshift** - light climbing out of the well reddens near the
  horizon. Cheap, and it sells the depth.
* **Binary lensing is an approximation.** Two-centre geodesics have no closed
  form. Superposed deflection fields, numerically integrated (~2x cost). It
  looks right; it is not a solved metric.

### Quality and power

The Black Wall is explicitly an energy-efficient screensaver - one draw call
for the whole wall. A per-pixel geodesic integrator is the opposite, and
across three 4K monitors it is a space heater.

* **First-run benchmark** picks a quality tier and stores it; the **Quality**
  slider overrides.
* Quality drives: integration step count, internal render scale, Kerr vs
  Schwarzschild, disk volumetric sample count, higher-order image depth.
* The image is mostly smooth gradients, so rendering at 50-70 % of native and
  upscaling is nearly free visually. That is the default.
* **One scene across all monitors**, a single wide viewport, as The Black Wall
  does. Lensing stays continuous across the screens and it costs one render.

---

## 3. Scenes

Ten. Cycled in random order with no immediate repeat (the shuffled-deck logic
from The Black Wall's figures). Each can be enabled or disabled.

| # | Scene | Description |
|---|---|---|
| 1 | **Void** | Clean vacuum. The shadow, the photon ring, and distant stars smeared into arcs around it. Nothing else. |
| 2 | **Feeding ended** | Horizon plus a settled accretion disk, the *Interstellar* silhouette: the disk's far side arcs over the top and under the bottom of the shadow. |
| 3 | **Feeding in progress** | As above, plus a star being torn apart far away, its stream reaching across the view in many windings. Scale cues matter more than the stream itself - see below. |
| 4 | **Near evaporation** | An almost point-sized horizon with lensing so violent it fills the entire view. The sky is a wound. |
| 5 | **Stardust scoop** | Scene 1 plus flashes inside the lensing region, each bending and fading on a real orbital path rather than drifting. |
| 6 | **Binary in void** | Two holes orbiting a common barycentre, fast, stars bent through one shared gravitational figure. |
| 7 | **Binary, feeding ended** | As 6, with a circumbinary disk lighting the process and morphing around the shared field. |
| 8 | **Nebula backdrop** | A hole crossing a nebula. The one scene with real colour; the nebula lenses into arcs. |
| 9 | **Galactic core** | Supermassive, dense star field, a few stars on visibly fast orbits (the Sgr A* / S2 look). Sells scale better than anything else. |
| 10 | **Merger aftermath** | Only reachable when the **Merger completes** event is on: a single larger hole with a disk still settling and a ringdown wobble dying out. |

**Conveying distance in scene 3.** A long stream is not by itself convincing.
The scale reads from: the doomed star rendered visibly tiny and tidally
elongated; a visible *lag* along the stream, so matter nearer the hole is
plainly older; many windings rather than one arc; and parallax during the
360 degree motion, which is the only cue that cannot be faked.

**Scene picker.** Two surfaces, both behind `Ctrl+Alt+S`:
* **In-screensaver overlay** - `Ctrl+Alt+S` while running lists the ten scenes
  over the live image; picking one warps straight to it.
* **Hidden settings section** - the settings dialog ships with no scene list;
  `Ctrl+Alt+S` inside the dialog reveals the checkboxes that control the
  rotation.

---

## 4. Events

Four, each with its own on/off switch in settings.

* **Merger completes.** Occasionally a binary actually merges instead of
  orbiting forever: frequency chirp, a flash, a ringdown wobble, then scene 10.
* **Fall-in through the horizon.** The camera crosses in: the whole sky
  compresses into a shrinking bright disk behind, spaghettification stretch,
  white-out, re-warp. Triggered **rarely at random, and on demand by a key
  combination** for demos and testing.
* **Relativistic jets.** Twin jets along the spin axis on feeding holes.
  Physically correct for an active hole, dramatic, and cheap.
* **Tidal disruption flare.** In scene 3, the moment the stream first slams
  into the disk and the whole system flares.

**Binary rotation speed.** A pre-merger orbit is genuinely milliseconds, which
on screen is a strobe and a photosensitivity risk. A **Time dilation** slider
governs it - physically honest, since that is what a distant observer sees -
with the default capped below the flicker range.

---

## 5. Camera

* **Warp-in.** Stars stretch to streaks, aberration squeezes the sky into a
  bright forward cone, then deceleration snaps it back and the hole resolves
  out of the middle. Reversed on warp-out.
* **Random arrival distance** every time: from the shadow occupying 5 % of the
  screen with the disk taking 50 %, out to 400 % coverage where the view must
  be turned to see anything at all.
* **Feeding holes are always entered above or below the disk plane**, so the
  gas shears past at speed and sparks fly toward the viewer.
* **Real orbit, not a static perch.** The camera is on an orbit; a static one
  would fall in. The orbit also gives genuine parallax between near and far
  lensed star images, which is what makes lensing read as three-dimensional
  rather than as a screen filter.
* **Enter** warps to another hole at another distance, and resets the warp
  timer.
* **Idle.** After 20 s with no input (or with controls disabled) the 360 degree
  motion begins, completing a full turn over the configured 3-999 s. Three
  selectable modes: **orbit** (parallax), **yaw in place** (simplest, whole sky
  guaranteed), **tumble** (yaw plus drifting pitch, over and under the disk).
* **Movement without movement.** If the user steers, dust shears left and
  right, faster and slower, and the disk's stripes move - but the hole does
  not change. It is hundreds of light years away. Walking changes nothing, and
  that is the point.
* **The 400 % case** gets deliberate auto-tour framing, so the idle motion
  finds the Einstein ring rather than sweeping past it.

---

## 6. Camera damage

Built on `glitch.frag`. Accumulates from **both** a slow random baseline and
from what is on screen - radiation off a feeding disk, a tidal flare, a jet,
a close warp - so that cause visibly precedes effect. Heals over the
configured heal time.

**The camera starts clean.** Damage does not begin until a **grace period**
has elapsed since the screensaver started - default 2 minutes, configurable,
0 disabling the delay. Whoever glances at the screen in the first minutes sees
an intact picture; the wear arrives only once the thing has been left running,
so the damage reads as time spent out there rather than as a broken build. The
grace period suppresses the random baseline *and* scene-caused damage alike,
and it restarts with the screensaver, not with each warp.

**Damage is gradual and cyclic.** After the grace period the camera goes
through *wear* (45 s - 2 min of hits), *heal* (the damage recedes over the heal
time) and *rest* (clean again for the grace period, at least 20 s), then wears
again. A hit never lands in one frame: it spills into the picture over
seconds, so cracks creep outward from the impact and sensor pixels die one at
a time; healing runs the same way in reverse. A cracked pane keeps its impact
point, and the web grows rather than jumping elsewhere.

* **Glass damage** - a radial spiderweb from an impact point that slightly
  refracts the image, with light bleeding along the crack lines when something
  bright sits behind them.
* **Matrix damage** - stuck-on pixels, dead-black pixels, dead rows and
  columns, and column readout smear below a saturated highlight.
* **Sensor bloom** when a flare goes off, and rolling-shutter tear during warp.
* **Palette** - white, greenish-purple, black, or a custom colour.

---

## 7. Settings

One X-macro table as in The Black Wall, driving defaults, clamping, the
registry (`HKCU\Software\TheBlackHole`), the CLI and the dialog.

### Motion and camera
| Setting | Range | Default |
|---|---|---|
| Side movement | on/off | on |
| Movement speed | 0-100 | 30 |
| Mouse controlled rotation | on/off | on |
| Exit on mouse move | on/off | on |
| Mouse move sensitivity | 0-100 | 50 |
| Mouse click resets view | on/off | off |
| Exit on any button | on/off | off |
| 360 rotation | on/off | on |
| 360 mode | orbit / yaw / tumble | orbit |
| 360 duration | 3-999 s | 120 s |
| Warp timer | 0 (off) - 120 min | 15 min |

### Particles
| Setting | Range | Default |
|---|---|---|
| Star count | 1000-20000 | 5500 |
| Particle size | 1-50 px | 3 px |
| Particle ghost tail | 0-100 | 30 |
| Particle blur | 0-100 | 20 |
| Star twinkle | 0-100 | 15 |

Star brightness follows a realistic magnitude distribution - a few blazing,
most faint. The sky is **fixed**: the same stars every warp, so a star can be
recognised and watched as it is smeared into an arc. Disk and dust particles
scale proportionally from **Particle size**.

### Colour (default palette: *Interstellar*)
| Setting | Default |
|---|---|
| Stars | cold blue-white |
| Space | near-black |
| Accretion disk | warm amber-white |
| Photon ring | white-gold |
| Shadow fill | near-black, tintable |
| Nebula | scene-dependent |

*Event Horizon colour* resolves into **two** settings, both present: the
**photon ring** (the blazing thread at the shadow's edge) and the **shadow
fill** (drawn as a tinted near-black rather than pure black, so the hole reads
as a hole and not as a dead region on an OLED panel).

### Physics
Spin (0-0.999) · Lensing strength (0.5x-3.0x) · Doppler mode (Interstellar /
true relativistic) · Gravitational redshift · Higher-order images ·
Time dilation · Jets · Merger completes · Fall-in · Tidal flare

### Scenes
Ten checkboxes, hidden behind `Ctrl+Alt+S` in the dialog.

### Camera damage
Enable · Grace period before any damage (0-30 min, default 2 min) ·
Heal time (default 45 s) · Glass damage · Matrix damage ·
Matrix palette (white / greenish-purple / black / custom)

### Power
Quality (auto-detected on first run, slider overrides) · Frame rate limit
(10-120, default 60)

---

## 8. Status

Built, running and verified on an Intel Iris Xe at 1280x720/60 fps, quality 80:

* the engine port - window, input, frame pacing, `/s` `/p` `/c` `/w`, PNG dump,
  the full command line
* the star field: point sprites into a sky cube map, with size, twinkle,
  magnitude distribution, a galactic band and the fixed seed
* the geodesic pass: horizon, shadow, photon ring, Einstein ring, star arcs,
  the accretion disk with higher-order images, Doppler beaming, gravitational
  redshift, sky aberration for the warp
* the camera: orbit, the three idle rotation modes, look-around, the warp
  state machine, coverage-to-radius
* quality: tiers, offscreen scaling, and the adaptive loop that settles itself
  against the frame budget and stores the result
* camera damage: glass cracks with refraction, dead and stuck pixels, dead
  rows and columns, sensor bloom, rolling-shutter tear, the grace period and
  the healing
* the settings dialog, all 43 settings, with the Ctrl+Alt+S scene section

Scenes working: **void**, **feeding ended**, **near evaporation**,
**galactic core**. The other six are dealt from the deck and configured, but
fall through to a plain lensed hole because what makes them distinct is not
written yet.

Not started:

* **the binary scenes.** Two centres break the single-plane assumption the
  whole integrator rests on, so they need a second code path: a Cartesian
  march with `a = -1.5 h^2 x / r^5` per hole, superposed. Roughly twice the
  cost, and the one piece of real new physics left.
* **the tidal stream** (feeding in progress), **the stardust flashes**, and
  **the nebula backdrop**. The last two are cheap: flashes are a sparse
  variant of the disk crossing already written, and a nebula is a function of
  the escape direction.
* **the four events**: merger, fall-in, jets, tidal flare.
* **the in-screensaver overlay picker.** Ctrl+Alt+S currently steps to the
  next enabled scene instead of listing them.
* **Linux / XScreenSaver: written, not yet run on Linux.** `platform_linux.c`
  (settings in `~/.config/theblackhole/settings.conf`), the embed mode that
  adopts XScreenSaver's window through `$XSCREENSAVER_WINDOW`, the settings
  page `res/linux/theblackhole.xml`, and the two `*-linux.sh` scripts. It has
  been syntax-checked but not yet compiled or run on a Linux machine.

## 9. Open

* **"Side movement"** carries over by name from The Black Wall, where it was
  automatic drift along an infinite wall. Here it is read as a slow lateral
  orbital drift of the camera. Worth confirming that is the intent, since it
  now overlaps with the 360 degree orbit mode.
* Nebula colour defaults per scene are not yet chosen.
* Key bindings for the fall-in summon and the overlay picker are provisional
  (`Ctrl+Alt+S` picker is fixed; fall-in summon proposed as `Ctrl+Alt+F`).
* Silent. No audio.
