#version 330 core
// Separable 9-tap gaussian. uStep = direction * texel size * radius / 4.
uniform sampler2D uTex;
uniform vec2      uStep;
in vec2 vUV;
out vec4 frag;
void main() {
    const float w0 = 0.2270270270, w1 = 0.1945945946, w2 = 0.1216216216,
                w3 = 0.0540540541, w4 = 0.0162162162;
    vec4 c = texture(uTex, vUV) * w0;
    c += (texture(uTex, vUV + uStep)       + texture(uTex, vUV - uStep))       * w1;
    c += (texture(uTex, vUV + uStep * 2.0) + texture(uTex, vUV - uStep * 2.0)) * w2;
    c += (texture(uTex, vUV + uStep * 3.0) + texture(uTex, vUV - uStep * 3.0)) * w3;
    c += (texture(uTex, vUV + uStep * 4.0) + texture(uTex, vUV - uStep * 4.0)) * w4;
    frag = c;
}
