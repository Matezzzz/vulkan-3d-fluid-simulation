#version 450

/**
 * render.vert
 *  - Vertex shader for rendering particles
 */


const int PARTICLE_BUFFER_SIZE = 1000000;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 172) float particle_base_size;      //base particle size in pixels (when 1.0 units away from camera)
    layout(offset = 236) float active_particle_w;       //particle W coordinate will have this value if particle takes place in the simulation
    layout(offset = 260) float particle_max_size;       //max particle size - no particle will be larger than this
};
layout(set = 0, binding = 1) buffer restrict readonly particles{
    vec4 particle_positions[PARTICLE_BUFFER_SIZE];
};

layout(push_constant) uniform constants{
    mat4 MVP;       //model-view-projection matrix
};

layout(location = 0) out float should_discard;


void main(){
    //get position of current particle
    vec4 pos = particle_positions[gl_VertexIndex];
    //if particle is active
    if (pos.w == active_particle_w){
        //compute position on screen (multiply particle position by model-view-projection matrix)
        vec4 scr_pos = MVP * vec4(pos.xyz, 1.0);
        //set point position
        gl_Position = scr_pos;
        //compute point size - base size divided by distance from camera, capped at particle_max_size
        gl_PointSize = min(particle_base_size / scr_pos.z, particle_max_size);
        //particle should not be discarded
        should_discard = 0.0;
    }else{
        //if particle is not active, discard all fragments during fragment shader
        should_discard = 1.0;
    }
}