#version 330 core
// The black hole itself: for every pixel, where did the light arriving here
// actually come from?
//
// Geometric units with M = 1, so the horizon sits at r = 2, the photon sphere
// at r = 3, and the shadow's edge at an impact parameter of 3*sqrt(3) = 5.196.
//
// A null geodesic in a Schwarzschild field stays in the plane through the
// origin spanned by the camera position and the ray direction, so the whole
// three-dimensional problem collapses to one ordinary differential equation
// in that plane:
//
//     d2u/dphi2 = -u + 3*u^2,   u = 1/r
//
// integrated with velocity Verlet, one force evaluation per step. Everything
// falls out of that: the Einstein ring, the photon sphere, the disk's far side
// wrapped over the top and under the bottom of the shadow, and the secondary
// and tertiary images just outside it. Nothing here is a screen-space warp.
//
// Everything the hole is feeding on lives in one plane - the disk, the tidal
// stream, the scooped rubble - so all of it rides on the same plane-crossing
// test and is lensed for free. Two holes do not fit in a plane at all; that
// case has its own shader (binary.frag).

uniform samplerCube uStars;
uniform vec3  uCamPos;        // camera position, units of M
uniform mat3  uCamBasis;      // right, up, forward
uniform float uTanHalfFov;
uniform float uAspect;

uniform int   uSteps;         // integration steps (the Quality setting)
uniform float uLensing;       // artistic multiplier on the deflection
uniform float uEscapeR;       // treat the ray as free once it is out this far
uniform float uSkyLod;        // explicit cube map mip level (see the sky sample)

uniform vec3  uDiskNormal;    // the spin axis; the disk lies perpendicular
uniform vec3  uDiskX;         // in-plane basis, for the azimuth
uniform vec3  uDiskY;
// What lies in the disk plane:
//   0 nothing   1 an accretion disk   2 scattered flashes   3 disk + tidal stream
uniform int   uDiskMode;
uniform float uOuterReach;    // furthest crossing radius worth testing, in M
uniform float uDiskInner;     // ISCO, which moves in as the hole spins up
uniform float uDiskOuter;
uniform float uDiskBright;
uniform float uDiskTime;
uniform int   uDoppler;       // 0 = the movie's even glow, 1 = true beaming
uniform float uRedshift;      // 1 = light climbing out of the well reddens
uniform float uDiskShear;     // lateral camera travel, dragged into the pattern

// The tidal stream, and the star coming apart at the far end of it.
uniform float uStarR;         // the doomed star's distance, in M
uniform float uStarAz;        // and its bearing
uniform float uStreamWind;    // how tightly the thread winds on its way in

uniform vec3  uDiskColor;
uniform vec3  uRingColor;
uniform vec3  uShadowColor;
uniform float uRingGlow;

// The warp. Arriving at a hole is a boost, so the sky is aberrated: the stars
// crowd into a bright cone ahead and thin out behind. Applied to the ray's
// escape direction, which is the physically right place for it - the geodesic
// is unchanged, only which part of the sky that direction lands on.
uniform float uBoost;         // 0 = at rest, ->1 = the arrival streak
uniform vec3  uBoostDir;

in  vec2 vUV;
out vec4 frag;

// --- small helpers --------------------------------------------------------
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i), b = hash21(i + vec2(1, 0));
    float c = hash21(i + vec2(0, 1)), d = hash21(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Banded turbulence: the disk is sheared into filaments by its own rotation.
//
// The noise is sampled on a circle rather than along a straight angular axis.
// That makes it periodic in the azimuth by construction, so there is no seam
// where the angle wraps, and - the reason it matters - the coordinates stay
// small. Feeding the raw angle in instead quantises the hash into visible
// blocks, because by the time anyone is watching, the disk has turned some
// thousands of radians and a float has run out of mantissa.
float disk_texture(float radius, float angle) {
    float v = 0.0, amp = 0.5, fr = 1.0;
    // a radius-dependent phase winds the filaments into a spiral
    float a = angle + radius * 1.9;
    vec2 dir = vec2(cos(a), sin(a));
    for (int i = 0; i < 4; ++i) {
        vec2 c = dir * (3.0 * fr) + vec2(radius * 0.9, radius * 0.31) * fr;
        v += amp * vnoise(c);
        amp *= 0.5;
        fr *= 2.3;
    }
    return v;
}

// Noise that is periodic around the disk.
//
// The azimuth comes out of atan() in -pi..pi, and it JUMPS across that
// boundary. Feed it straight into a noise function - worse, into one scaled by
// some fraction of a turn, so the jump is not even a whole number of them - and
// the pattern does not line up with itself where it wraps. The result is a hard
// radial seam cutting right across the disk, in the same place, forever.
// Sampling on a circle removes the possibility: cos and sin of the angle are
// continuous across the wrap, so there is no boundary left to line up.
float az_noise(float az, float k, float radial) {
    return vnoise(vec2(cos(az), sin(az)) * k + vec2(radial, radial * 0.37));
}

// A hot body seen through a Doppler shift: blue and bright coming at us, red
// and dim going away. `shift` is the ratio of observed to emitted frequency.
vec3 doppler_tint(vec3 base, float shift) {
    float s = clamp(shift, 0.25, 4.0);
    vec3 warm = vec3(1.00, 0.42, 0.12);
    vec3 cold = vec3(0.62, 0.78, 1.00);
    return base * mix(warm, cold, clamp((s - 0.6) / 1.2, 0.0, 1.0));
}

// ---------------------------------------------------------------------------
// The accretion disk, and the tidal stream feeding it, at a plane crossing.
vec3 disk_emission(float rc, vec3 p) {
    float az = atan(dot(p, uDiskY), dot(p, uDiskX));

    // Keplerian shear: the inner disk laps the outer one, and it does so fast.
    // This is gas at a large fraction of the speed of light; a sedate drift
    // reads as weather, not as a disk tearing itself apart.
    float omega = pow(rc, -1.5);
    float spin_t = uDiskTime * omega;

    // Rotation alone is nearly invisible, and that is not a bug in the speed.
    // The pattern is stretched into filaments that run ALONG the flow, so
    // turning it slides every streak along its own length - the barber-pole
    // problem - and it reads as a still photograph however fast it goes.
    // What you actually see in a disk is the gas spiralling IN. Feeding the
    // drift into the radius moves the pattern across its own filaments,
    // through a brightness envelope that stays put, and it starts falling.
    float inflow = uDiskTime * 0.85;
    float t = disk_texture(rc + inflow, az + spin_t * 16.0 + uDiskShear);
    t = mix(t, disk_texture(rc * 1.7 + 9.0 - inflow * 1.6,
                            az + spin_t * 31.0), 0.38);

    // Hot and fierce at the inner edge, fading to nothing at the rim. The fade
    // has to reach zero: a brightness floor lights the whole outer disk evenly,
    // and since the camera sits inside the rim that fills half the sky with
    // flat gas and leaves nowhere for the shadow or the stars to show.
    float x = (rc - uDiskInner) / max(uDiskOuter - uDiskInner, 1e-3);
    float profile = pow(clamp(1.0 - x, 0.0, 1.0), 2.2) *
                    smoothstep(0.0, 0.06, x) *
                    smoothstep(1.0, 0.82, x);

    // The disk is the brightest thing in the sky by a long way, so it is driven
    // well past 1.0 here. Held to a "correct" exposure it reads as brown smoke,
    // which is the one thing an accretion disk never looks like. The shoulder
    // at the end of main() keeps the excess from clipping into a flat slab.
    float e = 3.2 * uDiskBright * profile * (0.30 + 1.7 * t * t);

    // Flares, and compact knots with an edge to them. Smooth turbulence
    // averages out to something the eye reads as static; these give it
    // something to follow.
    float fl = az_noise(az + spin_t * 9.0, 2.4, rc * 0.5 + uDiskTime * 0.7);
    e *= 0.72 + 1.5 * fl * fl;
    float knot = az_noise(az + spin_t * 22.0, 6.0, rc * 1.7 + inflow * 2.0);
    e *= 1.0 + 2.6 * pow(clamp(knot - 0.55, 0.0, 1.0) * 2.2, 2.0);

    // Temperature across the disk: white-hot at the inner edge - the part that
    // looks like it would burn you - falling through the disk's own colour to a
    // deep ember at the rim. One flat tint is what makes a disk read as painted
    // on rather than as something with a furnace in the middle of it.
    vec3 hot  = vec3(1.00, 0.96, 0.90);
    vec3 cool = uDiskColor * vec3(0.80, 0.34, 0.13);
    float h = pow(clamp(1.0 - x, 0.0, 1.0), 1.3);
    vec3 tint = h > 0.5 ? mix(uDiskColor, hot, (h - 0.5) * 2.0)
                        : mix(cool, uDiskColor, h * 2.0);

    // --- the tidal stream --------------------------------------------------
    // A star caught too close is not swallowed, it is unwound: the near side
    // falls faster than the far side, and the whole thing is drawn out into a
    // thread that wraps the hole again and again before it joins the disk.
    // The thread is what carries the sense of scale. It reaches hundreds of
    // gravitational radii out to the star still coming apart at the end of it,
    // and the eye can follow it the whole way in.
    if (uDiskMode == 3) {
        // Nearly radial far out, winding tighter and tighter as it falls -
        // which is what the orbital clock does to it, the inner gas lapping
        // the outer gas the whole way down.
        float a_s = uStarAz
                  + uStreamWind * (inversesqrt(max(rc, 1.0)) -
                                   inversesqrt(max(uStarR, 1.0))) * 34.0
                  + uDiskTime * 0.10;
        float d = az - a_s;
        d = atan(sin(d), cos(d));                  // shortest way round
        // Thin where it is still falling freely, spreading as it piles in.
        float wid = 0.045 + 0.85 / max(rc, 1.0);
        float across = exp(-(d * d) / (wid * wid));
        // Thins with distance but never vanishes, so the far end stays
        // followable against the sky.
        float along = 0.22 + 0.78 / (1.0 + rc * 0.035);
        float lumpy = 0.45 + 1.3 * az_noise(a_s * 3.0 + uDiskTime * 0.5, 4.0, rc * 0.6);
        float se = 4.2 * across * along * lumpy *
                   smoothstep(uDiskInner * 0.9, uDiskInner * 1.6, rc);

        // The star itself: still bright enough to be a point, but already
        // drawn out along the thread it is feeding.
        float dr = (rc - uStarR) / max(uStarR * 0.06, 1.0);
        se += 26.0 * exp(-dr * dr) * exp(-(d * d) / (wid * wid * 4.0));

        float mixv = clamp(se / max(e + se, 1e-3), 0.0, 1.0);
        tint = mix(tint, mix(uDiskColor, vec3(1.0, 0.94, 0.86), 0.45), mixv);
        e += se;
    }

    // Light climbing out of the well arrives redder and dimmer.
    float grav = sqrt(max(1.0 - 2.0 / rc, 0.02));
    if (uDoppler == 1) {
        vec3 vhat = normalize(cross(uDiskNormal, p));
        float v = 1.0 / sqrt(rc);
        float mu = dot(vhat, normalize(uCamPos - p));
        float gamma = inversesqrt(max(1.0 - v * v, 1e-4));
        float shift = 1.0 / (gamma * (1.0 - v * mu));
        e *= pow(shift, 3.0);                      // relativistic beaming
        tint = doppler_tint(tint, shift);
        if (uRedshift > 0.5) {
            e *= grav;
            tint *= mix(vec3(1.0), vec3(1.0, 0.72, 0.5), 1.0 - grav);
        }
    } else if (uRedshift > 0.5) {
        e *= mix(1.0, grav, 0.6);
    }
    return tint * e;
}

// Rubble the hole has scooped up: no disk, just flashes. Each knot lights as it
// is compressed, fades, and is carried round by the same Keplerian clock the
// disk uses - so the flashes bend with the light and drift with the gravity
// exactly as the disk would, which is the whole reason for putting them in the
// plane instead of painting them onto the screen.
vec3 flash_emission(float rc, vec3 p) {
    float az = atan(dot(p, uDiskY), dot(p, uDiskX));
    float spin_t = uDiskTime * pow(rc, -1.5);
    float ang = az - spin_t * 16.0;                // into the co-rotating frame

    float n    = az_noise(ang, 4.2, rc * 0.8);
    float seed = az_noise(ang, 4.2, rc * 0.8 + 57.0);
    // Each knot keeps its own phase, so they do not all flash together.
    float ph  = fract(uDiskTime * 0.22 + seed);
    float env = pow(clamp(sin(ph * 3.14159265), 0.0, 1.0), 4.0);
    float knot = pow(clamp(n - 0.60, 0.0, 1.0) * 2.9, 2.0);

    // Only in the band where the bending is worth seeing them against.
    float band = smoothstep(uDiskInner * 0.9, uDiskInner * 1.5, rc) *
                 (1.0 - smoothstep(uOuterReach * 0.55, uOuterReach, rc));
    float e = 11.0 * knot * env * band;
    if (e <= 0.0) return vec3(0.0);

    float hot = clamp(1.0 - (rc - uDiskInner) / 26.0, 0.0, 1.0);
    vec3 tint = mix(uDiskColor, vec3(1.0, 0.97, 0.92), hot * hot);
    if (uRedshift > 0.5) e *= mix(1.0, sqrt(max(1.0 - 2.0 / rc, 0.02)), 0.6);
    return tint * e;
}

// ---------------------------------------------------------------------------
void main() {
    // --- the ray this pixel is asking about -------------------------------
    // uCamBasis' columns are right, up and forward, so the screen's centre is
    // +forward, not -forward as in a classic view matrix.
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamBasis * vec3(ndc.x * uTanHalfFov * uAspect,
                                          ndc.y * uTanHalfFov,
                                          1.0));

    float r0 = length(uCamPos);
    vec3  e1 = uCamPos / r0;                 // radial, the plane's first axis
    float dr = dot(dir, e1);
    vec3  tangential = dir - e1 * dr;
    float dt = length(tangential);

    vec3 accum = vec3(0.0);
    float min_r = r0;                        // how close the ray ever came

    // A perfectly radial ray has no orbital plane: it either leaves or falls.
    if (dt < 1e-6) {
        if (dr < 0.0) { frag = vec4(uShadowColor, 1.0); return; }
        frag = vec4(textureLod(uStars, dir, uSkyLod).rgb, 1.0);
        return;
    }
    vec3 e2 = tangential / dt;               // the plane's second axis

    // Whether the geodesic crosses the disk plane depends only on the angle,
    // not on the radius: h(phi) = r(phi) * (na*cos(phi) + nb*sin(phi)).
    float na = dot(e1, uDiskNormal);
    float nb = dot(e2, uDiskNormal);

    float u  = 1.0 / r0;
    float du = -dr / (r0 * dt);
    float phi = 0.0;
    float dphi = 3.3 * 3.14159265 / float(uSteps);
    float k = 3.0 * uLensing;                // the deflection term
    float acc = -u + k * u * u;

    float g_prev = na;                       // sign of the disk-plane offset
    float r_prev = r0;
    bool captured = false;
    bool escaped  = false;
    bool at_infinity = false;

    for (int i = 0; i < uSteps; ++i) {
        // Does the ray reach infinity inside this step? Out there u'' = -u is
        // negligible and u runs almost exactly linearly in phi, so the crossing
        // u = 0 is found analytically instead of by overshooting it. Stepping
        // past it and reading the sky from the overshot angle is worth several
        // degrees of error - enough to move every star.
        if (du < 0.0 && u + du * dphi <= 0.0) {
            phi += -u / du;
            escaped = true;
            at_infinity = true;
            break;
        }
        // velocity Verlet on d2u/dphi2 = -u + 3*L*u^2
        u  += du * dphi + 0.5 * acc * dphi * dphi;
        float acc_new = -u + k * u * u;
        du += 0.5 * (acc + acc_new) * dphi;
        acc = acc_new;
        phi += dphi;

        if (u >= 0.5) { captured = true; break; }   // inside the horizon
        if (u <= 0.0) { escaped = true; break; }    // numerically out
        float r = 1.0 / u;
        min_r = min(min_r, r);

        // --- whatever is orbiting in the plane ----------------------------
        float g = na * cos(phi) + nb * sin(phi);
        if (uDiskMode != 0 && g * g_prev < 0.0) {
            // linear crossing between the last step and this one
            float f  = g_prev / (g_prev - g);
            float rc = mix(r_prev, r, f);
            if (rc > uDiskInner && rc < uOuterReach) {
                float pc = phi - dphi * (1.0 - f);
                vec3 p = rc * (cos(pc) * e1 + sin(pc) * e2);
                accum += uDiskMode == 2 ? flash_emission(rc, p)
                                        : disk_emission(rc, p);
            }
        }
        g_prev = g;
        r_prev = r;

        if (r > uEscapeR && du < 0.0) { escaped = true; break; }
    }

    // --- the far half of the thread ---------------------------------------
    // The integrator cannot reach it. A ray heading outward is taken off the
    // books the moment it is bound for infinity - which is right, and which is
    // what keeps the shader fast - but the stream runs out to a star three
    // hundred gravitational radii away, and the ray is declared free long
    // before it gets there. Out at those radii the bending is negligible
    // anyway, so the crossing is found on the straight line instead: exact
    // where it matters, and free.
    if (uDiskMode == 3 && !captured) {
        float dn = dot(dir, uDiskNormal);
        if (abs(dn) > 1e-4) {
            float tt = -dot(uCamPos, uDiskNormal) / dn;
            if (tt > 0.0) {
                vec3 p = uCamPos + dir * tt;
                float rc = length(p);
                // only beyond where the integrator already looked
                if (rc > uDiskOuter && rc < uOuterReach)
                    accum += disk_emission(rc, p);
            }
        }
    }

    // The emission is driven hard on purpose, so it needs a shoulder rather
    // than a clip. The shoulder is taken from the brightest channel and the
    // other two ride along on the same factor: compressing each channel
    // separately washes the colour out as it brightens, which is how gas that
    // should be fierce gold ends up a pale cream. This way the hue survives all
    // the way up, and only the very hottest clips - to white, which is what an
    // overexposed hot body does.
    float lum = max(max(accum.r, accum.g), accum.b);
    if (lum > 1e-4) accum *= (1.0 - exp(-lum * 1.05)) * 1.12 / lum;

    // --- what the ray finally saw ----------------------------------------
    if (captured) {
        // The shadow is not pure black: a tint keeps it reading as a hole in
        // the sky rather than as a dead region of the panel.
        frag = vec4(uShadowColor + accum, 1.0);
        return;
    }

    vec3 sky = vec3(0.0);
    if (escaped || u > 0.0) {
        float r = 1.0 / max(u, 1e-9);
        vec3 pos_dir = cos(phi) * e1 + sin(phi) * e2;
        vec3 tan_dir = -sin(phi) * e1 + cos(phi) * e2;
        // At infinity the direction of travel is the radial direction; short
        // of it, it is the tangent to the orbit.
        vec3 exit = at_infinity ? pos_dir
                                : normalize((-du * r) * pos_dir + tan_dir);
        float boost_gain = 1.0;
        if (uBoost > 0.001) {
            // Relativistic aberration about the direction of travel:
            //   cos(a') = (cos(a) + b) / (1 + b*cos(a))
            float b = clamp(uBoost, 0.0, 0.999);
            float ca = dot(exit, uBoostDir);
            vec3  perp = exit - uBoostDir * ca;
            float lp = length(perp);
            float ca2 = (ca + b) / (1.0 + b * ca);
            float sa2 = sqrt(max(1.0 - ca2 * ca2, 0.0));
            exit = normalize(uBoostDir * ca2 + (lp > 1e-6 ? perp / lp : vec3(0.0)) * sa2);
            // ... and the headlight effect that goes with it
            float d = 1.0 / (sqrt(max(1.0 - b * b, 1e-4)) * (1.0 - b * ca2));
            boost_gain = clamp(pow(d, 2.0), 0.15, 12.0);
        }
        // textureLod, not texture: neighbouring pixels leave the loop after
        // different numbers of steps, so the screen-space derivatives that
        // automatic mip selection depends on are meaningless here. Left to
        // itself the driver picks a near-top mip in exactly the places where
        // the ray paths diverge, and the sky breaks into flat blocks.
        sky = textureLod(uStars, exit, uSkyLod).rgb * boost_gain;
    }

    // The photon sphere, given a little help so it reads on screen: rays that
    // grazed r = 3 have wound around the hole and arrive piled together.
    float ring = uRingGlow * exp(-pow((min_r - 3.0) * 1.7, 2.0));

    frag = vec4(sky + accum + uRingColor * ring, 1.0);
}
