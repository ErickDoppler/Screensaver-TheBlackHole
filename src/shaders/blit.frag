#version 330 core
uniform sampler2D uTex;
in vec2 vUV;
out vec4 frag;
void main() { frag = vec4(texture(uTex, vUV).rgb, 1.0); }
