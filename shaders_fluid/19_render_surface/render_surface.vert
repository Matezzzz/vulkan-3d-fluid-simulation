#version 450

const int fluid_width = 20;
const int fluid_height = 20;
const int fluid_depth = 20;
const int detailed_resolution = 4;


layout(location = 0) out ivec3 out_pos;

const int fluid_render_width = detailed_resolution * fluid_width - 1;
const int fluid_render_height = detailed_resolution * fluid_height - 1;

ivec3 getPos(int vertex_i){
    return ivec3(vertex_i % fluid_render_width, vertex_i % (fluid_render_width * fluid_render_height) / fluid_render_width, vertex_i / fluid_render_width / fluid_render_height);
}

void main(){
    out_pos = getPos(gl_VertexIndex);
}