#include <iostream>
#include <vector>
#include <string>

#include "just-a-vulkan-library/vulkan_include_all.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "simulation_constants.h"
#include "fluid_flow_sections.h"



using std::vector;
using std::string;

//window width, height
uint32_t screen_width = 1400;
uint32_t screen_height = 1400;
//window title
string app_name = "Vulkan fluid simulation";




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


    //image used for depth test using rendering - same resolution as window
    ExtImage depth_test_image = ImageInfo(screen_width, screen_height, VK_FORMAT_D16_UNORM, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT).create();
    ImageMemoryObject depth_image_memory{{depth_test_image}, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

    

    //data for uniform buffer containing all simulation parameters. Buffer layout is described in shaders_fluid/fluids_uniform_buffer_layout.txt
    SimulationParametersBufferData fluid_params_uniform_buffer;



    //Initialize shader context - Load all shaders
    DirectoryPipelinesContext fluid_context("shaders_fluid");

    
    SimulationDescriptors flow_context{fluid_params_uniform_buffer, device_local_buffer_creator};

    
    //List of sections that will be executed before simulation start
    SimulationInitializationSections init_sections{fluid_context, flow_context};

    //All sections that will run each simulation step
    SimulationStepSections draw_section_list{fluid_context, flow_context, flow_context.getVelocitiesSampler()};



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
    RenderParticlesSection render_particles_section(fluid_context, flow_context, render_pipeline_info, render_pass);
    //section used for rendering surface
    RenderSurfaceSection render_surface_section(fluid_context, flow_context, render_pipeline_info, render_pass); 
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

    //when all sections were created, each one recorded which descriptors it needed to function, now all descriptors can be allocated from a shared descriptor set
    fluid_context.createDescriptorPool();

    //Complete all sections - this is needed to update all descriptors
    init_sections.complete();   
    draw_section_list.complete();
    render_particles_section.complete();
    render_surface_section.complete();

    //record command buffer responsible for initializing the simulation
    CommandBuffer init_buffer{init_command_pool.allocateBuffer()};
    init_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    //record all sections used during initialization into the command buffer
    init_sections.run(init_buffer, flow_context);
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

    CommandBuffer render_command_buffer(render_command_pool.allocateBuffer());
    CommandBuffer simulation_step_buffer(render_command_pool.allocateBuffer());

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
            //record all sections
            draw_section_list.run(simulation_step_buffer, flow_context);
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
        // -> .record is split into two parts, transition() and execute()
        render_particles_section.transition(render_command_buffer, flow_context);
        //display_data_section->transition(render_command_buffer, flow_context);
        render_surface_section.transition(render_command_buffer, flow_context);
        render_command_buffer.cmdBeginRenderPass(render_pass_settings, render_pass, swapchain_image.getFramebuffer());

        //compute model-view-projection matrix
        MVP = projection * camera.view_matrix;
        //push MVP to be used by particle render shader
        render_particles_section.getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        //execute the section that renders all particles
        render_particles_section.execute(render_command_buffer);
        /*display_data_section->getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        display_data_section->execute(render_command_buffer);*/

        //push MVP to be used by surface render shader
        render_surface_section.getPushConstantData().write("MVP", glm::value_ptr(MVP), 16);
        //execute the section that 
        render_surface_section.execute(render_command_buffer);
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
