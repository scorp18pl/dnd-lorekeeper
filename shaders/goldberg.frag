#version 330 core
in  vec4 v_Color;
out vec4 FragColor;

void main() {
    if (v_Color.a < 0.01) discard;
    FragColor = v_Color;
}
