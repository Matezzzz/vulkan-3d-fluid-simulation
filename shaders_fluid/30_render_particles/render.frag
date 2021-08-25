#version 450

/**
 * render.frag
 *  - Fragment shader for rendering particles. Renders points as circles.
 */

//if not zero, particle should be discarded
layout(location = 0) in float should_discard;


layout(location = 0) out vec3 o_color;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 160) vec3 particle_color;
};


void main(){
    //if particle should be discarded or distance from point center is larger than 0.5 (fragment would be outside of circle), discard it
    if (should_discard != 0.0 || distance(gl_PointCoord, vec2(0.5, 0.5)) > 0.5){
        discard;
    }else{  //else set output color to particle_color
        o_color = particle_color;
    }
}