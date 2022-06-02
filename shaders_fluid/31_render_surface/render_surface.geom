#version 450

/**
 * render_surface.geom
 *  - Geometry shader for rendering water surface. Uses the marching cubes method described here https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu
 */


layout(points) in;
layout(triangle_strip, max_vertices=15) out;

layout(location = 0) in ivec3 pos[];

layout(location = 0) out vec3 normal;

layout(set = 0, binding = 0) uniform simulation_params_buffer{
    layout(offset = 116) int detailed_resolution;
};

//Marching cubes method - we want to render a surface defined by a 3D function f outputting a scalar. If f(x,y,z) < 0, point is outside the surface, f(x,y,z) > 0 means point is inside. f(x,y,z) defines points on the surface we want to render.
//space is divided into cells, each cell has 8 corners, in each corner, the function can either be positive or negative. That means there are 2^8 = 256 possibilities in which corners can be positive/negative (configuration can be expressed by 8 bits)
//if I take all eight corners, and for each one, I determine whether f in it is positive or negative. This gives me 8 bits, joining them together gives me the configuration number
//for each configuration, there are given triangles that define the surface
//triangle_counts represents how many triangles have to be rendered for given configuration, these go from 0 to 5
//uvec4 is used to work around glsl array memory alignment rules
layout(set = 0, binding = 1) uniform triangle_counts{
    uvec4 counts[256/4];
};
//triangle_vertices describe edge indices, 0 - 15 based on triangle count. 3 edge indices will be converted to 3 vertex positions and will form one triangle.
//uvec4 is used to work around glsl array memory alignment rules
layout(set = 0, binding = 2) uniform triangle_vertices{
    uvec4 vertex_edge_indices[15*256/4];
};
layout(set = 0, binding = 3, r32f) uniform restrict readonly image3D float_densities;

float getDensity(ivec3 index){
    return imageLoad(float_densities, index).r;
}

layout (push_constant) uniform render_surface_push{
    mat4 MVP;
};

//intex into counts like it was an array of uints
uint getEdgeCount(int i){
    return counts[i/4][i%4];
}

//intex into edge indices like it was an array of uints
uint getEdgeIndex(int i){
    return vertex_edge_indices[i/4][i%4];
}



//8 corners and the moves required to reach each one of them
ivec3 moves[8] = ivec3[](ivec3(0, 0, 0), ivec3(1, 0, 0), ivec3(1, 1, 0), ivec3(0, 1, 0), ivec3(0, 0, 1), ivec3(1, 0, 1), ivec3(1, 1, 1), ivec3(0, 1, 1));

//edges - from which to which corner does this edge go
ivec2 edges[12] = ivec2[](
    ivec2(0, 1), ivec2(1, 2), ivec2(2, 3), ivec2(3, 0),
    ivec2(4, 5), ivec2(5, 6), ivec2(6, 7), ivec2(7, 4), 
    ivec2(0, 4), ivec2(1, 5), ivec2(2, 6), ivec2(3, 7)
);

//render one triangle given 8 densities (values of function f(x,y,z) in corners) and information about how 
void renderTriangle(float[8] densities, int edge_indices_offset){
    //each point of current triangle in world space
    vec3 points[3];
    //go through three points in current triangle
    for (int i = 0; i < 3; i++){
        //find out the index of current edge
        uint edge_index = getEdgeIndex(edge_indices_offset+i);
        ivec2 edge = edges[edge_index];
        //find out where on the edge the vertex is using interpolation (if density in first point is -1 and in second 3, point will be one quarter from point 1 - that's where function should be 0)
        float a = densities[edge[0]] / (densities[edge[0]] - densities[edge[1]]);
        //compute point position in world space, vec3(0.5) is added to shift rendered cells half a unit to right, pos
        points[i] = (vec3(0.5, 0.5, 0.5) + pos[0] + moves[edge[0]] + (moves[edge[1]] - moves[edge[0]]) * a) / detailed_resolution;
    }
    //find out surface normal by taking the cross product of differences between triangle side vectors
    vec3 N = normalize(cross(points[1] - points[0], points[2] - points[0]));
    //go through the points once again
    for (int i = 0; i < 3; i++){
        //compute point position in screen space
        gl_Position = MVP * vec4(points[i], 1.0);
        //set point normal
        normal = N;
        //emit this vertex
        EmitVertex();
    }
    //end one triangle
    EndPrimitive();
}


void main(){
    //which configuration current cube is
    int configuration = 0;
    //densities in all 8 corners
    float densities[8];
    //go through all the corners
    for (int i = 0; i < 8; i++){
        //save density in current corner
        densities[i] = getDensity(pos[0] + moves[i]);
        //add current corner to configuration
        configuration |= int(densities[i] > 0) << i;
    }
    //read how many triangles should be rendered for current configuration
    uint triangle_count = getEdgeCount(configuration);
    //loop over all triangles
    for (int i = 0; i < triangle_count; i++){
        //configuration * 15 represents offset from start of vertex_edge_indices buffer, i * 3 represents current triangle offset in this configuration
        renderTriangle(densities, configuration * 15 + i * 3);
    }
}