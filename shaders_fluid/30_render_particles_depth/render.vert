#version 450

/**
 * render.vert
 *  - Vertex shader for rendering particles
 */


const int PARTICLE_BUFFER_SIZE = 1000000;


layout(set = 0, binding = 1) buffer restrict readonly particles{
    vec4 particle_positions[PARTICLE_BUFFER_SIZE];
};


layout(location = 0) out int vertex_id;


void main(){
    //get position of current particle
    gl_Position = particle_positions[gl_VertexIndex];
    vertex_id = gl_VertexIndex;
}