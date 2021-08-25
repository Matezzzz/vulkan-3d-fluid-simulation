#version 450

/**
 * render_surface.frag
 *  - Fragment shader for surface rendering
 */


layout(location = 0) in vec3 normal;

layout(location = 0) out vec3 o_color;

layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 176) vec3 light_dir;
    layout(offset = 192) vec3 ambient_color;
    layout(offset = 208) vec3 diffuse_color;
};



void main(){
    //normalize light direction
    vec3 L_dir = normalize(light_dir);
    //perform simple shading - color is ambient_color + diffuse_color * k, where k is a constant that decreases with increasing angle between normal and light direction
    o_color = ambient_color + max(0, dot(-L_dir, normal)) * diffuse_color;
}