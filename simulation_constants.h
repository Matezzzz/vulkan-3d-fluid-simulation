#ifndef SIMULATION_CONSTANTS_H
#define SIMULATION_CONSTANTS_H

#include "just-a-vulkan-library/vulkan_include_all.h"

//dimensions of fluid simulation grid
constexpr uint32_t fluid_width = 20, fluid_height = 20, fluid_depth = 20;



/**
 * Parameters for dispatching compute shaders:
 *  - Local group dispatch size is specified in shaders, larger volume is slightly better for performance
 *  - However, when setting fluid width / height / depth, respective local group sizes must be divisors of size in each dimension (local group size (5, 5, 5) works with fluid size (20, 20, 20), but not with (21, 20, 20) (x dimension invalid))
 *  - Global dispatch size, or dispatch size in code for short, represents number of local work groups in each dimensions - for aforementioned local group size (5, 5, 5) and fluid size (20, 20, 20), dispatch size would be (20 / 5, 20 / 5, 20 / 5) = (4, 4, 4)
*/
Size3 fluid_size{fluid_width, fluid_height, fluid_depth};
//local group size (must match value written in shaders)
Size3 fluid_local_group_size{5, 5, 5};
//global dispatch size for fluid shaders
Size3 fluid_dispatch_size = fluid_size / fluid_local_group_size;


//max amount of particles to be simulated
constexpr uint32_t particle_space_size = 1000000;
//local group size for particle shaders - particle computes are 1D - size is always (particle_local_group_size, 1, 1)
constexpr uint32_t particle_local_group_size = 1000;
//global dispatch size for particle shaders
Size3 particle_dispatch_size = Size3{particle_space_size / particle_local_group_size, 1, 1};

//detailed resolution is used for rendering water surface - resolution defines number of subdivisions on each side of simulation cube
constexpr uint32_t surface_render_resolution = 5;
Size3 surface_render_size{fluid_size * surface_render_resolution};
Size3 surface_render_local_group_size{5, 5, 5};
//global dispatch size for 
Size3 surface_render_dispatch_size = surface_render_size / surface_render_local_group_size;


/**
 * Simulation parameters
 *  - These are passed to shaders using an uniform buffer, they modify behaviour of different shaders
 */

//Particles are initialized as a cube, starting at given offset with given dimensions. Resolution specifies particle count for each size.
Size3 particle_init_cube_resolution{100, 100, 100};
glm::vec3 particle_init_cube_offset{5, 2, 1.5};
glm::vec3 particle_init_cube_size{10, 10, 2};

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
glm::vec3 particle_render_color{1, 0, 0};
//particle size - this number is divided by distance from camera, particles further away will appear smaller
constexpr float particle_render_size = 10;
//rendered particle size will not be larger than this number. This is done to prevent really close particles spanning large portion of the screen
constexpr float particle_render_max_size = 20;


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
//how inertias are converted to density grid - positive densities are divided by 30 and then saved
constexpr float simulation_float_density_division_coefficient = 30;
//coefficient for blurring float densities
constexpr float simulation_float_density_diffuse_coefficient = 0.1;
//how many times the blur operation is applied
constexpr uint32_t float_density_diffuse_steps = 4;

//ambient color for all fragments
const glm::vec3 render_surface_ambient_color{0, 0, 0.3};
//direction of directional light (all rays are parallel)
const glm::vec3 render_light_direction{1, -3, 1};
const glm::vec3 render_surface_diffuse_color{0, 0.8, 0.7};

//render background color (black)
const ClearValue background_color{0.0f, 0.0f, 0.0f};

//fluid surface is rendered at the border between neighboring cells (each computation will use current cell and the one after that) - for this reason, the total number of cells in each dimension is surface_render_dimension - 1
const Size3 fluid_surface_render_size{surface_render_size.x - 1, surface_render_size.y - 1, surface_render_size.z - 1};



//each cell type is represented by a different integer value, this enum lists them all
enum class CellType{
    CELL_INACTIVE, CELL_AIR, CELL_WATER, CELL_SOLID
};



class SimulationParametersBufferData : public UniformBufferRawDataSTD140{
public:
    SimulationParametersBufferData(){
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
        .write(particle_render_max_size);
    }
};


#endif