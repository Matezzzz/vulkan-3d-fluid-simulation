#version 450


layout(set = 0, binding = 0) buffer restrict readonly particle_densities{
    int densities[8000];
};

layout(push_constant) uniform constants{
    mat4 MVP;
};

layout(location = 0) out vec3 color;

const int fluid_width = 20;
const int fluid_height = 20;

ivec3 getPos(int vertex_i){
    return ivec3(vertex_i % fluid_width, vertex_i % (fluid_width * fluid_height) / fluid_width, vertex_i / fluid_width / fluid_height);
}

void main(){
    int i = gl_VertexIndex;
    ivec3 pos = getPos(i);
    vec4 scr_pos = MVP * vec4(pos.xyz + vec3(0.5, 0.5, 0.5), 1.0);
    gl_Position = scr_pos;
    gl_PointSize = 20 / scr_pos.z;
    float dens = log(densities[i]+1)/8;
    color = mix(vec3(0, 0, 1), vec3(1, 0, 0), dens);
}