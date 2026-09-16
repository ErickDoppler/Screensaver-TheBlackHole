#version 330 core
// Near-field dust: the proof that the camera is moving.
//
// The hole cannot show motion. It is hundreds of light years off, so no amount
// of flying changes its size or its shape by anything a human eye could catch,
// and faking that would be the one lie in the picture. What can show motion is
// what is close: a thin field of motes a few tens of units across, which the
// camera slides through while the hole sits exactly where it was.
//
// There is no vertex buffer. Each mote's home is hashed from gl_VertexID into
// a cube of side uCell, the whole field is offset by how far the camera has
// travelled, and the result is wrapped back into the cube - so the field is
// endless, fixed in space, and costs one glDrawArrays. The same trick The
// Black Wall used for the debris on its floor.

uniform mat4  uViewProj;
uniform vec3  uCamPos;
uniform vec3  uTravel;      // integrated camera velocity
uniform float uCell;        // side of the wrapping cube
uniform float uSpeed;       // 0..1, how fast we are going
uniform vec3  uColor;
uniform float uPointWorld;
uniform float uProjScale;
uniform float uMaxPointPx;

out float vAlpha;
out vec3  vColor;
flat out float vSize;

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

void main() {
    float i = float(gl_VertexID);
    vec3 home = vec3(hash11(i * 3.0 + 0.5),
                     hash11(i * 3.0 + 1.5),
                     hash11(i * 3.0 + 2.5)) * uCell;
    // mod() returns a positive remainder, so the field wraps cleanly in both
    // directions however long the session has been running.
    vec3 local = mod(home - uTravel, uCell) - uCell * 0.5;
    vec3 world = uCamPos + local;
    float d = length(local);

    gl_Position = uViewProj * vec4(world, 1.0);
    vSize = clamp(uPointWorld * uProjScale / max(d, 0.05), 1.0, uMaxPointPx);

    // Fade out of the far corners of the cube, so no mote ever pops into
    // existence at its edge, and out of the very near field, where one would
    // otherwise smear across half the screen.
    float far_fade  = 1.0 - smoothstep(uCell * 0.30, uCell * 0.5, d);
    float near_fade = smoothstep(0.4, 2.0, d);

    // Barely there when the camera is still, unmistakable when it is moving.
    // The dust is a read-out of speed, not scenery.
    float lit = 0.10 + 0.90 * uSpeed;

    vAlpha = far_fade * near_fade * lit * (0.35 + 0.65 * hash11(i * 7.0 + 11.0));
    vColor = uColor;
    gl_PointSize = max(vSize, 1.0);
}
