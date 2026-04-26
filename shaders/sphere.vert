#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4      u_Model;
uniform mat4      u_VP;
uniform sampler2D u_Heightmap;
uniform bool      u_HasHeightmap;
uniform float     u_HeightScale;

const float PI = 3.14159265359;

out vec3 v_LocalPos;

void main() {
    vec3  n    = normalize(a_Position);
    float disp = 0.0;
    if (u_HasHeightmap) {
        float u = (atan(-n.z, n.x) + PI) / (2.0 * PI);
        float v = asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5;
        disp = textureLod(u_Heightmap, vec2(u, v), 0.0).r * u_HeightScale;
    }
    v_LocalPos  = a_Position;
    gl_Position = u_VP * u_Model * vec4(a_Position + n * disp, 1.0);
}
