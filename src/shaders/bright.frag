#version 330 core
// Bright-pass for the glow. Keeps only what is hotter than the threshold, with
// a soft knee so there is no visible contour where the bloom starts, and feeds
// it to the blur.
//
// This is what makes the disk feel dangerous rather than merely orange. Real
// light that bright does not stay inside its own outline: it floods the lens,
// it floods the sensor, and it washes over everything near it. A disk drawn
// without it is a picture of a disk.

uniform sampler2D uTex;
uniform float uThreshold;
uniform float uKnee;

in vec2 vUV;
out vec4 frag;

void main() {
    vec3 c = texture(uTex, vUV).rgb;
    float l = max(max(c.r, c.g), c.b);
    // soft knee: fades in over uKnee below the threshold instead of switching
    float w = clamp((l - uThreshold + uKnee) / max(2.0 * uKnee, 1e-4), 0.0, 1.0);
    w *= w;
    frag = vec4(c * w, 1.0);
}
