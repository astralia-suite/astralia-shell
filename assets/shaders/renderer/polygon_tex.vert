#version 100
attribute vec2 a_pos;
attribute vec2 a_uv;
uniform vec2 u_viewport;
uniform vec4 u_model_ab;
uniform vec2 u_model_t;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    vec2 px = vec2(u_model_ab.x * a_pos.x + u_model_ab.y * a_pos.y + u_model_t.x,
                   u_model_ab.z * a_pos.x + u_model_ab.w * a_pos.y + u_model_t.y);
    vec2 ndc = vec2(px.x / u_viewport.x * 2.0 - 1.0,
                    1.0 - px.y / u_viewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
