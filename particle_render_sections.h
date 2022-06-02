#include "just-a-vulkan-library/vulkan_include_all.h"
#include "fluid_flow_sections.h"



/**
 * @brief Renders particle volume, front particles, white spheres, then computes blurred texture for normals, positions and depths
 * 
 */
class ParticleRenderSections : public FlowSectionList{
    FlowGraphicsPushConstantSection* m_postprocess_section_smooth;
    FlowDoubleFramebufferRenderPass* m_postprocessing_pass_smooth;
    VkSampler m_postprocess_sampler;
public:
    ParticleRenderSections(FlowContext& flow_context, DirectoryPipelinesContext& fluid_context, const Window& window, VkSampler postprocess_sampler) :
        FlowSectionList(flow_context), m_postprocess_sampler(postprocess_sampler)
    {
        //renders points with blending enables
        PipelineInfo render_pipeline_depth_info{window.size(), 1};
        render_pipeline_depth_info.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
        render_pipeline_depth_info.getBlendInfo().getBlendSettings(0).enableBlending();

        //renders points into two attachments with depth testing enables
        PipelineInfo render_pipeline_front_info{window.size(), 2};
        render_pipeline_front_info.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
        render_pipeline_front_info.getDepthStencilInfo().enableDepthTest().enableDepthWrite();

        //renders without any color attachments, just depth. With 3 coordinates per vertex.
        PipelineInfo render_pipeline_spheres_info{window.size(), 0};
        render_pipeline_spheres_info.getDepthStencilInfo().enableDepthTest().enableDepthWrite();
        render_pipeline_spheres_info.getVertexInputInfo().addFloatBuffer({3});

        //renders points into three color attachments - blurred colors, normals, and linear depth
        PipelineInfo postprocess_blur_pipeline_info{window.size(), 3};
        postprocess_blur_pipeline_info.getAssemblyInfo().setTopology(VK_PRIMITIVE_TOPOLOGY_POINT_LIST);



        //render particles without depth test and with blending - figure out how transparent each fragment should be
        auto render_pass_particles_depth = new FlowFramebufferRenderPass{flow_context, window.size(), FlowRenderPassAttachments(
            FlowColorAttachment(RENDER_VOLUME_IMAGE, ImgState::Sampled, background_clear_value)
        )};
        //render particles with depth attachment - figure how close the closest one are, what are their colors and their normals
        auto render_pass_particles_front = new FlowFramebufferRenderPass{flow_context, window.size(), FlowRenderPassAttachments(
            FlowColorAttachment(RENDER_FRONT_COLORS_IMAGE, ImgState::Sampled, background_clear_value),
            FlowColorAttachment(RENDER_FRONT_NORMALS_IMAGE, ImgState::Sampled),
            FlowDepthAttachment(RENDER_FRONT_DEPTH_IMAGE, ImgState::Sampled, VK_ATTACHMENT_STORE_OP_STORE)
        )};
        //render weird white spheres for the soft particles article - just depth, how far they are
        auto render_pass_spheres = new FlowFramebufferRenderPass{flow_context, window.size(), FlowRenderPassAttachments(
            FlowDepthAttachment(RENDER_SPHERES_DEPTH_IMAGE, ImgState::Sampled, VK_ATTACHMENT_STORE_OP_STORE)
        )};
        //responsible for blurring colors, normals and depths over time. Has two framebuffers, one to read from (and blur render with), other to render into
        m_postprocessing_pass_smooth = new FlowDoubleFramebufferRenderPass{flow_context, window.size(),
            FlowRenderPassAttachments(
                FlowColorAttachment(BLURRED_RENDER_1, ImgState::Sampled, background_clear_value),
                FlowColorAttachment(BLURRED_NORMAL_1, ImgState::Sampled),
                FlowColorAttachment(BLURRED_DEPTH_1, ImgState::Sampled)
            ),
            FlowRenderPassAttachments(
                FlowColorAttachment(BLURRED_RENDER_2, ImgState::Sampled, background_clear_value),
                FlowColorAttachment(BLURRED_NORMAL_2, ImgState::Sampled),
                FlowColorAttachment(BLURRED_DEPTH_2, ImgState::Sampled)
            )
        };

        //sampler with linear filtering
        VkSampler sampler_linear_interp = SamplerInfo().setFilters(VK_FILTER_LINEAR, VK_FILTER_LINEAR).create();

        //actual section for rendering particle volume - defines the shader being used, and which textures & buffers it needs to work
        auto render_particles_depth = new FlowGraphicsPushConstantSection(
            flow_context, fluid_context, "30_render_particles_depth",
            FlowPipelineSectionDescriptors{
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                    FlowStorageBufferR{"particles", PARTICLE_POSITIONS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT},
                    FlowCombinedImage{"smoke_animation", SMOKE_ANIMATION_IMG, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, sampler_linear_interp}
                }
            },
            particle_space_size, render_pipeline_depth_info, render_pass_particles_depth->render_pass
        );
        //add the section to the depth render pass
        render_pass_particles_depth->addSections(render_particles_depth);

        
        //section for rendering front particles
        auto render_particles_front = new FlowGraphicsPushConstantSection(
            flow_context, fluid_context, "30a_render_particles_closest",
            FlowPipelineSectionDescriptors{
                vector<FlowPipelineSectionDescriptorUsage>{
                    FlowUniformBuffer("simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT),
                    FlowStorageBufferR{"particles", PARTICLE_POSITIONS_BUF, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT},
                    FlowCombinedImage{"smoke_animation", SMOKE_ANIMATION_IMG, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, sampler_linear_interp},
                    FlowCombinedImage{"particle_densities", PARTICLE_DENSITIES_FLOAT_1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, sampler_linear_interp},
                    FlowStorageBufferR{"particle_colors", PARTICLE_COLORS_BUF, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT}
                }
            },
            particle_space_size, render_pipeline_front_info, render_pass_particles_front->render_pass
        );
        render_pass_particles_front->addSections(render_particles_front);

        //section for rendering the weird white sphere sphere
        auto render_spheres = new FlowGraphicsVertexPushConstantSection{
            flow_context, fluid_context, "40_spheres",
            FlowPipelineSectionDescriptors{{}},
            SPHERE_VERTEX_BUFFER, render_pipeline_spheres_info, render_pass_spheres->render_pass
        };
        render_pass_spheres->addSections(render_spheres);

        //section for blurring the images
        m_postprocess_section_smooth = new FlowGraphicsPushConstantSection{
            flow_context, fluid_context,
            "50_postprocess_smooth",
            FlowPipelineSectionDescriptors{{
                FlowUniformBuffer{"simulation_params_buffer", SIMULATION_PARAMS_BUF, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
                FlowCombinedImage{"render", RENDER_VOLUME_IMAGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"normals", RENDER_FRONT_NORMALS_IMAGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"depth", RENDER_FRONT_DEPTH_IMAGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"last_blurred_render", BLURRED_RENDER_1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"last_blurred_normal", BLURRED_NORMAL_1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"last_blurred_depth", BLURRED_DEPTH_1, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
                FlowCombinedImage{"colors", RENDER_FRONT_COLORS_IMAGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, postprocess_sampler},
            }},
            1, postprocess_blur_pipeline_info, m_postprocessing_pass_smooth->render_pass
        };
        m_postprocessing_pass_smooth->addSections(m_postprocess_section_smooth);

        
        addSections(render_pass_particles_depth, render_pass_particles_front, render_pass_spheres, m_postprocessing_pass_smooth);
    }
    void updateDescriptors(bool even_iteration){
        int last_blurred_render_image_index = even_iteration ? BLURRED_RENDER_2 : BLURRED_RENDER_1;
        int last_blurred_normal_image_index = even_iteration ? BLURRED_NORMAL_2 : BLURRED_NORMAL_1;
        int last_blurred_depth_image_index = even_iteration ? BLURRED_DEPTH_2 : BLURRED_DEPTH_1;

        m_postprocess_section_smooth->updateDescriptor(CombinedImageSamplerUpdateInfo("last_blurred_render", m_context.getImage(last_blurred_render_image_index), m_postprocess_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), last_blurred_render_image_index);
        m_postprocess_section_smooth->updateDescriptor(CombinedImageSamplerUpdateInfo("last_blurred_normal", m_context.getImage(last_blurred_normal_image_index), m_postprocess_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), last_blurred_normal_image_index);
        m_postprocess_section_smooth->updateDescriptor(CombinedImageSamplerUpdateInfo("last_blurred_depth",   m_context.getImage(last_blurred_depth_image_index), m_postprocess_sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), last_blurred_depth_image_index);
        m_postprocessing_pass_smooth->swapFramebuffers(even_iteration);
    }
};