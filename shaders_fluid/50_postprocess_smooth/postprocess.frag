#version 450



layout(location = 0) in vec2 uv;

layout(location = 0) out vec3 o_blurred_render;
layout(location = 1) out vec3 o_blurred_normal;
layout(location = 2) out float o_blurred_depth;


layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 440) float camera_near;
    layout(offset = 444) float camera_far;
    layout(offset = 460) float blending_past_coefficient;
    layout(offset = 464) vec3 light_color;
    layout(offset = 476) float ambient_color;
    layout(offset = 480) vec3 light_direction;
    layout(offset = 496) vec3 background_color;
};
layout(set = 0, binding = 1) uniform sampler2D render;
layout(set = 0, binding = 2) uniform sampler2D normals;
layout(set = 0, binding = 3) uniform sampler2D depth;
layout(set = 0, binding = 4) uniform sampler2D last_blurred_render;
layout(set = 0, binding = 5) uniform sampler2D last_blurred_normal;
layout(set = 0, binding = 6) uniform sampler2D last_blurred_depth;
layout(set = 0, binding = 7) uniform sampler2D colors;


layout (push_constant) uniform push_constants{
    vec2 viewport_shift;
    float blur_amount;
};


//convert depth buffer value to one between 0 at camera_far and 1 at camera_far, equation taken from https://learnopengl.com/Advanced-OpenGL/Depth-testing
float linearDepth(float depth){
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * camera_near * camera_far) / (camera_far + camera_near - ndc * (camera_far - camera_near)) / camera_far;
}


void main(){
    //clamp the volume-rendered value at 1.0
    vec3 volume = min(vec3(1, 1, 1), texture(render, uv).xyz);
    //current front particle rendered normal
    vec3 normal = texture(normals, uv).xyz;
    //current front particle depth converted to linear
    float depth = linearDepth(texture(depth, uv).x);
    //front color
    vec3 front_color = texture(colors, uv).rgb;

    //how much we should blend - blend less if the camera is moving a lot
    float k = mix(1, blending_past_coefficient, blur_amount);
    //adjust uv to allow rotating camera without breaking blurring
    vec2 new_uv = uv-viewport_shift;
    //if uv is out of range (this part of frame wasn't rendered last time) set k = 1 - we will use only the new value
    //problem - new particles will not be blurred at all, and will look a bit weird for a little while
    if (new_uv.x < 0 || new_uv.y < 0 || new_uv.x > 1 || new_uv.y > 1) k = 1;
    
    
    vec3 current_color, current_normal;
    //if current normal isn't defined, set the particle as black
    if (length(normal) == 0){
        current_color = vec3(0, 0, 0);
        current_normal = vec3(0, 0, 0);
    }else{
        //else, normalize current normal
        current_normal = normalize(normal);
        //simple lighting - diffuse light is dot product of -direction and normal. Interpolat
        float diffuse = max(0, dot(-light_direction, current_normal));
        //add ambient light (of same color as actual light) (but keep the value < 1)
        float light = min(1.0, diffuse + ambient_color);
        //color = particle volume in current pixel * light reaching it * front_color * light_color
        vec3 color = volume * light * front_color * light_color;
        //fade color out as it gets further from the camera
        current_color = mix(background_color, color, 1 - depth);
    }
    //save blurred values - render, normal and depth - mix the old value with coefficient (1 - k) and the new with k
    o_blurred_render = mix(texture(last_blurred_render, new_uv).xyz, current_color, k);
    o_blurred_normal = mix(texture(last_blurred_normal, new_uv).xyz, current_normal, k);
    o_blurred_depth = mix(texture(last_blurred_depth, new_uv).x, depth, k);
}