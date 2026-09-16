#version 330 core
// One star, drawn as a point sprite into a face of the sky cube map.
//
// Stars are point sources at infinity: their sprite size is a constant number
// of screen pixels, not a function of distance. A brighter star reads as a
// slightly larger dot because that is what a camera does with one, not
// because it is nearer.

layout(location = 0) in vec3  aDir;     // unit direction on the sky
layout(location = 1) in float aMag;     // 0 = faintest, 1 = blazing
layout(location = 2) in float aTemp;    // 0 = cool red, 1 = hot blue
layout(location = 3) in float aPhase;   // per-star twinkle phase

uniform mat4  uViewProj;     // the cube face's 90-degree frustum
uniform float uPointPx;      // Particle size, in pixels
uniform float uTwinkle;      // 0 = steady
uniform float uTime;
uniform vec3  uColor;        // the single cold star colour
uniform float uNatural;      // 1 = spread the colours by temperature instead

out float vAlpha;
out vec3  vColor;
flat out float vSize;

// Rough blackbody ramp, cool to hot, normalised to stay out of clipping.
vec3 temp_color(float t) {
    vec3 cool = vec3(1.00, 0.72, 0.45);
    vec3 mid  = vec3(1.00, 0.97, 0.92);
    vec3 hot  = vec3(0.72, 0.82, 1.00);
    return t < 0.5 ? mix(cool, mid, t * 2.0) : mix(mid, hot, (t - 0.5) * 2.0);
}

void main() {
    // Far enough to be beyond anything else drawn into the cube.
    gl_Position = uViewProj * vec4(aDir * 5000.0, 1.0);

    // A slow, uneven flicker. Two incommensurate rates so no pulse is visible.
    float tw = 1.0;
    if (uTwinkle > 0.0) {
        float f = 0.5 * (sin(uTime * 2.3 + aPhase * 6.2831) +
                         sin(uTime * 3.7 + aPhase * 12.9898));
        tw = 1.0 + uTwinkle * 0.55 * f;
    }

    // Magnitude drives brightness hard and size gently. The floor matters:
    // the magnitude distribution is already steep, and without one the great
    // majority of the field falls below what a screen can show.
    // The core is tight now, so a star covers far fewer pixels than the old
    // soft disc did. It needs the brightness back, or a correctly dense sky
    // reads as grey dust.
    float bright = (0.30 + 0.85 * aMag) * tw;
    // Size barely varies with magnitude. A star is a point source, and the
    // eye reads a field of same-sized dots at different brightnesses as
    // deep - but a field of dots at different SIZES as near, some of them
    // simply closer than others. Letting size track brightness is what
    // made the sky look like it had been slapped on a mile away. It is
    // also what the lensing shear stretches, so smaller sprites are the
    // same fix for stars smearing into dashes.
    // Size still barely tracks magnitude - a field of different-SIZED dots
    // reads as things at different distances, which is the opposite of what
    // a sky at infinity should say. What a bright star gets is a wider
    // halo, not a wider core.
    vSize  = uPointPx * (0.90 + 0.35 * aMag);
    vAlpha = clamp(bright, 0.0, 1.0);
    vColor = mix(uColor, temp_color(aTemp), uNatural);
    gl_PointSize = max(vSize, 1.0);
}
