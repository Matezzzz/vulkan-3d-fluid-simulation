#version 450

layout(location = 0) in float should_discard;


layout(location = 0) out vec3 o_color;


void main(){
    if (should_discard != 0.0 || distance(gl_PointCoord, vec2(0.5, 0.5)) > 0.5){
        discard;
    }else{
        o_color = vec3(0.0, 1.0, 0.0);
    }
}