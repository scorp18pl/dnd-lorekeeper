#version 330 core

layout(location = 0) in vec3 a_Position;

uniform mat4 u_Model;
uniform mat4 u_VP;

#define MAX_OVERLAYS 4
// Shared with fragment shader (same program)
uniform int   u_OvCount;
uniform float u_OvCenterLat[MAX_OVERLAYS];
uniform float u_OvCenterLon[MAX_OVERLAYS];
uniform float u_OvExtentKm[MAX_OVERLAYS];
uniform float u_PlanetRadiusKm;

out vec3 v_LocalPos;

void main() {
    v_LocalPos  = a_Position;
    gl_Position = u_VP * u_Model * vec4(a_Position, 1.0);
}
