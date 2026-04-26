#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4      u_Model;
uniform mat4      u_VP;
uniform sampler2D u_Heightmap;
uniform bool      u_HasHeightmap;
uniform float     u_HeightScale;

// Overlay heightmaps — share position uniforms with fragment shader
#define MAX_OVERLAYS 4
uniform sampler2D u_OvHeightmap[MAX_OVERLAYS];
uniform float     u_OvHmScale[MAX_OVERLAYS];
// u_OvCount, u_OvCenterLat/Lon/ExtentKm, u_PlanetRadiusKm declared in frag — same program

uniform int   u_OvCount;
uniform float u_OvCenterLat[MAX_OVERLAYS];
uniform float u_OvCenterLon[MAX_OVERLAYS];
uniform float u_OvExtentKm[MAX_OVERLAYS];
uniform float u_PlanetRadiusKm;

const float PI = 3.14159265359;

out vec3 v_LocalPos;

float sampleOvHm(int idx, vec2 uv) {
    if (idx == 0) return textureLod(u_OvHeightmap[0], uv, 0.0).r;
    if (idx == 1) return textureLod(u_OvHeightmap[1], uv, 0.0).r;
    if (idx == 2) return textureLod(u_OvHeightmap[2], uv, 0.0).r;
    return            textureLod(u_OvHeightmap[3], uv, 0.0).r;
}

void main() {
    vec3  n    = normalize(a_Position);
    float disp = 0.0;

    if (u_HasHeightmap) {
        float u = (atan(-n.z, n.x) + PI) / (2.0 * PI);
        float v = asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5;
        disp = textureLod(u_Heightmap, vec2(u, v), 0.0).r * u_HeightScale;
    }

    if (u_OvCount > 0) {
        float lat = asin(clamp(n.y, -1.0, 1.0));
        float lon = atan(-n.z, n.x);
        for (int i = 0; i < u_OvCount; ++i) {
            float lat_c    = radians(u_OvCenterLat[i]);
            float lon_c    = radians(u_OvCenterLon[i]);
            float half_ext = u_OvExtentKm[i] * 0.5 / u_PlanetRadiusKm;
            float dx = (lon - lon_c) * cos(lat_c);
            float dy = lat - lat_c;
            if (abs(dx) <= half_ext && abs(dy) <= half_ext) {
                vec2  hmUV = vec2(0.5 + dx / (2.0 * half_ext),
                                  0.5 + dy / (2.0 * half_ext));
                disp += sampleOvHm(i, hmUV) * u_OvHmScale[i];
            }
        }
    }

    v_LocalPos  = a_Position;
    gl_Position = u_VP * u_Model * vec4(a_Position + n * disp, 1.0);
}
