#version 450

// Fullscreen triangle (3 vertices, no vertex buffer needed)
// Covers clip space [-1, -1] to [3, 3], clipped to viewport

layout(set = 3, binding = 0) uniform UBO {
    vec2 uvMin;  // top-left UV in source texture
    vec2 uvMax;  // bottom-right UV in source texture
} pc;

layout(location = 0) out vec2 fragUV;

void main()
{
    // Generate fullscreen triangle from vertex ID
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);

    // Map to UV range [uvMin, uvMax] (flip Y for correct orientation)
    vec2 uv = vec2(pos.x, 1.0 - pos.y);
    fragUV = mix(pc.uvMin, pc.uvMax, uv);
}
