#version 450


layout(points) in;
layout(triangle_strip, max_vertices=4) out;

layout (location = 0) out vec2 uv;

const vec2 pos[4] = vec2[4](vec2(-1, -1), vec2(-1, 1), vec2(1, 1), vec2(1, -1));
const vec2 uvs[4] = vec2[4](vec2(0, 0), vec2(0, 1), vec2(1, 1), vec2(1, 0));

//create one vertex of a screen quad
void vertex(int i){
    uv = uvs[i];
    gl_Position = vec4(pos[i], 0.0, 1.0);
    EmitVertex();
}

void main(){
    //create a screen quad
    vertex(1);
    vertex(0);
    vertex(2);
    vertex(3);
}