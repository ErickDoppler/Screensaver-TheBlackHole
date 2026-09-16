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
uniform int   uDiskMode;      // 0 = none, 1 = circumbinary disk
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

// The pull of one centre, with its own h^2 = |d x v|^2 about that centre.
vec3 pull(vec3 d, vec3 v, float m) {
    float r2 = dot(d, d);
    if (r2 < 1e-4) return vec3(0.0);
    vec3 c = cross(d, v);
    return -(3.0 * uLensing * m * dot(c, c) / (r2 * r2 * sqrt(r2))) * d;
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

        // --- the circumbinary disk ----------------------------------------
        if (uDiskMode == 1) {
            float h_new = dot(x_new, uDiskNormal);
            if (h_new * prev_h < 0.0) {
                float f = prev_h / (prev_h - h_new);
                vec3 p = mix(x, x_new, f);
                float rc = length(p - uDiskNormal * dot(p, uDiskNormal));
                if (rc > uDiskInner && rc < uDiskOuter) {
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
