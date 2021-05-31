#include "just-a-vulkan-library/vulkan_include_all.h"



constexpr VkImageLayout LAYOUT_NOT_USED_YET = VK_IMAGE_LAYOUT_MAX_ENUM;
struct ImageState{
    VkImageLayout layout;
    VkAccessFlags access;
    VkPipelineStageFlags last_use;
    ImageState(VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED, VkAccessFlags access_ = 0, VkPipelineStageFlags last_use_ = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) :
        layout(layout_), access(access_), last_use(last_use_)
    {}
    bool operator==(const ImageState& s){
        return layout == s.layout && access == s.access;
    }
    bool operator!=(const ImageState& s){
        return !(*this == s);
    }
};


struct ComputeSectionImageUsage{
    int descriptor_index;
    VkPipelineStageFlags use_from;
    VkPipelineStageFlags use_to;
    VkImageLayout layout;
    VkAccessFlags access;
    ImageState toImageState(bool stage_from) const{
        return ImageState{layout, access, stage_from ? use_from : use_to};
    }
};


struct ComputeRenderpassSection{
    string name;
    int compute_x;
    int compute_y;
    int compute_z;
    vector<ComputeSectionImageUsage> descriptors_used;
    vector<DescriptorUpdateInfo> descriptor_infos;
};




class ComputeRenderpass{
    DirectoryPipelinesContext m_dir_context;
    vector<PipelineContext*> m_contexts;
    vector<Pipeline> m_pipelines;
    vector<DescriptorSet> m_descriptor_sets;
    vector<ExtImage> m_images;
    CommandBuffer m_init_buffer;
    CommandBuffer m_draw_buffer;
public:
    ComputeRenderpass(const string& shader_dir, const vector<ExtImage>& attachments, const vector<ComputeRenderpassSection>& sections, vector<ImageState> image_states, CommandPool& command_pool) :
        m_dir_context(shader_dir), m_contexts(sections.size()), m_pipelines(sections.size()), m_descriptor_sets(sections.size()),
        m_images(attachments), m_init_buffer(command_pool.allocateBuffer()), m_draw_buffer(command_pool.allocateBuffer())
    {
        for (int i = 0; i < sections.size(); i++){
            m_contexts[i] = &m_dir_context.getContext(sections[i].name);
            m_contexts[i]->reserveDescriptorSets(1);
        }
        m_dir_context.createDescriptorPool();


        for (int i = 0; i < sections.size(); i++){
            m_contexts[i]->allocateDescriptorSets(m_descriptor_sets[i]);
            m_descriptor_sets[i].updateDescriptorsV(sections[i].descriptor_infos);
        }
        vector<ImageState> first_image_states, last_image_states;
        getStartingAndEndingImageStates(sections, first_image_states, last_image_states);
        recordInitBuffer(first_image_states, image_states, m_init_buffer);
        
        for (int i = 0; i < sections.size(); i++){
            m_pipelines[i] = m_contexts[i]->createComputePipeline();
        }

        m_draw_buffer.startRecordPrimary();
        for (int i = 0; i < sections.size(); i++){
            //transition images to correct layouts if necessary
            for (const ComputeSectionImageUsage& img : sections[i].descriptors_used){
                if (img.toImageState(true) != image_states[img.descriptor_index]){
                    m_draw_buffer.cmdBarrier(
                        image_states[img.descriptor_index].last_use, img.use_from, m_images[img.descriptor_index].createMemoryBarrier(
                            image_states[img.descriptor_index].layout, image_states[img.descriptor_index].access, img.layout, img.access
                        )
                    );
                    image_states[img.descriptor_index] = img.toImageState(false);
                }
            }
            m_draw_buffer.cmdBindPipeline(m_pipelines[i], m_descriptor_sets[i]);
            m_draw_buffer.cmdDispatchCompute(sections[i].compute_x, sections[i].compute_y, sections[i].compute_z);
        }
        m_draw_buffer.endRecordPrimary();
    }
private:
    void getStartingAndEndingImageStates(const vector<ComputeRenderpassSection>& sections, vector<ImageState>& image_first_uses, vector<ImageState>& image_last_uses){
        image_first_uses.resize(m_images.size(), ImageState{LAYOUT_NOT_USED_YET});
        image_last_uses.resize(m_images.size(), ImageState{LAYOUT_NOT_USED_YET});
        for (const ComputeRenderpassSection& section : sections){
            for (const ComputeSectionImageUsage& img : section.descriptors_used){
                ImageState& s = image_first_uses[img.descriptor_index];
                if (s.layout == LAYOUT_NOT_USED_YET){
                    s = img.toImageState(true);
                }
                image_last_uses[img.descriptor_index] = img.toImageState(false);
            }
        }
    }
    void recordInitBuffer(const vector<ImageState>& image_first_uses, vector<ImageState>& image_states, CommandBuffer& init_buffer){
        m_init_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        for (int i = 0; i < (int) m_images.size(); i++){
            if (image_first_uses[i].layout == LAYOUT_NOT_USED_YET){
                PRINT_ERROR("Image not used at all during the compute pipeline. Index: " << i);
            }else{
                VkImageMemoryBarrier m = m_images[i].createMemoryBarrier(image_states[i].layout, image_states[i].access, image_first_uses[i].layout, image_first_uses[i].access);
                init_buffer.cmdBarrier(image_states[i].last_use, image_first_uses[i].last_use, m);
                image_states[i] = ImageState{image_first_uses[i]};
            }
        }
        m_init_buffer.endRecordPrimary();
    }
};

