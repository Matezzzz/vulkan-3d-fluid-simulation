#ifndef FLOW_SECTIONS_H
#define FLOW_SECTIONS_H

#include "just-a-vulkan-library/vulkan_include_all.h"
#include <memory>

using std::unique_ptr;
using std::make_unique;


struct Size3{
    uint32_t x, y, z;
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
    ImageUsageStage(VkPipelineStageFlags from_, VkPipelineStageFlags to_) : from(from_), to(to_)
    {}
    ImageUsageStage(VkPipelineStageFlags stage) : ImageUsageStage(stage, stage)
    {}
    VkPipelineStageFlags get(bool get_from) const{
        return get_from ? from : to;
    }
};


struct FlowSectionImageUsage{
    int descriptor_index;
    ImageUsageStage usage_stages;
    ImageState state;
    FlowSectionImageUsage(int descriptor_index_, ImageUsageStage usage_stages_, ImageState img_state_) : 
        descriptor_index(descriptor_index_), usage_stages(usage_stages_), state(img_state_)
    {}
    PipelineImageState toImageState(bool stage_from) const{
        return PipelineImageState{state, usage_stages.get(stage_from)};
    }
};



class FlowSectionExec{
public:
    virtual void execute(CommandBuffer&) = 0;
};


class FlowSection{
public:
    vector<FlowSectionImageUsage> m_images_used;
    FlowSection(const vector<FlowSectionImageUsage>& usages) : m_images_used(usages)
    {}
    virtual void initialize(DirectoryPipelinesContext&){}
    virtual unique_ptr<FlowSectionExec> complete() = 0;
};


class FlowTransitionSection : public FlowSection{
public:
    FlowTransitionSection(const vector<FlowSectionImageUsage>& usages) : FlowSection(usages)
    {}

};



class FlowComputeSectionExec : public FlowSectionExec{
    Pipeline pipeline;
    DescriptorSet set;
    Size3 compute_size;
public:
    FlowComputeSectionExec(PipelineContext* ctx, Size3 comp_size, const vector<DescriptorUpdateInfo>& update_infos) :
        pipeline(ctx->createComputePipeline()), compute_size(comp_size)
    {
        ctx->allocateDescriptorSets(set);
        set.updateDescriptorsV(update_infos);
    }
    virtual void execute(CommandBuffer& buffer){
        buffer.cmdBindPipeline(pipeline, set);
        buffer.cmdDispatchCompute(compute_size.x, compute_size.y, compute_size.z);
    }
};


class FlowComputeSection : public FlowSection{
    string m_shader_dir_name;
    Size3 m_compute_size;
    
    vector<DescriptorUpdateInfo> m_descriptor_infos;
    PipelineContext* m_context = nullptr;
public:
    FlowComputeSection(const string& name, Size3 compute_size, const vector<FlowSectionImageUsage>& usages, const vector<DescriptorUpdateInfo>& update_infos) :
        FlowSection(usages), m_shader_dir_name(name), m_compute_size(compute_size), m_descriptor_infos(update_infos)
    {}
    virtual void initialize(DirectoryPipelinesContext& ctx){
        m_context = &ctx.getContext(m_shader_dir_name);
        m_context->reserveDescriptorSets(1);
    }
    virtual unique_ptr<FlowSectionExec> complete(){
        return make_unique<FlowComputeSectionExec>(m_context, m_compute_size, m_descriptor_infos);
    }
};




#endif