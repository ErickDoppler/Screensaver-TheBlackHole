#version 330 core
// Shapes a star.
//
// Not as a disc. A disc with a softened rim is what The Black Wall's pixels
// wanted - they were panel LEDs, and they were meant to have an edge - but a
// star is a point source, and a point source does not have an edge at all.
// What a lens and a sensor actually make of one is a very tight core with a
// faint halo spreading out around it, and the ratio between those two is the
// whole reason a light reads as a LIGHT, at an unknowable distance, instead of
// as a small bright spot painted on the backdrop.
//
// Two gaussians, then: a hard one for the core, a wide weak one for the halo.
// Output is premultiplied for additive blending.

in float vAlpha;
in vec3  vColor;
flat in float vSize;

out vec4 frag;

void main() {
    float r = length(gl_PointCoord - 0.5) * 2.0;

    // The core is tight enough that it lands on roughly one texel whatever the
    // sprite is nominally sized at, so the star stays a point and the sprite
    // is really just the envelope the halo needs room in.
    float core = exp(-r * r * 13.0);
    float halo = exp(-r * r * 2.1) * 0.20;
    float m = core + halo;
    if (m < 0.003) discard;

    float a = vAlpha * m;
    frag = vec4(vColor * a, a);
}
