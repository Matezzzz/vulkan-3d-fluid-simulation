


layout(location = 0) out vec4 o_color;


void main(){
    if (distance(gl_PointCoord, vec2(0.5, 0.5)) > 0.5){
        discard;
    }else{
        o_color = vec3(0.0, 1.0, 0.0);
    }
}