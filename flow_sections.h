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


class FlowBufferContext{
    vector<ExtImage> images;
    vector<Buffer> buffers;
    vector<PipelineImageState> image_states;
public:
    FlowBufferContext(const vector<ExtImage>& images_, const vector<Buffer>& buffers_, const vector<PipelineImageState>& image_states_) : 
        images(images_), buffers(buffers_), image_states(image_states_)
    {}
    ExtImage& getImage(int index){return images[index];}
    Buffer& getBuffer(int index) {return buffers[index];}
    PipelineImageState& getImageState(int index){return image_states[index];}
    const PipelineImageState& getImageState(int index) const{return image_states[index];}
};



class FlowPipelineSectionImageUsage{
protected:
    FlowSectionImageUsage usage;
    DescriptorUpdateInfo info;
public:
    FlowPipelineSectionImageUsage(int descriptor_index_, ImageUsageStage usage_stages_, ImageState img_state_, const DescriptorUpdateInfo& desc_info) : 
        usage(descriptor_index_, usage_stages_, img_state_), info{desc_info}
    {}
    const FlowSectionImageUsage& getImageUsage() const{
        return usage;
    }
    DescriptorUpdateInfo getUpdateInfo(FlowBufferContext& ctx) const{
        DescriptorUpdateInfo i2(info);
        if (i2.isImage()){
            i2.imageInfo()->imageView = ctx.getImage(usage.descriptor_index);
        }else{
            i2.bufferInfo()->buffer = ctx.getBuffer(usage.descriptor_index);
        }
        return i2;
    }
};




class FlowStorageImage : public FlowPipelineSectionImageUsage{
public:
    FlowStorageImage(const string& name, int descriptor_index, ImageUsageStage usage_stages, ImageState img_state) :
        FlowPipelineSectionImageUsage(descriptor_index, usage_stages, img_state,
            StorageImageUpdateInfo(name, VK_NULL_HANDLE, img_state.layout))
    {}
};


class FlowCombinedImage : public FlowPipelineSectionImageUsage{
public:
    FlowCombinedImage(const string& name, int descriptor_index, ImageUsageStage usage_stages, ImageState img_state, VkSampler sampler) :
        FlowPipelineSectionImageUsage(descriptor_index, usage_stages, img_state,
            CombinedImageSamplerUpdateInfo{name, VK_NULL_HANDLE, sampler, img_state.layout})
    {}
};




class FlowSection{
public:
    vector<FlowSectionImageUsage> m_images_used;
    FlowSection(const vector<FlowSectionImageUsage>& usages) : m_images_used(usages)
    {}
    virtual void initializeShader(DirectoryPipelinesContext&){}
    virtual void complete(){};
    virtual void execute(CommandBuffer&) = 0;

    void transitionAllImages(CommandBuffer& buffer, FlowBufferContext& flow_context){
         //transition images to correct layouts if necessary
        for (const FlowSectionImageUsage& img : m_images_used){
            //if (img.toImageState(true) != image_states[img.descriptor_index]){
            int i = img.descriptor_index;
            buffer.cmdBarrier(
                flow_context.getImageState(i).last_use, img.usage_stages.from,
                flow_context.getImage(img.descriptor_index).createMemoryBarrier(flow_context.getImageState(i), img.state)
            );
            flow_context.getImageState(i) = img.toImageState(false);
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
    void complete(){
        for (unique_ptr<FlowSection>& s : *this){
            s->complete();
        }
    }
    void getLastImageStates(vector<PipelineImageState>& states) const{
        for (const unique_ptr<FlowSection>& section : *this){
            for (FlowSectionImageUsage img : section->m_images_used){
                states[img.descriptor_index] = img.toImageState(false);
            }
        }
    }
private:
    void addSections(){}
    template<typename T, typename... Args>
    void addSections(T* ptr, Args... args){
        emplace_back(unique_ptr<T>(ptr));
        addSections(args...);
    }
};




class FlowTransitionSection : public FlowSection{
public:
    FlowTransitionSection(const vector<FlowSectionImageUsage>& usages) : FlowSection(usages)
    {}
    virtual void execute(CommandBuffer&){}
};


class FlowIntoLoopTransitionSection : public FlowTransitionSection{
public:
    template<typename... Args>
    FlowIntoLoopTransitionSection(int img_count, const Args&... args) : FlowTransitionSection(getImageUsages(img_count, args...))
    {}
private:
    template<typename... Args>
    vector<FlowSectionImageUsage> getImageUsages(int img_count, const Args&... args){
        vector<PipelineImageState> all_image_states(img_count);
        fillImageUsages(all_image_states, args...);
        vector<FlowSectionImageUsage> image_usages;
        for (int i = 0; i < img_count; i++){
            if (all_image_states[i].layout != VK_IMAGE_LAYOUT_UNDEFINED){
                image_usages.push_back(FlowSectionImageUsage{i, ImageUsageStage{all_image_states[i].last_use}, all_image_states[i]});
            }
        }
        return image_usages;
    }
    void fillImageUsages(vector<PipelineImageState>&){}
    template<typename... Args>
    void fillImageUsages(vector<PipelineImageState>& usages, const SectionList& l, const Args&... args){
        l.getLastImageStates(usages);
        fillImageUsages(usages, args...);
    }
};





class FlowClearColorSection : public FlowSection{
    VkClearValue m_clear_value;
    const ExtImage& m_image;
public:
    FlowClearColorSection(FlowBufferContext& ctx, int descriptor_index, VkClearValue clear_value) :
        FlowSection({FlowSectionImageUsage(descriptor_index, ImageUsageStage(VK_PIPELINE_STAGE_TRANSFER_BIT), ImageState{IMAGE_TRANSFER_DST})}), m_clear_value(clear_value), m_image(ctx.getImage(descriptor_index))
    {}
    virtual void execute(CommandBuffer& buffer){
        buffer.cmdClearColor(m_image, ImageState{IMAGE_TRANSFER_DST}, m_clear_value.color);
    }
};


class FlowPipelineSectionDescriptors{
    FlowBufferContext& m_context;
    vector<FlowPipelineSectionImageUsage> m_images;
public:
    FlowPipelineSectionDescriptors(FlowBufferContext& ctx, const vector<FlowPipelineSectionImageUsage>& images) : m_context(ctx), m_images(images)
    {}
    vector<FlowSectionImageUsage> getImageUsages() const{
        vector<FlowSectionImageUsage> usages;
        usages.reserve(m_images.size());
        for (const FlowPipelineSectionImageUsage& u : m_images){
            usages.push_back(u.getImageUsage());
        }
        return usages;
    }
    vector<DescriptorUpdateInfo> getUpdateInfos() const{
        vector<DescriptorUpdateInfo> infos;
        infos.reserve(m_images.size());
        for (const FlowPipelineSectionImageUsage& u : m_images){
            infos.push_back(u.getUpdateInfo(m_context));
        }
        return infos;
    }
};


class FlowPipelineSection : public FlowSection{
protected:
    PipelineContext& m_context;
    Pipeline m_pipeline;
public:
    //constructor for compute pipelines
    FlowPipelineSection(DirectoryPipelinesContext& ctx, const string& name, const vector<FlowSectionImageUsage>& usages) :
        FlowSection(usages), m_context(ctx.getContext(name)), m_pipeline(m_context.createComputePipeline())
    {}
    //constructor for graphical pipelines
    FlowPipelineSection(DirectoryPipelinesContext& ctx, const string& name, const vector<FlowSectionImageUsage>& usages, const PipelineInfo& pipeline_info, VkRenderPass render_pass, uint32_t subpass_index = 0) :
        FlowSection(usages), m_context(ctx.getContext(name)), m_pipeline(m_context.createPipeline(pipeline_info, render_pass, subpass_index))
    {}
    PipelineContext& getShaderContext(){
        return m_context;
    }
};

class FlowSimplePipelineSection : public FlowPipelineSection{
    DescriptorSet m_descriptor_set;
    vector<DescriptorUpdateInfo> m_descriptor_update_infos;
public:
    FlowSimplePipelineSection(DirectoryPipelinesContext& ctx, const string& name, const FlowPipelineSectionDescriptors& usages) :
        FlowPipelineSection(ctx, name, usages.getImageUsages()), m_descriptor_update_infos(usages.getUpdateInfos())
    {
        m_context.reserveDescriptorSets(1);
    }
    FlowSimplePipelineSection(DirectoryPipelinesContext& ctx, const string& name, const FlowPipelineSectionDescriptors& usages, const PipelineInfo& pipeline_info, VkRenderPass render_pass, uint32_t subpass_index = 0) :
        FlowPipelineSection(ctx, name, usages.getImageUsages(), pipeline_info, render_pass, subpass_index), m_descriptor_update_infos(usages.getUpdateInfos())
    {
        m_context.reserveDescriptorSets(1);
    }

    virtual void complete(){
        m_context.allocateDescriptorSets(m_descriptor_set);
        m_descriptor_set.updateDescriptorsV(m_descriptor_update_infos);
    }
    virtual void execute(CommandBuffer& buffer){
        buffer.cmdBindPipeline(m_pipeline, m_descriptor_set);
    }
};


class FlowComputeSection : public FlowSimplePipelineSection{
    Size3 m_compute_size;
public:
    FlowComputeSection(DirectoryPipelinesContext& ctx, const string& name, const FlowPipelineSectionDescriptors& usages, Size3 compute_size) :
        FlowSimplePipelineSection(ctx, name, usages), m_compute_size(compute_size)
    {}
    virtual void execute(CommandBuffer& buffer){
        FlowSimplePipelineSection::execute(buffer);
        buffer.cmdDispatchCompute(m_compute_size.x, m_compute_size.y, m_compute_size.z);
    }
};


class FlowGraphicsSection : public FlowSimplePipelineSection{
    uint32_t m_vertex_count;
public:
    FlowGraphicsSection(DirectoryPipelinesContext& ctx, const string& name, const FlowPipelineSectionDescriptors& usages,
            uint32_t vertex_count, const PipelineInfo& pipeline_info, VkRenderPass render_pass, uint32_t subpass_index = 0) :
        FlowSimplePipelineSection(ctx, name, usages, pipeline_info, render_pass, subpass_index), m_vertex_count(vertex_count)
    {}
    virtual void execute(CommandBuffer& buffer){
        FlowSimplePipelineSection::execute(buffer);
        buffer.cmdDrawVertices(m_vertex_count);
    }
};


template<typename T>
class FlowPushConstantSection : public T{
    PushConstantData m_push_constant_data;
public:
    /**
     * Args should be:
     * @param args (for compute section) Size3 compute_size
     * @param args (for graphics section) uint32_t vertex_count, const PipelineInfo& pipeline_info, VkRenderPass render_pass, uint32_t subpass_index = 0
     */
    template<typename... Args>
    FlowPushConstantSection(DirectoryPipelinesContext& ctx, const string& name, const FlowPipelineSectionDescriptors& usages, const Args&... args) :
        T(ctx, name, usages, args...), m_push_constant_data(this->m_context.createPushConstantData())
    {}
    PushConstantData& getPushConstantData(){
        return m_push_constant_data;
    }
    virtual void execute(CommandBuffer& buffer){
        buffer.cmdPushConstants(this->m_pipeline, m_push_constant_data);
        T::execute(buffer);
    }
};


typedef FlowPushConstantSection<FlowComputeSection> FlowComputePushConstantSection;
typedef FlowPushConstantSection<FlowGraphicsSection> FlowGraphicsPushConstantSection;




#endif