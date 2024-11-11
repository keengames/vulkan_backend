#ifndef KEEN_VULKAN_GPU_ALLOCATOR_HPP_INCLUDED
#define KEEN_VULKAN_GPU_ALLOCATOR_HPP_INCLUDED

#include "vulkan_api.hpp"

#if !defined( KEEN_TRACK_VULKAN_ALLOCATIONS )
#   if KEEN_USING( KEEN_MEMORY_TRACKER )
#       define KEEN_TRACK_VULKAN_ALLOCATIONS    KEEN_ON
#   else
#       define KEEN_TRACK_VULKAN_ALLOCATIONS    KEEN_OFF
#   endif
#endif

namespace keen
{

    struct VulkanGpuAllocator;
    struct VulkanGpuAllocation;

    struct VulkanGpuAllocatorParameters
    {
        VulkanApi*                          pVulkan = nullptr;
        MemoryAllocator*                    pAllocator = nullptr;
        VkAllocationCallbacks*              pAllocCallbacks = nullptr;
        VkDevice                            device = VK_NULL_HANDLE;
        VkPhysicalDevice                    physicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties    memoryProperties{};
        VkInstance                          instance = VK_NULL_HANDLE;
        size_t                              blockSizeInBytes = 0u;
        bool                                enableDeviceAddressExtension = false;
    };

    enum class VulkanGpuMemoryUsage : uint8
    {
        Auto,
        Auto_PreferDevice,
        Auto_PreferHost,
    };

    enum class VulkanGpuMemoryFlag : uint8
    {
        PersistentlyMapped,
        HostSequentialWrite,
        HostRandomAccess,
        DedicatedDeviceMemory,
    };

    using VulkanGpuMemoryFlagMask = Bitmask32<VulkanGpuMemoryFlag>;

    struct VulkanGpuAllocationInfo
    {
        VulkanGpuAllocation*    pAllocation;
#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        uint32                  memoryTypeIndex;
#endif
    };

    struct VulkanGpuBufferResult
    {
        VkBuffer                buffer;
        VulkanGpuAllocationInfo allocationInfo;
        MemoryBlock             mappedMemory;
        size_t                  sizeInBytes;
        VkMemoryPropertyFlags   memoryFlags;
    };

    struct VulkanGpuImageResult
    {
        VkImage                 image;
        VulkanGpuAllocationInfo allocationInfo;
        uint32                  memoryTypeIndex;
        size_t                  sizeInBytes;
        VkMemoryPropertyFlags   memoryFlags;
    };

    struct VulkanGpuDeviceMemoryAllocation
    {
        VkDeviceMemory          memory;
#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        uint32                  memoryTypeIndex;
#endif
    };

    namespace vulkan
    {

        VulkanGpuAllocator*     createGpuAllocator( const VulkanGpuAllocatorParameters& parameters );
        void                    destroyGpuAllocator( MemoryAllocator* pAllocator, VulkanGpuAllocator* pGpuAllocator );

        void                    traceGpuAllocations( const VulkanGpuAllocator* pGpuAllocator );

        bool                    allocateGpuBuffer( VulkanGpuBufferResult* pResult, VulkanGpuAllocator* pGpuAllocator, VulkanGpuMemoryUsage memoryUsage, VulkanGpuMemoryFlagMask flags, uint32 minAlignment, const VkBufferCreateInfo& bufferCreateInfo, const DebugName& debugName );
        void                    freeGpuBuffer( VulkanGpuAllocator* pGpuAllocator, VkBuffer buffer, VulkanGpuAllocationInfo allocationInfo );

        void                    flushCpuMemoryCache( VulkanGpuAllocator* pGpuAllocator, ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes );
        void                    invalidateCpuMemoryCache( VulkanGpuAllocator* pGpuAllocator, ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes );

        bool                    allocateGpuImage( VulkanGpuImageResult* pResult, VulkanGpuAllocator* pGpuAllocator, VulkanGpuMemoryUsage memoryUsage, VulkanGpuMemoryFlagMask flags, const VkImageCreateInfo& imageCreateInfo, const DebugName& debugName );
        void                    freeGpuImage( VulkanGpuAllocator* pGpuAllocator, VkImage image, VulkanGpuAllocationInfo allocationInfo );

        struct AllocateGpuDeviceMemoryParameters
        {
            uint64                                  sizeInBytes;
            uint32                                  memoryTypeIndex;
            Optional<GraphicsDeviceMemoryPriority>  priority;
            Optional<VkBuffer>                      dedicatedBuffer;
            Optional<VkImage>                       dedicatedImage;
            DebugName                               debugName;
        };
        Result<VulkanGpuDeviceMemoryAllocation> allocateGpuDeviceMemory( VulkanGpuAllocator* pGpuAllocator, const AllocateGpuDeviceMemoryParameters& parameters );
        void                                    freeGpuDeviceMemory( VulkanGpuAllocator* pGpuAllocator, VulkanGpuDeviceMemoryAllocation allocation );

        void*                   mapGpuMemory( VulkanGpuAllocator* pGpuAllocator, VulkanGpuAllocation* pAllocation );
        void                    unmapGpuMemory( VulkanGpuAllocator* pGpuAllocator, VulkanGpuAllocation* pAllocation );

    }

}

#endif
