#version 450


layout(set = 0, binding = 0) buffer restrict readonly particles{
    vec4 particle_positions[65536];
};

layout(push_constant) uniform constants{
    mat4 MVP;
};

layout(location = 0) out float should_discard;

const uint particle_batch_size = 256;

void main(){
    vec4 pos = particle_positions[gl_VertexIndex];
    if (pos.w == 1.0){
        //gl_Position = vec4(pos.xy / 10.0 - vec2(1.0, 1.0), 0.0, 1.0);
        vec4 scr_pos = MVP * vec4(pos.xyz, 1.0);
        gl_Position = scr_pos;
        gl_PointSize = 20 / scr_pos.z;
        should_discard = 0.0;
    }else{
        should_discard = 1.0;
    }
}