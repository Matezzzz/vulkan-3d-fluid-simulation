#version 450
#extension GL_EXT_samplerless_texture_functions : require


/**
 * Cubes raytracing algorithm - the main idea
 *  - The world is composed of blocks. In the current version, these are 1x1x1, but theoretically, they can be of any size.
 *  - 
 *  - when the ray is inside a block
 *      1) Eliminate sides based ray direction signs. 
 *          - Ignoring the third axis for a while, imagine a 2D square in normal cartesian coordinates fom [0, 0] to [1, 1], x axis is right and y is up. 
 *          - For example, for ray v=(1, 1) we can eliminate bottom and left sides, since no matter the position of the ray, it cannot exit the block through them. 3D is analogous except we deal with a cube and not a square
 *          - This is implemented in the getBlockBoundaries(ivec3 pos, vec3 dir). This function returns boundaries that can be hit from inside the block.
 *          - This would be (1, 1) for square mentioned above, since ray can hit the right side, where x = 1, and top side, where y = 1. Combined (x = 1, y = 1)
 *      2) Compute 'times' the ray requires to hit each boundary, if the ray moved at speed = length(ray_direction) = 1. These are simply equal to 'times' = (boundaries - ray_position_inside_block) / ray_direction'.
 *          - Due to step 1), all times will be greater than 0. The lowest time will be the one of the boundary to be hit first. Based on the side hit, select next block to execute this algorithm on. Start over from 1).
 */


//ray direction
layout(location = 0) in vec3 v_ray_vec;

//result color
layout(location = 0) out vec3 o_color;

//world data descriptor
layout(set = 0, binding = 0) uniform itexture3D u_world_data;

//holds inverse MVP matrix and camera position
layout( push_constant ) uniform push_const{
    mat4 MVP_inverse;
    vec3 camera_pos;
};


//get boundaries of block that can be hit by ray with given direction from inside
ivec3 getBlockBoundaries(ivec3 pos, vec3 dir){
    ivec3 boundaries = ivec3(
        (dir.x < 0) ? pos.x : (pos.x + 1),
        (dir.y < 0) ? pos.y : (pos.y + 1),
        (dir.z < 0) ? pos.z : (pos.z + 1)
    );
    return boundaries;
}
//return block id at given position
int blockIdAt(ivec3 pos){
    //get block id at given pos. 0 = lod.
    return texelFetch(u_world_data, pos, 0).r;
}

//check whether given pos is out of bounds
bool isOutOfBounds(ivec3 pos){
    ivec3 tex_size = textureSize(u_world_data, 0);
    //     whether any coord is < 0     || any coord is greater than world size
    return any(lessThan(pos, ivec3(0))) || any(greaterThan(pos, tex_size));
}

//light position. Used for shading individual blocks
const vec3 light_pos = vec3(50, 50, 50);
const vec3 light_dir = normalize(vec3(1.0, 5.0, -3.0));

//make sure the value x isn't too close to
float minMagnitude(float x, float min_mag){
    //if the value is too close to 0
    if (abs(x) < min_mag){
        //if it's < 0, return -min_magnitude, otherwise +min_magnitude
        if (x < 0) return -min_mag;
        return min_mag;
    }
    //value is fine, return it
    return x;
}


//all sides that can be hit. p = positive. n = negative
const int hit_x_p = 0;
const int hit_x_n = 1;
const int hit_y_p = 2;
const int hit_y_n = 3;
const int hit_z_p = 4;
const int hit_z_n = 5;
//hit inside - ray was inside a solid block when starting
const int hit_inside = 6;

//block types
const int block_id_none = 0;
const int block_id_air = 1;
const int block_id_gnd = 2;
const int block_id_water = 3;

//RGB colors for each block type
const vec3 none_color = vec3(0, 0, 0);
const vec3 air_color = vec3(0, 1, 1);
const vec3 ground_color = vec3(0, 1, 0);
const vec3 water_color = vec3(0, 0, 1);

//when blkock type is neither of those specified above
const vec3 invalid_block_color = vec3(1, 0, 1); 

const vec3 normals[] = vec3[](
    vec3(-1, 0, 0),
    vec3( 1, 0, 0),
    vec3(0, -1, 0),
    vec3(0,  1, 0),
    vec3(0, 0, -1),
    vec3(0, 0,  1)
);


//simple approximation of colors underwater. Linear interpolation between color and water color, coefficient = dist / water_fade_length
const int water_fade_length = 10;
vec3 computeColorThroughWater(vec3 color, float underwater_dist){
    return mix(color, water_color, min(1, underwater_dist / water_fade_length));
}

//if after a ray tracing step, the distance to a non-hit block boundary is smaller than this number, it is moved to the next block. 
//when this correction wasn't present, weird graphical glitches could happen
const float small_dist_compensation = 0.00001;
//all direction magnitudes below this number are clamped to it, otherwise, division by tiny numbers could create glitches too
const float dir_min_mag = 0.001;
//when refracting, ray start point has to be moved along ray direction a bit so it falls in the correct block after refraction. dir * refract_move_coeff is added to ray pos
const float refract_move_coeff = 0.0001;

//holds information about the result of one ray hit
struct RayHit{
    int block_id;   //id of hit block
    vec3 pos;       //position of hit
    ivec3 block_index;
    int side;       //which side was hit, one of hit_*** values
};

//non-recursive functions to figure out color of objects
vec3 sampleNone(RayHit hit1, vec3 ray_dir1){
    return none_color;
}
vec3 sampleAir(RayHit hit1, vec3 ray_dir1){
    return air_color;
}
//takes into account how much light falls onto surface, based on distance from light and angle between normal and light direction
vec3 sampleGround(RayHit hit1, vec3 ray_dir1){
    return max(dot(light_dir, normals[hit1.side]), 0.1) * ground_color * 40 / distance(light_pos, hit1.pos);
}
vec3 sampleWater(RayHit hit1, vec3 ray_dir1){
    return water_color;
}
//go through all cases, sample block based on it's ID
vec3 sampleBlockAt(RayHit hit, vec3 ray_dir){
    switch (hit.block_id){
        case block_id_none:
            return sampleNone(hit, ray_dir);
        case block_id_air:
            return sampleAir(hit, ray_dir);
        case block_id_gnd:
            return sampleGround(hit, ray_dir);
        case block_id_water:
            return sampleWater(hit, ray_dir);
        default:
            return invalid_block_color;
    }
}

//trace ray given starting point and direction - find closest block of different type than the one currently in
RayHit traceRay(vec3 start, vec3 dir){
    //make sure magnitude of every component of vec3 is larger than dir_min_mag
    dir = vec3(
        minMagnitude(dir.x, dir_min_mag),
        minMagnitude(dir.y, dir_min_mag),
        minMagnitude(dir.z, dir_min_mag)
    );
    //exact position of traced ray
    vec3 cur_pos = start;
    //index of block inside which the ray is
    ivec3 current_block_index = ivec3(cur_pos);
    //blocks of which type should be ignored for this ray
    int pass_through_block_id = blockIdAt(current_block_index);
    //bounds of current block in direction of ray
    ivec3 current_block_bounds;
    //how long will it take ray to reach each boundary
    vec3 times;
    //no move has been made yet, if ray hits now, it's inside a block
    int last_move = hit_inside;
    //id of current block the ray is in
    int current_block_id;
    //trace ray going through blocks
    for (int step_count = 0; step_count < 1000; step_count++){
        //if current block index is out of bounds, return RayHit with current position info and block_id_none
        if (isOutOfBounds(current_block_index)){
            return RayHit(block_id_none, cur_pos, current_block_index, last_move);
        }
        //get block id at given index
        current_block_id = blockIdAt(current_block_index);
        //if this isn't the block through which ray passes, return block hit
        if (current_block_id != pass_through_block_id){
            return RayHit(current_block_id, cur_pos, current_block_index, last_move);
        }
        //get current block boundaries
        current_block_bounds = getBlockBoundaries(current_block_index, dir);
        //compute times - how quickly will ray hit each boundary
        times = (current_block_bounds - cur_pos) / dir;

        /**
         * The following conditions do the same thing for different sides hit. The hit side is the one with lowest time.
         * We shall call axis of the boundary that was hit active.
         *  - position of active axis is set to boundary active axis value
         *  - update position for non-active axes by multiplying non-active axes dir part with time of active axis
         *  - compute next block index. If any non-active axis is really close to being in next block, it is moved there. This prevents glitches due to dividing/multiplying floating numbers of vastly different magnitudes.
         *  - last_move is updated based on which boundary is hit - if next block was full, it would contain the side of it which was hit.
         */
        //if x boundary is hit first
        if (times.x < times.y && times.x < times.z){
            cur_pos.x = current_block_bounds.x;
            cur_pos.yz += dir.yz * times.x;
            current_block_index = ivec3(
                current_block_bounds.x - ((dir.x < 0) ? 1 : 0),
                cur_pos.y + small_dist_compensation * sign(dir.y),
                cur_pos.z + small_dist_compensation * sign(dir.z)
            );
            last_move = (dir.x > 0) ? hit_x_p : hit_x_n;
        }
        //if y boundary is hit first
        else if (times.y < times.z){
            cur_pos.y = current_block_bounds.y;
            cur_pos.xz += dir.xz * times.y;
            current_block_index = ivec3(
                cur_pos.x + small_dist_compensation * sign(dir.x),
                current_block_bounds.y - ((dir.y < 0) ? 1 : 0),
                cur_pos.z + small_dist_compensation * sign(dir.z)
            );
            last_move = (dir.y > 0) ? hit_y_p : hit_y_n;
        }
        //if z boundary is hit first
        else{
            cur_pos.z = current_block_bounds.z;
            cur_pos.xy += dir.xy * times.z;
            current_block_index = ivec3(
                cur_pos.x + small_dist_compensation * sign(dir.x),
                cur_pos.y + small_dist_compensation * sign(dir.y),
                current_block_bounds.z - ((dir.z < 0) ? 1 : 0)
            );
            last_move = (dir.z > 0) ? hit_z_p : hit_z_n;
        }
    }
    //if no block was hit during all iterations (propablyt a glitch), return RayHit with current information
    return RayHit(block_id_none, cur_pos, current_block_index, last_move);
}


RayHit traceReflectedRay(vec3 start, vec3 dir){
    return traceRay(start + dir * refract_move_coeff, dir);
}

//trace ray after refraction - offset start by dir, so start falls into new block
RayHit traceRefractedRay(vec3 start, vec3 dir){
    return traceRay(start + dir * refract_move_coeff, dir);
}

//water sampling with reflection and refraction
vec3 sampleWaterRecursive(RayHit hit1, vec3 ray_dir1){
    //interpolate between reflection and refraction based on this constant
    float c = dot(ray_dir1, normals[hit1.side]) / 4;
    //reflection
    vec3 ray_dir2 = reflect(ray_dir1, normals[hit1.side]);
    RayHit hit2 = traceRay(hit1.pos, ray_dir2);
    vec3 result = c * sampleBlockAt(hit2, ray_dir2);
    //refraction
    ray_dir2 = refract(ray_dir1, normals[hit1.side], 1/1.33);
    hit2 = traceRefractedRay(hit1.pos, ray_dir2);
    result += (1 - c) * computeColorThroughWater(sampleBlockAt(hit2, ray_dir2), distance(hit2.pos, hit1.pos));
    return result;
}

vec3 sampleAirRecursive(RayHit hit1, vec3 ray_dir1){
    //if sampling air, it is assumed we are underwater, compute refracted  and reflected ray. c - value to interpolate between reflection and refraction
    float c = max(0, (-dot(ray_dir1, normals[hit1.side]) - 0.67) / 0.33);
    //reflection
    vec3 ray_dir2 = reflect(ray_dir1, normals[hit1.side]);
    RayHit hit2 = traceReflectedRay(hit1.pos, ray_dir2);
    vec3 result = (1 - c) * computeColorThroughWater(sampleBlockAt(hit2, ray_dir2), distance(hit2.pos, hit1.pos));
    //if the reflection isn't total, compute refraction as well
    if (c != 0){
        vec3 ray_dir2 = refract(ray_dir1, normals[hit1.side], 1.33);
        RayHit hit2 = traceRefractedRay(hit1.pos, ray_dir2);
        result += c * sampleBlockAt(hit2, ray_dir2);
    }
    return result;
}
//sample block at, but with recursive calls for reflection and refraction for water / air
vec3 sampleBlockAtRecursive(RayHit hit, vec3 ray_dir){
    switch (hit.block_id){
        case block_id_none:
            return sampleNone(hit, ray_dir);
        case block_id_air:
            return sampleAirRecursive(hit, ray_dir);
        case block_id_gnd:
            return sampleGround(hit, ray_dir);
        case block_id_water:
            return sampleWaterRecursive(hit, ray_dir);
        default:
            return vec3(1, 0, 0);
    }
}




void main(){
    //if camera is inside a block, paint the whole screen green without tracing any rays
    int inside_block = blockIdAt(ivec3(camera_pos));
    if (inside_block == block_id_gnd){
        o_color = ground_color;
        return;
    }
    //otherwise, trace ray from camera pos with given direction
    vec3 ray_dir = normalize(v_ray_vec);
    RayHit hit = traceRay(camera_pos, ray_dir);
    //sample block at target pos
    o_color = sampleBlockAtRecursive(hit, ray_dir);
    //if underwater, mix result color with color of water
    if (inside_block == block_id_water){
        o_color = computeColorThroughWater(o_color, distance(hit.pos, camera_pos));
    }
}