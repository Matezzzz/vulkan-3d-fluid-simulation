#include <iostream>
#include <vector>
#include <string>

#include "vulkan_include_all.h"

using std::vector;
using std::string;

uint32_t screen_width = 1400;
uint32_t screen_height = 1400;
string app_name = "Hello Vulkan :)";



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
    CommandBuffer draw_command_buffer = command_pool.allocateBuffer();

    // * Create local object creator -  used to copy data from RAM to GPU * 
    //uint32_t image_width = 1024;
    //uint32_t image_height = 1024;
    //const uint32_t max_image_or_buffer_size_bytes = 4*image_width*image_height;
    //LocalObjectCreator device_local_buffer_creator{queue, max_image_or_buffer_size_bytes};


    // * Creating an image on the GPU *
    //Image img = ImageInfo(screen_width, screen_height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT).create();
    //ImageMemoryObject memory({img}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    // * Creating an image view *
    //ImageView color_image_view = color_image.createView();

    // * Creating a sampler *
    //VkSampler sampler = SamplerInfo().create();

    // * Creating a pipelines context *
    //DirectoryPipelinesContext pipelines_context{"shaders"};
    //PipelineContext& shader_ctx(pipelines_context.getContext("basic"));
    //shader_ctx.reserveDescriptorSets(1, 1);
    //after reserving all sets call:
    //pipelines_context.createDescriptorPool();


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

    
    // * Allocating descriptor sets *
    //DescriptorSet set_textures, set_uniform_buffer;
    //shader_ctx.allocateDescriptorSets(set_textures, set_uniform_buffer);
    //actual sets to bind, used later when binding pipeline
    //vector<VkDescriptorSet> copper_descriptor_sets{set_textures, set_uniform_buffer};
    
    // * Updating descriptor sets *
    /*set_textures.updateDescriptors(
        DescriptorUpdateInfo{"in_shader_descriptor_name",   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, texture_sampler, copper_plate_images.getImageView(0)},
    );*/

    // * Managing push constant data *
    //PushConstantData copper_push_constants = shader_ctx.createPushConstantData();

    // * Create a render pass *
    VkRenderPass renderpass = SimpleRenderPassInfo{swapchain.getFormat(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}.create();
    // width, height, clear color, depth clear color
    RenderPassSettings renderpass_settings{screen_width, screen_height, {{0.3f, 0.3f, 0.3f}, {1.f, 0U}}};

    // * Create framebuffers for all swapchain images *
    swapchain.createFramebuffers(renderpass);

    // * Create standalone framebuffer *
    //VkFramebuffer render_framebuffer = FramebufferInfo(screen_width, screen_height, {color_image_view, depth_image.createView()}, renderpass).create();

    // * Create a pipeline *
    //PipelineInfo pipeline_info{screen_width, screen_height, 1};
    //pipeline_info.getVertexInputInfo().addFloatBuffer({2});
    //pipeline_info.getDepthStencilInfo().enableDepthTest().enableDepthWrite();
    //Pipeline copper_pipeline = shader_ctx.createPipeline(pipeline_info, renderpass);
    //Pipeline normals_pipeline = normals_context.createPipeline(pipeline_info, renderpass);

    // * Initialize projection matrices and camera * 
    //Camera camera{{0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, window};
    // matrix to invert y axis - the one in vulkan is inverted compared to the one in OpenGL, for which GLM was written
    //glm::mat4 invert_y_mat(1.0);
    //invert_y_mat[1][1] = -1;
    //glm::mat4 projection = glm::perspective(glm::radians(45.f), 1.f*window.getWidth() / window.getHeight(), 0.1f, 200.f) * invert_y_mat;
    //glm::mat4 MVP;

    // * Structure to watch status of currently sent frame *
    SubmitSynchronization frame_synchronization;
    Fence frame_done_fence;
    frame_synchronization.setEndFence(frame_done_fence);
    
    while (window.running())
    {
        window.update();
        //camera.update(0.01f);

        //get current image to render into
        SwapchainImage swapchain_image = swapchain.acquireImage();

        // * Start recording a command buffer *
        draw_command_buffer.startRecordPrimary();
        // * Set memory barrier *
        /*draw_command_buffer.cmdBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,\
            {color_image.createMemoryBarrier(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
             depth_image.createMemoryBarrier(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)});*/

        // * Begin render pass and bind pipeline *    
        draw_command_buffer.cmdBeginRenderPass(renderpass_settings, renderpass, swapchain_image.getFramebuffer());
        //draw_command_buffer.cmdBindPipeline(copper_pipeline, copper_descriptor_sets);
        
        // * Write push constants *
        //MVP = projection * camera.view_matrix;
        //copper_push_constants.write("MVP", glm::value_ptr(MVP), 16).write("camera_pos", glm::value_ptr(camera.getPosition()), 3);
        //draw_command_buffer.cmdPushConstants(copper_pipeline, copper_push_constants);

        // * Bind vertex buffers and draw *
        //draw_command_buffer.cmdBindVertexBuffers({copper_vertex_buffer});
        //draw_command_buffer.cmdDrawVertices(copper_vertex_buffer.getSize() / sizeof(float) / 2);

        // * End render pass *
        draw_command_buffer.cmdEndRenderPass();

        draw_command_buffer.endRecordPrimary();
        
        // * Submit command buffer and wait for it to finish*
        queue.submit(draw_command_buffer, frame_synchronization);
        if (!frame_synchronization.waitFor(SYNC_FRAME))
        {
            cout << "Waiting for queue failed.\n";
        }
        frame_done_fence.reset();
        
        // * Present rendered image and reset buffer for next frame *
        swapchain.presentImage(swapchain_image, present_queue);

        draw_command_buffer.resetBuffer(false);
    }    
    return 0;
}
