#ifndef KEEN_VULKAN_COMMAND_BUFFER_HPP_INCLUDED
#define KEEN_VULKAN_COMMAND_BUFFER_HPP_INCLUDED

#include "keen/base/small_dynamic_array.hpp"
#include "vulkan_types.hpp"

namespace keen
{
    struct VulkanBreadcrumbBuffer;

    struct VulkanReadCommandBufferState
    {
        const GraphicsCommandBuffer*        pCommandBuffer = nullptr;
        const GraphicsCommandBufferChunk*   pChunk = nullptr;
        const GraphicsCommand*              pCommand = nullptr;
        uint32                              commandIndex = 0u;
    };

    struct VulkanRecordCommandBufferParameters
    {
        GraphicsFrameId         frameId{};
        VulkanQueueInfos        queueInfos{};
        VulkanBreadcrumbBuffer* pBreadcrumbBuffer = nullptr;
        VkDescriptorSet         bindlessDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet         emptyDescriptorSet = VK_NULL_HANDLE;
    };

    struct VulkanRecordCommandBufferState
    {
        VulkanReadCommandBufferState        readState;

        uint                                oldFpuExceptionState = 0u;

        GraphicsFrameId                     frameId{};
        VulkanQueueInfos                    queueInfos{};
        VkDescriptorSet                     bindlessDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet                     emptyDescriptorSet = VK_NULL_HANDLE;

        GraphicsOptionalShaderStageMask     shaderStageMask;
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        VulkanBreadcrumbBuffer*             pBreadcrumbBuffer = nullptr;
#endif

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        const VulkanRenderPipeline*         pCurrentRenderPipeline = nullptr;
        const VulkanComputePipeline*        pCurrentComputePipeline = nullptr;
#endif
    };

    namespace vulkan
    {

        void                    beginCommandBufferReading( VulkanReadCommandBufferState* pState, const GraphicsCommandBuffer* pCommandBuffer );
        const GraphicsCommand*  readNextCommand( VulkanReadCommandBufferState* pState );

        void        recordCommandBuffer( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const GraphicsCommandBuffer* pCommandBuffer, const VulkanRecordCommandBufferParameters& parameters );

        void        beginCommandBufferRecording( VulkanRecordCommandBufferState* pState, const GraphicsCommandBuffer* pCommandBuffer, const VulkanRecordCommandBufferParameters& parameters );
        bool        recordNextCommand( VulkanRecordCommandBufferState* pState, VulkanApi* pVulkan, VkCommandBuffer commandBuffer );
        void        endCommandBufferRecording( VulkanRecordCommandBufferState* pState );

    }

}

#endif
