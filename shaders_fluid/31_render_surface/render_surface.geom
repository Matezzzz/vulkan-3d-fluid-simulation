#version 450
#extension GL_EXT_scalar_block_layout : require




layout(points) in;
layout(triangle_strip, max_vertices=15) out;

layout(location = 0) in ivec3 pos[];

layout(location = 0) out vec3 normal;

layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 116) int detailed_resolution;
};
layout(set = 0, binding = 1, std430) uniform triangle_counts{
    uint counts[256];
};
layout(set = 0, binding = 2, std430) uniform triangle_vertices{
    uint vertex_edge_indices[15*256];
};
layout(set = 0, binding = 3, r32f) uniform restrict readonly image3D float_densities;

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


void renderTriangle(float[8] densities, int edge_indices_offset){
    vec3 points[3];
    for (int i = 0; i < 3; i++){
        uint edge_index = vertex_edge_indices[edge_indices_offset+i];
        ivec2 edge = edges[edge_index];
        float a = densities[edge[0]] / (densities[edge[0]] - densities[edge[1]]);
        points[i] = (vec3(0.5, 0.5, 0.5) + pos[0] + moves[edge[0]] + (moves[edge[1]] - moves[edge[0]]) * a) / detailed_resolution;
    }
    vec3 N = normalize(cross(points[1] - points[0], points[2] - points[0]));
    for (int i = 0; i < 3; i++){
        gl_Position = MVP * vec4(points[i], 1.0);
        normal = N;
        EmitVertex();
    }
    EndPrimitive();
}


void main(){
    int triangle_variant = 0;
    float densities[8];
    for (int i = 0; i < 8; i++){
        densities[i] = getDensity(pos[0] + moves[i]);
        triangle_variant |= int(densities[i] > 0) << i;
    }
    uint triangle_count = counts[triangle_variant];
    for (int i = 0; i < triangle_count; i++){
        renderTriangle(densities, triangle_variant * 15 + i * 3);
    }
}