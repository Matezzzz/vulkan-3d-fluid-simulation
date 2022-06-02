#version 450

/**
 * render.frag
 *  - Fragment shader for rendering particles. Just sample texture at given coordinates, discard if alpha too low
 */

//if not zero, particle should be discarded
layout(location = 0) in vec2 uv;


layout(location = 0) out vec4 o_color;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 404) float particle_opacity_multiplier; //final fragment alpha will be multiplied by this
    layout(offset = 408) float particle_opacity_threshold;  //all fragments with alpha less than this will be discarded
};
layout(set = 0, binding = 2) uniform sampler2D smoke_animation;



const float default_opacity = 0.2;
const float opacity_threshold = 0.2;

void main(){
    float A = texture(smoke_animation, uv).w;
    //if texture is too transparent here, discard the fragment, is done for performance reasons
    if (A < particle_opacity_threshold){
        discard;
    }else{
        //set out color. multiply by default opacity - one particle is mostly transparent, 
        o_color = vec4(1, 1, 1, A * particle_opacity_multiplier);
    }
}
