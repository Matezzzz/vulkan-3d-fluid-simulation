#version 450

/**
 * display_data.vert
 *  - This shader can be used to display some data in fluid grid as colored points.
 */

layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 0) uvec3 fluid_size;
};
layout(set = 0, binding = 1, r32ui) uniform restrict readonly uimage3D particle_densities;

layout(push_constant) uniform constants{
    mat4 MVP;
};

layout(location = 0) out vec3 color;



ivec3 getPos(int vertex_i){
    return ivec3(vertex_i % fluid_size.x, vertex_i % (fluid_size.x * fluid_size.y) / fluid_size.x, vertex_i / fluid_size.x / fluid_size.y);
}

void main(){
    int i = gl_VertexIndex;
    ivec3 pos = getPos(i);
    vec4 scr_pos = MVP * vec4(pos.xyz + vec3(0.5, 0.5, 0.5), 1.0);
    gl_Position = scr_pos;
    gl_PointSize = 20 / scr_pos.z;
    float dens = log(imageLoad(particle_densities, pos).x+1)/8;
    color = mix(vec3(0, 0, 1), vec3(1, 0, 0), dens);
}