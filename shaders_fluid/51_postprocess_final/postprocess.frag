#version 450



layout(location = 0) in vec2 uv;

layout(location = 0) out vec3 o_color;

layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 432) float soft_particles_depth_smooth_range;
    layout(offset = 436) float soft_particles_contrast;
    layout(offset = 440) float camera_near;
    layout(offset = 444) float camera_far;
    layout(offset = 448) vec3 soft_particles_sphere_color;
    layout(offset = 516) float final_render_color_saturation;
};
layout(set = 0, binding = 1) uniform sampler2D source;
layout(set = 0, binding = 2) uniform sampler2D depth;
layout(set = 0, binding = 3) uniform sampler2D spheres_depth;


//increase distance between val and mid by a given factor
float sat(float mid, float k, float val){
    return mid + (val - mid) * k;
}
//somehow saturate given color
vec3 saturate(vec3 c){
    //min and max color
    float mn = min(min(c.x, c.y), c.z), mx = max(max(c.x, c.y), c.z);
    float dif = mx - mn;
    //the difference between min and max we will try to reach
    float new_dif = 1 - pow(1 - dif, final_render_color_saturation);
    //simply - we put all three colors on the same line, compute a point, distances of all points will be scaled by a constant, so that max(r, g, b) - min(r, g, b) = new_dif after the scaling
    float k = new_dif / dif;
    float mid = mn * dif / (1 + mn - mx) + mn;
    return vec3(sat(mid, k, c.x), sat(mid, k, c.y), sat(mid, k, c.z));
}



//convert depth buffer value to one between 0 at camera_far and 1 at camera_far, equation taken from https://learnopengl.com/Advanced-OpenGL/Depth-testing
float linearDepth(float depth){
    float ndc = depth * 2.0 - 1.0;
    return (2.0 * camera_near * camera_far) / (camera_far + camera_near - ndc * (camera_far - camera_near)) / camera_far;
}



vec3 frontColor(vec2 uv){
    //particle color, if there is one, or default background color
    //saturate it more before using - result looks much better (lighting isn't as accurate, but it looks pretty, so whatever)
    vec3 particle_color = saturate(texture(source, uv).rgb);
    //how far the particles are (already in linear space)
    float depth_particles = texture(depth, uv).x;
    //how far the sphere is
    float depth_spheres = linearDepth(texture(spheres_depth, uv).x);
    //if there is no sphere in current fragment (depth should be 1, count with some rounding errors)
    if (depth_spheres >= 0.99) return particle_color;

    float dif = depth_spheres - depth_particles;
    //if sphere is before the particles, return sphere color
    if (dif < 0){
        return soft_particles_sphere_color;
    //if particles are only a short distance between the sphere
    }else if (dif < soft_particles_depth_smooth_range){
        //convert to linear interpolation coefficient, then use the soft particles technique
        float d = dif / soft_particles_depth_smooth_range;
        //use default expression from the article to compute interpolation coefficient
        float k = 0.5*pow(2*((d > 0.5) ? 1-d : d), soft_particles_contrast);
        //mix sphere and particle color accordingly
        return mix(particle_color, soft_particles_sphere_color, k);
    //if particles are far before the sphere, just return their color
    }else{
        return particle_color;
    }
}


void main(){
    o_color = frontColor(uv).rgb;
}