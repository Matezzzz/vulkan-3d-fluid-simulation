#version 450

layout(location = 0) in float should_discard;


layout(location = 0) out vec3 o_color;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 160) vec3 particle_color;
};


void main(){
    if (should_discard != 0.0 || distance(gl_PointCoord, vec2(0.5, 0.5)) > 0.5){
        discard;
    }else{
        o_color = particle_color;
    }
}