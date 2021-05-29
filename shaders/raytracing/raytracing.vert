#version 450
#extension GL_EXT_scalar_block_layout : require
 
//screen quad vertex position
layout (location = 0) in vec2 v_position;

//ray direction
layout(location = 0) out vec3 v_ray_vec;

//inverse of model-view-projection matrix
layout( push_constant ) uniform push_const{
    mat4 MVP_inverse;
};


void main(){
    //compute ray vector in current boundary point. This will cause it to be interpolated over all fragments
    v_ray_vec = (MVP_inverse * vec4(v_position, 0.0, 1.0)).xyz;
    //add z = 0 and w = 1 to form vertex position
    gl_Position = vec4(v_position, 0.0, 1.0);
}
