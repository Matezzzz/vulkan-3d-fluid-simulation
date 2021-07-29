#version 450
#extension GL_EXT_scalar_block_layout : require

layout(points) in;
layout(triangle_strip, max_vertices=15) out;

layout(location = 0) in ivec3 pos[];

layout(location = 0) out vec3 color;
layout(location = 1) out int o_triangle_variant;
layout(location = 2) out uint o_debug_value;

layout(set = 0, binding = 0, std430) uniform triangle_counts{
    uint counts[256];
};
layout(set = 0, binding = 1, std430) uniform triangle_vertices{
    uint vertex_edge_indices[15*256];
};
layout(set = 0, binding = 2, r32f) uniform restrict readonly image3D float_densities;

float getDensity(ivec3 index){
    return imageLoad(float_densities, index).r;
}

layout (push_constant) uniform render_surface_push{
    mat4 MVP;
};

ivec3 moves[8] = ivec3[](ivec3(0, 0, 0), ivec3(1, 0, 0), ivec3(1, 1, 0), ivec3(0, 1, 0), ivec3(0, 0, 1), ivec3(1, 0, 1), ivec3(1, 1, 1), ivec3(0, 1, 1));

ivec2 edges[12] = ivec2[](
    ivec2(0, 1), ivec2(1, 2), ivec2(2, 3), ivec2(3, 0),
    ivec2(4, 5), ivec2(5, 6), ivec2(6, 7), ivec2(7, 4), 
    ivec2(0, 4), ivec2(1, 5), ivec2(2, 6), ivec2(3, 7)
);


void main(){
    int triangle_variant = 0;
    float densities[8];
    for (int i = 0; i < 8; i++){
        densities[i] = getDensity(pos[0] + moves[i]);
        triangle_variant |= int(densities[i] > 0) << i;
    }
    o_triangle_variant = triangle_variant;
    uint triangle_count = counts[triangle_variant];
    o_debug_value = counts[0];
    for (int i = 0; i < 3*triangle_count; i++){
        uint edge_index = vertex_edge_indices[triangle_variant*15+i];
        ivec2 edge = edges[edge_index];
        float a = densities[edge[0]] / (densities[edge[0]] - densities[edge[1]]);
        vec3 pos = vec3(0.5, 0.5, 0.5) + pos[0] + moves[edge[0]] + (moves[edge[1]] - moves[edge[0]]) * a;
        gl_Position = MVP * vec4(pos, 1.0);

        if (edge_index == 255){
            color = vec3(triangle_count / 10.0, 0, 0);
        }else if(0 < a && a < 1){
            color = vec3(1, 1, 1);
        }else{
            color = vec3(0, 0, 0);
        }
        EmitVertex();
        if (i % 3 == 2) EndPrimitive();
    }
}