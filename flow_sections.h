#ifndef FLOW_SECTIONS_H
#define FLOW_SECTIONS_H

#include "just-a-vulkan-library/vulkan_include_all.h"
#include <memory>

using std::unique_ptr;
using std::make_unique;


struct Size3{
    uint32_t x, y, z;
    Size3 operator/(const Size3& s) const{
        return Size3{x / s.x, y / s.y, z / s.z};
    }
};

class PipelineImageState : public ImageState{
public:
    VkPipelineStageFlags last_use;
    PipelineImageState(VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED, VkAccessFlags access_ = 0, VkPipelineStageFlags last_use_ = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) :
        ImageState(layout_, access_), last_use(last_use_)
    {}
    PipelineImageState(ImageState state, VkPipelineStageFlags last_use_ = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) :
        ImageState(state), last_use(last_use_)
    {}
};



class ImageUsageStage{
public:
    VkPipelineStageFlags from;
    VkPipelineStageFlags to;
    ImageUsageStage() : ImageUsageStage(VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM)
    {}
    ImageUsageStage(VkPipelineStageFlags from_, VkPipelineStageFlags to_) : from(from_), to(to_)
    {}
    ImageUsageStage(VkPipelineStageFlags stage) : ImageUsageStage(stage, stage)
    {}
    VkPipelineStageFlags get(bool get_from) const{
        return get_from ? from : to;
    }
};


class FlowSectionImageUsage{
public:
    int descriptor_index;
    ImageUsageStage usage_stages;
    ImageState state;
    FlowSectionImageUsage() : FlowSectionImageUsage(-1, ImageUsageStage(), ImageState{IMAGE_INVALID})
    {}
    FlowSectionImageUsage(int descriptor_index_, ImageUsageStage usage_stages_, ImageState img_state_) : 
        descriptor_index(descriptor_index_), usage_stages(usage_stages_), state(img_state_)
    {}
    PipelineImageState toImageState(bool stage_from) const{
        return PipelineImageState{state, usage_stages.get(stage_from)};
    }
};



class FlowSection{
public:
    vector<FlowSectionImageUsage> m_images_used;
    FlowSection(const vector<FlowSectionImageUsage>& usages) : m_images_used(usages)
    {}
    virtual void initializeShader(DirectoryPipelinesContext&){}
    virtual void complete(const vector<ExtImage>&) = 0;
    virtual void execute(CommandBuffer&, vector<PipelineImageState>&) = 0;

    void transitionAllImages(CommandBuffer& buffer, const vector<ExtImage>& images, vector<PipelineImageState>& image_states){
         //transition images to correct layouts if necessary
        for (const FlowSectionImageUsage& img : m_images_used){
            //if (img.toImageState(true) != image_states[img.descriptor_index]){
            buffer.cmdBarrier(
                image_states[img.descriptor_index].last_use, img.usage_stages.from,
                images[img.descriptor_index].createMemoryBarrier(image_states[img.descriptor_index], img.state)
            );
            image_states[img.descriptor_index] = img.toImageState(false);
        }
    }
};




class FlowTransitionSection : public FlowSection{
public:
    FlowTransitionSection(const vector<ImageState>& new_states) : FlowSection(createImageUsageVector(new_states))
    {}
    virtual void complete(const vector<ExtImage>&){}
    virtual void execute(CommandBuffer&, vector<PipelineImageState>&){}
private:
    vector<FlowSectionImageUsage> createImageUsageVector(const vector<ImageState>& new_states){
        vector<FlowSectionImageUsage> usg(new_states.size());
        for (int i = 0; i < usg.size(); i++){
            usg[i] = FlowSectionImageUsage(i, ImageUsageStage(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT), new_states[i]);
        }
        return usg;
    }
};






class FlowClearColorSection : public FlowSection{
    VkClearValue m_clear_value;
    const ExtImage* m_image;
public:
    FlowClearColorSection(int descriptor_index, VkClearValue clear_value) :
        FlowSection({FlowSectionImageUsage(descriptor_index, ImageUsageStage(VK_PIPELINE_STAGE_TRANSFER_BIT), ImageState{IMAGE_TRANSFER_DST})}), m_clear_value(clear_value)
    {}
    virtual void complete(const vector<ExtImage>& attachments){
        m_image = &attachments[m_images_used[0].descriptor_index];
    }
    virtual void execute(CommandBuffer& buffer, vector<PipelineImageState>&){
        buffer.cmdClearColor(*m_image, ImageState{IMAGE_TRANSFER_DST}, m_clear_value.color);
    }
};



class FlowComputeSection : public FlowSection{
    string m_shader_dir_name;
    Size3 m_compute_size;
    
    vector<DescriptorUpdateInfo> m_descriptor_update_infos;
    
    DescriptorSet m_descriptor_set;
protected:
    PipelineContext& m_context;
    Pipeline m_pipeline;
public:
    FlowComputeSection(DirectoryPipelinesContext& ctx, const string& name, Size3 compute_size, const vector<FlowSectionImageUsage>& usages, const vector<DescriptorUpdateInfo>& update_infos) :
        FlowSection(usages), m_shader_dir_name(name), m_compute_size(compute_size), m_descriptor_update_infos(update_infos), m_context(ctx.getContext(m_shader_dir_name))
    {
        m_context.reserveDescriptorSets(1);
    }
    PipelineContext& getShaderContext(){
        return m_context;
    }
    virtual void complete(const vector<ExtImage>&){
        m_pipeline = m_context.createComputePipeline();
        m_context.allocateDescriptorSets(m_descriptor_set);
        m_descriptor_set.updateDescriptorsV(m_descriptor_update_infos);
    }
    virtual void execute(CommandBuffer& buffer, vector<PipelineImageState>&){
        buffer.cmdBindPipeline(m_pipeline, m_descriptor_set);
        buffer.cmdDispatchCompute(m_compute_size.x, m_compute_size.y, m_compute_size.z);
    }
};


class FlowComputePushConstantSection : public FlowComputeSection{
    PushConstantData m_push_constant_data;
public:
    FlowComputePushConstantSection(DirectoryPipelinesContext& ctx, const string& name, Size3 compute_size, const vector<FlowSectionImageUsage>& usages, const vector<DescriptorUpdateInfo>& update_infos) :
        FlowComputeSection(ctx, name, compute_size, usages, update_infos), m_push_constant_data(m_context.createPushConstantData())
    {}
    PushConstantData& getPushConstantData(){
        return m_push_constant_data;
    }
    virtual void execute(CommandBuffer& buffer, vector<PipelineImageState>& image_states){
        buffer.cmdPushConstants(m_pipeline, m_push_constant_data);
        FlowComputeSection::execute(buffer, image_states);
    }
};


class FlowCommandSection : public FlowSection{
    VkCommandBuffer m_buffer;
    uint32_t m_iteration_count;
    vector<FlowSectionImageUsage> m_last_usages;
public:
    FlowCommandSection(VkCommandBuffer commands, const vector<FlowSectionImageUsage>& first_usages, const vector<FlowSectionImageUsage>& last_usages, uint32_t iteration_count = 1) :
        FlowSection(first_usages), m_buffer(commands), m_iteration_count(iteration_count), m_last_usages(last_usages)
    {}
    virtual void execute(CommandBuffer& buffer, vector<PipelineImageState>& image_states){
        for (uint32_t i = 0; i < m_iteration_count; i++){
            vkCmdExecuteCommands(buffer, 1, &m_buffer);
        }
        for (const FlowSectionImageUsage& img : m_last_usages){
            image_states[img.descriptor_index] = img.toImageState(false);
        }
    }
};



class SectionList : public vector<unique_ptr<FlowSection>>{
public:
    template<typename... Args>
    SectionList(Args*... args){
        reserve(sizeof...(args));
        addSections(args...);
    }
private:
    void addSections(){}
    template<typename T, typename... Args>
    void addSections(T* ptr, Args... args){
        emplace_back(unique_ptr<T>(ptr));
        addSections(args...);
    }
};




#endif