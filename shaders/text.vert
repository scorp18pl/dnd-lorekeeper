#version 330 core
layout(location = 0) in vec2  a_Pos;
layout(location = 1) in vec2  a_UV;
layout(location = 2) in vec4  a_Fill;
layout(location = 3) in vec4  a_Outline;
layout(location = 4) in float a_OutlineW;

uniform mat4 u_Proj;

out vec2  v_UV;
out vec4  v_Fill;
out vec4  v_Outline;
out float v_OutlineW;

void main() {
    v_UV       = a_UV;
    v_Fill     = a_Fill;
    v_Outline  = a_Outline;
    v_OutlineW = a_OutlineW;
    gl_Position = u_Proj * vec4(a_Pos, 0.0, 1.0);
}
