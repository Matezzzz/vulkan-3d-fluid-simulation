#include <iostream>
#include <vector>
#include <string>

#include "just-a-vulkan-library/vulkan_include_all.h"
#include "flow_command_buffer.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>

using std::vector;
using std::string;

uint32_t screen_width = 1400;
uint32_t screen_height = 1400;
string app_name = "Hello Vulkan :)";



uint32_t fluid_width = 20, fluid_height = 20, fluid_depth = 20;
Size3 fluid_size{fluid_width, fluid_height, fluid_depth};
Size3 fluid_local_group_size{10, 10, 1};
Size3 fluid_dispatch_size = fluid_size / fluid_local_group_size;


constexpr uint32_t particle_batch_count = 256;
constexpr uint32_t particles_per_batch = 256;
Size3 particle_local_group_size{particles_per_batch, 1, 1};
constexpr uint32_t max_particle_count = particle_batch_count*particles_per_batch;
Size3 particle_dispatch_size = Size3{particles_per_batch, particle_batch_count, 1} / particle_local_group_size;

const float pressure_air = 1.0;

constexpr uint32_t divergence_solve_iterations = 100;


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
    Device& device = physical_device.requestExtensions({VK_KHR_SWAPCHAIN_EXTENSION_NAME})\
        .requestScreenSupportQueues({{2, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT}}, window)\
        .createLogicalDevice(instance);
    
    // * Get references to requested queues *
    Queue& queue = device.getQueue(0, 0);
    Queue& present_queue = device.getQueue(0, 1);

    // * Create swapchain *
    Swapchain swapchain = SwapchainInfo(physical_device, window).setUsages(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT).create();
    
    // * Create command pool and default command buffer *
    CommandPool command_pool = CommandPoolInfo{0}.create();
    CommandPool render_command_pool = CommandPoolInfo{0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT}.create();

    // * Create local object creator -  used to copy data from RAM to GPU * 
    //uint32_t image_width = 1024;
    //uint32_t image_height = 1024;
    //const uint32_t max_image_or_buffer_size_bytes = 4*image_width*image_height;
    //LocalObjectCreator device_local_buffer_creator{queue, max_image_or_buffer_size_bytes};


    // * Creating an image on the GPU *
    ImageInfo velocity_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    ExtImage velocities_1_img = velocity_image_info.create();
    ExtImage velocities_2_img = velocity_image_info.create();

    ImageInfo cell_type_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage cell_types_img = cell_type_image_info.create();
    ExtImage cell_types_new_img = cell_type_image_info.create();
    ExtImage particles_img = ImageInfo(particles_per_batch, particle_batch_count, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT).create();
    ImageInfo pressures_image_info = ImageInfo(fluid_width, fluid_height, fluid_depth, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    ExtImage pressures_1_img = pressures_image_info.create();
    ExtImage pressures_2_img = pressures_image_info.create();
    ExtImage divergence_img = pressures_image_info.create(); //settings for divergence are the same as for pressure
    ImageMemoryObject memory({velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, particles_img, pressures_1_img, pressures_2_img, divergence_img}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);



    VkSampler velocities_sampler = SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).disableNormalizedCoordinates().setWrapMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE).create();

    enum Attachments{
        VELOCITIES_1, VELOCITIES_2, CELL_TYPES, NEW_CELL_TYPES, PARTICLES, PRESSURES_1, PRESSURES_2, DIVERGENCES, IMAGE_COUNT
    };


    ImageUsageStage usage_compute(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);



    FlowBufferContext flow_context{
        {velocities_1_img, velocities_2_img, cell_types_img, cell_types_new_img, particles_img, pressures_1_img, pressures_2_img, divergence_img},
        {},
        vector<PipelineImageState>(IMAGE_COUNT)
    };    

    enum CellTypes{
        CELL_INACTIVE, CELL_AIR, CELL_WATER, CELL_SOLID
    };

    // * Create a render pass *
    VkRenderPass render_pass = SimpleRenderPassInfo{swapchain.getFormat(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}.create();
    // width, height, clear color, depth clear color
    RenderPassSettings render_pass_settings{screen_width, screen_height, {{0.0f, 0.0f, 0.0f}, {1.f, 0U}}};

    // * Create framebuffers for all swapchain images *
    swapchain.createFramebuffers(render_pass);



    

    DirectoryPipelinesContext fluid_context("shaders_fluid");



    FlowPipelineSectionDescriptors pressure_solve_image_usages{
        flow_context,
        vector<FlowPipelineSectionImageUsage>{
            FlowStorageImage{"cell_types", CELL_TYPES,  usage_compute, ImageState{IMAGE_STORAGE_R}},
            FlowStorageImage{"divergences", DIVERGENCES, usage_compute, ImageState{IMAGE_STORAGE_R}},
            FlowStorageImage{"pressures_1", PRESSURES_1, usage_compute, ImageState{IMAGE_STORAGE_RW}},
            FlowStorageImage{"pressures_2", PRESSURES_2, usage_compute, ImageState{IMAGE_STORAGE_RW}},
        }
    };
    auto pressure_section = new FlowComputePushConstantSection(
        fluid_context, "05_pressure",
        pressure_solve_image_usages,
        fluid_dispatch_size
    );
    SectionList pressure_solve_section_list(pressure_section);



    SectionList draw_section_list_1{
        new FlowClearColorSection(flow_context, NEW_CELL_TYPES, ClearValue(CELL_INACTIVE)),
        new FlowComputeSection(
            fluid_context, "00_update_grid",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"particles", PARTICLES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            particle_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "00a_update_active",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "00b_compute_extrapolated_velocities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types", CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"extrapolated_velocities", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "00c_extrapolate_velocities",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"new_cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"cell_types", CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_W}},
                    FlowStorageImage{"extrapolated_velocitites", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_R}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "00d_update_cell_types",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"new_cell_types", NEW_CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"cell_types", CELL_TYPES, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "01_advect",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types",      CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowCombinedImage{"velocities_src", VELOCITIES_1, usage_compute, ImageState{IMAGE_SAMPLER}, velocities_sampler},
                    FlowStorageImage{"velocities_dst",  VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "02_forces",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "03_diffuse",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types",     CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities_src", VELOCITIES_2, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities_dst", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "08_solids",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "04_prepare_pressure",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
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

    SectionList draw_section_list_2{
        new FlowComputeSection(
            fluid_context, "06_fix_divergence",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"pressures", PRESSURES_2,  usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_RW}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "04_prepare_pressure",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"cell_types", CELL_TYPES,   usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"velocities", VELOCITIES_1, usage_compute, ImageState{IMAGE_STORAGE_R}},
                    FlowStorageImage{"divergences", DIVERGENCES,  usage_compute, ImageState{IMAGE_STORAGE_W}}
                }
            },
            fluid_dispatch_size
        ),
        new FlowComputeSection(
            fluid_context, "09_particles",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowCombinedImage{"velocities", VELOCITIES_1,   usage_compute, ImageState{IMAGE_SAMPLER}, velocities_sampler},
                    FlowStorageImage{"particles", PARTICLES, usage_compute, ImageState{IMAGE_STORAGE_RW}},
                }
            },
            particle_dispatch_size
        )
    };

    PipelineInfo render_pipeline_info{screen_width, screen_height, 1};
    render_pipeline_info.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    auto render_section = new FlowGraphicsPushConstantSection(
        fluid_context, "10_render",
        FlowPipelineSectionDescriptors{
            flow_context,
            vector<FlowPipelineSectionImageUsage>{
                FlowStorageImage{"particles", PARTICLES, ImageUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), ImageState{IMAGE_STORAGE_R}}
            }
        },
        max_particle_count, render_pipeline_info, render_pass
    );
    SectionList render_section_list(render_section);



    SectionList init_sections{
        new FlowClearColorSection(flow_context, VELOCITIES_1, ClearValue(0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context, VELOCITIES_2, ClearValue(0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context,   CELL_TYPES, ClearValue(CELL_INACTIVE)),
        new FlowClearColorSection(flow_context,    PARTICLES, ClearValue(0.f, 0.f, 0.f)),
        new FlowClearColorSection(flow_context,  PRESSURES_1, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  PRESSURES_2, ClearValue(0.f)),
        new FlowClearColorSection(flow_context,  DIVERGENCES, ClearValue(0.f)),
        new FlowComputeSection(
            fluid_context, "000_init_particles",
            FlowPipelineSectionDescriptors{
                flow_context,
                vector<FlowPipelineSectionImageUsage>{
                    FlowStorageImage{"particles", PARTICLES, ImageUsageStage(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT), ImageState{IMAGE_STORAGE_W}}
                }
            },
            Size3{10, 10, 1}
        ),
        new FlowIntoLoopTransitionSection(IMAGE_COUNT, draw_section_list_1, pressure_solve_section_list, draw_section_list_2, render_section_list)
    };

    fluid_context.createDescriptorPool();


    init_sections.complete();
    FlowCommandBuffer init_buffer{command_pool};
    init_buffer.startRecordPrimary();
    init_buffer.record(flow_context, init_sections);
    init_buffer.endRecord();    

    draw_section_list_1.complete();
    pressure_solve_section_list.complete();
    draw_section_list_2.complete();
    render_section_list.complete();

    FlowCommandBuffer draw_buffer(command_pool);
    draw_buffer.startRecordPrimary();
    draw_buffer.record(flow_context, draw_section_list_1);

    /*FlowCommandBuffer pressure_solve_buffer(command_pool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    pressure_solve_buffer.startRecordSecondary(CommandBufferInheritanceInfo(), VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT);
    pressure_section->getPushConstantData().write("isEvenIteration", false);
    pressure_solve_buffer.record(images, pressure_solve_section_list, image_states);
    pressure_section->getPushConstantData().write("isEvenIteration", true);
    pressure_solve_buffer.record(images, pressure_solve_section_list, image_states);
    pressure_solve_buffer.endRecord();*/

    for (uint32_t i = 0; i < divergence_solve_iterations; i++){
        //draw_buffer.cmdExecuteCommands(pressure_solve_buffer);
        pressure_section->getPushConstantData().write("isEvenIteration", (i % 2 == 0) ? 1U : 0U);
        draw_buffer.record(flow_context, pressure_solve_section_list);
    }
    
    draw_buffer.record(flow_context, draw_section_list_2);
    draw_buffer.endRecord();
    

    


    // * Managing uniform buffer data for given context *
    //MixedBufferData push_c_data = shader_ctx.createUniformBufferData(1, "u_light_data");
    //push_c_data.write("light_positions", light_positions).write("light_colors", light_colors);

    // * Creating buffers *
    /*vector<Buffer> m_vertex_buffers = device_local_buffer_creator.createBuffers(
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VertexCreator::createPlane(15, 15, -15, -15, 30, 30),
        VertexCreator::screenQuadTexCoords(),
    );*/
    //Buffer& plane_vertices      = m_vertex_buffers[0];
    //Buffer& screen_q_vertices       = m_vertex_buffers[1];
    

    // * Updating descriptor sets *
    /*
    descriptor_set_advect.updateDescriptors(
        StorageImageUpdateInfo{"velocities_2", velocities_2_img, VK_IMAGE_LAYOUT_GENERAL},
        StorageImageUpdateInfo{"cell_types", cell_type_img, VK_IMAGE_LAYOUT_GENERAL},
        CombinedImageSamplerUpdateInfo{"velocities_1_sampler", velocities_1_img, advect_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
    );
    descriptor_set_forces.updateDescriptors(
        StorageImageUpdateInfo{"velocities_2", velocities_2_img, VK_IMAGE_LAYOUT_GENERAL},
        StorageImageUpdateInfo{"cell_types", cell_type_img, VK_IMAGE_LAYOUT_GENERAL}
    );
    */


    
    // * Create standalone framebuffer *
    //VkFramebuffer render_framebuffer = FramebufferInfo(screen_width, screen_height, {color_image_view, depth_image.createView()}, renderpass).create();

    // * Create a pipeline *
    //PipelineInfo pipeline_info{screen_width, screen_height, 1};
    //pipeline_info.getVertexInputInfo().addFloatBuffer({2});
    //pipeline_info.getDepthStencilInfo().enableDepthTest().enableDepthWrite();
    //Pipeline copper_pipeline = shader_ctx.createPipeline(pipeline_info, renderpass);
    //Pipeline normals_pipeline = normals_context.createPipeline(pipeline_info, renderpass);

    // * Initialize projection matrices and camera * 
    Camera camera{{10.f, 10.f, -10.f}, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}, window};
    //matrix to invert y axis - the one in vulkan is inverted compared to the one in OpenGL, for which GLM was written
    glm::mat4 invert_y_mat(1.0);
    invert_y_mat[1][1] = -1;
    glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f*window.getWidth() / window.getHeight(), 0.1f, 200.f) * invert_y_mat;
    glm::mat4 MVP;

    // * Structure to watch status of currently sent frame *
    SubmitSynchronization frame_synchronization;
    Fence frame_done_fence;
    frame_synchronization.setEndFence(frame_done_fence);

    queue.submit(init_buffer, frame_synchronization);
    frame_synchronization.waitFor(SYNC_FRAME);

    FlowCommandBuffer render_command_buffer(render_command_pool);
    while (window.running())
    {
        window.update();
        camera.update(0.01f);


        queue.submit(draw_buffer, frame_synchronization);
        frame_synchronization.waitFor(SYNC_FRAME);

        //get current image to render into
        SwapchainImage swapchain_image = swapchain.acquireImage();

        // * Start recording a command buffer *
        render_command_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        
        // * Write push constants *
        MVP = projection * camera.view_matrix;
        

        render_command_buffer.cmdBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            swapchain_image.createMemoryBarrier(ImageState{IMAGE_NEWLY_CREATED}, ImageState{IMAGE_COLOR_ATTACHMENT})
        );
        render_section->transitionAllImages(render_command_buffer, flow_context);
        render_command_buffer.cmdBeginRenderPass(render_pass_settings, render_pass, swapchain_image.getFramebuffer());
        render_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        render_section->execute(render_command_buffer);
        render_command_buffer.cmdEndRenderPass();
        render_command_buffer.endRecord();
        
        

        // * Bind vertex buffers and draw *
        //draw_command_buffer.cmdBindVertexBuffers({copper_vertex_buffer});
        //draw_command_buffer.cmdDrawVertices(copper_vertex_buffer.getSize() / sizeof(float) / 2);
        
        
        // * Submit command buffer and wait for it to finish*
        swapchain.prepareToDraw();
        queue.submit(render_command_buffer, frame_synchronization);
        frame_synchronization.waitFor(SYNC_FRAME);
        
        // * Present rendered image and reset buffer for next frame *
        swapchain.presentImage(swapchain_image, present_queue);

        render_command_buffer.resetBuffer(false);
    }
    queue.waitFor();
    return 0;
}
