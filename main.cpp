#include <iostream>
#include <vector>
#include <string>

#include "just-a-vulkan-library/vulkan_include_all.h"

#include <glm/glm.hpp>
#include "particle_render_sections.h"



using std::vector;
using std::string;

//window width, height
uint32_t screen_width = 1400;
uint32_t screen_height = 1400;
//window title
string app_name = "Vulkan fluid simulation";


/**
 * @brief Camera that tracks how much the viewport rotates every frame. This makes it possible to blur particle trails in 2d space even when rotating the camera.
 * 
 */
class ViewportTrackingCamera : public ProjectionCamera{
    float m_ratio;
    float m_fov_tan;
    glm::vec2 m_viewport_shift;
    float m_blur_amount;
public:
    ViewportTrackingCamera(const vec3& position, const vec3& dir, const vec3& up, Window& window, ProjectionSettings projection_s = ProjectionSettings()) :
        ProjectionCamera(position, dir, up, window, projection_s), m_ratio(projection_s.getRatio()), m_fov_tan(2*tanf(projection_s.getFOV()/2))
    {}
    void update(float time_step){
        float old_rot_up = m_current_rot_up;
        float old_rot_side = m_current_rot_sides;
        ProjectionCamera::update(time_step);

        //compute how much we should move the place from which we sample in the blur shader
        m_blur_amount = 1 - std::min(glm::length(m_velocity) / 20.f, 1.f);
        m_viewport_shift = glm::vec2(
            (m_current_rot_sides - old_rot_side) / m_fov_tan,
            (m_current_rot_up - old_rot_up) / m_fov_tan
        );
    }
    const glm::vec2& getViewportShift() const{
        return m_viewport_shift;
    }
    const float* getViewportShiftPtr() const{
        return glm::value_ptr(m_viewport_shift);
    }
    //write viewport shift into uniform buffer data - this is used to eventually send it to shaders as a push constant
    UniformBufferLayoutData& writeViewportShift(UniformBufferLayoutData& data, const string& name = "viewport_shift"){
        return data.write(name, getViewportShiftPtr(), 2);
    }
    float getBlurAmount() const{
        return m_blur_amount;
    }
};







int main(){
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

    //data for uniform buffer containing all simulation parameters. Buffer layout is described in shaders_fluid/fluids_uniform_buffer_layout.txt
    SimulationParametersBufferData fluid_params_uniform_buffer;

    //Initialize shader context - Load all shaders
    DirectoryPipelinesContext fluid_context("shaders_fluid");
    

    //record command buffer responsible for initializing the simulation
    CommandBuffer init_buffer{init_command_pool.allocateBuffer()};
    init_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    SimulationDescriptors flow_context{fluid_params_uniform_buffer, window.size(), queue};
    
    //List of sections that will be executed before simulation start
    SimulationInitializationSections init_sections{fluid_context, flow_context};

    //All sections that will run each simulation step
    SimulationStepSections draw_section_list{fluid_context, flow_context, flow_context.getVelocitiesSampler()};


    //just a sampler with nearest filtering
    VkSampler postprocess_sampler = SamplerInfo().create();


    ParticleRenderSections render_particles{flow_context, fluid_context, window, postprocess_sampler};

    //setup render pipeline info
    PipelineInfo pipeline_info_points{window.size(), 1};
    pipeline_info_points.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);

    //finally, render into output image - uses only one swapchain image, contains the soft particles implementation
    FlowSwapchainRenderPass postprocessing_pass_final{flow_context, swapchain, FlowRenderPassAttachments()};
    //section for copying render result into the swapchain image and doing soft particles
    auto postprocess_section_final = new FlowGraphicsSection{
        flow_context, fluid_context,
        "51_postprocess_final",
        FlowPipelineSectionDescriptors{
            {
                FlowUniformBuffer{"simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
                FlowCombinedImage{"source", BLURRED_RENDER_1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"depth", BLURRED_DEPTH_1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"spheres_depth", RENDER_SPHERES_DEPTH_IMAGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler}
            }
        },
        1, pipeline_info_points, postprocessing_pass_final.render_pass
    };
    postprocessing_pass_final.addSections(postprocess_section_final);



    //sections used for rendering particles, surface and data(disabled by default)
    //RenderParticlesSection render_particles_smooth(fluid_context, flow_context, render_pipeline_info, render_pass);


    //when all sections were created, each one recorded which descriptors it needed to function, now all descriptors can be allocated from a shared descriptor set
    fluid_context.createDescriptorPool();

    //Complete all sections - this is needed to update all descriptors
    init_sections.complete();   
    draw_section_list.complete();

    render_particles.complete();
    postprocessing_pass_final.complete();

    //record all sections used during initialization into the command buffer
    init_sections.run(init_buffer);
    
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
    ViewportTrackingCamera camera{{10.f, 10.f, -10.f}, {0.f, 0.f, 1.f}, {0.f, -1.f, 0.f}, window, ProjectionSettings().setDistances(camera_near, camera_far)};

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

    SubmitSynchronization render_synchronization_paused;
    render_synchronization_paused.setEndFence(Fence());

    CommandBuffer render_command_buffer(render_command_pool.allocateBuffer());
    CommandBuffer simulation_step_buffer(render_command_pool.allocateBuffer());

    //whether simulation is paused - during a pause, simulation is static, but camera can still move
    bool paused = false;

    //current time in seconds
    float current_time = 0.f;
    bool even_iteration = true;

    //if <0, left mouse click can be used to add force into the fluid
    float punch_cooldown = 0.0;

    //weird white sphere position and velocity
    vec3 sphere_position = vec3(10, 10, 10);
    vec3 sphere_velocity = vec3(0, 0, 0);

    //if <0, right mouse can be used to teleport the sphere to the camera
    float sphere_cooldown = 0.0;

    //these are used to send data to shaders in real time
    auto& render_depth_push_constants = flow_context.getPushConstants("30_render_particles_depth");
    auto& render_closest_push_constants = flow_context.getPushConstants("30a_render_particles_closest");
    auto& render_spheres_push_constants = flow_context.getPushConstants("40_spheres");
    auto& blur_push_constants = flow_context.getPushConstants("50_postprocess_smooth");

    //for adding camera force
    auto& add_force_push_constants = flow_context.getPushConstants("08_forces");

    //while user hasn't closed the window
    while (window.running()){
        //update used descriptors in the blur section and swap the framebuffer
        render_particles.updateDescriptors(even_iteration);
        sphere_cooldown -= simulation_time_step;
        punch_cooldown -= simulation_time_step;

        //select last rendered image in the final section
        int current_frame_image_index = even_iteration ? BLURRED_RENDER_1 : BLURRED_RENDER_2;
        int current_frame_depth_index = even_iteration ? BLURRED_DEPTH_1 : BLURRED_DEPTH_2;
        postprocess_section_final->updateDescriptor(CombinedImageSamplerUpdateInfo("source", flow_context.getImage(current_frame_image_index), postprocess_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), current_frame_image_index);
        postprocess_section_final->updateDescriptor(CombinedImageSamplerUpdateInfo("depth", flow_context.getImage(current_frame_depth_index), postprocess_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), current_frame_depth_index);

        //check user doesn't want to quit, update events
        window.update();

        //move the white sphere
        sphere_position += sphere_velocity * simulation_time_step;
        //if right button is clicked, move the sphere before the camera and reset setting cooldown
        if (window.mouseRight() && sphere_cooldown < 0.f){
            sphere_velocity = camera.getDirection() * 2.f;
            sphere_position = camera.getPosition() + sphere_velocity / 2.f;
            sphere_cooldown = 0.3f;
        }

        //move camera according to its' velocity and keys pressed
        camera.update(simulation_time_step);

        //update time since simulation began
        current_time += simulation_time_step;

        //if Q or E keys are pressed, pause/resume the simulation
        if (window.keyOn(GLFW_KEY_Q)) paused = true;
        if (window.keyOn(GLFW_KEY_E)) paused = false;

        //if simulation isn't paused
        if (!paused){
            //record GPU commands for one simulation step
            simulation_step_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            
            bool do_hit = window.mouseLeft() && punch_cooldown < 0;
            auto& forces_push_constants = camera.writeDirection(camera.writePosition(add_force_push_constants));
            if (do_hit){
                punch_cooldown = 0.1;
                forces_push_constants.write("add_camera_force", 1u);
            }else{
                forces_push_constants.write("add_camera_force", 0u);
            }
            //record all sections
            draw_section_list.run(simulation_step_buffer);

            simulation_step_buffer.endRecord();
        
            //submit recorded command buffer to the queue
            queue.submit(simulation_step_buffer, simulation_step_synchronization);
        }
        
        //update MVP matrix, time and other constants in all shaders
        camera.writeMVP(render_depth_push_constants).write("time", current_time);
        camera.writeMVP(render_closest_push_constants).write("time", current_time);
        camera.writeMVP(render_spheres_push_constants).write("sphere_position", glm::value_ptr(sphere_position), 3);        
        camera.writeViewportShift(blur_push_constants).write("blur_amount", camera.getBlurAmount());

        //record all rendering sections
        render_command_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        render_particles.run(render_command_buffer);
        postprocessing_pass_final.run(render_command_buffer);
        
        render_command_buffer.endRecord();

        //wait until the screen image can be rendered into
        postprocessing_pass_final.waitForSwapchainImage();

        //if simulation isn't paused, we need to wait until all simulation sections have finished before rendering(render_synchronization)
        //if it is paused, we don't care(render_synchronization_paused)
        SubmitSynchronization& current_sync = paused ? render_synchronization_paused : render_synchronization;

        //execute render command buffer
        queue.submit(render_command_buffer, current_sync);
        //wait for rendering to finish
        current_sync.waitFor(SYNC_SECOND);
        
        //present rendered image
        postprocessing_pass_final.presentResult(present_queue);

        //reset recorded command buffers
        render_command_pool.reset(false);
        even_iteration = !even_iteration;
    }
    //wait for all operations on main queue to finish, then end the app
    queue.waitFor();
    }
    return 0;
}
