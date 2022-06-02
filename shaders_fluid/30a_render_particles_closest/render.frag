#version 450

/**
 * render.frag
 *  - Fragment shader for rendering particles. Approximates normals
 */

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 world_pos;
layout(location = 2) in vec3 color;


layout(location = 0) out vec3 o_color;
layout(location = 1) out vec3 o_normal;

layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 0) uvec3 fluid_size;
    layout(offset = 116) int detailed_resolution;
    layout(offset = 408) float particle_opacity_threshold;
};
layout(set = 0, binding = 2) uniform sampler2D smoke_animation;
layout(set = 0, binding = 3) uniform sampler3D particle_densities;

//step in texture to approximate normal
float d = 0.001;

//just a utility method to compute one component of density gradient - used to compute normals
float densDif(vec3 tex_pos, int i){
    vec3 a = vec3(0, 0, 0);
    a[i] = d;
    return (texture(particle_densities, tex_pos+a).r - texture(particle_densities, tex_pos-a).r) / 2 / d;
}


void main(){
    //compute position in densities texture
    vec3 tex_pos = world_pos / fluid_size;
    float A = texture(smoke_animation, uv).w;
    //it this part of the particle is too transparent, discard the fragment
    if (A < particle_opacity_threshold){
        discard;
    }else{
        //save particle color
        o_color = color;
        //compute normal - it points in the direction of the lowest density (= -gradient of density)
        o_normal = vec3(densDif(tex_pos, 0), densDif(tex_pos, 1), densDif(tex_pos, 2));
    }
}