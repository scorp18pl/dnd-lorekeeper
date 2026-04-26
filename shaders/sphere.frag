#version 330 core

in vec3 v_LocalPos;
out vec4 FragColor;

uniform sampler2D u_Texture;
uniform bool      u_HasTexture;
uniform vec3      u_BaseColor;

#define MAX_OVERLAYS 4
uniform sampler2D u_OvTex[MAX_OVERLAYS];
uniform int       u_OvCount;
uniform float     u_OvCenterLat[MAX_OVERLAYS];
uniform float     u_OvCenterLon[MAX_OVERLAYS];
uniform float     u_OvExtentKm[MAX_OVERLAYS];
uniform float     u_OvOpacity[MAX_OVERLAYS];
uniform float     u_PlanetRadiusKm;

const float PI = 3.14159265359;

vec4 sampleOv(int idx, vec2 uv) {
    if (idx == 0) return texture(u_OvTex[0], uv);
    if (idx == 1) return texture(u_OvTex[1], uv);
    if (idx == 2) return texture(u_OvTex[2], uv);
    return            texture(u_OvTex[3], uv);
}

void main() {
    vec3 n = normalize(v_LocalPos);

    vec3  lightDir = normalize(vec3(1.0, 0.8, 0.6));
    float diff     = max(dot(n, lightDir), 0.0);
    float light    = 0.25 + 0.75 * diff;

    vec4 color;
    if (u_HasTexture) {
        float u = (atan(-n.z, n.x) + PI) / (2.0 * PI);
        float v = asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5;
        color = vec4(texture(u_Texture, vec2(u, v)).rgb, 1.0);
    } else {
        color = vec4(u_BaseColor * light, 1.0);
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
                vec2  ovUV  = vec2(0.5 + dx / (2.0 * half_ext),
                                   0.5 + dy / (2.0 * half_ext));
                vec4  ovCol = sampleOv(i, ovUV);
                float alpha = ovCol.a * u_OvOpacity[i];
                color.rgb   = mix(color.rgb, ovCol.rgb, alpha);
            }
        }
    }

    FragColor = color;
}
