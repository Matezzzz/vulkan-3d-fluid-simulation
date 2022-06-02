#ifndef SIMULATION_CONSTANTS_H
#define SIMULATION_CONSTANTS_H

#include "just-a-vulkan-library/vulkan_include_all.h"

//dimensions of fluid simulation grid
constexpr uint32_t fluid_width = 20, fluid_height = 20, fluid_depth = 20;


//!! SOME PARAMETERS ARE OUTDATED, AND MODIFYING THEM DOES NOTHING.
//!! CHANGING THEM WOULD REQUIRE REWRITING A LOT OF CODE, SO I JUST LEAVE THEM HERE
//!! (reason - they are required to pad the rest of values in the uniform buffer, removing them would mean changing all offsets in shaders)

/**
 * Parameters for dispatching compute shaders:
 *  - Local group dispatch size is specified in shaders, larger volume is slightly better for performance
 *  - However, when setting fluid width / height / depth, respective local group sizes must be divisors of size in each dimension (local group size (5, 5, 5) works with fluid size (20, 20, 20), but not with (21, 20, 20) (x dimension invalid))
 *  - Global dispatch size, or dispatch size in code for short, represents number of local work groups in each dimensions - for aforementioned local group size (5, 5, 5) and fluid size (20, 20, 20), dispatch size would be (20 / 5, 20 / 5, 20 / 5) = (4, 4, 4)
*/
const Size3 fluid_size{fluid_width, fluid_height, fluid_depth};
//local group size (must match value written in shaders)
const Size3 fluid_local_group_size{5, 5, 5};
//global dispatch size for fluid shaders
const Size3 fluid_dispatch_size = fluid_size / fluid_local_group_size;


//max amount of particles to be simulated
//!! When modifying this variable, for the simulation to work correctly, a constant in the shaders has to be changed as well
//!! Change 'const int PARTICLE_BUFFER_SIZE = 1000000;' to match the number specified here
//!! shaders affected - init_particles.comp, update_densities.comp, particles.comp, update_detailed_densities.comp, render.vert, diffuse_particles.comp
//also, for this change to have any effect, change particle_init_cube_resolution variable below (otherwise, the same amount of particles will be spawned)
constexpr uint32_t particle_space_size = 1000000;
//local group size for particle shaders - particle computes are 1D - size is always (particle_local_group_size, 1, 1)
constexpr uint32_t particle_local_group_size = 1000;
//global dispatch size for particle shaders
const Size3 particle_dispatch_size = Size3{particle_space_size / particle_local_group_size, 1, 1};

//detailed resolution is used for rendering water surface - resolution defines number of subdivisions on each side of simulation cube
constexpr uint32_t surface_render_resolution = 5;
const Size3 surface_render_size{fluid_size * surface_render_resolution};
const Size3 surface_render_local_group_size{5, 5, 5};
//global dispatch size for 
const Size3 surface_render_dispatch_size = surface_render_size / surface_render_local_group_size;


/**
 * Simulation parameters
 *  - These are passed to shaders using an uniform buffer, they modify behaviour of different shaders
 */
//Particles are initialized as a cube, starting at given offset with given dimensions. Resolution specifies particle count for each size.
const Size3 particle_init_cube_resolution{40, 40, 40};
const glm::vec3 particle_init_cube_offset{8, 8, 8};
const glm::vec3 particle_init_cube_size{4, 4, 4};

//particle w coordinate will be set to this constant when particle is active, can be any number except 0
constexpr float active_particle_w = 1;


constexpr float simulation_time_step = 0.01;

//pressure of air cells in simulation
constexpr float simulation_air_pressure = 1;
//width, height and depth of one simulation cell
constexpr float simulation_cell_width = 1;
constexpr float simulation_fluid_density = 1;
//force of gravity, applied each second
constexpr float simulation_gravity = 10.0;

//how much velocities in fluid get diffused (per second)
//diffusion is done by simply averaging cell velocity with neighboring ones 
// - given diffusion coefficient k, velocity will be (1 - 6*k) * current_cell_velocity + k * velocities_of_all_6_surrounding_cells
constexpr float simulation_diffusion_coefficient = 0.01;



//how many iterations are used when solving for pressure
constexpr uint32_t divergence_solve_iterations = 200;

//particle color used when rendering
const glm::vec3 particle_render_color{1, 0, 0};
//particle size - larger makes smoke look a bit better, but costs a lot of performance
constexpr float particle_render_size = 1.f/6.f;
//! outdated, no effect
constexpr float particle_render_max_size = 200;


//position of the fountain spewing fluid upwards
constexpr glm::uvec3 fountain_position{fluid_width / 2, fluid_height - 2, fluid_depth / 2};

constexpr float fountain_force = -3000;
//velocity at the border of solid cells will be at least this, pointing away from the solid cell, into the fluid
constexpr float solid_repel_velocity = 0.01;


/**
 * Fluid surface rendering
 *  - Start by computing number of particles in each cell on the detailed grid (detailed grid has surface_render_resolution^3 cells in each simulation cell)
 *  - Particle distribution is never uniform - there are places with zero density even in the midst of the fluid
 *    - I try to fix this problem by using a field of inertia values for each detailed grid point
 *    - When there is a particle inside current cell or at least one neighbor, increase inertia value, else decrease it (inertias are kept from the previous frame, and are increased / decreased)
 *    - Inertias are used to render the surface later on
 *  - Now inertias are converted to floating point values
 *    - values with zero inertia are deemed to be outside of the fluid, their floating point equivalent is -1.0
 *    - values with non-zero inertia are converted to positive floating point numbers
 *    - then, if floating point values on the 3D grid are interpreted as a 4D function, then the set of all points in which the function would be equal to 0 represents the fluid surface
 *  - This representation is still prone to have holes in the fluid when there are no particles present at the given time - to fix this, a blur operation is applied multiple times to the resulting function, again, in attempt to reduce smoothness errors
 *    - Blurring is done as diffusion described above
 *  - The surface is then rendered from the floating point representation using the marching cubes method described here https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu
 *    - Shading is really simple - light has two components, ambient and diffuse that are added together
 *      - Ambient is the same base color for all fragments
 *      - Diffuse is scaled down based on the angle between light direction and surface normal
 */




//! THE FOLLOWING ARE OUTDATED FOR PARTICLE SIMULATION, see top of file for explanation
//max inertia in one field
constexpr int simulation_densities_max_inertia = 100;
//how much inertia is increased during a frame when there are any particles present
constexpr int simulation_inertia_increase_filled = 4;
//how many neighbours must be filled for inertia to increase
//for smaller detailed resolutions, larger values are better - inertia won't increase into empty space, only between many filled points. For larger detailed resolutions, it is better to have each particle affect all neighboring cells so that less holes are present. 
constexpr int simulation_inertia_required_neighbour_hits = 1;
//how much is inertia increased per active neighbor
constexpr int simulation_inertia_increase_neighbour = 1;
//inertia decrease when it hasn't increased this frame
constexpr int simulation_inertia_decrease = 1;
//! OUTDATED SECTION END

//how particle densities are converted floating point - positive densities are divided by 1 and then saved
constexpr float simulation_float_density_division_coefficient = 1;
//coefficient for blurring float densities
constexpr float simulation_float_density_diffuse_coefficient = 0.1;
//how many times the blur operation is applied
constexpr uint32_t float_density_diffuse_steps = 4;

//ambient color for all fragments
const glm::vec3 render_surface_ambient_color{0, 0, 0.3};
//direction of directional light (all rays are parallel)
const glm::vec3 render_light_direction{1, -3, 1};
const glm::vec3 render_surface_diffuse_color{0, 0.8, 0.7};



//fluid surface is rendered at the border between neighboring cells (each computation will use current cell and the one after that) - for this reason, the total number of cells in each dimension is surface_render_dimension - 1
const Size3 fluid_surface_render_size{surface_render_size.x - 1, surface_render_size.y - 1, surface_render_size.z - 1};



//colors of the corners of the initial particle cube. Values in the middle are computed using linear interpolation
const vector<glm::vec3> start_cube_colors{vec3(0.889870058188535, 0.0, 1), vec3(1, 0.0, 0.1455915988212837), vec3(0.0, 0.8074791121168801, 1), vec3(0.4895510943733825, 0.0, 1), vec3(0.0, 0.12178483166098886, 1), vec3(1, 0.8265471880527357, 0.0), vec3(1, 0.6584850247274971, 0.0), vec3(0.3138543594473049, 1, 0.0)};
//how much to saturate cube colors before saving them
const float init_cube_saturation = 10.f;

//used when rendering volume - after particle fragment alpha is larger than the threshold below, it is multiplied by this before being written
const float particle_opacity_multiplier = 0.1f;
//all particle fragments with alpha less than this are discarded, when rendering both volume and front
const float particle_opacity_threshold = 0.2f;

//how many frames does the smoke_frames texture contain. After running out of frames, animation just loops, starting again with the first one
const int total_animation_frames = 30;
//how many frames should be played each second
const float animation_fps = 20.f;
//how many images are in each row & column of the smoke frames image
const int animation_texture_width_images = 6;
const int animation_texture_height_images = 5;

//how much force will be added when right-clicking
const float camera_add_force_magnitude = 10000.f;

//if linear depth difference is between the sphere and particles is smaller than this, and particles are before the cube, interpolation will happen
const float soft_particles_depth_smooth_range = 0.05;
//contrast parameter in the soft particles equation
const float soft_particles_contrast = 1;
//camera near and far plane distances
const float camera_near = 0.1;
const float camera_far = 40;

//if not moving, current frame is added with the coefficient of 0.01, while the past uses 0.99
//when moving, coefficient is dynamically scaled based on moving speed, can be up to 1 for current frame and 0 for last.
const float blending_past_coefficient = 0.01;

//light color, ambient light strength of the same color, and light direction (I use just a basic directional light)
const vec3 light_color{1.f, 1.f, 1.f};
const float ambient_light = 0.3f;
const vec3 light_direction{1.f, -1.f, -1.f};

//what color does the soft particles sphere have
const vec3 soft_particles_sphere_color{1.f, 0.f, 1.f};

//render background color (black)
const vec3 background_color{0.0f, 0.0f, 0.0f};
//background clear value - same as color above, used by vulkan functions
const ClearValue background_clear_value{background_color.x, background_color.y, background_color.z};


//diffusion = force moving particles from regions with high density to regions with lower one.
//Force is multiplied by this constant before being applied.
const float particle_diffusion_strength = .2f;
//Cap diffusion force to this magnitude
const float particle_diffusion_acceleration_cap = 1.f;

//how much to saturate final render color
const float final_render_color_saturation = 3.f;

//each cell type is represented by a different integer value, this enum lists them all
//particle simulation only uses water and solids
enum class CellType{
    CELL_INACTIVE, CELL_AIR, CELL_WATER, CELL_SOLID
};


/**
 * SimulationParametersBufferData
 *  - Holds all simulation parameters in a buffer. Buffer layout is described in shaders_fluid/fluids_uniform_buffer_layout.txt
 */
class SimulationParametersBufferData : public UniformBufferRawDataSTD140{
public:
    SimulationParametersBufferData() : UniformBufferRawDataSTD140(264) {
        writeIVec3((int32_t*) &fluid_size).write(fluid_size.volume())
        .write((uint32_t) CellType::CELL_INACTIVE).write((uint32_t) CellType::CELL_AIR).write((uint32_t) CellType::CELL_WATER).write((uint32_t) CellType::CELL_SOLID)
        .write(simulation_time_step).write(simulation_air_pressure).write(simulation_cell_width).write(simulation_fluid_density)
        .write(glm::uvec2(particle_dispatch_size.x * particle_local_group_size, particle_dispatch_size.y)).write(particle_init_cube_resolution).write(particle_init_cube_resolution.volume()).write(particle_init_cube_offset).write(particle_init_cube_size)
        .write(simulation_gravity)
        .write(simulation_diffusion_coefficient)
        .write(surface_render_resolution).write(surface_render_size.volume())
        .write(simulation_densities_max_inertia).write(simulation_inertia_increase_filled).write(simulation_inertia_required_neighbour_hits).write(simulation_inertia_increase_neighbour).write(simulation_inertia_decrease)
        .write(simulation_float_density_division_coefficient)
        .write(simulation_float_density_diffuse_coefficient)
        .write(particle_render_color).write(particle_render_size)
        .write(render_light_direction).write(render_surface_ambient_color).write(render_surface_diffuse_color)
        .write(fluid_surface_render_size)
        .write(active_particle_w)
        .write(fountain_position).write(fountain_force)
        .write(solid_repel_velocity)
        .write(particle_render_max_size)
        .writeArray(start_cube_colors).write(init_cube_saturation)
        .write(particle_opacity_multiplier).write(particle_opacity_threshold)
        .write(total_animation_frames).write(animation_fps).write(animation_texture_width_images).write(animation_texture_height_images)
        .write(camera_add_force_magnitude)
        .write(soft_particles_depth_smooth_range).write(soft_particles_contrast)
        .write(camera_near).write(camera_far)
        .write(soft_particles_sphere_color)
        .write(blending_past_coefficient).write(light_color).write(ambient_light).write(light_direction)
        .write(background_color)
        .write(particle_diffusion_strength).write(particle_diffusion_acceleration_cap)
        .write(final_render_color_saturation);
    }
};


#endif