#version 330 core

// Patch-based quadtree LOD vertex shader.
// Each draw call renders one (N+1)×(N+1) patch; face/bounds come from uniforms.
layout(location = 0) in vec2 a_PatchUV;   // [0,1]² patch-local UV
layout(location = 1) in vec2 a_MorphUV;  // CDLOD morph target (nearest even-indexed neighbour)

uniform mat4      u_Model;
uniform mat4      u_VP;
uniform sampler2D u_Heightmap;
uniform bool      u_HasHeightmap;
uniform float     u_HeightScale;

#define MAX_OVERLAYS 4
uniform sampler2D u_OvHeightmap[MAX_OVERLAYS];
uniform float     u_OvHmScale[MAX_OVERLAYS];

uniform int   u_OvCount;
uniform float u_OvCenterLat[MAX_OVERLAYS];
uniform float u_OvCenterLon[MAX_OVERLAYS];
uniform float u_OvExtentKm[MAX_OVERLAYS];
uniform float u_PlanetRadiusKm;

// Per-patch quadtree params
uniform int   u_Face;          // cube face 0-5
uniform vec2  u_PatchOrigin;   // patch origin in face UV [0,1]
uniform float u_PatchSize;     // patch side length in face UV
uniform float u_MorphFactor;   // CDLOD: 0 = no morph, 1 = fully snapped to coarse position

const float PI = 3.14159265359;

out vec3 v_LocalPos;

float sampleOvHm(int idx, vec2 uv) {
    if (idx == 0) return textureLod(u_OvHeightmap[0], uv, 0.0).r;
    if (idx == 1) return textureLod(u_OvHeightmap[1], uv, 0.0).r;
    if (idx == 2) return textureLod(u_OvHeightmap[2], uv, 0.0).r;
    return            textureLod(u_OvHeightmap[3], uv, 0.0).r;
}

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

// Map cube-face (face, s, t) → unit sphere position.
// Basis vectors match CubeSphere.cpp winding (CCW from outside).
vec3 faceDir(int face, float s, float t) {
    if (face == 0) return normalize(vec3( 1.0, -t, -s));  // +X
    if (face == 1) return normalize(vec3(-1.0, -t,  s));  // -X
    if (face == 2) return normalize(vec3( s,  1.0,  t));  // +Y
    if (face == 3) return normalize(vec3( s, -1.0, -t));  // -Y
    if (face == 4) return normalize(vec3( s,  -t,  1.0)); // +Z
    return              normalize(vec3(-s,  -t, -1.0));   // -Z
}

void main() {
    // CDLOD morph: blend toward coarse-grid position to eliminate LOD seam cracks
    vec2  morphedUV = mix(a_PatchUV, a_MorphUV, u_MorphFactor);

    // Map patch-local UV → face UV → cube [-1,1]
    vec2  faceUV = u_PatchOrigin + morphedUV * u_PatchSize;
    vec2  st     = faceUV * 2.0 - 1.0;
    vec3  n      = faceDir(u_Face, st.x, st.y);

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

    v_LocalPos  = n;
    gl_Position = u_VP * u_Model * vec4(n * (1.0 + disp), 1.0);
}
