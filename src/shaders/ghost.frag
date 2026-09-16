#version 330 core
// Ghost tails: keep the brighter of the fresh frame and the decayed history.
// On a light background (inverted theme) darkness is the signal instead, so
// the same is done on the inverted picture.
uniform sampler2D uCurrent;
uniform sampler2D uPrevious;
uniform float     uDecay;   // per-frame multiplier, already frame-rate corrected
uniform float     uInvert;  // 1 = light background
in vec2 vUV;
out vec4 frag;
void main() {
    vec4 cur = texture(uCurrent, vUV);
    vec4 prev = texture(uPrevious, vUV);
    if (uInvert > 0.5) {
        vec3 c = 1.0 - cur.rgb;
        vec3 p = (1.0 - prev.rgb) * uDecay;
        frag = vec4(1.0 - max(c, p), 1.0);
    } else {
        frag = max(cur, prev * uDecay);
    }
}
