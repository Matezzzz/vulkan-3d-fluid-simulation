#version 450


layout(set = 0, binding = 0, rgba32f) uniform restrict readonly image2D particles;

layout(push_constant) uniform constants{
    mat4 MVP;
};


const uint particle_batch_size = 256;

void main(){
    vec3 pos = imageLoad(particles, ivec2(gl_VertexIndex % particle_batch_size, gl_VertexIndex / particle_batch_size)).xyz;
    gl_Position = vec4(pos.xy / 10.0 - vec2(1.0, 1.0), 0.0, 1.0); //MVP * vec4(pos, 1.0);
    gl_PointSize = 20 / pos.z;
}