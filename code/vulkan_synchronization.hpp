#ifndef KEEN_VULKAN_SYNCHRONIZATION_HPP_INCLUDED
#define KEEN_VULKAN_SYNCHRONIZATION_HPP_INCLUDED

// inspired from https://github.com/Tobski/simple_vulkan_synchronization

#include "vulkan_api.hpp"

namespace keen
{

    struct VulkanMemoryBarrier
    {
        VkPipelineStageFlags    srcStageMask;
        VkPipelineStageFlags    dstStageMask;
        VkMemoryBarrier         barrier;
    };

    struct VulkanImageMemoryBarrier
    {
        VkPipelineStageFlags    srcStageMask;
        VkPipelineStageFlags    dstStageMask;
        VkImageMemoryBarrier    barrier;
    };

    namespace vulkan
    {

        VulkanMemoryBarrier         getVulkanMemoryBarrier( const GraphicsMemoryBarrier& barrier, GraphicsOptionalShaderStageMask optionalShaderStages );
        VulkanImageMemoryBarrier    getVulkanImageMemoryBarrier( const GraphicsTextureBarrier& barrier, GraphicsOptionalShaderStageMask optionalShaderStages );


    }

}

#endif
