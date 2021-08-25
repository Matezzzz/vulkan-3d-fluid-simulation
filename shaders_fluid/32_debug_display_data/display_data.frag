#version 450

/**
 * display_data.frag
 *  - Fragment shader for display data. just sets point color to the one defined in display_data.vert
 */


layout(location = 0) in vec3 color;


layout(location = 0) out vec3 o_color;


void main(){
    o_color = color;
}