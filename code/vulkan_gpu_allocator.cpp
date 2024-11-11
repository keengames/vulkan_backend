#include "vulkan_gpu_allocator.hpp"

#include "keen/base/atomic.hpp"
#include "keen/base/profiler.hpp"
#include "keen/base/format_string.hpp"

#define KEEN_VMA_ALLOCATOR KEEN_ON

#if KEEN_USING( KEEN_VMA_ALLOCATOR )

// vk_mem_alloc.h uses snprintf but doesn't include stdio itself
#include <stdio.h>
#include "vk_mem_alloc.hpp"

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
#   include "keen/base/memory_tracker.hpp"
#endif


namespace keen
{
    struct VulkanGpuAllocator
    {
        Mutex                           vmaAllocatorMutex;
        MemoryAllocator*                pAllocator;
        VmaAllocator                    vmaAllocator;
        VmaVulkanFunctions              vmaVulkanFunctions;
        VmaDeviceMemoryCallbacks        vmaDeviceCallbacks;
        VkAllocationCallbacks*          pAllocationCallbacks;
        VulkanApi*                      pVulkan;
        VkDevice                        device;

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        static constexpr size_t MaxMemoryTypeCount = 32;
        using AllocatorHandleArray = DynamicArray<MemoryAllocatorHandle,32u>;
        using MemoryTypeHeapIndexArray = DynamicArray<uint32,32u>;
        AllocatorHandleArray            memoryTypeAllocators;
        MemoryTypeHeapIndexArray        memoryTypeHeapIndices;
        AllocatorHandleArray            heapAllocators;
#endif
    };

    static void vmaAllocateDeviceMemoryFunction( VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size );
    static void vmaFreeDeviceMemoryFunction( VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size );

    static void fillVmaAllocationCreateInfo( VmaAllocationCreateInfo* pVmaInfo, VulkanGpuMemoryUsage memoryUsage, VulkanGpuMemoryFlagMask flags );

    VulkanGpuAllocator* vulkan::createGpuAllocator( const VulkanGpuAllocatorParameters& parameters )
    {
        VulkanGpuAllocator* pGpuAllocator = newObjectZero<VulkanGpuAllocator>( parameters.pAllocator, "VulkanGpuAllocator"_debug );
        if( pGpuAllocator == nullptr )
        {
            return nullptr;
        }

        pGpuAllocator->pAllocator                                               = parameters.pAllocator;
        pGpuAllocator->vmaVulkanFunctions.vkGetPhysicalDeviceProperties         = parameters.pVulkan->vkGetPhysicalDeviceProperties;
        pGpuAllocator->vmaVulkanFunctions.vkGetPhysicalDeviceMemoryProperties   = parameters.pVulkan->vkGetPhysicalDeviceMemoryProperties;
        pGpuAllocator->vmaVulkanFunctions.vkAllocateMemory                      = parameters.pVulkan->vkAllocateMemory;
        pGpuAllocator->vmaVulkanFunctions.vkFreeMemory                          = parameters.pVulkan->vkFreeMemory;
        pGpuAllocator->vmaVulkanFunctions.vkMapMemory                           = parameters.pVulkan->vkMapMemory;
        pGpuAllocator->vmaVulkanFunctions.vkUnmapMemory                         = parameters.pVulkan->vkUnmapMemory;
        pGpuAllocator->vmaVulkanFunctions.vkFlushMappedMemoryRanges             = parameters.pVulkan->vkFlushMappedMemoryRanges;
        pGpuAllocator->vmaVulkanFunctions.vkInvalidateMappedMemoryRanges        = parameters.pVulkan->vkInvalidateMappedMemoryRanges;
        pGpuAllocator->vmaVulkanFunctions.vkBindBufferMemory                    = parameters.pVulkan->vkBindBufferMemory;
        pGpuAllocator->vmaVulkanFunctions.vkBindImageMemory                     = parameters.pVulkan->vkBindImageMemory;
        pGpuAllocator->vmaVulkanFunctions.vkGetBufferMemoryRequirements         = parameters.pVulkan->vkGetBufferMemoryRequirements;
        pGpuAllocator->vmaVulkanFunctions.vkGetImageMemoryRequirements          = parameters.pVulkan->vkGetImageMemoryRequirements;
        pGpuAllocator->vmaVulkanFunctions.vkCreateBuffer                        = parameters.pVulkan->vkCreateBuffer;
        pGpuAllocator->vmaVulkanFunctions.vkDestroyBuffer                       = parameters.pVulkan->vkDestroyBuffer;
        pGpuAllocator->vmaVulkanFunctions.vkCreateImage                         = parameters.pVulkan->vkCreateImage;
        pGpuAllocator->vmaVulkanFunctions.vkDestroyImage                        = parameters.pVulkan->vkDestroyImage;
        pGpuAllocator->vmaVulkanFunctions.vkCmdCopyBuffer                       = parameters.pVulkan->vkCmdCopyBuffer;
        pGpuAllocator->vmaVulkanFunctions.vkGetBufferMemoryRequirements         = parameters.pVulkan->vkGetBufferMemoryRequirements;
        //pGpuAllocator->vmaVulkanFunctions.vkBindImageMemory2KHR                   = parameters.pVulkan->vkBindImageMemory2KHR;
        //pGpuAllocator->vmaVulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = parameters.pVulkan->vkGetPhysicalDeviceMemoryProperties2KHR;

        pGpuAllocator->vmaDeviceCallbacks.pfnAllocate   = (PFN_vmaAllocateDeviceMemoryFunction)(void*)vmaAllocateDeviceMemoryFunction;
        pGpuAllocator->vmaDeviceCallbacks.pfnFree       = (PFN_vmaFreeDeviceMemoryFunction)(void*)vmaFreeDeviceMemoryFunction;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice                = parameters.physicalDevice;
        allocatorInfo.device                        = parameters.device;
        allocatorInfo.instance                      = parameters.instance;
        allocatorInfo.pAllocationCallbacks          = parameters.pAllocCallbacks;
        allocatorInfo.preferredLargeHeapBlockSize   = parameters.blockSizeInBytes;
        allocatorInfo.pVulkanFunctions              = &pGpuAllocator->vmaVulkanFunctions;
        allocatorInfo.flags                         |= VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;

        if( parameters.enableDeviceAddressExtension )
        {
            allocatorInfo.flags                     |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        }
        allocatorInfo.pDeviceMemoryCallbacks        = &pGpuAllocator->vmaDeviceCallbacks;

        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        VulkanResult vmaResult = vmaCreateAllocator( &allocatorInfo, &pGpuAllocator->vmaAllocator );
        if( vmaResult.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create vulkan memory allocator! error=%s\n", vmaResult );
            destroyGpuAllocator( parameters.pAllocator, pGpuAllocator );
            return nullptr;
        }

        {
            char* pStatsString;
            vmaBuildStatsString( pGpuAllocator->vmaAllocator, &pStatsString, VK_FALSE );
            if( pStatsString != nullptr )
            {
                KEEN_TRACE_INFO( "Vulkan Memory info:\n%s\n", pStatsString );

                vmaFreeStatsString( pGpuAllocator->vmaAllocator, pStatsString );
            }
        }

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        {
            for( uint32 heapIndex = 0u; heapIndex < parameters.memoryProperties.memoryHeapCount; ++heapIndex )
            {
                const VkMemoryHeap memoryHeap = parameters.memoryProperties.memoryHeaps[ heapIndex ];
                                
                DynamicArray<StringView,16u> heapProperties;
                if( isBitmaskSet( memoryHeap.flags, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) )
                {
                    heapProperties.pushBack( "DeviceLocal"_s );
                }
                if( isBitmaskSet( memoryHeap.flags, VK_MEMORY_HEAP_MULTI_INSTANCE_BIT ) )
                {
                    heapProperties.pushBack( "MultiInstance"_s );
                }

                const DebugName debugName = DebugName::createFormatted( "VulkanHeap#%02d(%k)", heapIndex, joinArguments<StringView>( heapProperties, ", "_s ) );
                const MemoryAllocatorHandle allocatorHandle = debug::registerAllocator( debugName );
                pGpuAllocator->heapAllocators.pushBack( allocatorHandle );
            }
            for( uint32 memoryTypeIndex = 0u; memoryTypeIndex < parameters.memoryProperties.memoryTypeCount; ++memoryTypeIndex )
            {
                const VkMemoryType memoryType = parameters.memoryProperties.memoryTypes[ memoryTypeIndex ];

                DynamicArray<StringView,16u> memoryProperties;
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) )
                {
                    memoryProperties.pushBack( "DeviceLocal"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) )
                {
                    memoryProperties.pushBack( "HostVisible"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) )
                {
                    memoryProperties.pushBack( "HostCoherent"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) )
                {
                    memoryProperties.pushBack( "HostCached"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ) )
                {
                    memoryProperties.pushBack( "LazilyAllocated"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_PROTECTED_BIT ) )
                {
                    memoryProperties.pushBack( "Protected"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD ) )
                {
                    memoryProperties.pushBack( "DeviceCoherentAMD"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD ) )
                {
                    memoryProperties.pushBack( "DeviceUncachedAMD"_s );
                }
                if( isBitmaskSet( memoryType.propertyFlags, VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV ) )
                {
                    memoryProperties.pushBack( "RdmaCapableNV"_s );
                }

                const DebugName debugName = DebugName::createFormatted( "VulkanType#%02d(%k)", memoryTypeIndex, joinArguments<StringView>( memoryProperties, ", "_s ) );
                const MemoryAllocatorHandle allocatorHandle = debug::registerAllocator( debugName );
                pGpuAllocator->memoryTypeAllocators.pushBack( allocatorHandle );
                pGpuAllocator->memoryTypeHeapIndices.pushBack( memoryType.heapIndex );
            }
        }
#endif

        pGpuAllocator->vmaAllocatorMutex.create( "VmaAllocatorMutex"_debug );
        pGpuAllocator->pAllocationCallbacks = parameters.pAllocCallbacks;
        pGpuAllocator->pVulkan              = parameters.pVulkan;
        pGpuAllocator->device               = parameters.device;

        return pGpuAllocator;
    }

    void vulkan::destroyGpuAllocator( MemoryAllocator* pAllocator, VulkanGpuAllocator* pGpuAllocator )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );
        if( pGpuAllocator->vmaAllocator != nullptr )
        {
#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
            for( size_t i = 0u; i < pGpuAllocator->memoryTypeAllocators.getCount(); ++i )
            {
                debug::traceMemoryReport( 0u, pGpuAllocator->memoryTypeAllocators[ i ] );
                debug::unregisterAllocator( pGpuAllocator->memoryTypeAllocators[ i ] );
            }           
#endif

            MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
            vmaDestroyAllocator( pGpuAllocator->vmaAllocator );
            pGpuAllocator->vmaAllocator = nullptr;
        }

        pGpuAllocator->vmaAllocatorMutex.destroy();
        deleteObject( pAllocator, pGpuAllocator );
    }

    void vulkan::traceGpuAllocations( const VulkanGpuAllocator* pGpuAllocator )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );
#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
#   if 1
        for( size_t i = 0u; i < pGpuAllocator->memoryTypeAllocators.getCount(); ++i )
        {
            debug::traceMemoryReport( 0u, pGpuAllocator->memoryTypeAllocators[ i ] );
        }
#   else
        TlsStackDynamicArray<MemoryTrackerAllocationInfo> allocations( 4_kib );
        debug::fillAllocationInfos( &allocations, pGpuAllocator->vmaAllocatorHandle );
        for( size_t i = 0u; i < allocations.getSize(); ++i )
        {
            const MemoryTrackerAllocationInfo& allocationInfo = allocations[ i ];
            KEEN_TRACE_INFO( "\"%s\",%d\n", allocationInfo.name, allocationInfo.size );
        }
#   endif
#else
        KEEN_UNUSED1( pGpuAllocator );
#endif
    }

    bool vulkan::allocateGpuBuffer( VulkanGpuBufferResult* pResult, VulkanGpuAllocator* pGpuAllocator, VulkanGpuMemoryUsage memoryUsage, VulkanGpuMemoryFlagMask flags, uint32 minAlignment, const VkBufferCreateInfo& bufferCreateInfo, const DebugName& debugName )
    {
        KEEN_PROFILE_CPU( vk_allocateGpuBuffer );

        KEEN_ASSERT( pResult != nullptr );      
        zeroValue( pResult );

        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );

        VmaAllocationCreateInfo vmaAllocCreateInfo = {};
        fillVmaAllocationCreateInfo( &vmaAllocCreateInfo, memoryUsage, flags );

        VmaAllocationInfo allocationInfo;
        VulkanResult result = vmaCreateBufferWithAlignment( pGpuAllocator->vmaAllocator, &bufferCreateInfo, &vmaAllocCreateInfo, minAlignment, &pResult->buffer, (VmaAllocation*)&pResult->allocationInfo.pAllocation, &allocationInfo );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vmaCreateBufferWithAlignment failed with error '%s'\n", result );
            return false;
        }

        vmaGetMemoryTypeProperties( pGpuAllocator->vmaAllocator, allocationInfo.memoryType, &pResult->memoryFlags );

        if( vmaAllocCreateInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT )
        {
            KEEN_ASSERT( allocationInfo.pMappedData != nullptr ); // this should never fail because all allocations that have this flag require a HOST_VISIBLE memory type
            pResult->mappedMemory = createMemoryBlock( allocationInfo.pMappedData, rangecheck_cast<size_t>( bufferCreateInfo.size ) );
            KEEN_ASSERT( isPointerAligned( pResult->mappedMemory.pStart, minAlignment ) );
        }
        else
        {
            pResult->mappedMemory = InvalidMemoryBlock;
        }
        pResult->sizeInBytes = rangecheck_cast<size_t>( allocationInfo.size );

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        pResult->allocationInfo.memoryTypeIndex = allocationInfo.memoryType;
        debug::insertAllocation( pGpuAllocator->memoryTypeAllocators[ allocationInfo.memoryType ], pResult->allocationInfo.pAllocation, rangecheck_cast<size_t>( allocationInfo.size ), {}, debugName, 0 );

        const uint32 heapIndex = pGpuAllocator->memoryTypeHeapIndices[ allocationInfo.memoryType ];
        debug::insertAllocation( pGpuAllocator->heapAllocators[ heapIndex ], pResult->allocationInfo.pAllocation, rangecheck_cast<size_t>( allocationInfo.size ), {}, debugName, 0 );
#else
        KEEN_UNUSED1( debugName );
#endif

        return true;
    }

    void vulkan::freeGpuBuffer( VulkanGpuAllocator* pGpuAllocator, VkBuffer buffer, VulkanGpuAllocationInfo allocationInfo )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
        vmaDestroyBuffer( pGpuAllocator->vmaAllocator, buffer, (VmaAllocation)allocationInfo.pAllocation );

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        debug::eraseAllocation( pGpuAllocator->memoryTypeAllocators[ allocationInfo.memoryTypeIndex ], allocationInfo.pAllocation, {} );

        const uint32 heapIndex = pGpuAllocator->memoryTypeHeapIndices[ allocationInfo.memoryTypeIndex ];
        debug::eraseAllocation( pGpuAllocator->heapAllocators[ heapIndex ], allocationInfo.pAllocation, {} );
#endif
    }

    bool vulkan::allocateGpuImage( VulkanGpuImageResult* pResult, VulkanGpuAllocator* pGpuAllocator, VulkanGpuMemoryUsage memoryUsage, VulkanGpuMemoryFlagMask flags,const VkImageCreateInfo& imageCreateInfo, const DebugName& debugName )
    {
        KEEN_ASSERT( !debugName.isEmpty() );

        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );

        VmaAllocationCreateInfo vmaAllocCreateInfo = {};
        fillVmaAllocationCreateInfo( &vmaAllocCreateInfo, memoryUsage, flags );

        VmaAllocationInfo allocationInfo;
        VulkanResult result = vmaCreateImage( pGpuAllocator->vmaAllocator, &imageCreateInfo, &vmaAllocCreateInfo, &pResult->image, (VmaAllocation*)&pResult->allocationInfo.pAllocation, &allocationInfo );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vmaCreateImage failed with error '%s'\n", result );
            return false;
        }

        vmaGetMemoryTypeProperties( pGpuAllocator->vmaAllocator, allocationInfo.memoryType, &pResult->memoryFlags );

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        pResult->allocationInfo.memoryTypeIndex = allocationInfo.memoryType;
        debug::insertAllocation( pGpuAllocator->memoryTypeAllocators[ allocationInfo.memoryType ], pResult->allocationInfo.pAllocation, rangecheck_cast<size_t>( allocationInfo.size ), {}, debugName, 0 );

        const uint32 heapIndex = pGpuAllocator->memoryTypeHeapIndices[ allocationInfo.memoryType ];
        debug::insertAllocation( pGpuAllocator->heapAllocators[ heapIndex ], pResult->allocationInfo.pAllocation, rangecheck_cast<size_t>( allocationInfo.size ), {}, debugName, 0 );
#else
        KEEN_UNUSED1( debugName );
#endif

        pResult->sizeInBytes = rangecheck_cast<size_t>( allocationInfo.size );
        return true;
    }

    void vulkan::freeGpuImage( VulkanGpuAllocator* pGpuAllocator, VkImage image, VulkanGpuAllocationInfo allocationInfo )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
        vmaDestroyImage( pGpuAllocator->vmaAllocator, image, (VmaAllocation)allocationInfo.pAllocation );
#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        debug::eraseAllocation( pGpuAllocator->memoryTypeAllocators[ allocationInfo.memoryTypeIndex ], allocationInfo.pAllocation, {} );

        const uint32 heapIndex = pGpuAllocator->memoryTypeHeapIndices[ allocationInfo.memoryTypeIndex ];
        debug::eraseAllocation( pGpuAllocator->heapAllocators[ heapIndex ], allocationInfo.pAllocation, {} );
#endif
    }

    Result<VulkanGpuDeviceMemoryAllocation> vulkan::allocateGpuDeviceMemory( VulkanGpuAllocator* pGpuAllocator, const AllocateGpuDeviceMemoryParameters& parameters )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        KEEN_ASSERT( !( parameters.dedicatedBuffer.isSet() && parameters.dedicatedImage.isSet() ) );

        VkMemoryAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocateInfo.allocationSize     = parameters.sizeInBytes;
        allocateInfo.memoryTypeIndex    = parameters.memoryTypeIndex;

        const void** ppNext = &allocateInfo.pNext;

        VkMemoryPriorityAllocateInfoEXT memoryPriority{ VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT };
        if( parameters.priority.isSet() )
        {
            memoryPriority.priority = vulkan::getMemoryPriority( parameters.priority.get() );
            appendToStructChain( &ppNext, &memoryPriority );
        }

        VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
        if( parameters.dedicatedBuffer.isSet() )
        {
            dedicatedAllocateInfo.buffer = parameters.dedicatedBuffer.get();
            appendToStructChain( &ppNext, &dedicatedAllocateInfo );
        }
        if( parameters.dedicatedImage.isSet() )
        {
            dedicatedAllocateInfo.image = parameters.dedicatedImage.get();
            appendToStructChain( &ppNext, &dedicatedAllocateInfo );
        }

        VkDeviceMemory deviceMemory;
        VulkanResult result = pGpuAllocator->pVulkan->vkAllocateMemory( pGpuAllocator->device, &allocateInfo, pGpuAllocator->pAllocationCallbacks, &deviceMemory );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkAllocateMemory of %d bytes from memory type %d for \"%s\" failed with error '%s'\n", parameters.sizeInBytes, parameters.memoryTypeIndex, parameters.debugName, result );
            return result.getErrorId();
        }

        VulkanGpuDeviceMemoryAllocation allocationResult;
        allocationResult.memory = deviceMemory;

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        allocationResult.memoryTypeIndex = parameters.memoryTypeIndex;
        debug::insertAllocation( pGpuAllocator->memoryTypeAllocators[ parameters.memoryTypeIndex ], deviceMemory, rangecheck_cast<size_t>( parameters.sizeInBytes ), {}, parameters.debugName, 0 );

        const uint32 heapIndex = pGpuAllocator->memoryTypeHeapIndices[ parameters.memoryTypeIndex ];
        debug::insertAllocation( pGpuAllocator->heapAllocators[ heapIndex ], deviceMemory, rangecheck_cast<size_t>( parameters.sizeInBytes ), {}, parameters.debugName, 0 );
#endif
        return allocationResult;
    }

    void vulkan::freeGpuDeviceMemory( VulkanGpuAllocator* pGpuAllocator, VulkanGpuDeviceMemoryAllocation allocation )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        pGpuAllocator->pVulkan->vkFreeMemory( pGpuAllocator->device, allocation.memory, pGpuAllocator->pAllocationCallbacks );

#if KEEN_USING( KEEN_TRACK_VULKAN_ALLOCATIONS )
        debug::eraseAllocation( pGpuAllocator->memoryTypeAllocators[ allocation.memoryTypeIndex ], allocation.memory, {} );

        const uint32 heapIndex = pGpuAllocator->memoryTypeHeapIndices[ allocation.memoryTypeIndex ];
        debug::eraseAllocation( pGpuAllocator->heapAllocators[ heapIndex ], allocation.memory, {} );
#endif
    }

    void* vulkan::mapGpuMemory( VulkanGpuAllocator* pGpuAllocator, VulkanGpuAllocation* pAllocation )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
        void* pResult;
        VulkanResult result = vmaMapMemory( pGpuAllocator->vmaAllocator, (VmaAllocation)pAllocation, &pResult );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vmaMapMemory failed with error '%s'\n", result );
            return nullptr;
        }
        return pResult;
    }

    void vulkan::unmapGpuMemory( VulkanGpuAllocator* pGpuAllocator, VulkanGpuAllocation* pAllocation )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
        vmaUnmapMemory( pGpuAllocator->vmaAllocator, (VmaAllocation)pAllocation );
    }

    void vulkan::flushCpuMemoryCache( VulkanGpuAllocator* pGpuAllocator, ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        KEEN_ASSERT( allocations.getCount() == offsets.getCount() );
        KEEN_ASSERT( allocations.getCount() == sizes.getCount() );
        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
        vmaFlushAllocations( pGpuAllocator->vmaAllocator, (uint32)allocations.getCount(), (VmaAllocation*)allocations.getStart(), offsets.getStart(), sizes.getStart() );
    }
    
    void vulkan::invalidateCpuMemoryCache( VulkanGpuAllocator* pGpuAllocator, ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes )
    {
        TlsAllocatorScope allocatorScope( pGpuAllocator->pAllocator );

        KEEN_ASSERT( allocations.getCount() == offsets.getCount() );
        KEEN_ASSERT( allocations.getCount() == sizes.getCount() );
        MutexLock lock( &pGpuAllocator->vmaAllocatorMutex );
        vmaInvalidateAllocations( pGpuAllocator->vmaAllocator, (uint32)allocations.getCount(), (VmaAllocation*)allocations.getStart(), offsets.getStart(), sizes.getStart() );
    }

    static uint64_atomic s_memoryTypeAllocationSize[ 32u ];

    static void vmaAllocateDeviceMemoryFunction( VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size )
    {
        KEEN_UNUSED2( allocator, memory );
        uint64 totalSize = 0u;
        if( memoryType < KEEN_COUNTOF( s_memoryTypeAllocationSize ) )
        {
            totalSize = atomic::add_uint64_ordered( &s_memoryTypeAllocationSize[ memoryType ], (uint64)size );
        }
        //KEEN_TRACE_DEBUG( "[graphics] VMA allocated %,d KiBytes of memory of type %d (total:%d MiB)\n", size / 1024u, memoryType, totalSize / KEEN_MiB );
    }

    /// Callback function called before vkFreeMemory.
    static void vmaFreeDeviceMemoryFunction( VmaAllocator allocator, uint32_t memoryType, VkDeviceMemory memory, VkDeviceSize size )
    {
        KEEN_UNUSED4( allocator, memory, size, memoryType );
        //KEEN_TRACE_DEBUG( "[graphics] VMA freed %,d KiBytes of memory of type %d\n", size / 1024u, memoryType );
    }

    static void fillVmaAllocationCreateInfo( VmaAllocationCreateInfo* pVmaInfo, VulkanGpuMemoryUsage memoryUsage, VulkanGpuMemoryFlagMask flags )
    {
        switch( memoryUsage )
        {
        case VulkanGpuMemoryUsage::Auto:
            pVmaInfo->usage = VMA_MEMORY_USAGE_AUTO;            
            break;

        case VulkanGpuMemoryUsage::Auto_PreferDevice:
            pVmaInfo->usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;

        case VulkanGpuMemoryUsage::Auto_PreferHost:
            pVmaInfo->usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            break;

        default:
            KEEN_BREAK( "Invalid Vulkan GPU Memory type!\n" );
            return;
        }

        if( flags.isSet( VulkanGpuMemoryFlag::PersistentlyMapped ) )
        {
            pVmaInfo->flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        if( flags.isSet( VulkanGpuMemoryFlag::HostSequentialWrite ) )
        {
            pVmaInfo->flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }
        if( flags.isSet( VulkanGpuMemoryFlag::HostRandomAccess ) )
        {
            pVmaInfo->flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }
        if( flags.isSet( VulkanGpuMemoryFlag::DedicatedDeviceMemory ) )
        {
            pVmaInfo->flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }
    }

}


#define VMA_IMPLEMENTATION
KEEN_UNUSED_WARNING_BEGIN
#include "vk_mem_alloc.hpp"
KEEN_UNUSED_WARNING_END

#else


#endif


