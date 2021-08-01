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

uint32_t screen_width = 1400;
uint32_t screen_height = 1400;
string app_name = "Vulkan fluid simulation";



uint32_t fluid_width = 20, fluid_height = 20, fluid_depth = 20;
Size3 fluid_size{fluid_width, fluid_height, fluid_depth};
Size3 fluid_local_group_size{10, 10, 1};
Size3 fluid_dispatch_size = fluid_size / fluid_local_group_size;



Size3 particle_space_size{256*256, 1, 1};
Size3 particle_local_group_size{256, 1, 1};
Size3 particle_dispatch_size = particle_space_size / particle_local_group_size;

const int detailed_densities_resolution = 4;
Size3 detailed_densities_size = fluid_size * detailed_densities_resolution;
Size3 detailed_densities_local_group_size{10, 10, 1};
Size3 detailed_densities_dispatch_size = detailed_densities_size / detailed_densities_local_group_size;


const float pressure_air = 1.0;

constexpr uint32_t divergence_solve_iterations = 100;

constexpr uint32_t float_density_diffuse_steps = 4;



int main()
{
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

    // * Create local object creator -  used to copy data from RAM to GPU * 
    const uint32_t max_image_or_buffer_size_bytes = 256 * 15 * 4;
    LocalObjectCreator device_local_buffer_creator{queue, max_image_or_buffer_size_bytes};

    MarchingCubesBuffers marching_cubes;


    // * Creating an image on the GPU *
    ImageInfo velocity_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    ExtImage velocities_1_img = velocity_image_info.create();
    ExtImage velocities_2_img = velocity_image_info.create();

    ImageInfo cell_type_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage cell_types_img = cell_type_image_info.create();
    ExtImage cell_types_new_img = cell_type_image_info.create();

    Buffer particles_buffer = BufferInfo(particle_space_size.volume() * 4 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).create();
    Buffer densities_buffer = BufferInfo(fluid_size.volume() * sizeof(int), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT).create();
    
    BufferInfo detailed_densities_info(detailed_densities_size.volume() * sizeof(int), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer detailed_densities_buffer = detailed_densities_info.create();
    Buffer detailed_densities_inertia_buffer = detailed_densities_info.create();


    ImageInfo pressures_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage pressures_1_img = pressures_image_info.create();
    ExtImage pressures_2_img = pressures_image_info.create();
    ExtImage divergence_img = pressures_image_info.create(); //settings for divergence are the same as for pressure
    ExtImage float_densities_1_img = ImageInfo(fluid_width*detailed_densities_resolution, fluid_height*detailed_densities_resolution, fluid_depth*detailed_densities_resolution, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT).create();
    ExtImage float_densities_2_img = ImageInfo(fluid_width*detailed_densities_resolution, fluid_height*detailed_densities_resolution, fluid_depth*detailed_densities_resolution, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT).create();


    ExtImage depth_test_image = ImageInfo(screen_width, screen_height, VK_FORMAT_D16_UNORM, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT).create();

    ImageMemoryObject memory({velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, pressures_1_img, pressures_2_img, divergence_img, float_densities_1_img, float_densities_2_img, depth_test_image}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    BufferMemoryObject buffer_memory({particles_buffer, densities_buffer, marching_cubes.triangle_count_buffer, marching_cubes.vertex_edge_indices_buffer, detailed_densities_buffer, detailed_densities_inertia_buffer}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    marching_cubes.loadData(device_local_buffer_creator);

    VkSampler velocities_sampler = SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).disableNormalizedCoordinates().setWrapMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).create();

    enum Attachments{
        VELOCITIES_1, VELOCITIES_2, CELL_TYPES, NEW_CELL_TYPES, PRESSURES_1, PRESSURES_2, DIVERGENCES, PARTICLE_DENSITIES_FLOAT_1, PARTICLE_DENSITIES_FLOAT_2, IMAGE_COUNT
    };
    enum BufferAttachments{
        PARTICLES_BUF, PARTICLE_DENSITIES_BUF, MARCHING_CUBES_COUNTS_BUF, MARCHING_CUBES_EDGES_BUF, DETAILED_DENSITIES_BUF, DENSITIES_INERTIA_BUF, BUFFER_COUNT
    };

    DescriptorUsageStage usage_compute(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    FlowBufferContext flow_context{
        {velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, pressures_1_img, pressures_2_img, divergence_img, float_densities_1_img, float_densities_2_img},
        vector<PipelineImageState>(IMAGE_COUNT, PipelineImageState{ImageState{IMAGE_NEWLY_CREATED}}),
        {particles_buffer, densities_buffer, marching_cubes.triangle_count_buffer, marching_cubes.vertex_edge_indices_buffer, detailed_densities_buffer, detailed_densities_inertia_buffer},
        vector<PipelineBufferState>(BUFFER_COUNT, PipelineBufferState{BufferState{BUFFER_NEWLY_CREATED}})
    };    

    enum CellTypes{
        CELL_INACTIVE, CELL_AIR, CELL_WATER, CELL_SOLID
    };

    

    DirectoryPipelinesContext fluid_context("shaders_fluid");


    SectionList init_sections{
        new FlowClearColorSection(flow_context, VELOCITIES_1, ClearValue(0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context, VELOCITIES_2, ClearValue(0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context,   CELL_TYPES, ClearValue(CELL_INACTIVE)),
        new FlowClearColorSection(flow_context,  PRESSURES_1, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  PRESSURES_2, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  DIVERGENCES, ClearValue(0.f)),
        new FlowComputeSection(
            fluid_context, "00_init_particles",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_W}}
                }
            },
            Size3{1, 256, 1}
        )
    };

    

    SectionList draw_section_list_1{
        new FlowClearColorSection(flow_context, NEW_CELL_TYPES, ClearValue(CELL_INACTIVE)),
        new FlowComputeSection(
            fluid_context, "01_clear_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particle_densities", PARTICLE_DENSITIES_BUF, usage_compute, BufferState{BUFFER_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "02_update_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
                    FlowStorageBuffer{"particle_densities", PARTICLE_DENSITIES_BUF, usage_compute, BufferState{BUFFER_STORAGE_RW}}
                }
            },
            particle_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "03_update_water",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particle_densities", PARTICLE_DENSITIES_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
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
        new FlowClearColorSection(flow_context, PRESSURES_1, ClearValue(pressure_air)),
        new FlowClearColorSection(flow_context, PRESSURES_2, ClearValue(pressure_air)),
    };


    auto pressure_section = new FlowComputePushConstantSection(
        fluid_context, "13_solve_pressure",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowStorageImage{"cell_types", CELL_TYPES,  usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"divergences", DIVERGENCES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"pressures_1", PRESSURES_1, usage_compute, ImageState{IMAGE_STORAGE_RW}},
                FlowStorageImage{"pressures_2", PRESSURES_2, usage_compute, ImageState{IMAGE_STORAGE_RW}}
            }
        },
        fluid_dispatch_size
    );
    SectionList pressure_solve_section_list(pressure_section);


    SectionList draw_section_list_2{
        new FlowComputeSection(
            fluid_context, "14_fix_divergence",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
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
                    FlowCombinedImage{"velocities", VELOCITIES_1,   usage_compute, ImageState{IMAGE_SAMPLER}, velocities_sampler},
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_RW}},
                }
            },
            particle_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "16_clear_detailed_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particle_densities", DETAILED_DENSITIES_BUF, usage_compute, BufferState{BUFFER_STORAGE_W}},
                }
            }, 
            detailed_densities_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "17_update_detailed_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particles", PARTICLES_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
                    FlowStorageBuffer{"particle_densities", DETAILED_DENSITIES_BUF, usage_compute, BufferState{BUFFER_STORAGE_RW}}
                }
            }, 
            particle_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "18_compute_detailed_densities_inertia",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"particle_densities", DETAILED_DENSITIES_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
                    FlowStorageBuffer{"densities_inertia", DENSITIES_INERTIA_BUF, usage_compute, BufferState{BUFFER_STORAGE_RW}}
                }
            }, 
            detailed_densities_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "19_compute_float_densities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowStorageBuffer{"densities_inertia", DENSITIES_INERTIA_BUF, usage_compute, BufferState{BUFFER_STORAGE_R}},
                    FlowStorageImage{"float_densities", PARTICLE_DENSITIES_FLOAT_1, usage_compute, ImageState{IMAGE_STORAGE_W}},
                }
            }, 
            detailed_densities_dispatch_size
        ),
    };

    auto float_densities_diffuse_section = new FlowComputePushConstantSection(
        fluid_context, "20_diffuse_float_densities",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"densities_1", PARTICLE_DENSITIES_FLOAT_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                FlowStorageImage{"densities_2", PARTICLE_DENSITIES_FLOAT_2, usage_compute, ImageState{IMAGE_STORAGE_W}},
            }
        }, 
        detailed_densities_dispatch_size
    );
    SectionList float_densities_diffuse_section_list(float_densities_diffuse_section);

    // * Create a render pass *
    VkRenderPass render_pass = SimpleRenderPassInfo{swapchain.getFormat(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, depth_test_image.getFormat(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}.create();
    RenderPassSettings render_pass_settings{screen_width, screen_height, {{0.0f, 0.0f, 0.0f}, {1.f, 0U}}};

    // * Create framebuffers for all swapchain images *
    swapchain.createFramebuffers(render_pass, depth_test_image);


    PipelineInfo render_pipeline_info{screen_width, screen_height, 1};
    render_pipeline_info.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    render_pipeline_info.getDepthStencilInfo().enableDepthTest().enableDepthWrite();
    auto render_section = new FlowGraphicsPushConstantSection(
        fluid_context, "30_render_particles",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowStorageBuffer{"particles", PARTICLES_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), BufferState{BUFFER_STORAGE_R}}
            }
        },
        particle_space_size.volume(), render_pipeline_info, render_pass
    );
    auto render_surface_section = new FlowGraphicsPushConstantSection(
        fluid_context, "31_render_surface",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowUniformBuffer{"triangle_counts", MARCHING_CUBES_COUNTS_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT), BufferState{BUFFER_UNIFORM}},
                FlowUniformBuffer{"triangle_vertices", MARCHING_CUBES_EDGES_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT), BufferState{BUFFER_UNIFORM}},
                FlowStorageImage{"float_densities", PARTICLE_DENSITIES_FLOAT_2, DescriptorUsageStage{VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT}, ImageState{IMAGE_STORAGE_R}}
            }
        },
        79*79*79, render_pipeline_info, render_pass
    );
    auto display_data_section = new FlowGraphicsPushConstantSection(
        fluid_context, "32_display_data",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionDescriptorUsage>{
                FlowStorageBuffer{"particle_densities", PARTICLE_DENSITIES_BUF, DescriptorUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), BufferState{BUFFER_STORAGE_R}}
            }
        },
        fluid_size.volume(), render_pipeline_info, render_pass
    );
    

    SectionList render_section_list(render_section, display_data_section, render_surface_section);


    fluid_context.createDescriptorPool();


    FlowCommandBuffer init_buffer{init_command_pool};
    init_sections.complete();
    init_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    init_buffer.record(flow_context, init_sections);
    init_buffer.cmdBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
        depth_test_image.createMemoryBarrier(ImageState{IMAGE_NEWLY_CREATED}, ImageState{IMAGE_DEPTH_STENCIL_ATTACHMENT})    
    );
    init_buffer.endRecord();    

    draw_section_list_1.complete();
    pressure_solve_section_list.complete();
    draw_section_list_2.complete();
    float_densities_diffuse_section_list.complete();
    render_section_list.complete();

    


    // * Initialize projection matrices and camera * 
    Camera camera{{10.f, 10.f, -10.f}, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}, window};
    //matrix to invert y axis - the one in vulkan is inverted compared to the one in OpenGL, for which GLM was written
    glm::mat4 invert_y_mat(1.0);
    invert_y_mat[1][1] = -1;
    glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f*window.getWidth() / window.getHeight(), 0.1f, 200.f) * invert_y_mat;
    glm::mat4 MVP;


    Semaphore draw_end_semaphore;

    // * Structures to watch status of currently sent frame *
    SubmitSynchronization draw_synchronization;
    draw_synchronization.addEndSemaphore(draw_end_semaphore);

    SubmitSynchronization render_synchronization;
    render_synchronization.addStartSemaphore(draw_end_semaphore);
    render_synchronization.setEndFence(Fence());

    SubmitSynchronization init_sync;
    init_sync.setEndFence(Fence());
    queue.submit(init_buffer, init_sync);
    init_sync.waitFor(SYNC_FRAME);
    
    FlowCommandBuffer render_command_buffer(render_command_pool);
    FlowCommandBuffer draw_buffer(render_command_pool);

    bool paused = false;

    while (window.running())
    {
        window.update();
        camera.update(0.01f);

        if (window.keyOn(GLFW_KEY_Q)) paused = true;
        if (window.keyOn(GLFW_KEY_E)) paused = false;

        if (!paused){
            draw_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            draw_buffer.record(flow_context, draw_section_list_1);

            for (uint32_t i = 0; i < divergence_solve_iterations; i++){
                pressure_section->getPushConstantData().write("is_even_iteration", (i % 2 == 0) ? 1U : 0U);
                draw_buffer.record(flow_context, pressure_solve_section_list);
            }
            
            draw_buffer.record(flow_context, draw_section_list_2);

            for (uint32_t i = 0; i < float_density_diffuse_steps; i++){
                float_densities_diffuse_section->getPushConstantData().write("is_even_iteration", (i % 2 == 0) ? 1U : 0U);
                draw_buffer.record(flow_context, float_densities_diffuse_section_list);
            }

            draw_buffer.endRecord();
        
            queue.submit(draw_buffer, draw_synchronization);
        }
        

        //get current image to render into
        SwapchainImage swapchain_image = swapchain.acquireImage();

        render_command_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        
        render_command_buffer.cmdBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            swapchain_image.createMemoryBarrier(ImageState{IMAGE_NEWLY_CREATED}, ImageState{IMAGE_COLOR_ATTACHMENT})
        );
        render_section->transitionAllImages(render_command_buffer, flow_context);
        display_data_section->transitionAllImages(render_command_buffer, flow_context);
        render_surface_section->transitionAllImages(render_command_buffer, flow_context);
        render_command_buffer.cmdBeginRenderPass(render_pass_settings, render_pass, swapchain_image.getFramebuffer());

        MVP = projection * camera.view_matrix;
        render_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        render_section->execute(render_command_buffer);
        display_data_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        display_data_section->execute(render_command_buffer);
        render_surface_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        render_surface_section->execute(render_command_buffer);
        render_command_buffer.cmdEndRenderPass();
        render_command_buffer.endRecord();
        
        
        // * Submit command buffer and wait for it to finish*
        swapchain.prepareToDraw();
        queue.submit(render_command_buffer, render_synchronization);
        render_synchronization.waitFor(SYNC_FRAME);
        
        // * Present rendered image and reset buffer for next frame *
        swapchain.presentImage(swapchain_image, present_queue);

        draw_buffer.resetBuffer(false);
        render_command_buffer.resetBuffer(false);
    }
    queue.waitFor();
    return 0;
}
