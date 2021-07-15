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

void main(){
    int i = gl_VertexIndex;

    ivec3 pos = ivec3(i % fluid_width, i % (fluid_width * fluid_height) / fluid_width, i / fluid_width / fluid_height);
    vec4 scr_pos = MVP * vec4(pos.xyz + vec3(0.5, 0.5, 0.5), 1.0);
    gl_Position = scr_pos;
    gl_PointSize = 20 / scr_pos.z;
    float dens = log(densities[i]+1)/8;
    color = mix(vec3(0, 0, 1), vec3(1, 0, 0), dens);
}