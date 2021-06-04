#include "flow_sections.h"




class FlowCommandBuffer : public CommandBuffer{
    DirectoryPipelinesContext m_dir_context;
public:
    FlowCommandBuffer(const string& shader_dir, const vector<ExtImage>& images, const vector<unique_ptr<FlowSection>>& sections, vector<PipelineImageState> image_states, CommandPool& command_pool, bool loop = false) :
        CommandBuffer(command_pool.allocateBuffer()), m_dir_context(shader_dir)
    {
        vector<unique_ptr<FlowSectionExec>> sections_exec;
        for (auto& s : sections){
            s->initialize(m_dir_context);
        }
        m_dir_context.createDescriptorPool();
        sections_exec.reserve(sections.size());
        for (auto& s : sections){
            sections_exec.push_back(s->complete());
        }
        vector<PipelineImageState> first_image_states, last_image_states;
        getStartingAndEndingImageStates(images.size(), sections, first_image_states, last_image_states);

        startRecordPrimary();
        if (loop){
            for (int i = 0; i < images.size(); i++){
                if (first_image_states[i] != image_states[i]){
                    PRINT_ERROR("Image " << i << " is in wrong state for this command buffer. Required: " << first_image_states[i].toString() << ", Got: " << image_states[i].toString())
                }
            }
        }else{
            convertImagesToStartingConfig(images, first_image_states, image_states);
        }
        
        for (int i = 0; i < sections.size(); i++){
            //transition images to correct layouts if necessary
            for (const FlowSectionImageUsage& img : sections[i]->m_images_used){
                if (img.toImageState(true) != image_states[img.descriptor_index]){
                    cmdBarrier(
                        image_states[img.descriptor_index].last_use, img.usage_stages.from,
                        images[img.descriptor_index].createMemoryBarrier(image_states[img.descriptor_index], img.state)
                    );
                    image_states[img.descriptor_index] = img.toImageState(false);
                }
            }
            sections_exec[i]->execute(*this);
        }
        endRecordPrimary();
    }
private:
    void getStartingAndEndingImageStates(int image_count, const vector<unique_ptr<FlowSection>>& sections, vector<PipelineImageState>& image_first_uses, vector<PipelineImageState>& image_last_uses){
        image_first_uses.resize(image_count, PipelineImageState{LAYOUT_NOT_USED_YET});
        image_last_uses.resize(image_count, PipelineImageState{LAYOUT_NOT_USED_YET});
        for (auto& section : sections){
            for (const FlowSectionImageUsage& img : section->m_images_used){
                PipelineImageState& s = image_first_uses[img.descriptor_index];
                if (s.layout == LAYOUT_NOT_USED_YET){
                    s = img.toImageState(true);
                }
                image_last_uses[img.descriptor_index] = img.toImageState(false);
            }
        }
    }
    void convertImagesToStartingConfig(const vector<ExtImage>& images, const vector<PipelineImageState>& image_first_uses, vector<PipelineImageState>& image_states){
        for (int i = 0; i < (int) images.size(); i++){
            if (image_first_uses[i].layout == LAYOUT_NOT_USED_YET){
                PRINT_ERROR("Image not used at all during the compute pipeline. Index: " << i);
            }else{
                VkImageMemoryBarrier m = images[i].createMemoryBarrier(image_states[i], image_first_uses[i]);
                cmdBarrier(image_states[i].last_use, image_first_uses[i].last_use, m);
                image_states[i] = PipelineImageState{image_first_uses[i]};
            }
        }
    }
};

