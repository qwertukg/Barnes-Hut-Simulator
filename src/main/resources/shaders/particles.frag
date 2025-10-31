#version 460 core

out vec4 FragColor;

void main() {
    float dist = length(gl_PointCoord - vec2(0.5));
    float alpha = smoothstep(0.5, 0.0, dist);
    FragColor = vec4(0.8, 0.85, 1.0, alpha);
}
