#include "just-a-vulkan-library/vulkan_include_all.h"
#include "marching_cubes.h"
#include "simulation_constants.h"





//Enum of all images that are used during the simulation
enum ImageAttachments{
    RENDER_VOLUME_IMAGE, RENDER_FRONT_COLORS_IMAGE, RENDER_FRONT_NORMALS_IMAGE, RENDER_FRONT_DEPTH_IMAGE, RENDER_SPHERES_DEPTH_IMAGE,
    BLURRED_RENDER_1, BLURRED_NORMAL_1, BLURRED_DEPTH_1, BLURRED_RENDER_2, BLURRED_NORMAL_2, BLURRED_DEPTH_2,
    VELOCITIES_1, VELOCITIES_2, CELL_TYPES, NEW_CELL_TYPES, PRESSURES_1, PRESSURES_2, DIVERGENCES,
    PARTICLE_DENSITIES_IMG, DETAILED_DENSITIES_IMG, DETAILED_DENSITIES_INERTIA_IMG, PARTICLE_DENSITIES_FLOAT_1, PARTICLE_DENSITIES_FLOAT_2,
    SMOKE_ANIMATION_IMG, IMAGE_COUNT
};
//enum of all buffers that are used during the simulation
enum BufferAttachments{
    PARTICLE_POSITIONS_BUF, PARTICLE_VELOCITIES_BUF, PARTICLE_COLORS_BUF, SPHERE_VERTEX_BUFFER, MARCHING_CUBES_COUNTS_BUF, MARCHING_CUBES_EDGES_BUF, SIMULATION_PARAMS_BUF, BUFFER_COUNT
};





class SimulationDescriptors : public FlowContext{
    VkSampler m_velocities_sampler;
public:
    SimulationDescriptors(const UniformBufferRawDataSTD140& fluid_params_uniform_buffer, const glm::uvec2& screen_size, Queue& transfer_queue) :
        FlowContext(IMAGE_COUNT, BUFFER_COUNT)
    {
        /**
         * Allocating buffers and images on the GPU
         *  - All textures and buffers that will be used for computation are created here
         *  - When creating a texture, several parameters must be given
         *    - Size - width, height and depth in pixels
         *    - Format - what format does each pixel have, and how many values are stored per pixel. Used values - RGBA32F - 4 floating point values, R32F - 1 float, R8U - 8byte unsigned int
         *    - Usage - how the texture will be used. Used values - transfer_dst(for filling the image with a value), storage(reading/writing texture in shaders), sampled(can be sampled with linear interpolation)
         */
        //velocities image info - RGBA32F(A dimension not used, RGB format not supported)

    
        ImageInfo render_image_info{screen_size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
        ImageInfo depth_image_info{screen_size, VK_FORMAT_D16_UNORM, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
        ImageInfo depth_blurred_info{screen_size, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};

        addImage(RENDER_VOLUME_IMAGE, render_image_info.create());
        addImage(RENDER_FRONT_COLORS_IMAGE, render_image_info.create());
        addImage(RENDER_FRONT_NORMALS_IMAGE, render_image_info.create());
        addImage(RENDER_FRONT_DEPTH_IMAGE, depth_image_info.create());
        addImage(RENDER_SPHERES_DEPTH_IMAGE, depth_image_info.create());
        addImage(BLURRED_RENDER_1, render_image_info.create());
        addImage(BLURRED_NORMAL_1, render_image_info.create());
        addImage(BLURRED_DEPTH_1, depth_blurred_info.create());


        //these two have to be cleared before running, so they have to have the transfer DST bit set
        render_image_info.addUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        depth_blurred_info.addUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        addImage(BLURRED_RENDER_2, render_image_info.create());
        addImage(BLURRED_NORMAL_2, render_image_info.create());
        addImage(BLURRED_DEPTH_2, depth_blurred_info.create());


        ImageInfo fluid_rgba32f{fluid_size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
        addImage(VELOCITIES_1, fluid_rgba32f.create());
        addImage(VELOCITIES_2, fluid_rgba32f.create());

        ImageInfo fluid_r8ui{fluid_size, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
        addImage(CELL_TYPES, fluid_r8ui.create());
        addImage(NEW_CELL_TYPES, fluid_r8ui.create());

        ImageInfo fluid_r32f{fluid_size, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
        addImage(PRESSURES_1, fluid_r32f.create());
        addImage(PRESSURES_2, fluid_r32f.create());
        addImage(DIVERGENCES, fluid_r32f.create());

        ImageInfo fluid_r32ui{fluid_size, VK_FORMAT_R32_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
        addImage(PARTICLE_DENSITIES_IMG, fluid_r32ui.create());

        ImageInfo detailed_r32ui = fluid_r32ui.copy().setSize(surface_render_size);
        addImage(DETAILED_DENSITIES_IMG, detailed_r32ui.create());
        addImage(DETAILED_DENSITIES_INERTIA_IMG, detailed_r32ui.create());

        ImageInfo detailed_r32f = fluid_r32f.copy().setSize(surface_render_size).addUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
        addImage(PARTICLE_DENSITIES_FLOAT_1, detailed_r32f.create());
        addImage(PARTICLE_DENSITIES_FLOAT_2, detailed_r32f.create());

        ImageInfo smoke_info = ImageInfo(1536, 1279, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        addImageUploadable(SMOKE_ANIMATION_IMG, smoke_info.create());
        


        /**
         * Creating buffers
         *  - Required parameters
         *    - Size in bytes
         *    - Usage - storage(reading/writing buffer in shaders), transfer_dst(copying to buffer)
         */
        //BUFFERS

        BufferInfo particle_info{particle_space_size * 4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
        addBuffer(PARTICLE_POSITIONS_BUF, particle_info.create());
        addBuffer(PARTICLE_VELOCITIES_BUF, particle_info.create());
        addBuffer(PARTICLE_COLORS_BUF, particle_info.create());

        Vertices sphere_vertices = VertexCreator::unitSphere(40, 40);
        BufferInfo sphere_info{sphere_vertices.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        addBufferVertexUploadable(SPHERE_VERTEX_BUFFER, sphere_info.create(), sphere_vertices.size() / 3);

        MarchingCubesBuffers marching_cubes;
        addBufferUploadable(MARCHING_CUBES_COUNTS_BUF, marching_cubes.triangle_count_buffer);
        addBufferUploadable(MARCHING_CUBES_EDGES_BUF, marching_cubes.vertex_edge_indices_buffer);

        BufferInfo simulation_params{fluid_params_uniform_buffer, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        addBufferUploadable(SIMULATION_PARAMS_BUF, simulation_params.create());

        //assign memory to all images and buffers
        allocate();


        FlowStagingBuffer transfer{*this, getLargestCopySize()};
        ImageData smoke_data{"smoke_frames.png", 4};
        transfer.transferImage(transfer_queue, SMOKE_ANIMATION_IMG, smoke_data);
        transfer.transferBuffer(transfer_queue, MARCHING_CUBES_COUNTS_BUF, marching_cubes.loadCounts());
        transfer.transferBuffer(transfer_queue, MARCHING_CUBES_EDGES_BUF, marching_cubes.loadEdgeIndices());
        transfer.transferBuffer(transfer_queue, SIMULATION_PARAMS_BUF, fluid_params_uniform_buffer);
        transfer.transferBuffer(transfer_queue, SPHERE_VERTEX_BUFFER, sphere_vertices);
    
        //sampler used for getting velocity texture values. Includes linear interpolation, coordinates from 0 to texture size, and clamping values to edge
        m_velocities_sampler = SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).setWrapMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).create();
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
const FlowUniformBuffer simulation_parameters_buffer_compute_usage{"simulation_params_buffer", SIMULATION_PARAMS_BUF, usage_compute};


/**** DESCRIPTIONS OF ALL SECTIONS AND THEIR PURPOSE IN THE SIMULATION IS DESCRIBED IN README.md ****/
class SimulationInitializationSections : public FlowSectionList{
public:
    SimulationInitializationSections(DirectoryPipelinesContext& fluid_context, FlowContext& flow_context) :
        FlowSectionList{flow_context, {
            new FlowClearColorSection(flow_context, VELOCITIES_1, ClearValue(0.f, 0.f, 0.f, 0.f)),
            new FlowClearColorSection(flow_context,   CELL_TYPES, ClearValue((uint32_t) CellType::CELL_INACTIVE)),
            new FlowClearColorSection(flow_context,  DETAILED_DENSITIES_INERTIA_IMG, ClearValue(0)),
            new FlowComputeSection(
                flow_context, fluid_context, "00_init_particles",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageBufferW{"particle_positions", PARTICLE_POSITIONS_BUF, usage_compute},
                        FlowStorageBufferW{"particle_velocities", PARTICLE_VELOCITIES_BUF, usage_compute},
                        FlowStorageBufferW{"particle_colors", PARTICLE_COLORS_BUF, usage_compute},
                    }
                },
                particle_dispatch_size
            ),
            new FlowClearColorSection(flow_context, BLURRED_RENDER_2, ClearValue(0.f, 0.f, 0.f, 0.f)),
            new FlowClearColorSection(flow_context, BLURRED_NORMAL_2, ClearValue(0.f, 0.f, 0.f, 0.f)),
            new FlowClearColorSection(flow_context, BLURRED_DEPTH_2, ClearValue(1.f)),
            new FlowClearColorSection(flow_context, PARTICLE_DENSITIES_FLOAT_2, ClearValue(0.f))
        }}
    {}
};


class SimulationStepSections : public FlowSectionList{
public:
    SimulationStepSections(DirectoryPipelinesContext& fluid_context, FlowContext& flow_context, VkSampler velocities_sampler) :
        FlowSectionList{flow_context, {
            new FlowClearColorSection(flow_context, PARTICLE_DENSITIES_IMG, ClearValue((uint32_t) 0)),
            new FlowComputeSection(
                flow_context, fluid_context, "01_update_densities",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageBufferR{"particles", PARTICLE_POSITIONS_BUF, usage_compute},
                        FlowStorageImageRW{"particle_densities", PARTICLE_DENSITIES_IMG, usage_compute}
                    }
                },
                particle_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "02_update_water",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"particle_densities", PARTICLE_DENSITIES_IMG, usage_compute},
                        FlowStorageImageW{"cell_types", NEW_CELL_TYPES, usage_compute},
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "03_update_air",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageRW{"cell_types", NEW_CELL_TYPES, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "04_compute_extrapolated_velocities",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES, usage_compute},
                        FlowStorageImageR{"velocities", VELOCITIES_1, usage_compute},
                        FlowStorageImageW{"extrapolated_velocities", VELOCITIES_2, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "05_set_extrapolated_velocities",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"new_cell_types", NEW_CELL_TYPES, usage_compute},
                        FlowStorageImageR{"cell_types", CELL_TYPES, usage_compute},
                        FlowStorageImageW{"velocities", VELOCITIES_1, usage_compute},
                        FlowStorageImageR{"extrapolated_velocitites", VELOCITIES_2, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "06_update_cell_types",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        FlowStorageImageR{"new_cell_types", NEW_CELL_TYPES, usage_compute},
                        FlowStorageImageW{"cell_types", CELL_TYPES, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "07_advect",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types",      CELL_TYPES,   usage_compute},
                        FlowCombinedImage{"velocities_src", VELOCITIES_1, usage_compute, velocities_sampler},
                        FlowStorageImageW{"velocities_dst",  VELOCITIES_2, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputePushConstantSection(
                flow_context, fluid_context, "08_forces",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,   usage_compute},
                        FlowStorageImageRW{"velocities", VELOCITIES_2, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "09_diffuse",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types",     CELL_TYPES,   usage_compute},
                        FlowStorageImageR{"velocities_src", VELOCITIES_2, usage_compute},
                        FlowStorageImageW{"velocities_dst", VELOCITIES_1, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "10_solids",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,   usage_compute},
                        FlowStorageImageRW{"velocities", VELOCITIES_1, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "11_compute_divergence",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        FlowStorageImageR{"velocities", VELOCITIES_1, usage_compute},
                        FlowStorageImageW{"divergences",DIVERGENCES,  usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowClearColorSection(flow_context, PRESSURES_1, ClearValue(simulation_air_pressure)),
            new FlowClearColorSection(flow_context, PRESSURES_2, ClearValue(simulation_air_pressure)),
            new FlowComputeLoopPushConstantSection(
                flow_context, fluid_context, "12_solve_pressure",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,  usage_compute},
                        FlowStorageImageR{"divergences", DIVERGENCES, usage_compute},
                        FlowStorageImageRW{"pressures_1", PRESSURES_1, usage_compute},
                        FlowStorageImageRW{"pressures_2", PRESSURES_2, usage_compute}
                    }
                },
                fluid_dispatch_size, divergence_solve_iterations
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "13_fix_divergence",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,   usage_compute},
                        FlowStorageImageR{"pressures", PRESSURES_2,  usage_compute},
                        FlowStorageImageRW{"velocities", VELOCITIES_1, usage_compute}
                    }
                },
                fluid_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "14_particles",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,   usage_compute},
                        FlowCombinedImage{"velocities", VELOCITIES_1,   usage_compute, velocities_sampler},
                        FlowStorageBufferRW{"particle_positions", PARTICLE_POSITIONS_BUF, usage_compute},
                        FlowStorageBufferRW{"particle_velocities", PARTICLE_VELOCITIES_BUF, usage_compute},
                    }
                },
                particle_dispatch_size
            ),
            new FlowClearColorSection(flow_context, DETAILED_DENSITIES_IMG, ClearValue(0u)),
            new FlowComputeSection(
                flow_context, fluid_context, "15_update_detailed_densities",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageBufferR{"particles", PARTICLE_POSITIONS_BUF, usage_compute},
                        FlowStorageImageRW{"particle_densities", DETAILED_DENSITIES_IMG, usage_compute}
                    }
                }, 
                particle_dispatch_size
            ),
            /*new FlowComputeSection(
                flow_context, fluid_context, "16_compute_detailed_densities_inertia",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImage{"particle_densities", DETAILED_DENSITIES_IMG, usage_compute, ImageState{IMAGE_STORAGE_R}},
                        FlowStorageImage{"densities_inertia", DETAILED_DENSITIES_INERTIA_IMG, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                    }
                }, 
                surface_render_dispatch_size
            ),*/
            new FlowComputeSection(
                flow_context, fluid_context, "17_compute_float_densities",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"particle_densities", DETAILED_DENSITIES_IMG, usage_compute},
                        FlowStorageImageW{"float_densities", PARTICLE_DENSITIES_FLOAT_1, usage_compute},
                    }
                }, 
                surface_render_dispatch_size
            ),
            new FlowComputeSection(
                flow_context, fluid_context, "17b_diffuse_particles",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,   usage_compute},
                        FlowCombinedImage{"float_densities", PARTICLE_DENSITIES_FLOAT_1, usage_compute, velocities_sampler},
                        FlowStorageBufferRW{"particle_positions", PARTICLE_POSITIONS_BUF, usage_compute},
                        FlowStorageBufferRW{"particle_velocities", PARTICLE_VELOCITIES_BUF, usage_compute},
                    }
                }, 
                particle_dispatch_size
            ),
            new FlowComputeLoopPushConstantSection(
                flow_context, fluid_context, "18_diffuse_float_densities",
                FlowPipelineSectionDescriptors{
                    vector<FlowPipelineSectionDescriptorUsage>{
                        simulation_parameters_buffer_compute_usage,
                        FlowStorageImageR{"cell_types", CELL_TYPES,   usage_compute},
                        FlowStorageImageRW{"densities_1", PARTICLE_DENSITIES_FLOAT_1, usage_compute},
                        FlowStorageImageRW{"densities_2", PARTICLE_DENSITIES_FLOAT_2, usage_compute},
                    }
                }, 
                surface_render_dispatch_size, float_density_diffuse_steps
            )
        }}
    {}
};


/**
 * RenderParticlesSection
 *  - This section renders all particles in the simulation 
 */
class RenderParticlesSection : public FlowGraphicsPushConstantSection{
public:
    RenderParticlesSection(DirectoryPipelinesContext& fluid_context, FlowContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        FlowGraphicsPushConstantSection(
            flow_context, fluid_context, "30_render_particles",
            FlowPipelineSectionDescriptors{
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                    FlowStorageBufferR{"particles", PARTICLE_POSITIONS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT},
                    FlowCombinedImage{"smoke_animation", SMOKE_ANIMATION_IMG, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).create()}
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
    RenderSurfaceSection(DirectoryPipelinesContext& fluid_context, FlowContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        FlowGraphicsPushConstantSection(
            flow_context, fluid_context, "31_render_surface",
            FlowPipelineSectionDescriptors{
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                    FlowUniformBuffer{"triangle_counts", MARCHING_CUBES_COUNTS_BUF, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT},
                    FlowUniformBuffer{"triangle_vertices", MARCHING_CUBES_EDGES_BUF, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT},
                    FlowStorageImageR{"float_densities", PARTICLE_DENSITIES_FLOAT_2, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT}
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
    RenderDataSection(DirectoryPipelinesContext& fluid_context, FlowContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        FlowGraphicsPushConstantSection(
            flow_context, fluid_context, "32_debug_display_data",
            FlowPipelineSectionDescriptors{
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT),
                    FlowStorageImageR{"particle_densities", PARTICLE_DENSITIES_IMG, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT}
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
    bool surface_on = false;
    bool data_on = false;

    RenderSections(DirectoryPipelinesContext& fluid_context, FlowContext& flow_context, const PipelineInfo& render_pipeline_info, VkRenderPass render_pass) :
        m_particles (fluid_context, flow_context, render_pipeline_info, render_pass),
        m_surface   (fluid_context, flow_context, render_pipeline_info, render_pass),
        m_data      (fluid_context, flow_context, render_pipeline_info, render_pass)
    {}
    void complete(){
        m_particles.complete();
        m_surface.complete();
        m_data.complete();
    }
    void transition(CommandBuffer& command_buffer){
        //for each section - if enabled, transition all descriptors to be used by it
        if (particles_on) m_particles.transition(command_buffer);
        if (surface_on)   m_surface.  transition(command_buffer);
        if (data_on)      m_data.     transition(command_buffer);
    }
    void execute(CommandBuffer& command_buffer, const glm::mat4& MVP, const glm::vec3& camera_pos, float time){
        //for each section - if enabled, write MVP matrix, then render using it
        if (particles_on){
            m_particles.getPushConstantData()
                .write("MVP", glm::value_ptr(MVP), 16)
                .write("camera_pos", glm::value_ptr(camera_pos), 3)
                .write("time", time);
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
