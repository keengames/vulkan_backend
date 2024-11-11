#ifndef KEEN_VULKAN_TYPES_HPP_INCLUDED
#define KEEN_VULKAN_TYPES_HPP_INCLUDED

#include "keen/base/pixel_format.hpp"
#include "keen/base/zone_allocator.hpp"
#include "keen/base/bit_array_count.hpp"
#include "vulkan_api.hpp"
#include "vulkan_gpu_allocator.hpp"
#include "vulkan_breadcrumbs.hpp"
#include "global/graphics_system_private.hpp"

namespace keen
{

    struct VulkanFrame;
    class VulkanSwapChain;

    struct VulkanPipelineLayout : public GraphicsPipelineLayout
    {
        VkPipelineLayout        pipelineLayout;
        bool                    useBindlessDescriptors;
    };

    struct VulkanRenderPipeline : public GraphicsRenderPipeline
    {
        VkPipeline              pipeline;
        bool8                   scissorTestEnabled;
    };

    struct VulkanSwapChainWrapper : public GraphicsSwapChain
    {
        VulkanSwapChain*        pSwapChain;
    };

    struct VulkanComputePipeline : public GraphicsComputePipeline
    {
        VkPipeline              pipeline;
    };

    struct VulkanDeviceMemory : public GraphicsDeviceMemory
    {
        VulkanGpuDeviceMemoryAllocation allocation;
        bool                            isCoherent;
    };

    struct VulkanTexture : public GraphicsTexture
    {
        VkImage                 image;
        VkImageView             imageView;
        VulkanGpuAllocationInfo allocation;
    };

    struct VulkanBuffer : public GraphicsBuffer
    {
        VkBuffer                    buffer;
        VulkanGpuAllocationInfo     allocation;
        uint64                      boundMemoryOffset;
        const VulkanDeviceMemory*   pBoundDeviceMemory;
        VkDeviceAddress             deviceAddress;  // only valid with the correct usage flags
    };

    struct VulkanSampler : public GraphicsSampler
    {
        VkSampler                   sampler;
    };

    struct VulkanDescriptorSetLayout : public GraphicsDescriptorSetLayout
    {
        VkDescriptorSetLayout       layout;
        Array<VulkanSampler*>       staticSamplers;
    };

    struct VulkanGpuProfileEvent
    {
    };

    struct VulkanDescriptorPool;

    struct VulkanDescriptorSet : public GraphicsDescriptorSet
    {
        VkDescriptorSet             set;
    };

    struct StaticVulkanDescriptorSet : public VulkanDescriptorSet
    {
        VulkanDescriptorPool*       pPool;
    };

    struct VulkanQueryPool : public GraphicsQueryPool
    {
        VkQueryPool                 queryPool;
    };

    enum class VulkanDescriptorPoolType
    {
        Dynamic,
        Static
    };

    struct VulkanDescriptorPool
    {
        VulkanDescriptorPool*               pNext;
        VkDescriptorPool                    pool;
        VulkanDescriptorPoolType            type;

        ZoneAllocator                       setAllocator;
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
        DynamicArray<VulkanDescriptorSet*>  dynamicDescriptorSets;
#endif
    };

    class VulkanRenderContext;

    struct VulkanCommandPool
    {
        VkCommandPool                       commandPool;
        DynamicArray<VkCommandBuffer>       commandBuffers;
        size_t                              allocatedCommandBufferCount;
    };

    struct VulkanUsedSwapChainInfo
    {
        static constexpr size_t MaxSwapChainCount = 64u;
        DynamicArray<VkSwapchainKHR,MaxSwapChainCount>          swapChains;
        DynamicArray<VkSemaphore,MaxSwapChainCount>             imageAvailableSemaphores;
        DynamicArray<VkPipelineStageFlags,MaxSwapChainCount>    waitStageMasks;
        DynamicArray<uint32,MaxSwapChainCount>                  imageIndices;
    };

    struct VulkanFrame : public GraphicsFrame
    {
        VkFence                             fence;
        bool                                isRunning;

        Array<VulkanCommandPool>            commandPools;           // one for each thread

        VkCommandBuffer                     mainCommandBuffer;
        VkDescriptorSet                     bindlessDescriptorSet;
        BitArrayCount                       bindlessTexturesDirtyMask;
        BitArrayCount                       bindlessSamplersDirtyMask;

        VkSemaphore                         renderingFinishedSemaphore;

        DynamicArray<VulkanSwapChain*,64u>  targetSwapChains;
        VulkanUsedSwapChainInfo             swapChainInfo;

        VulkanDescriptorPool*               pDescriptorPool;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        VulkanBreadcrumbBuffer*             pBreadcrumbBuffer;
#endif
    };

    struct VulkanTransferBatch : GraphicsTransferBatch
    {       
        VkCommandPool                       commandPool;
        VkCommandBuffer                     commandBuffer;
        VkFence                             fence;
    };

    struct VulkanTransferQueue
    {
        Array<VulkanTransferBatch>  batches;
        DynamicArray<uint32>        freeBatchIndices;       
        GraphicsTransferBatchId     nextId;
    };

    using VulkanGraphicsDeviceMemoryTypeInfos = StaticArray<GraphicsDeviceMemoryTypeInfo, VK_MAX_MEMORY_TYPES>;

    struct VulkanSharedData
    {
        VkAllocationCallbacks*                              pVulkanAllocationCallbacks;

        GraphicsDeviceInfo                                  info;
        VulkanGraphicsDeviceMemoryTypeInfos                 memoryTypeInfos;

        VkPhysicalDeviceProperties                          deviceProperties;
        VkPhysicalDeviceVulkan11Properties                  deviceProperties_1_1;
        VkPhysicalDeviceVulkan12Properties                  deviceProperties_1_2;
        VkPhysicalDeviceMemoryProperties                    deviceMemoryProperties;
        VkPhysicalDeviceFeatures                            deviceFeatures;
        VkPhysicalDeviceVulkan11Features                    deviceFeatures_1_1;
        VkPhysicalDeviceVulkan12Features                    deviceFeatures_1_2;

        Array<VkQueueFamilyProperties>                      queueFamilyProperties;
        uint32                                              graphicsQueueFamilyIndex;
        uint32                                              presentQueueFamilyIndex;
        uint32                                              computeQueueFamilyIndex;
        uint32                                              transferQueueFamilyIndex;
        VulkanQueueInfos                                    queueInfos;
        VkQueue                                             graphicsQueue;
        VkQueue                                             presentQueue;
        VkQueue                                             computeQueue;
        VkQueue                                             transferQueue;

        VkDescriptorSet                                     emptyDescriptorSet;

        bool                                                isVkGetQueryPoolResultsBrokenOnAmdDeviceLost;
    };
        
}

#endif 
