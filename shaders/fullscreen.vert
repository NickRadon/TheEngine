#version 460
// Fullscreen triangle; the NDC position is forwarded for ray reconstruction.
layout(location = 0) out vec2 outNdc;

void main()
{
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outNdc = uv * 2.0 - 1.0;
    gl_Position = vec4(outNdc, 0.0, 1.0);
}
