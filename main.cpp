#include <iostream>
#include <vector>
#include <string>

#include "just-a-vulkan-library/vulkan_include_all.h"
#include "flow_command_buffer.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "marching_cubes.h"

using std::vector;
using std::string;

//window width, height
uint32_t screen_width = 1024;
uint32_t screen_height = 1024;
//window title
string app_name = "Vulkan fluid simulation";




//dimensions of fluid simulation grid
uint32_t fluid_width = 20, fluid_height = 20, fluid_depth = 20;


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
constexpr int surface_render_resolution = 5;
Size3 surface_render_size = fluid_size * surface_render_resolution;
Size3 surface_render_local_group_size{5, 5, 5};
//global dispatch size for 
Size3 surface_render_dispatch_size = surface_render_size / surface_render_local_group_size;


/**
 * Simulation parameters
 *  - These are passed to shaders using an uniform buffer, they modify behaviour of different shaders
 */

//Particles are initialized as a cube, starting at given offset with given dimensions. Resolution specifies particle count for each size.
glm::uvec3 particle_init_cube_resolution{100, 100, 100};
glm::vec3 particle_init_cube_offset{5, 2, 1.5};
glm::vec3 particle_init_cube_size{10, 10, 10};



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

//each cell type is represented by a different integer value, this enum lists them all
enum class CellType{
    CELL_INACTIVE, CELL_AIR, CELL_WATER, CELL_SOLID
};

//how many iterations are used when solving for pressure
constexpr uint32_t divergence_solve_iterations = 200;

//particle color used when rendering
glm::vec3 particle_render_color{1, 0, 0};
//particle size - this number is divided by distance from camera, particles further away will appear smaller
constexpr float particle_render_size = 20;





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

int main(){
    // * Load vulkan library *
    VulkanLibrary library;

    // * Create vulkan instance *
    const vector<string> instance_extensions {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VulkanInstance& instance = library.createInstance(VulkanInstanceCreateInfo().appName(app_name).requestExtensions(instance_extensions));
    
    // * Create window *
    Window window = instance.createWindow(screen_width, screen_height, app_name);

    // * Choose a physical device *
    PhysicalDevice physical_device = PhysicalDevices(instance).choose();

    // * Create logical device *
    Device& device = physical_device.requestExtensions({VK_KHR_SWAPCHAIN_EXTENSION_NAME})
        .requestFeatures(PhysicalDeviceFeatures().enableGeometryShader())
        .requestScreenSupportQueues({{2, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT}}, window)
        .createLogicalDevice(instance);
    
    // * Get references to requested queues *
    Queue& queue = device.getQueue(0, 0);
    Queue& present_queue = device.getQueue(0, 1);

    // * Create swapchain *
    Swapchain swapchain = SwapchainInfo(physical_device, window).setUsages(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT).create();
    
    // * Create command pool and default command buffer *
    CommandPool init_command_pool = CommandPoolInfo{0}.create();
    CommandPool render_command_pool = CommandPoolInfo{0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT}.create();

    // * Create local object creator -  used to copy data from RAM to GPU local buffers / images*
    //the largest buffer that will need to be copied is triangle list for marching cubes method - 256 variants * 15 vertices per variant * 4 bytes per int
    const uint32_t max_image_or_buffer_size_bytes = 256 * 15 * 4;
    LocalObjectCreator device_local_buffer_creator{queue, max_image_or_buffer_size_bytes};


    /**
     * Allocating buffers and images on the GPU
     *  - All textures and buffers that will be used for computation are created here
     *  - When creating a texture, several parameters must be given
     *    - Size - width, height and depth in pixels
     *    - Format - what format does each pixel have, and how many values are stored per pixel. Used values - RGBA32F - 4 floating point values, R32F - 1 float, R8U - 8byte unsigned int
     *    - Usage - how the texture will be used. Used values - transfer_dst(for filling the image with a value), storage(reading/writing texture in shaders), sampled(can be sampled with linear interpolation)
     */
    //velocities image info - RGBA32F(A dimension not used, RGB format not supported)
    ImageInfo velocity_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    ExtImage velocities_1_img = velocity_image_info.create();
    ExtImage velocities_2_img = velocity_image_info.create();

    ImageInfo cell_type_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage cell_types_img = cell_type_image_info.create();
    ExtImage cell_types_new_img = cell_type_image_info.create();

    ImageInfo pressures_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage pressures_1_img = pressures_image_info.create();
    ExtImage pressures_2_img = pressures_image_info.create();
    ExtImage divergence_img = pressures_image_info.create(); //settings for divergence are the same as for pressure

    ExtImage densities_image = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT).create();

    ImageInfo detailed_densities_info(surface_render_size.x, surface_render_size.y, surface_render_size.z, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage detailed_densities_image = detailed_densities_info.create();
    ExtImage detailed_densities_inertia_image = detailed_densities_info.create();

    ExtImage float_densities_1_img = ImageInfo(fluid_width*surface_render_resolution, fluid_height*surface_render_resolution, fluid_depth*surface_render_resolution, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT).create();
    ExtImage float_densities_2_img = ImageInfo(fluid_width*surface_render_resolution, fluid_height*surface_render_resolution, fluid_depth*surface_render_resolution, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT).create();

    //image used for depth test using rendering - same resolution as window
    ExtImage depth_test_image = ImageInfo(screen_width, screen_height, VK_FORMAT_D16_UNORM, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT).create();
    
    //Allocate memory for all created images on the GPU
    ImageMemoryObject memory({velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, pressures_1_img, pressures_2_img, divergence_img, densities_image, detailed_densities_image, detailed_densities_inertia_image,  float_densities_1_img, float_densities_2_img, depth_test_image}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    
    /**
     * Creating buffers
     *  - Required parameters
     *    - Size in bytes
     *    - Usage - storage(reading/writing buffer in shaders), transfer_dst(copying to buffer)
     */

    //buffer for all particles (3 values position, 1 for determining whether particle is active or not)
    Buffer particles_buffer = BufferInfo(particle_space_size * 4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).create();

    //buffers for marching cubes method
    MarchingCubesBuffers marching_cubes;

    //data for uniform buffer containing all simulation parameters. Buffer layout is described in shaders_fluid/fluids_uniform_buffer_layout.txt
    UniformBufferRawDataSTD140 fluid_params_uniform_buffer;
    fluid_params_uniform_buffer
        .writeIVec3((int32_t*) &fluid_size).write(fluid_size.volume())
        .write((uint32_t) CellType::CELL_INACTIVE).write((uint32_t) CellType::CELL_AIR).write((uint32_t) CellType::CELL_WATER).write((uint32_t) CellType::CELL_SOLID)
        .write(simulation_time_step).write(simulation_air_pressure).write(simulation_cell_width).write(simulation_fluid_density)
        .write(glm::uvec2(256, 256)).write(particle_init_cube_resolution).write(particle_init_cube_offset).write(particle_init_cube_size)
        .write(simulation_gravity)
        .write(simulation_diffusion_coefficient)
        .write(surface_render_resolution).write(surface_render_size.volume())
        .write(simulation_densities_max_inertia).write(simulation_inertia_increase_filled).write(simulation_inertia_required_neighbour_hits).write(simulation_inertia_increase_neighbour).write(simulation_inertia_decrease)
        .write(simulation_float_density_division_coefficient)
        .write(simulation_float_density_diffuse_coefficient)
        .write(particle_render_color).write(particle_render_size)
        .write(render_light_direction).write(render_surface_ambient_color).write(render_surface_diffuse_color)
        .write(fluid_surface_render_size);

    //create the buffer that will hold all simulation parameters
    Buffer simulation_parameters_buffer = BufferInfo(fluid_params_uniform_buffer, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).create();

    //allocate GPU memory for all buffers
    BufferMemoryObject buffer_memory({particles_buffer, marching_cubes.triangle_count_buffer, marching_cubes.vertex_edge_indices_buffer, simulation_parameters_buffer}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //load marching cubes buffer data from files and copy them to the GPU
    marching_cubes.loadData(device_local_buffer_creator);
    //copy fluid parameters buffer to the GPU
    device_local_buffer_creator.copyToLocal(fluid_params_uniform_buffer, simulation_parameters_buffer);

    //sampler used for getting velocity texture values. Includes linear interpolation, coordinates from 0 to texture size, and clamping values to edge
    VkSampler velocities_sampler = SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).disableNormalizedCoordinates().setWrapMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).create();

    //Enum of all images that are used during the simulation
    enum ImageAttachments{
        VELOCITIES_1, VELOCITIES_2, CELL_TYPES, NEW_CELL_TYPES, PRESSURES_1, PRESSURES_2, DIVERGENCES, PARTICLE_DENSITIES_IMG, DETAILED_DENSITIES_IMG, DETAILED_DENSITIES_INERTIA_IMG, PARTICLE_DENSITIES_FLOAT_1, PARTICLE_DENSITIES_FLOAT_2, IMAGE_COUNT
    };
    //enum of all buffers that are used during the simulation
    enum BufferAttachments{
        PARTICLES_BUF, MARCHING_CUBES_COUNTS_BUF, MARCHING_CUBES_EDGES_BUF, SIMULATION_PARAMS_BUF, BUFFER_COUNT
    };

    //descriptors created with this stage will be used only during compute shader
    DescriptorUsageStage usage_compute(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    //Holds all images and buffers, and the states they are currently in
    FlowBufferContext flow_context{
        {velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, pressures_1_img, pressures_2_img, divergence_img, densities_image, detailed_densities_image, detailed_densities_inertia_image,  float_densities_1_img, float_densities_2_img, depth_test_image},
        vector<PipelineImageState>(IMAGE_COUNT, PipelineImageState{ImageState{IMAGE_NEWLY_CREATED}}),
        {particles_buffer, marching_cubes.triangle_count_buffer, marching_cubes.vertex_edge_indices_buffer, simulation_parameters_buffer},
        vector<PipelineBufferState>(BUFFER_COUNT, PipelineBufferState{BufferState{BUFFER_NEWLY_CREATED}})
    };    

    //Initialize shader context - Load all shaders
    DirectoryPipelinesContext fluid_context("shaders_fluid");

    //simulation params buffer is used many times with the same parameters, create a variable for it
    FlowUniformBuffer simulation_parameters_buffer_compute_usage{"simulation_params_buffer", SIMULATION_PARAMS_BUF, usage_compute, BufferState{BUFFER_UNIFORM}};

    /**
     * All sections and their purpose in the simulation will be explained elsewhere, this is just a short introduction into parameters are used to construct each section.
     *  - A FlowSection is a single operation on the GPU. Sections can run in parallel, however, when two sections use the same texture, second one will wait for the first one to finish. Sections used are:
     *    - FlowClearColorSection - fills an image with the given clear value. Parameters are - context, image to clear, clear color
     *    - FlowComputeSection - Runs a single compute shader. Parameters - shader context, shader directory name, DESCRIPTORS_USED, global dispatch size
     *    - FlowGraphicsSection - Runs a single graphics pipeline, possibly with multiple shaders. Parameters - shader context, shader dir name, DESCRIPTORS_USED, vertex count, graphics pipeline info, render_pass
     *    - FlowComputePushConstantSection & FlowGraphicsPushConstantSection - These are normal Compute/Graphics sections with added support for push constants in shaders
     * - DESCRIPTORS_USED
     *    - This parameter describes all descriptors used by the section. This includes descriptor context and list of images/buffers:
     *       - Each descriptor contains: name in shaders, index in descriptor context, usage(in which shader stages the descriptor is used), and state, in which the image/buffer should be during this section
     *          - States include STORAGE_R(reading from shaders), STORAGE_W(writing in shaders), STORAGE_RW(both) and SAMPLER(being sampled in shaders)
     * - The following list, although long, only specifies the order of sections, and what images/buffers are used. 
     */
    

    //List of sections that will be executed before simulation start
    SectionList init_sections{
        new FlowClearColorSection(flow_context, VELOCITIES_1, ClearValue(0.f, 0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context, VELOCITIES_2, ClearValue(0.f, 0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context,   CELL_TYPES, ClearValue((uint32_t) CellType::CELL_INACTIVE)),
        new FlowClearColorSection(flow_context,  PRESSURES_1, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  PRESSURES_2, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  DIVERGENCES, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  DETAILED_DENSITIES_INERTIA_IMG, ClearValue(0)),
        new FlowComputeSection(
            fluid_context, "00_init_particles",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_W}},
                }
            },
            Size3{1,particle_space_size / particle_local_group_size, 1}
        )
    };

    
    //All sections that will run each frame, before pressure solve
    SectionList draw_section_list_1{
        new FlowClearColorSection(flow_context, NEW_CELL_TYPES, ClearValue((uint32_t) CellType::CELL_INACTIVE)),
        new FlowClearColorSection(flow_context, PARTICLE_DENSITIES_IMG, ClearValue((uint32_t) 0)),
        new FlowComputeSection(
            fluid_context, "02_update_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
                    FlowStorageImage{"particle_densities", PARTICLE_DENSITIES_IMG, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            particle_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "03_update_water",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"particle_densities", PARTICLE_DENSITIES_IMG, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_W}},
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "04_update_air",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "05_compute_extrapolated_velocities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types", CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"extrapolated_velocities", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "06_set_extrapolated_velocities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"new_cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"cell_types", CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_W}},
                    FlowStorageImage{"extrapolated_velocitites", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_R}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "07_update_cell_types",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageImage{"new_cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"cell_types", CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "08_advect",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types",      CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowCombinedImage{"velocities_src", VELOCITIES_1, usage_compute, ImageState{IMAGE_SAMPLER}, velocities_sampler},
                    FlowStorageImage{"velocities_dst",  VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "09_forces",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "10_diffuse",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types",     CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities_src", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities_dst", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "11_solids",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "12_prepare_pressure",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"divergences",DIVERGENCES,  usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowClearColorSection(flow_context, PRESSURES_1, ClearValue(simulation_air_pressure)),
        new FlowClearColorSection(flow_context, PRESSURES_2, ClearValue(simulation_air_pressure)),
    };

    //section used to solve for pressure
    auto pressure_section = new FlowComputePushConstantSection(
        fluid_context, "13_solve_pressure",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                simulation_parameters_buffer_compute_usage,
                FlowStorageImage{"cell_types", CELL_TYPES,  usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"divergences", DIVERGENCES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"pressures_1", PRESSURES_1, usage_compute, ImageState{IMAGE_STORAGE_RW}},
                FlowStorageImage{"pressures_2", PRESSURES_2, usage_compute, ImageState{IMAGE_STORAGE_RW}}
            }
        },
        fluid_dispatch_size
    );
    SectionList pressure_solve_section_list(pressure_section);

    //sections used after pressure solve
    SectionList draw_section_list_2{
        new FlowComputeSection(
            fluid_context, "14_fix_divergence",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"pressures", PRESSURES_2,  usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "15_particles",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowCombinedImage{"velocities", VELOCITIES_1,   usage_compute, ImageState{IMAGE_SAMPLER}, velocities_sampler},
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_RW}},
                }
            },
            particle_dispatch_size
        ),
        new FlowClearColorSection(flow_context, DETAILED_DENSITIES_IMG, ClearValue(0u)),
        new FlowComputeSection(
            fluid_context, "17_update_detailed_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
                    FlowStorageImage{"particle_densities", DETAILED_DENSITIES_IMG, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            }, 
            particle_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "18_compute_detailed_densities_inertia",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"particle_densities", DETAILED_DENSITIES_IMG, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"densities_inertia", DETAILED_DENSITIES_INERTIA_IMG, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            }, 
            surface_render_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "19_compute_float_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    simulation_parameters_buffer_compute_usage,
                    FlowStorageImage{"densities_inertia", DETAILED_DENSITIES_INERTIA_IMG, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"float_densities", PARTICLE_DENSITIES_FLOAT_1, usage_compute, ImageState{IMAGE_STORAGE_W}},
                }
            }, 
            surface_render_dispatch_size
        ),
    };

    //section used to blur densities
    auto float_densities_diffuse_section = new FlowComputePushConstantSection(
        fluid_context, "20_diffuse_float_densities",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                simulation_parameters_buffer_compute_usage,
                FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"densities_1", PARTICLE_DENSITIES_FLOAT_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"densities_2", PARTICLE_DENSITIES_FLOAT_2, usage_compute, ImageState{IMAGE_STORAGE_W}},
            }
        }, 
        surface_render_dispatch_size
    );
    SectionList float_densities_diffuse_section_list(float_densities_diffuse_section);

    // * Create a render pass - all graphics shaders must be executed inside one, this render pass uses previously created depth image and images that can be displayed into the app window*
    VkRenderPass render_pass = SimpleRenderPassInfo{swapchain.getFormat(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, depth_test_image.getFormat(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}.create();
    RenderPassSettings render_pass_settings{screen_width, screen_height, {background_color, {1.f, 0U}}};

    // * Create framebuffers for all swapchain images, using given depth image *
    swapchain.createFramebuffers(render_pass, depth_test_image);

    //setup render pipeline info
    PipelineInfo render_pipeline_info{screen_width, screen_height, 1};
    //all vertices will be interpreted as points
    render_pipeline_info.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    //enable depth testing
    render_pipeline_info.getDepthStencilInfo().enableDepthTest().enableDepthWrite();
    //section used for rendering particles
    auto render_section = new FlowGraphicsPushConstantSection(
        fluid_context, "30_render_particles",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT), BufferState{BUFFER_UNIFORM}),
                FlowStorageBuffer{"particles", PARTICLES_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), BufferState{BUFFER_STORAGE_R}}
            }
        },
        particle_space_size, render_pipeline_info, render_pass
    );
    //section used for rendering surface
    auto render_surface_section = new FlowGraphicsPushConstantSection(
        fluid_context, "31_render_surface",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT), BufferState{BUFFER_UNIFORM}),
                FlowUniformBuffer{"triangle_counts", MARCHING_CUBES_COUNTS_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT), BufferState{BUFFER_UNIFORM}},
                FlowUniformBuffer{"triangle_vertices", MARCHING_CUBES_EDGES_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT), BufferState{BUFFER_UNIFORM}},
                FlowStorageImage{"float_densities", PARTICLE_DENSITIES_FLOAT_2, DescriptorUsageStage{VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT}, ImageState{IMAGE_STORAGE_R}}
            }
        },
        fluid_surface_render_size.volume(), render_pipeline_info, render_pass
    );
    //section that can be used to display contents of 3D textures, used for debugging
    /*auto display_data_section = new FlowGraphicsPushConstantSection(
        fluid_context, "32_debug_display_data",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), BufferState{BUFFER_UNIFORM}),
                FlowStorageBuffer{"particle_densities", PARTICLE_DENSITIES_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), BufferState{BUFFER_STORAGE_R}}
            }
        },
        fluid_size.volume(), render_pipeline_info, render_pass
    );
    

    SectionList render_section_list(render_section, display_data_section, render_surface_section);
    */
    SectionList render_section_list(render_section, render_surface_section);

    //when all sections were created, each one recorded which descriptors it needed to function, now all descriptors can be allocated from a shared descriptor set
    fluid_context.createDescriptorPool();


    

    //Complete all sections - this is needed to update all descriptors
    init_sections.complete();   
    draw_section_list_1.complete();
    pressure_solve_section_list.complete();
    draw_section_list_2.complete();
    float_densities_diffuse_section_list.complete();
    render_section_list.complete();

    //record command buffer responsible for initializing the simulation
    FlowCommandBuffer init_buffer{init_command_pool};
    init_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    //record all sections used during initialization into the command buffer
    init_buffer.record(flow_context, init_sections);
    //transition depth image to be used as a depth attachment next frame
    init_buffer.cmdBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
        depth_test_image.createMemoryBarrier(ImageState{IMAGE_NEWLY_CREATED}, ImageState{IMAGE_DEPTH_STENCIL_ATTACHMENT})    
    );
    init_buffer.endRecord();
    //create a synchronization object to track whether initialization has finished
    SubmitSynchronization init_sync;
    //add fence - it will be signalled when submission finishes, CPU can see its' state
    init_sync.setEndFence(Fence());
    //submit all initialization commands to be executed
    queue.submit(init_buffer, init_sync);
    //wait at most one second for all commands to finish
    init_sync.waitFor(SYNC_SECOND);


    // * Initialize projection matrices and camera * 
    Camera camera{{10.f, 10.f, -10.f}, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}, window};
    //matrix to invert y axis - the one in vulkan is inverted compared to the one in OpenGL, for which GLM was written
    glm::mat4 invert_y_mat(1.0);
    invert_y_mat[1][1] = -1;
    glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f*window.getWidth() / window.getHeight(), 0.1f, 200.f) * invert_y_mat;
    glm::mat4 MVP;

    //Semaphore is a synchronization object, it will be signalled after one simulation step finishes and rendering can begin, it is watchable by the GPU
    Semaphore simulation_step_end_semaphore;

    //Watches whether simulation step is finished
    SubmitSynchronization simulation_step_synchronization;
    //add semaphore to be signalled when simulation step finishes executing
    simulation_step_synchronization.addEndSemaphore(simulation_step_end_semaphore);

    //Watches whether rendering is finished
    SubmitSynchronization render_synchronization;
    render_synchronization.addStartSemaphore(simulation_step_end_semaphore);
    //add fence to be signalled when rendering finishes
    render_synchronization.setEndFence(Fence());

    FlowCommandBuffer render_command_buffer(render_command_pool);
    FlowCommandBuffer simulation_step_buffer(render_command_pool);

    //whether simulation is paused - during a pause, simulation is static, but camera can still move
    bool paused = false;

    //while user hasn't closed the window
    while (window.running()){
        window.update();
        
        //move camera according to its' velocity and keys pressed
        camera.update(simulation_time_step);

        //if Q or E keys are pressed, pause/resume the simulation
        if (window.keyOn(GLFW_KEY_Q)) paused = true;
        if (window.keyOn(GLFW_KEY_E)) paused = false;

        //if simulation isn't paused
        if (!paused){
            simulation_step_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            //record all sections before pressure solve
            simulation_step_buffer.record(flow_context, draw_section_list_1);

            //use iterative algorithm to solve for pressures
            for (uint32_t i = 0; i < divergence_solve_iterations; i++){
                pressure_section->getPushConstantData().write("is_even_iteration", (i % 2 == 0) ? 1U : 0U);
                simulation_step_buffer.record(flow_context, pressure_solve_section_list);
            }
            //record all sections after pressure solve
            simulation_step_buffer.record(flow_context, draw_section_list_2);

            //go through all densities blur steps
            for (uint32_t i = 0; i < float_density_diffuse_steps; i++){
                float_densities_diffuse_section->getPushConstantData().write("is_even_iteration", (i % 2 == 0) ? 1U : 0U);
                simulation_step_buffer.record(flow_context, float_densities_diffuse_section_list);
            }
            
            simulation_step_buffer.endRecord();
        
            //submit recorded command buffer to the queue
            queue.submit(simulation_step_buffer, simulation_step_synchronization);
        }
        

        //get current window image to render into
        SwapchainImage swapchain_image = swapchain.acquireImage();

        render_command_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        
        //transition window image to be rendered into
        render_command_buffer.cmdBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            swapchain_image.createMemoryBarrier(ImageState{IMAGE_NEWLY_CREATED}, ImageState{IMAGE_COLOR_ATTACHMENT})
        );
        //transition all images to be ready for rendering. Normally, this is a part of command_buffer.record call, however, that is not possible during a render pass
        // -> .record is split into two parts, transitionAllImages and execute()
        render_section->transitionAllImages(render_command_buffer, flow_context);
        //display_data_section->transitionAllImages(render_command_buffer, flow_context);
        render_surface_section->transitionAllImages(render_command_buffer, flow_context);
        render_command_buffer.cmdBeginRenderPass(render_pass_settings, render_pass, swapchain_image.getFramebuffer());

        //compute model-view-projection matrix
        MVP = projection * camera.view_matrix;
        //push MVP to be used by particle render shader
        render_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        //execute the section that renders all particles
        render_section->execute(render_command_buffer);
        /*display_data_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        display_data_section->execute(render_command_buffer);*/

        //push MVP to be used by surface render shader
        render_surface_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        //execute the section that 
        render_surface_section->execute(render_command_buffer);
        render_command_buffer.cmdEndRenderPass();
        render_command_buffer.endRecord();
        
        //wait until window image can be rendered into
        swapchain.prepareToDraw();
        //execute render command buffer
        queue.submit(render_command_buffer, render_synchronization);
        //wait for rendering to finish
        render_synchronization.waitFor(SYNC_SECOND);
        
        //present rendered image
        swapchain.presentImage(swapchain_image, present_queue);

        //reset recorded command buffers
        simulation_step_buffer.resetBuffer(false);
        render_command_buffer.resetBuffer(false);
    }
    //wait for all operations on main queue to finish, then end the app
    queue.waitFor();
    return 0;
}
