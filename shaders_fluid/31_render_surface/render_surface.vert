#version 450

/**
 * render_surface.vert
 *  - Vertex shader used for rendering fluid surface. Converts vertex ID to cell position in the grid.
 */


layout(location = 0) out ivec3 out_pos;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 224) uvec3 fluid_surface_render_size;
};


ivec3 getPos(int vertex_i){
    return ivec3(vertex_i % fluid_surface_render_size.x, vertex_i % (fluid_surface_render_size.x * fluid_surface_render_size.y) / fluid_surface_render_size.x, vertex_i / fluid_surface_render_size.x / fluid_surface_render_size.y);
}

void main(){
    //compute cell position from gl_VertexIndex
    out_pos = getPos(gl_VertexIndex);
}