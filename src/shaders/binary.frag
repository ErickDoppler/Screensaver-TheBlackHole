#version 330 core
// Two black holes, about to merge.
//
// This cannot use the trick the single-hole shader is built on. There, a null
// geodesic stays in one plane - the one spanned by the camera and the ray - so
// the whole problem collapses to a single ODE in that plane and costs almost
// nothing. Put a second mass anywhere off that plane and the geodesic leaves
// it immediately. There is no plane to reduce to, and no closed form either:
// the two-centre problem in general relativity does not have one.
//
// So this marches in full three dimensions instead, with the Cartesian form of
// the Schwarzschild null geodesic:
//
//     d2x/dl2  =  -3 * m * h^2 * x / |x|^5,     h = |x cross v|
//
// which is exactly equivalent to the Binet equation the other shader solves,
// and which can be superposed. Check on the constant: for a circular photon
// orbit |a| = |v|^2 / R with |v| = 1 and h = R, giving 3m/R^2 = 1/R, so R = 3m
// - the photon sphere at three gravitational radii, as it should be.
//
// Superposing two of these is an approximation - h is only conserved about a
// single centre - but it is the right approximation: each hole bends light
// correctly on its own, the shared figure between them comes out of the sum,
// and the error is smallest exactly where the eye is looking, which is near
// one hole or the other.

uniform samplerCube uStars;
uniform vec3  uCamPos;
uniform mat3  uCamBasis;
uniform float uTanHalfFov;
uniform float uAspect;

uniform int   uSteps;
uniform float uLensing;
uniform float uSkyLod;
uniform float uEscapeR;

// The pair. Positions are updated on the CPU every frame as they wind in.
uniform vec3  uHoleA, uHoleB;
uniform float uMassA, uMassB;

// A circumbinary disk, when they are feeding. It lies in the orbital plane and
// is warped by the pair beneath it.
uniform int   uDiskMode;      // 0 none, 1 circumbinary disk, 2 burning rubble,
                              // 4 a disk round each hole, bridged across the gap
uniform vec3  uDiskNormal, uDiskX, uDiskY;
uniform float uDiskInner, uDiskOuter, uDiskBright, uDiskTime;
uniform float uRedshift;

uniform vec3  uDiskColor;
uniform vec3  uRingColor;
uniform vec3  uShadowColor;
uniform float uRingGlow;
uniform float uBoost;
uniform vec3  uBoostDir;

in  vec2 vUV;
out vec4 frag;

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

// periodic in the azimuth, so there is no seam where atan() wraps
float az_noise(float az, float k, float radial) {
    return vnoise(vec2(cos(az), sin(az)) * k + vec2(radial, radial * 0.37));
}

// Banded turbulence, as the single-hole disk uses: the gas is sheared into
// filaments that run along the flow. Sampled on a circle so it is periodic in
// the azimuth and the coordinates stay small however long the disk has turned.
float disk_texture(float radius, float angle) {
    float v = 0.0, amp = 0.5, fr = 1.0;
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

// A hot body seen through a Doppler shift: blue and bright coming at us, red
// and dim going away.
vec3 doppler_tint(vec3 base, float shift) {
    float s = clamp(shift, 0.25, 4.0);
    return base * mix(vec3(1.00, 0.42, 0.12), vec3(0.62, 0.78, 1.00),
                      clamp((s - 0.6) / 1.2, 0.0, 1.0));
}

// The pull of one centre, with its own h^2 = |d x v|^2 about that centre.
vec3 pull(vec3 d, vec3 v, float m) {
    float r2 = dot(d, d);
    if (r2 < 1e-4) return vec3(0.0);
    vec3 c = cross(d, v);
    return -(3.0 * uLensing * m * dot(c, c) / (r2 * r2 * sqrt(r2))) * d;
}

// Knots of scooped rubble burning in the pair's plane, the same list the
// single-hole shader draws: (orbital radius, azimuth at ignition, age in
// seconds, age on the disk clock). Out here they orbit the pair's common
// centre, well outside the two holes, because nothing survives between them.
#define BH_FLARE_MAX 24
uniform vec4  uFlare[BH_FLARE_MAX];
uniform int   uFlareCount;
uniform float uFlareLife;
uniform float uFlareSpin;   // how fast a knot is wound, set per scene

// One knot, sheared along its orbit and heating past the visible band as it
// goes. See the same function in lens.frag for what each part is doing.
vec3 flare_emission(float rc, vec3 p) {
    if (uFlareCount <= 0) return vec3(0.0);
    float az = atan(dot(p, uDiskY), dot(p, uDiskX));
    float omega = pow(max(rc, 1.0), -1.5) * uFlareSpin;

    vec3 sum = vec3(0.0);
    for (int i = 0; i < uFlareCount; ++i) {
        vec4 f = uFlare[i];
        float k = clamp(f.z / uFlareLife, 0.0, 1.0);

        /* the stripe thickens with its own radius, so a knot a hundred
           radii out is a band of gas and not a wire */
        float wr = f.x * mix(0.025, 0.11, k) + 0.4;
        float dr = (rc - f.x) / wr;
        float radial = exp(-dr * dr);
        if (radial < 0.003) continue;

        float d = az - (f.y + omega * f.w);
        d = atan(sin(d), cos(d));
        float wid = mix(0.45, 0.14, smoothstep(0.0, 0.3, k));
        float along = exp(-(d * d) / (wid * wid));
        along *= 0.55 + 0.9 * az_noise(f.y + omega * f.w + d * 3.0, 3.5, f.x + f.w * 0.2);

        float spark = exp(-f.z * f.z * 26.0) * exp(-(d * d) / 0.02);
        float vis = (1.0 - k) * exp(-3.4 * k * k);
        float e = (7.5 * along + 34.0 * spark) * radial * vis;
        if (e <= 0.0) continue;

        vec3 warm   = mix(uDiskColor, vec3(1.0, 0.72, 0.38), 0.5);
        vec3 tint = mix(warm, vec3(1.0, 0.97, 0.94), smoothstep(0.0, 0.18, k));
        tint = mix(tint, vec3(0.62, 0.80, 1.00), smoothstep(0.20, 0.62, k));
        tint = mix(tint, vec3(0.60, 0.45, 1.00), smoothstep(0.62, 1.00, k));

        if (uRedshift > 0.5)
            e *= mix(1.0, sqrt(max(1.0 - 2.0 * uMassA / max(rc, 2.1), 0.02)), 0.6);
        sum += tint * e;
    }
    return sum;
}

// One disk, shared.
//
// Two holes this close do not each keep a disk: they clear a cavity and sit
// inside one enormous sheet that belongs to both of them. The pair is a
// rotating pair of masses, so it drives the sheet rather than just orbiting in
// it, and three things come out of that - all of them visible in the picture:
//
//   * TWO SPIRAL ARMS, because a binary's potential has two lobes. They are
//     the pattern the pair carves as it turns, so they turn with the pair and
//     wind outward rather than sitting still.
//   * A BRIGHT CORE, where streams reach off the cavity wall and feed each
//     hole. The two curl in opposite directions round their own hole, which is
//     what makes the middle read as an S rather than as a blob.
//   * RIPPLES, running outward across the whole sheet. Each pass of the pair
//     kicks the gas again, so the wake is a train of rings - the only thing in
//     the picture that says the two shadows at the centre are doing work on
//     everything around them.
//
// The sheet is cool: it is enormous, and gas that far out has had time to
// spread and to radiate. So the colour runs from a cream-white core, through
// gold where the arms are still being squeezed, to a grey-teal at the rim.
vec3 shared_disk(vec3 p, float rc) {
    float az = atan(dot(p, uDiskY), dot(p, uDiskX));
    float sep = max(length(uHoleB - uHoleA), 1e-3);

    // Where the pair is pointing now: everything the binary drives is keyed to
    // this, so the whole figure turns with the orbit.
    vec3 toA = uHoleA - uDiskNormal * dot(uHoleA, uDiskNormal);
    float azA = atan(dot(toA, uDiskY), dot(toA, uDiskX));

    vec3 sum = vec3(0.0);

    // --- the middle: a whirlpool round each hole ----------------------------
    // Each hole has wound the gas nearest it into a spiral of its own, and the
    // two spirals meet where the holes face each other. They curl the way the
    // pair turns, so the pair reads as one S-shaped figure rather than as two
    // unrelated eddies - and which way the S lies follows the orbit.
    if (rc < uDiskInner * 1.45) {
        for (int i = 0; i < 2; ++i) {
            vec3 hole = i == 0 ? uHoleA : uHoleB;
            float m = i == 0 ? uMassA : uMassB;
            vec3 d = p - hole;
            d -= uDiskNormal * dot(d, uDiskNormal);
            float rl = length(d);
            float horizon = 2.6 * m;
            float reach = sep * 0.78;
            if (rl < horizon || rl > reach) continue;

            // Two trailing arms, wound as a log spiral. The sign of the log
            // term is what sets the curl; it follows the rotation, so the
            // whole figure turns the way the pair does.
            float al = atan(dot(d, uDiskY), dot(d, uDiskX));
            float wind = 2.0 * al + 3.1 * log(max(rl / horizon, 1.0)) - 2.0 * azA;
            float arm = pow(0.5 + 0.5 * cos(wind), 1.5);

            float x = clamp((rl - horizon) / max(reach - horizon, 1e-3), 0.0, 1.0);
            float body = smoothstep(0.0, 0.06, x) * pow(1.0 - x, 1.1);
            float turb = disk_texture(rl / max(m, 0.05) * 0.25 + uDiskTime * 0.06,
                                      al + 1.2 * log(max(rl, 1.0)));
            float e = 1.15 * uDiskBright * body * (0.22 + 0.5 * turb + 1.5 * arm);

            // The bands nearest the hole are the hottest gas in the picture
            // and read blue-white; further out they cool through cream into
            // the gold of the sheet.
            vec3 hot  = vec3(0.80, 0.90, 1.00);
            vec3 warm = vec3(1.00, 0.88, 0.68);
            vec3 tint = mix(hot, warm, smoothstep(0.05, 0.55, x));
            tint = mix(tint, mix(uDiskColor, vec3(1.0, 0.72, 0.42), 0.5),
                       smoothstep(0.45, 1.0, x));
            sum += tint * e;
        }
    }

    // --- the sheet ----------------------------------------------------------
    if (rc > uDiskInner) {
        float x01 = clamp((rc - uDiskInner) / max(uDiskOuter - uDiskInner, 1e-3), 0.0, 1.0);

        // Two arms, winding outward as a log spiral and carried round by the
        // pair. Logarithmic because that is what a pattern driven at one radius
        // and sheared by a Keplerian flow becomes.
        float wind = 2.0 * (az - azA) - 3.4 * log(max(rc / uDiskInner, 1.0));
        float arms = 0.5 + 0.5 * cos(wind);
        arms = pow(arms, 2.4);

        // Ripples: every turn of the pair sends another crest out through the
        // sheet, so they are evenly spaced in radius and travel outward.
        float wave = 6.283185 * (rc / (uDiskInner * 0.62) - uDiskTime * 0.22);
        float ripple = 0.5 + 0.5 * cos(wave);
        ripple = mix(1.0, ripple, 0.7 * smoothstep(0.02, 0.22, x01));

        // A little turbulence so the rings are gas and not a diffraction chart
        float t = disk_texture(rc / uDiskInner * 0.8 + uDiskTime * 0.05,
                               az + 0.6 * log(max(rc, 1.0)));

        float prof = smoothstep(0.0, 0.05, x01) * pow(1.0 - x01, 1.15);
        float e = 0.5 * uDiskBright * prof * ripple *
                  (0.22 + 0.45 * t + 1.9 * arms);

        // Cream at the cavity wall, gold along the arms, cool grey out at the
        // rim where the sheet is thin and old.
        vec3 core = vec3(1.00, 0.95, 0.86);
        vec3 gold = mix(uDiskColor, vec3(1.0, 0.82, 0.55), 0.55);
        vec3 cold = vec3(0.42, 0.56, 0.60);
        vec3 tint = mix(core, gold, smoothstep(0.0, 0.22, x01));
        tint = mix(tint, cold, smoothstep(0.12, 0.55, x01));
        // the arms are hotter than the gas between them
        tint = mix(tint, mix(tint, vec3(1.0, 0.93, 0.82), 0.6), arms);

        if (uRedshift > 0.5)
            e *= mix(1.0, sqrt(max(1.0 - 2.0 / max(rc, 2.1), 0.02)), 0.4);
        sum += tint * e;
    }
    return sum;
}

// Deflection from both centres at once. This is the whole physics of the
// scene: the shared gravitational figure is not drawn anywhere, it is simply
// what the sum of these two does to the light passing between them.
//
// Each term carries the angular momentum about its own centre, which makes
// either hole exact when the other is far away and keeps the sum smooth
// everywhere. Taking h^2 about "whichever hole dominates here" instead
// switched abruptly across the surface where the two pulls balance: rays a
// pixel apart were bent by very different amounts, and with two holes of
// similar mass that surface cuts straight through the lensed disk, tearing it
// into a saw-tooth.
vec3 accel(vec3 x, vec3 v) {
    return pull(x - uHoleA, v, uMassA) + pull(x - uHoleB, v, uMassB);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamBasis * vec3(ndc.x * uTanHalfFov * uAspect,
                                          ndc.y * uTanHalfFov, 1.0));
    vec3 x = uCamPos;
    vec3 v = dir;
    vec3 accum = vec3(0.0);

    // Step length scales with how far the ray is from the nearer hole: fine
    // where the field is steep, long out in the empty parts, which is what
    // makes a three-dimensional march affordable at all.
    float ra = length(x - uHoleA), rb = length(x - uHoleB);
    float near = min(ra, rb);

    bool captured = false;
    bool escaped = false;
    // closest approach, in units of each hole's own mass, for the photon rings
    float min_r = min(ra / max(uMassA, 1e-3), rb / max(uMassB, 1e-3));
    float prev_h = dot(x, uDiskNormal);

    for (int i = 0; i < uSteps; ++i) {
        ra = length(x - uHoleA);
        rb = length(x - uHoleB);
        // distance to the nearer horizon's scale, so a light hole gets as fine
        // a march as a heavy one
        float na = ra / max(uMassA, 1e-3), nb = rb / max(uMassB, 1e-3);
        near = min(ra, rb);
        min_r = min(min_r, min(na, nb));

        // Horizons. Each sits at twice its own mass.
        if (ra < 2.0 * uMassA || rb < 2.0 * uMassB) { captured = true; break; }
        if (length(x) > uEscapeR) { escaped = true; break; }

        // Long strides out in the empty parts, fine ones where the field is
        // steep. Without the long end, rays run out of steps before they
        // escape and end up sampling the sky along a half-finished
        // direction - which shows as ragged dark patches beside the holes.
        // Within a few photon-sphere radii the stride tightens further: a
        // coarse step there lands at a different phase for neighbouring rays,
        // and the difference shows as ripples along the lensed disk.
        float fine = mix(0.07, 0.20, smoothstep(6.0, 24.0, min(na, nb)));
        float step = clamp(near * fine, 0.05, 40.0);

        // velocity Verlet again, in Cartesian this time; the second force is
        // taken with the predicted direction, since h^2 depends on it
        vec3 a0 = accel(x, v);
        vec3 x_new = x + v * step + 0.5 * a0 * step * step;
        vec3 v_pred = normalize(v + a0 * step);
        vec3 a1 = accel(x_new, v_pred);
        v = normalize(v + 0.5 * (a0 + a1) * step);

        // --- whatever is orbiting the pair in their plane -------------------
        if (uDiskMode != 0) {
            float h_new = dot(x_new, uDiskNormal);
            if (h_new * prev_h < 0.0) {
                float f = prev_h / (prev_h - h_new);
                vec3 p = mix(x, x_new, f);
                float rc = length(p - uDiskNormal * dot(p, uDiskNormal));
                if (uDiskMode == 2) {
                    if (rc > uDiskInner && rc < uEscapeR) accum += flare_emission(rc, p);
                } else if (uDiskMode == 4) {
                    if (rc < uDiskOuter) accum += shared_disk(p, rc);
                } else if (rc > uDiskInner && rc < uDiskOuter) {
                    float az = atan(dot(p, uDiskY), dot(p, uDiskX));
                    float omega = pow(max(rc, 1.0), -1.5);
                    float st = uDiskTime * omega;
                    // The disk does not sit still over a pair winding in. It is
                    // pulled out of round by whichever hole is beneath it, and
                    // the bulge chases them round - the disk morphing with the
                    // orbit, which is the thing worth watching in this scene.
                    vec3 toA = uHoleA - uDiskNormal * dot(uHoleA, uDiskNormal);
                    float azA = atan(dot(toA, uDiskY), dot(toA, uDiskX));
                    float pull = cos(az - azA);
                    float warp = 1.0 + 0.55 * pull * exp(-(rc - uDiskInner) * 0.05);

                    float n = az_noise(az + st * 14.0, 3.0, rc * 0.5 + uDiskTime * 0.8);
                    float x01 = (rc - uDiskInner) / max(uDiskOuter - uDiskInner, 1e-3);
                    float prof = pow(clamp(1.0 - x01, 0.0, 1.0), 2.0) *
                                 smoothstep(0.0, 0.08, x01) *
                                 smoothstep(1.0, 0.80, x01);
                    float e = 3.0 * uDiskBright * prof * warp * (0.35 + 1.6 * n * n);
                    float hot = clamp(1.0 - x01, 0.0, 1.0);
                    vec3 tint = mix(uDiskColor * vec3(0.8, 0.35, 0.14),
                                    vec3(1.0, 0.96, 0.90), hot * hot);
                    if (uRedshift > 0.5)
                        e *= mix(1.0, sqrt(max(1.0 - 2.0 * uMassA / max(rc, 2.1), 0.02)), 0.6);
                    accum += tint * e;
                }
            }
            prev_h = h_new;
        }

        x = x_new;
    }

    float lum = max(max(accum.r, accum.g), accum.b);
    if (lum > 1e-4) accum *= (1.0 - exp(-lum * 1.05)) * 1.12 / lum;

    if (captured) { frag = vec4(uShadowColor + accum, 1.0); return; }

    vec3 exit = v;
    float boost_gain = 1.0;
    if (uBoost > 0.001) {
        float b = clamp(uBoost, 0.0, 0.999);
        float ca = dot(exit, uBoostDir);
        vec3 perp = exit - uBoostDir * ca;
        float lp = length(perp);
        float ca2 = (ca + b) / (1.0 + b * ca);
        float sa2 = sqrt(max(1.0 - ca2 * ca2, 0.0));
        exit = normalize(uBoostDir * ca2 + (lp > 1e-6 ? perp / lp : vec3(0.0)) * sa2);
        float d = 1.0 / (sqrt(max(1.0 - b * b, 1e-4)) * (1.0 - b * ca2));
        boost_gain = clamp(pow(d, 2.0), 0.15, 12.0);
    }
    vec3 sky = textureLod(uStars, exit, uSkyLod).rgb * boost_gain;

    // Each hole gets its own photon ring, and where the two figures overlap
    // between them the rings run together - which is the shared gravitational
    // figure made visible.
    float ring = uRingGlow * exp(-pow((min_r - 3.0) * 1.7, 2.0));
    frag = vec4(sky + accum + uRingColor * ring, 1.0);
}
