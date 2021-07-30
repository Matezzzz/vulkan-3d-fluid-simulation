#version 450

layout(location = 0) in vec3 normal;

layout(location = 0) out vec3 o_color;


const vec3 light_dir = normalize(vec3(1, 1, -2.0));

const vec3 ambient_color = vec3(0, 0, 0.3);
const vec3 diffuse_color = vec3(0, 0.5, 0.8);

void main(){
    o_color = ambient_color + max(0, dot(light_dir, normal)) * diffuse_color;
}