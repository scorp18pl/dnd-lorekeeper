#version 330 core

// Patch-based quadtree LOD vertex shader.
// Each draw call renders one (N+1)×(N+1) patch; face/bounds come from uniforms.
layout(location = 0) in vec2 a_PatchUV;   // [0,1]² patch-local UV
layout(location = 1) in vec2 a_MorphUV;  // CDLOD morph target (nearest even-indexed neighbour)

uniform mat4 u_Model;
uniform mat4 u_VP;

#define MAX_OVERLAYS 4
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

out vec3 v_LocalPos;

// Map cube-face (face, s, t) → unit sphere position.
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

    v_LocalPos  = n;
    gl_Position = u_VP * u_Model * vec4(n, 1.0);
}
