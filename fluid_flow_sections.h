#include "just-a-vulkan-library/vulkan_include_all.h"
#include "marching_cubes.h"
#include "simulation_constants.h"

//Enum of all images that are used during the simulation
enum ImageAttachments{
    VELOCITIES_1, VELOCITIES_2, CELL_TYPES, NEW_CELL_TYPES, PRESSURES_1, PRESSURES_2, DIVERGENCES, PARTICLE_DENSITIES_IMG, DETAILED_DENSITIES_IMG, DETAILED_DENSITIES_INERTIA_IMG, PARTICLE_DENSITIES_FLOAT_1, PARTICLE_DENSITIES_FLOAT_2, IMAGE_COUNT
};
//enum of all buffers that are used during the simulation
enum BufferAttachments{
    PARTICLES_BUF, MARCHING_CUBES_COUNTS_BUF, MARCHING_CUBES_EDGES_BUF, SIMULATION_PARAMS_BUF, BUFFER_COUNT
};





class SimulationDescriptors{
    FlowDescriptorContext m_context;
    VkSampler m_velocities_sampler;
public:
    SimulationDescriptors(const UniformBufferRawDataSTD140& fluid_params_uniform_buffer, LocalObjectCreator& device_local_object_creator){
        /**
         * Allocating buffers and images on the GPU
         *  - All textures and buffers that will be used for computation are created here
         *  - When creating a texture, several parameters must be given
         *    - Size - width, height and depth in pixels
         *    - Format - what format does each pixel have, and how many values are stored per pixel. Used values - RGBA32F - 4 floating point values, R32F - 1 float, R8U - 8byte unsigned int
         *    - Usage - how the texture will be used. Used values - transfer_dst(for filling the image with a value), storage(reading/writing texture in shaders), sampled(can be sampled with linear interpolation)
         */
        //velocities image info - RGBA32F(A dimension not used, RGB format not supported)
        ImageInfo velocity_image_info = ImageInfo(fluid_size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        ExtImage velocities_1_img = velocity_image_info.create();
        ExtImage velocities_2_img = velocity_image_info.create();

        ImageInfo cell_type_image_info = ImageInfo(fluid_size, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        ExtImage cell_types_img = cell_type_image_info.create();
        ExtImage cell_types_new_img = cell_type_image_info.create();

        ImageInfo pressures_image_info = ImageInfo(fluid_size, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        ExtImage pressures_1_img = pressures_image_info.create();
        ExtImage pressures_2_img = pressures_image_info.create();
        ExtImage divergence_img = pressures_image_info.create(); //settings for divergence are the same as for pressure

        ExtImage densities_image = ImageInfo(fluid_size, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT).create();

        ImageInfo detailed_densities_info(surface_render_size, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
        ExtImage detailed_densities_image = detailed_densities_info.create();
        ExtImage detailed_densities_inertia_image = detailed_densities_info.create();

        ExtImage float_densities_1_img = ImageInfo(surface_render_size, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT).create();
        ExtImage float_densities_2_img = ImageInfo(surface_render_size, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT).create();

        //Allocate memory for all created images on the GPU
        ImageMemoryObject memory({velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, pressures_1_img, pressures_2_img, divergence_img, densities_image, detailed_densities_image, detailed_densities_inertia_image,  float_densities_1_img, float_densities_2_img}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);




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

        //create the buffer that will hold all simulation parameters
        Buffer simulation_parameters_buffer = BufferInfo(fluid_params_uniform_buffer, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).create();

        //allocate GPU memory for all buffers
        BufferMemoryObject buffer_memory({particles_buffer, marching_cubes.triangle_count_buffer, marching_cubes.vertex_edge_indices_buffer, simulation_parameters_buffer}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        //load marching cubes buffer data from files and copy them to the GPU
        marching_cubes.loadData(device_local_object_creator);
        //copy fluid parameters buffer to the GPU
        device_local_object_creator.copyToLocal(fluid_params_uniform_buffer, simulation_parameters_buffer);

        //Holds all images and buffers, and the states they are currently in
        m_context = FlowDescriptorContext{
            {velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, pressures_1_img, pressures_2_img, divergence_img, densities_image, detailed_densities_image, detailed_densities_inertia_image,  float_densities_1_img, float_densities_2_img},
            {particles_buffer, marching_cubes.triangle_count_buffer, marching_cubes.vertex_edge_indices_buffer, simulation_parameters_buffer},
        };

        //sampler used for getting velocity texture values. Includes linear interpolation, coordinates from 0 to texture size, and clamping values to edge
        m_velocities_sampler = SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).disableNormalizedCoordinates().setWrapMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).create();
    }
    operator FlowDescriptorContext&(){
        return m_context;
    }
    VkSampler getVelocitiesSampler(){
        return m_velocities_sampler;
    }
};









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


//the following two constants are used often for initializing flow sections
//descriptors created with this stage will be used only during compute shader
const VkPipelineStageFlags usage_compute(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
//simulation params buffer is used many times with the same parameters, create a variable for it
const FlowUniformBuffer simulation_parameters_buffer_compute_usage{"simulation_params_buffer", SIMULATION_PARAMS_BUF, usage_compute, BufferState{BUFFER_UNIFORM}};


class SimulationInitializationSections : public FlowSectionList{
public:
    SimulationInitializationSections(DirectoryPipelinesContext& fluid_context, FlowDescriptorContext& flow_context) :
        FlowSectionList{flow_context,
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
                particle_dispatch_size
            )
        }
    {}
};


class SimulationStepSections : public FlowSectionList{
public:
    SimulationStepSections(DirectoryPipelinesContext& fluid_context, FlowDescriptorContext& flow_context, VkSampler velocities_sampler) :
        FlowSectionList{flow_context,
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
                fluid_context, "12_compute_divergence",
                FlowPipelineSectionDescriptors{
                    flow_context,
                    vector<FlowPipelineSectionDescriptorUsage>{
                        FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                        FlowStorageImage{"divergences",DIVERGENCES,  usage_compute, ImageState{IMAGE_STORAGE_W}}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowClearColorSection(flow_context, PRESSURES_1, ClearValue(simulation_air_pressure)),
            new FlowClearColorSection(flow_context, PRESSURES_2, ClearValue(simulation_air_pressure)),
            new FlowLoopPushConstantSection<FlowComputePushConstantSection>(divergence_solve_iterations, flow_context,
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
            ),
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
            new FlowLoopPushConstantSection<FlowComputePushConstantSection>(float_density_diffuse_steps, flow_context,
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
            )
        }
    {}
};


/**
 * RenderParticlesSection
 *  - This section renders all particles in the simulation 
 */
class RenderParticlesSection : public FlowGraphicsPushConstantSection{
public:
    RenderParticlesSection(DirectoryPipelinesContext& fluid_context, FlowDescriptorContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        FlowGraphicsPushConstantSection(
            fluid_context, "30_render_particles",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, BufferState{BUFFER_UNIFORM}),
                    FlowStorageBuffer{"particles", PARTICLES_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, BufferState{BUFFER_STORAGE_R}}
                }
            },
            particle_space_size, render_pipeline_info, render_pass
        )
    {}
};


/**
 * RenderParticlesSection
 *  - This section renders the fluid surface
 */
class RenderSurfaceSection : public FlowGraphicsPushConstantSection{
public:
    RenderSurfaceSection(DirectoryPipelinesContext& fluid_context, FlowDescriptorContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        FlowGraphicsPushConstantSection(
            fluid_context, "31_render_surface",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, BufferState{BUFFER_UNIFORM}),
                    FlowUniformBuffer{"triangle_counts", MARCHING_CUBES_COUNTS_BUF, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, BufferState{BUFFER_UNIFORM}},
                    FlowUniformBuffer{"triangle_vertices", MARCHING_CUBES_EDGES_BUF, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, BufferState{BUFFER_UNIFORM}},
                    FlowStorageImage{"float_densities", PARTICLE_DENSITIES_FLOAT_2, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, ImageState{IMAGE_STORAGE_R}}
                }
            },
            fluid_surface_render_size.volume(), render_pipeline_info, render_pass
        )
    {}
};


/**
 * RenderDataSection
 *  - This section can be used to display data on a grid. It is disabled by default, it was used for debugging
 */
class RenderDataSection : public FlowGraphicsPushConstantSection{
public:
    RenderDataSection(DirectoryPipelinesContext& fluid_context, FlowDescriptorContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        FlowGraphicsPushConstantSection(
            fluid_context, "32_debug_display_data",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, BufferState{BUFFER_UNIFORM}),
                    FlowStorageImage{"particle_densities", DETAILED_DENSITIES_IMG, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, ImageState{IMAGE_STORAGE_R}}
                }
            },
            fluid_size.volume(), render_pipeline_info, render_pass
        )
    {}
};



/**
 * RenderSections
 *  - Contains three subsections, one for rendering particles, other for surface, third for data. They can be toggled on / off in real time using flags particles_on, surface_on and data_on
 */
class RenderSections{
    RenderParticlesSection m_particles;
    RenderSurfaceSection m_surface;
    RenderDataSection m_data;
public:
    bool particles_on = true;
    bool surface_on = true;
    bool data_on = false;

    RenderSections(DirectoryPipelinesContext& fluid_context, FlowDescriptorContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        m_particles (fluid_context, flow_context, render_pipeline_info, render_pass),
        m_surface   (fluid_context, flow_context, render_pipeline_info, render_pass),
        m_data      (fluid_context, flow_context, render_pipeline_info, render_pass)
    {}
    void complete(){
        m_particles.complete();
        m_surface.complete();
        m_data.complete();
    }
    void transition(CommandBuffer& command_buffer, FlowDescriptorContext& flow_context){
        if (particles_on) m_particles.transition(command_buffer, flow_context);
        if (surface_on)   m_surface.  transition(command_buffer, flow_context);
        if (data_on)      m_data.     transition(command_buffer, flow_context);
    }
    void execute(CommandBuffer& command_buffer, const glm::mat4& MVP){
        if (particles_on){
            m_particles.getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
            m_particles.execute(command_buffer);
        }
        if (surface_on){
            m_surface.getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
            m_surface.execute(command_buffer);
        }
        if (data_on){
            m_data.getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
            m_data.execute(command_buffer);
        }
    }
};
