#version 330 core

in vec3 v_LocalPos;
out vec4 FragColor;

uniform sampler2D u_Texture;
uniform bool      u_HasTexture;
uniform vec3      u_BaseColor;

const float PI = 3.14159265359;

void main() {
    vec3 n = normalize(v_LocalPos);

    // Simple directional light so an untextured globe reads as 3D
    vec3  lightDir = normalize(vec3(1.0, 0.8, 0.6));
    float diff     = max(dot(n, lightDir), 0.0);
    float light    = 0.25 + 0.75 * diff;

    vec3 color;
    if (u_HasTexture) {
        float u = (atan(n.z, n.x) + PI) / (2.0 * PI);
        float v = asin(clamp(n.y, -1.0, 1.0)) / PI + 0.5;
        color = texture(u_Texture, vec2(u, v)).rgb;
        FragColor = vec4(color, 1.0);
    } else {
        color = u_BaseColor;
        FragColor = vec4(color * light, 1.0);
    }
}
