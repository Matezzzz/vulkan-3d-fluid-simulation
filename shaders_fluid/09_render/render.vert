

layout(set = 0, binding = 0, rgb32f) uniform restrict readonly image1D particle_positions;

layout(push_constant){
    mat4 MVP;
    vec3 camera_pos;
};


void main(){
    vec3 pos = imageLoad(particle_positions, gl_VertexID).xyz;
    gl_Position = MVP * vec4(vec3, 1.0);
    gl_PointSize = 20 / distance(pos, camera_pos);
}