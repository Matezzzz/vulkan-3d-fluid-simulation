#include "just-a-vulkan-library/vulkan_include_all.h"
#include <memory>


using std::unique_ptr;
using std::make_unique;

enum ImageStatesEnum{
    IMAGE_SAMPLER,
    IMAGE_STORAGE_R,
    IMAGE_STORAGE_W,
    IMAGE_STORAGE_RW
};
constexpr VkImageLayout image_states_layouts[]{
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_IMAGE_LAYOUT_GENERAL,
    VK_IMAGE_LAYOUT_GENERAL,
    VK_IMAGE_LAYOUT_GENERAL
};
constexpr VkAccessFlags image_states_accesses[]{
    VK_ACCESS_SHADER_READ_BIT,
    VK_ACCESS_SHADER_READ_BIT,
    VK_ACCESS_SHADER_WRITE_BIT,
    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
};


struct Size3{
    int x, y, z;
};



constexpr VkImageLayout LAYOUT_NOT_USED_YET = VK_IMAGE_LAYOUT_MAX_ENUM;
class ImageState{
public:
    VkImageLayout layout;
    VkAccessFlags access;
    
    ImageState(VkImageLayout layout_, VkAccessFlags access_) :
        layout(layout_), access(access_)
    {}
    ImageState(ImageStatesEnum s) : ImageState(image_states_layouts[s], image_states_accesses[s])
    {}
    bool operator==(const ImageState& s){
        return layout == s.layout && access == s.access;
    }
    bool operator!=(const ImageState& s){
        return !(*this == s);
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





class ComputeRenderpass{
    DirectoryPipelinesContext m_dir_context;
    vector<ExtImage> m_images;
    CommandBuffer m_init_buffer;
    CommandBuffer m_draw_buffer;
    vector<unique_ptr<FlowSectionExec>> m_sections; 
public:
    ComputeRenderpass(const string& shader_dir, const vector<ExtImage>& attachments, const vector<unique_ptr<FlowSection>>& sections, vector<PipelineImageState> image_states, CommandPool& command_pool) :
        m_dir_context(shader_dir), m_images(attachments), m_init_buffer(command_pool.allocateBuffer()), m_draw_buffer(command_pool.allocateBuffer())
    {
        for (auto& s : sections){
            s->initialize(m_dir_context);
        }
        m_dir_context.createDescriptorPool();
        m_sections.reserve(sections.size());
        for (auto& s : sections){
            m_sections.push_back(s->complete());
        }
        vector<PipelineImageState> first_image_states, last_image_states;
        getStartingAndEndingImageStates(sections, first_image_states, last_image_states);
        recordInitBuffer(first_image_states, image_states, m_init_buffer);

        m_draw_buffer.startRecordPrimary();
        for (int i = 0; i < sections.size(); i++){
            //transition images to correct layouts if necessary
            for (const FlowSectionImageUsage& img : sections[i]->m_images_used){
                if (img.toImageState(true) != image_states[img.descriptor_index]){
                    m_draw_buffer.cmdBarrier(
                        image_states[img.descriptor_index].last_use, img.usage_stages.from, m_images[img.descriptor_index].createMemoryBarrier(
                            image_states[img.descriptor_index].layout, image_states[img.descriptor_index].access, img.state.layout, img.state.access
                        )
                    );
                    image_states[img.descriptor_index] = img.toImageState(false);
                }
            }
            m_sections[i]->execute(m_draw_buffer);
        }
        m_draw_buffer.endRecordPrimary();
    }
private:
    void getStartingAndEndingImageStates(const vector<unique_ptr<FlowSection>>& sections, vector<PipelineImageState>& image_first_uses, vector<PipelineImageState>& image_last_uses){
        image_first_uses.resize(m_images.size(), PipelineImageState{LAYOUT_NOT_USED_YET});
        image_last_uses.resize(m_images.size(), PipelineImageState{LAYOUT_NOT_USED_YET});
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
    void recordInitBuffer(const vector<PipelineImageState>& image_first_uses, vector<PipelineImageState>& image_states, CommandBuffer& init_buffer){
        m_init_buffer.startRecordPrimary(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        for (int i = 0; i < (int) m_images.size(); i++){
            if (image_first_uses[i].layout == LAYOUT_NOT_USED_YET){
                PRINT_ERROR("Image not used at all during the compute pipeline. Index: " << i);
            }else{
                VkImageMemoryBarrier m = m_images[i].createMemoryBarrier(image_states[i].layout, image_states[i].access, image_first_uses[i].layout, image_first_uses[i].access);
                init_buffer.cmdBarrier(image_states[i].last_use, image_first_uses[i].last_use, m);
                image_states[i] = PipelineImageState{image_first_uses[i]};
            }
        }
        m_init_buffer.endRecordPrimary();
    }
};

