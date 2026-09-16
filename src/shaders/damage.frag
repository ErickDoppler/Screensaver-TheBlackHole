#version 330 core
// Camera damage, applied to the finished frame. Descended from The Black Wall's
// signal-loss pass, but this camera is a physical object out in the open beside
// something violent, so the damage is to its glass and its sensor rather than
// to a video signal.
//
//   uGlass  0..1  cracks in the front element, which refract and bleed light
//   uMatrix 0..1  dead and stuck pixels, dead rows and columns, readout smear
//   uBloom  0..1  the sensor saturating, right after a flare
//   uTear   0..1  rolling-shutter tear, during a warp
//
// All four are driven by the simulation, not by this shader: something has to
// have happened for damage to appear, and the grace period keeps the picture
// clean for the first minutes whatever else is going on.

uniform sampler2D uTex;
uniform vec2  uResolution;
uniform float uTime;
uniform float uGlass;
uniform float uMatrix;
uniform float uBloom;
uniform float uTear;
uniform vec3  uDeadColor;     // the matrix palette
uniform float uGreenPurple;   // 1 = the two-tone palette, mixed per pixel
uniform vec2  uImpact;        // where the glass took the hit, in UV

in vec2 vUV;
out vec4 frag;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Distance to the nearest crack in a spiderweb centred on the impact: a set of
// radial fractures with a little wander in them, plus two concentric rings.
// Returns 0 on a crack, rising to 1 away from one. grow 0..1 is how far the
// web has spread: each fracture sets off after its own delay and runs outward,
// and a ring segment only shows once the fracture in its wedge has reached it,
// so a web creeps out of the impact and retracts into it again as it heals.
float web(vec2 d, float seed, float grow) {
    float r = length(d);
    float a = atan(d.y, d.x);
    // radial fractures, unevenly spaced so they do not read as a wheel
    const float SPOKES = 9.0;
    float k = a * SPOKES / 6.2831853;
    float cell = floor(k);
    float wobble = (hash12(vec2(cell, seed)) - 0.5) * 0.55;
    float da = abs(fract(k - wobble) - 0.5) / SPOKES * 6.2831853;
    // a fracture widens as it runs out from the impact, and stops
    float reach = 0.22 + 0.5 * hash12(vec2(cell, seed + 3.0));
    float delay = 0.45 * hash12(vec2(cell, seed + 7.0));
    float now = reach * clamp((grow - delay) / (1.0 - delay), 0.0, 1.0);
    float radial = da * r / max(0.004 + 0.02 * r, 1e-4);
    radial = mix(radial, 1e3, step(now, r));
    // two rings around the impact
    float ring = 1e3;
    for (int i = 1; i <= 2; ++i) {
        float rr = reach * (0.35 * float(i));
        if (rr < now) ring = min(ring, abs(r - rr) / 0.006);
    }
    return clamp(min(radial, ring), 0.0, 1.0);
}

void main() {
    vec2 uv = vUV;
    float t = uTime;

    // ---- rolling shutter: the sensor reads out row by row, and during a warp
    // the scene has moved by the time the bottom rows are read --------------
    if (uTear > 0.0) {
        float row = uv.y;
        uv.x += (row - 0.5) * 0.035 * uTear * (0.6 + 0.4 * sin(t * 2.3));
    }

    vec3 col;
    float crack = 1.0;
    if (uGlass > 0.0) {
        // A crack is a wedge of glass at the wrong angle: it displaces what is
        // behind it slightly, which is what makes it read as glass rather than
        // as a line drawn on the picture.
        vec2 d = uv - uImpact;
        d.x *= uResolution.x / max(uResolution.y, 1.0);
        crack = web(d, 17.0, clamp(uGlass / 0.85, 0.0, 1.0));
        // the web spreads and retracts with the damage (0.85 is the peak the
        // simulation allows), and deepens as it spreads
        float g = (1.0 - crack) * mix(0.45, 1.0, uGlass);
        vec2 n = normalize(d + 1e-6);
        col = texture(uTex, clamp(uv + n * g * 0.010, 0.0, 1.0)).rgb;
        // light finds the fracture and runs along it
        vec3 behind = texture(uTex, clamp(uv + n * 0.02, 0.0, 1.0)).rgb;
        col += behind * g * 0.9;
        col *= 1.0 - 0.55 * g;          // and the wedge itself is dark
    } else {
        col = texture(uTex, clamp(uv, 0.0, 1.0)).rgb;
    }

    // ---- the sensor saturating on something very bright --------------------
    if (uBloom > 0.0) {
        float l = dot(col, vec3(0.299, 0.587, 0.114));
        col += col * uBloom * smoothstep(0.55, 1.4, l) * 1.6;
        // and the column smearing below a saturated highlight, the way a CCD
        // bleeds charge down its readout column
        vec2 up = vec2(uv.x, uv.y + 0.02);
        float above = dot(texture(uTex, clamp(up, 0.0, 1.0)).rgb, vec3(0.299, 0.587, 0.114));
        col += uDeadColor * uBloom * smoothstep(0.9, 1.6, above) * 0.25;
    }

    // ---- the matrix itself: pixels that no longer work ---------------------
    if (uMatrix > 0.0) {
        vec2 px = floor(gl_FragCoord.xy);
        vec3 dead = uDeadColor;
        if (uGreenPurple > 0.5) {
            // the two-tone palette: each broken pixel picks one end or the other
            float w = hash12(px * 0.73 + 4.0);
            dead = w < 0.5 ? vec3(0.55, 1.0, 0.42) : vec3(0.62, 0.25, 0.95);
        }
        // Stuck-on and dead-black pixels are permanent for a given matrix, so
        // they are hashed from the pixel alone: they must not crawl. Each pixel
        // has its own threshold, so as uMatrix rises they die one at a time,
        // and as it falls they recover one at a time. No constant floor here:
        // that made thousands of pixels fail in the first frame of damage.
        float h = hash12(px);
        float stuck_t = 0.032 * uMatrix * uMatrix;
        float black_t = 2.0 * stuck_t;
        if (h < stuck_t)      col = dead * (0.6 + 0.8 * hash12(px + 13.0));
        else if (h < black_t) col = vec3(0.0);

        // whole dead rows and columns, which is how a matrix usually fails
        float rowh = hash12(vec2(0.0, px.y));
        float colh = hash12(vec2(px.x, 0.0));
        float line_t = 0.0106 * uMatrix * uMatrix;
        if (rowh < line_t) col = mix(col, dead * 0.8, 0.9);
        if (colh < line_t * 0.6) col = mix(col, vec3(0.0), 0.85);
    }

    frag = vec4(col, 1.0);
}
