#ifndef KEEN_VULKAN_RENDERING_HPP_INCLUDED
#define KEEN_VULKAN_RENDERING_HPP_INCLUDED

#include "keen/base/array.hpp"
#include "keen/base/semaphore.hpp"
#include "keen/os/thread.hpp"
#include "vulkan_types.hpp"

namespace keen
{
    struct TaskSystem;
    struct TaskQueue;
    struct TaskExecutionParameters;
    class VulkanSwapChain;

    struct VulkanRenderContextParameters
    {
        MemoryAllocator*        pAllocator = nullptr;

        VulkanApi*              pVulkan = nullptr;
        VkDevice                device = nullptr;
        VkPhysicalDevice        physicalDevice = nullptr;

        VulkanGraphicsObjects*  pObjects = nullptr;
        VulkanSharedData*       pSharedData = nullptr;

        bool                    isNonInteractiveApplication = false;
        bool                    enableBreadcrumbs = false;

        uint32                  frameCount = 2u;

        bool                    enableBindlessDescriptors = false;
        uint32                  bindlessTextureCount = 0u;
        uint32                  bindlessSamplerCount = 0u;
    };

    struct VulkanUsedSwapChainInfo;

    class VulkanRenderContext
    {
    public:
        bool                                    tryCreate( const VulkanRenderContextParameters& parameters );
        void                                    destroy();

        VulkanFrame*                            beginFrame( ArrayView<GraphicsSwapChain*> swapChains );
        void                                    submitFrame( VulkanFrame* pFrame, const GraphicsBindlessDescriptorSet& bindlessDescriptorSet );

        void                                    waitForAllFramesFinished();

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
        void                                    executeValidationLayerDetectionCode( VulkanBuffer* pBuffer );
#endif

        void                                    traceFrameBreadcrumbs();
        void                                    traceDeviceLossReason();
        bool                                    handleDeviceLost( VulkanResult result );

    private:
        MemoryAllocator*                        m_pAllocator;
        VulkanApi*                              m_pVulkan;
        VkDevice                                m_device;
        VkPhysicalDevice                        m_physicalDevice;

        VulkanGraphicsObjects*                  m_pObjects;
        VulkanSharedData*                       m_pSharedData;
        VkDescriptorPool                        m_bindlessDescriptorSetPool;

        Array<VulkanFrame>                      m_frames;
        uint32                                  m_currentFrameId;
        bool                                    m_isNonInteractiveApplication;

#if KEEN_USING( KEEN_PROFILER )
        uint32_atomic                           m_vulkanDescriptorSetCount;
#endif

        void                                    executeFrame( VulkanFrame* pFrame, const GraphicsBindlessDescriptorSet& bindlessDescriptorSet );
        void                                    waitForFrame( VulkanFrame* pFrame );
        void                                    prepareFrame( VulkanFrame* pFrame );

        void                                    recordStartOfFrameCommands( VulkanFrame* pFrame, VkCommandBuffer commandBuffer );
        void                                    recordEndOfFrameCommands( VulkanFrame* pFrame, VkCommandBuffer commandBuffer );
        void                                    recordAndSubmitCommands( VulkanFrame* pFrame );
#if !defined( KEEN_BUILD_MASTER )
        void                                    recordAndSubmitCommandsSplit( VulkanFrame* pFrame );
#endif

        enum class SubmitCommandBufferFlag
        {
            IsFirstCommandBuffer,
            IsLastCommandBuffer,
            WaitAfterSubmit,
        };
        using SubmitCommandBufferFlags = Bitmask32<SubmitCommandBufferFlag>;
        bool                                    submitCommandBuffer( VulkanFrame* pFrame, VkCommandBuffer commandBuffer, SubmitCommandBufferFlags flags, DebugName debugName );

        bool                                    resizeCommandPool( VulkanCommandPool* pCommandPool, size_t newSize );
        VkCommandBuffer                         allocateCommandBuffer( VulkanFrame* pFrame, size_t threadIndex );
    };
}

#endif 
