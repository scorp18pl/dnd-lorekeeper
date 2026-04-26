#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4      u_Model;
uniform mat4      u_VP;
uniform sampler2D u_Heightmap;
uniform bool      u_HasHeightmap;
uniform float     u_HeightScale;

#define MAX_OVERLAYS 4
uniform sampler2D u_OvHeightmap[MAX_OVERLAYS];
uniform float     u_OvHmScale[MAX_OVERLAYS];

// Shared with fragment shader (same program)
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

// AEQD forward: returns vec3(u, v, inside).
vec3 aeqdUV(float lat, float lon, float lat0, float lon0, float extent_km) {
    float dlon  = lon - lon0;
    float cos_c = clamp(sin(lat0)*sin(lat) + cos(lat0)*cos(lat)*cos(dlon), -1.0, 1.0);
    float c     = acos(cos_c);
    float sin_c = sin(c);
    float k     = (c < 1e-6) ? 1.0 : c / sin_c;
    float x_km  = k * cos(lat) * sin(dlon) * u_PlanetRadiusKm;
    float y_km  = k * (cos(lat0)*sin(lat) - sin(lat0)*cos(lat)*cos(dlon)) * u_PlanetRadiusKm;
    float half  = extent_km * 0.5;
    float u     = 0.5 + x_km / extent_km;
    float v     = 0.5 + y_km / extent_km;
    float inside = (abs(x_km) <= half && abs(y_km) <= half) ? 1.0 : 0.0;
    return vec3(u, v, inside);
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
            vec3 r = aeqdUV(lat, lon,
                            radians(u_OvCenterLat[i]),
                            radians(u_OvCenterLon[i]),
                            u_OvExtentKm[i]);
            if (r.z > 0.5)
                disp += sampleOvHm(i, r.xy) * u_OvHmScale[i];
        }
    }

    v_LocalPos  = a_Position;
    gl_Position = u_VP * u_Model * vec4(a_Position + n * disp, 1.0);
}
