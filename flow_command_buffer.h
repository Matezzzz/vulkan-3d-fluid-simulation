#include "flow_sections.h"




class FlowCommandBuffer : public CommandBuffer{
public:
    FlowCommandBuffer(CommandPool& command_pool) : CommandBuffer(command_pool.allocateBuffer())
    {}
    void record(const vector<ExtImage>& images, const vector<unique_ptr<FlowSection>>& sections, vector<PipelineImageState> image_states, bool start_record = true, bool end_record = true)
    {
        for (auto& s : sections){
            s->complete(images);
        }
        vector<PipelineImageState> first_image_states, last_image_states;
        getStartingAndEndingImageStates(images.size(), sections, first_image_states, last_image_states);
        if (start_record) startRecordPrimary();
        
        convertImagesToStartingConfig(images, first_image_states, image_states);
        
        for (const unique_ptr<FlowSection>& section : sections){
            //transition images to correct layouts if necessary
            section->transitionAllImages(*this, images, image_states);
            section->execute(*this, image_states);
        }
        if (end_record) endRecord();
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
            if (image_first_uses[i].layout != LAYOUT_NOT_USED_YET){
                VkImageMemoryBarrier m = images[i].createMemoryBarrier(image_states[i], image_first_uses[i]);
                cmdBarrier(image_states[i].last_use, image_first_uses[i].last_use, m);
                image_states[i] = PipelineImageState{image_first_uses[i]};
            }
        }
    }
};
