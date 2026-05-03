#version 330 core
in vec2  v_UV;
in vec4  v_Fill;
in vec4  v_Outline;
in float v_OutlineW;

uniform sampler2D u_Atlas;

out vec4 FragColor;

void main() {
    float d = texture(u_Atlas, v_UV).r;

    // SDF edge sits at 128/255 ≈ 0.502; smoothing ≈ 1-2 pixels wide
    const float edge = 128.0 / 255.0;
    const float sm   = 0.06;

    float fill    = smoothstep(edge - sm, edge + sm, d);
    float outline = smoothstep(edge - v_OutlineW - sm, edge - v_OutlineW + sm, d);

    vec3  col   = mix(v_Outline.rgb, v_Fill.rgb, fill);
    float alpha = max(v_Fill.a * fill, v_Outline.a * outline);
    FragColor = vec4(col, alpha);
}
