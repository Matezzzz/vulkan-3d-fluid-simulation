#ifndef MARCHING_CUBES_H
#define MARCHING_CUBES_H

#include <fstream>

#include "just-a-vulkan-library/vulkan_include_all.h"

using std::ifstream;

class MarchingCubesBuffers{
public:
    Buffer triangle_count_buffer;
    Buffer vertex_edge_indices_buffer;
    
    MarchingCubesBuffers() :
        triangle_count_buffer(BufferInfo(4*256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).create()),
        vertex_edge_indices_buffer(BufferInfo(4*15*256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).create())
    {}
    void loadData(LocalObjectCreator& local_object_creator){
        loadFromFile(local_object_creator, triangle_count_buffer, "surface_render_data/polygon_counts.txt", 256);
        loadFromFile(local_object_creator, vertex_edge_indices_buffer, "surface_render_data/polygon_edge_indices.txt", 256*15);
    }
private:
    void loadFromFile(LocalObjectCreator& local_object_creator, Buffer& buffer, const string& filename, uint32_t size){
        vector<uint32_t> data;
        data.reserve(size);
        ifstream data_file(filename);
        uint32_t a;
        for (int i = 0; i < size; i++){
            data_file >> a;
            data.push_back(a);
        }
        local_object_creator.copyToLocal(data, buffer);
    }
};



#endif