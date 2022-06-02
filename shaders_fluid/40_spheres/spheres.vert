#version 450

//Renders a sphere at a given position


layout (location = 0) in vec3 position;


layout (push_constant) uniform push_constants{
    mat4 MVP;
    vec3 sphere_position;
};


void main(){
    //move sphere vertex to the given position, then transform it into screen space
    gl_Position = MVP * vec4(sphere_position + position, 1.0);
}