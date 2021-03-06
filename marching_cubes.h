#ifndef MARCHING_CUBES_H
#define MARCHING_CUBES_H

#include <fstream>

#include "just-a-vulkan-library/vulkan_include_all.h"

using std::ifstream;



/**
 * MarchingCubesBuffers
 *  - This class is responsible for loading buffers that will be later used by the marching cubes method.
 */
class MarchingCubesBuffers{
public:
    //buffer of triangle counts for each configuration
    Buffer triangle_count_buffer;
    //buffer of edge indices for each configuration
    Buffer vertex_edge_indices_buffer;
    
    MarchingCubesBuffers() :
        //4 bytes (uint) * 256 possible configurations
        triangle_count_buffer(BufferInfo(4*256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).create()),
        //4 bytes (uint) * 15 indices per config * 256 possible configurations
        vertex_edge_indices_buffer(BufferInfo(4*15*256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT).create())
    {}
    void loadData(LocalObjectCreator& local_object_creator){
        loadFromFile(local_object_creator, triangle_count_buffer, "surface_render_data/polygon_counts.txt", 256);
        loadFromFile(local_object_creator, vertex_edge_indices_buffer, "surface_render_data/polygon_edge_indices.txt", 256*15);
    }
private:
    void loadFromFile(LocalObjectCreator& local_object_creator, Buffer& buffer, const string& filename, uint32_t size){
        //load all numbers from provided file
        vector<uint32_t> data;
        data.reserve(size);
        ifstream data_file(filename);
        uint32_t a;
        for (int i = 0; i < size; i++){
            data_file >> a;
            data.push_back(a);
        }
        //copy loaded numbers to buffer on the GPU
        local_object_creator.copyToLocal(data, buffer);
    }
};



#endif