#version 450


layout(points) in;
layout(triangle_strip, max_vertices=4) out;

layout (location = 0) in int vertex_id[];

layout (location = 0) out vec2 uv;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 172) float particle_base_size;      //base particle size in pixels (when 1.0 units away from camera)
    layout(offset = 236) float active_particle_w;       //particle W coordinate will have this value if particle takes place in the simulation
    layout(offset = 412) int total_animation_frames;    //total animation frames in texture
    layout(offset = 416) float animation_fps;           //animation frames-per-second
    layout(offset = 420) int animation_texture_width_images;    //how many columns does the animation texture contain
    layout(offset = 424) int animation_texture_height_images;   //how many rows does the animation texture contain
};

layout (push_constant) uniform render_surface_push{
    mat4 MVP;
    float time;
};

//base quad positions and texture uvs, positions will be scaled
const vec2 pos[4] = vec2[4](vec2(-1, -1), vec2(-1, 1), vec2(1, 1), vec2(1, -1));
const vec2 uvs[4] = vec2[4](vec2(0, 0), vec2(0, 1), vec2(1, 1), vec2(1, 0));


vec2 getAnimationShift(){
    //compute index of a frame         + shift kinda randomly for current vertex, then take a remainder by total animation to loop the animation
    int i = (int(time * animation_fps) + vertex_id[0] * 13) % total_animation_frames;
    //return an index in animation coordinates - for animation image count 6x5 -> x from 0 to 6, y from 0 to 5
    return vec2(i % animation_texture_width_images, i / animation_texture_width_images);
}

void vertex(vec4 scr_pos, int i){
    //compute uv coordinates - (current shift + shift in current cell according to basic uv) -> position in animation coordinates, divide by anim size to get uvs between 0 and 1
    uv = (getAnimationShift() + uvs[i]) / vec2(animation_texture_width_images, animation_texture_height_images);
    //swap the y coordinate - animation texture should be played by rows, top-to-bottom, but glsl has y = 0 at the bottom. subtracting from 1 fixes this problem
    uv = vec2(uv.x, 1-uv.y);
    //set the position - scr pos in in screen coordinates, not normalized by w
    //if we add the same component to all xy coordinates, it means all particles will be rotated towards the camera, and perspective division by w later will cause them to look according to perspective as well
    gl_Position = scr_pos + vec4(pos[i] * particle_base_size, 0, 0);
    //create current vertex, geometry shader function
    EmitVertex();
}


void main(){
    //if particle is active
    if (gl_in[0].gl_Position.w == active_particle_w){
        vec4 scr_pos = MVP * vec4(gl_in[0].gl_Position);

        //just create the four vertices, in the order for triangle_strip output type to form a quad
        vertex(scr_pos, 1);
        vertex(scr_pos, 0);
        vertex(scr_pos, 2);
        vertex(scr_pos, 3);
    }
}