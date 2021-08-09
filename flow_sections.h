#ifndef FLOW_SECTIONS_H
#define FLOW_SECTIONS_H

#include "just-a-vulkan-library/vulkan_include_all.h"
#include <memory>

using std::unique_ptr;
using std::make_unique;








struct Size3 : public glm::uvec3{
    using glm::uvec3::uvec3;
    Size3 operator/(const Size3& s) const{
        return Size3{x / s.x, y / s.y, z / s.z};
    }
    uint32_t volume() const{
        return x * y * z;
    }
    Size3 operator*(uint32_t k) const{
        return Size3{k * x, k * y, k * z};
    }
};




class PipelineBufferState : public BufferState{
public:
    VkPipelineStageFlags last_use;
    PipelineBufferState(BufferState state, VkPipelineStageFlags last_use_ = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) : BufferState(state), last_use(last_use_)
    {}

    PipelineBufferState(VkPipelineStageFlags last_use_ = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT) : last_use(last_use_)
    {}
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





class DescriptorUsageStage{
public:
    VkPipelineStageFlags from;
    VkPipelineStageFlags to;
    DescriptorUsageStage() : DescriptorUsageStage(VK_PIPELINE_STAGE_FLAG_BITS_MAX_ENUM)
    {}
    DescriptorUsageStage(VkPipelineStageFlags from_, VkPipelineStageFlags to_) : from(from_), to(to_)
    {}
    DescriptorUsageStage(VkPipelineStageFlags stage) : DescriptorUsageStage(stage, stage)
    {}
    VkPipelineStageFlags get(bool get_from) const{
        return get_from ? from : to;
    }
};






class FlowSectionDescriptorUsage{
public:
    bool is_image;
    int descriptor_index;
    DescriptorUsageStage usage_stages;
    union{
        ImageState image;
        BufferState buffer;
    } state;
    FlowSectionDescriptorUsage() : FlowSectionDescriptorUsage(-1, DescriptorUsageStage(), ImageState{IMAGE_INVALID})
    {}
    //constructor for images
    FlowSectionDescriptorUsage(int descriptor_index_, DescriptorUsageStage usage_stages_, ImageState img_state) : 
        is_image(true), descriptor_index(descriptor_index_), usage_stages(usage_stages_), state{.image=img_state}
    {}
    //constructor for buffers
    FlowSectionDescriptorUsage(int descriptor_index_, DescriptorUsageStage usage_stages_, BufferState buf_state) : 
        is_image(false), descriptor_index(descriptor_index_), usage_stages(usage_stages_), state{.buffer=buf_state}
    {}
    PipelineImageState toImageState(bool stage_from) const{
        return PipelineImageState{state.image, usage_stages.get(stage_from)};
    }
    PipelineBufferState toBufferState(bool stage_from) const{
        return PipelineBufferState{state.buffer, usage_stages.get(stage_from)};
    }
    bool isImage() const{
        return is_image;
    }
};



class FlowBufferContext{
    vector<ExtImage> m_images;
    vector<PipelineImageState> m_image_states;

    vector<Buffer> m_buffers;
    vector<PipelineBufferState> m_buffer_states;
public:
    FlowBufferContext(const vector<ExtImage>& images, const vector<PipelineImageState>& image_states, const vector<Buffer>& buffers, const vector<PipelineBufferState>& buffer_states) : 
        m_images(images), m_image_states(image_states), m_buffers(buffers), m_buffer_states(buffer_states)
    {}
    ExtImage& getImage(int index){return m_images[index];}
    Buffer& getBuffer(int index) {return m_buffers[index];}
    PipelineImageState& getImageState(int index){return m_image_states[index];}
    const PipelineImageState& getImageState(int index) const{return m_image_states[index];}
    PipelineBufferState& getBufferState(int index){return m_buffer_states[index];}
    const PipelineBufferState& getBufferState(int index) const{return m_buffer_states[index];}
};



class FlowPipelineSectionDescriptorUsage{
protected:
    FlowSectionDescriptorUsage usage;
    DescriptorUpdateInfo info;
public:
    FlowPipelineSectionDescriptorUsage(int descriptor_index, DescriptorUsageStage usage_stages, BufferState buf_state, const DescriptorUpdateInfo& desc_info) : 
        usage(descriptor_index, usage_stages, buf_state), info{desc_info}
    {}
    FlowPipelineSectionDescriptorUsage(int descriptor_index, DescriptorUsageStage usage_stages, ImageState img_state, const DescriptorUpdateInfo& desc_info) : 
        usage(descriptor_index, usage_stages, img_state), info{desc_info}
    {}
    const FlowSectionDescriptorUsage& getUsage() const{
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




class FlowStorageImage : public FlowPipelineSectionDescriptorUsage{
public:
    FlowStorageImage(const string& name, int descriptor_index, DescriptorUsageStage usage_stages, ImageState img_state) :
        FlowPipelineSectionDescriptorUsage(descriptor_index, usage_stages, img_state,
            StorageImageUpdateInfo(name, VK_NULL_HANDLE, img_state.layout))
    {}
};


class FlowCombinedImage : public FlowPipelineSectionDescriptorUsage{
public:
    FlowCombinedImage(const string& name, int descriptor_index, DescriptorUsageStage usage_stages, ImageState img_state, VkSampler sampler) :
        FlowPipelineSectionDescriptorUsage(descriptor_index, usage_stages, img_state,
            CombinedImageSamplerUpdateInfo{name, VK_NULL_HANDLE, sampler, img_state.layout})
    {}
};



class FlowUniformBuffer : public FlowPipelineSectionDescriptorUsage{
public:
    FlowUniformBuffer(const string& name, int descriptor_index, DescriptorUsageStage usage_stages, BufferState buf_state) :
        FlowPipelineSectionDescriptorUsage(descriptor_index, usage_stages, buf_state,
            UniformBufferUpdateInfo{name, VK_NULL_HANDLE})
    {}
};


class FlowStorageBuffer : public FlowPipelineSectionDescriptorUsage{
public:
    FlowStorageBuffer(const string& name, int descriptor_index, DescriptorUsageStage usage_stages, BufferState buf_state) :
        FlowPipelineSectionDescriptorUsage(descriptor_index, usage_stages, buf_state,
            StorageBufferUpdateInfo{name, VK_NULL_HANDLE})
    {}
};




class FlowSection{
public:
    vector<FlowSectionDescriptorUsage> m_descriptors_used;
    FlowSection(const vector<FlowSectionDescriptorUsage>& usages) : m_descriptors_used(usages)
    {}
    virtual void initializeShader(DirectoryPipelinesContext&){}
    virtual void complete(){};
    virtual void execute(CommandBuffer&) = 0;

    void transitionAllImages(CommandBuffer& buffer, FlowBufferContext& flow_context){
         //transition images to correct layouts if necessary
        for (const FlowSectionDescriptorUsage& d : m_descriptors_used){
            //if (img.toImageState(true) != image_states[img.descriptor_index]){
            int i = d.descriptor_index;

            if (d.isImage()){
                PipelineImageState& state = flow_context.getImageState(i);
                buffer.cmdBarrier(state.last_use, d.usage_stages.from, flow_context.getImage(i).createMemoryBarrier(state, d.state.image));
                state = d.toImageState(false);
            }else{
                PipelineBufferState& state = flow_context.getBufferState(i);
                buffer.cmdBarrier(state.last_use, d.usage_stages.from, flow_context.getBuffer(i).createMemoryBarrier(state.access, d.state.buffer.access));
                state = d.toBufferState(false);
            }
            
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
    /*void getLastImageStates(vector<PipelineImageState>& states) const{
        for (const unique_ptr<FlowSection>& section : *this){
            for (const FlowSectionDescriptorUsage& img : section->m_descriptors_used){
                states[img.descriptor_index] = img.toImageState(false);
            }
        }
    }*/
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
    FlowTransitionSection(const vector<FlowSectionDescriptorUsage>& usages) : FlowSection(usages)
    {}
    virtual void execute(CommandBuffer&){}
};


/*class FlowIntoLoopTransitionSection : public FlowTransitionSection{
public:
    template<typename... Args>
    FlowIntoLoopTransitionSection(int img_count, const Args&... args) : FlowTransitionSection(getImageUsages(img_count, args...))
    {}
private:
    template<typename... Args>
    vector<FlowSectionDescriptorUsage> getImageUsages(int img_count, const Args&... args){
        vector<PipelineImageState> all_image_states(img_count);
        fillImageUsages(all_image_states, args...);
        vector<FlowSectionDescriptorUsage> image_usages;
        for (int i = 0; i < img_count; i++){
            if (all_image_states[i].layout != VK_IMAGE_LAYOUT_UNDEFINED){
                image_usages.push_back(FlowSectionDescriptorUsage{i, DescriptorUsageStage{all_image_states[i].last_use}, all_image_states[i]});
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
};*/





class FlowClearColorSection : public FlowSection{
    VkClearValue m_clear_value;
    const ExtImage& m_image;
public:
    FlowClearColorSection(FlowBufferContext& ctx, int descriptor_index, VkClearValue clear_value) :
        FlowSection({FlowSectionDescriptorUsage(descriptor_index, DescriptorUsageStage(VK_PIPELINE_STAGE_TRANSFER_BIT), ImageState{IMAGE_TRANSFER_DST})}), m_clear_value(clear_value), m_image(ctx.getImage(descriptor_index))
    {}
    virtual void execute(CommandBuffer& buffer){
        buffer.cmdClearColor(m_image, ImageState{IMAGE_TRANSFER_DST}, m_clear_value.color);
    }
};


class FlowPipelineSectionDescriptors{
    FlowBufferContext& m_context;
    vector<FlowPipelineSectionDescriptorUsage> m_images;
public:
    FlowPipelineSectionDescriptors(FlowBufferContext& ctx, const vector<FlowPipelineSectionDescriptorUsage>& images) : m_context(ctx), m_images(images)
    {}
    vector<FlowSectionDescriptorUsage> getImageUsages() const{
        vector<FlowSectionDescriptorUsage> usages;
        usages.reserve(m_images.size());
        for (const FlowPipelineSectionDescriptorUsage& u : m_images){
            usages.push_back(u.getUsage());
        }
        return usages;
    }
    vector<DescriptorUpdateInfo> getUpdateInfos() const{
        vector<DescriptorUpdateInfo> infos;
        infos.reserve(m_images.size());
        for (const FlowPipelineSectionDescriptorUsage& u : m_images){
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
    FlowPipelineSection(DirectoryPipelinesContext& ctx, const string& name, const vector<FlowSectionDescriptorUsage>& usages) :
        FlowSection(usages), m_context(ctx.getContext(name)), m_pipeline(m_context.createComputePipeline())
    {}
    //constructor for graphical pipelines
    FlowPipelineSection(DirectoryPipelinesContext& ctx, const string& name, const vector<FlowSectionDescriptorUsage>& usages, const PipelineInfo& pipeline_info, VkRenderPass render_pass, uint32_t subpass_index = 0) :
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