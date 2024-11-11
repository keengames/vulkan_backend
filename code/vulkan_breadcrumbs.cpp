#include "vulkan_breadcrumbs.hpp"
#include "keen/base/write_stream.hpp"
#include "keen/base/inivariables.hpp"
#include "vulkan_api.hpp"

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )

namespace keen
{
    KEEN_DEFINE_BOOL_VARIABLE( s_forceManualBreadcrumbs, "breadcrumbs/forceManualBreadcrumbs", false, "" );
    KEEN_DEFINE_BOOL_VARIABLE( s_forceSyncManualBreadcrumbs, "breadcrumbs/forceSyncManualBreadcrumbs", true, "" );

    Result<uint32> findMarkerBufferMemoryTypeIndex( VulkanApi* pVulkan, uint32_t memTypeBits )
    {
        VkPhysicalDeviceMemoryProperties memoryProperties = {};
        pVulkan->vkGetPhysicalDeviceMemoryProperties( pVulkan->physicalDevice, &memoryProperties );
    
        const uint32 expectedFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for( uint32 i = 0; i < memoryProperties.memoryTypeCount; ++i )
        {
            const uint32 typeMask = 1u << i;
            if( isBitmaskSet( memTypeBits, typeMask ) && isBitmaskSet( memoryProperties.memoryTypes[ i ].propertyFlags, expectedFlags ) )
            {
                return i;
            }
        }

        return ErrorId_NotFound;
    }

    static void formatZoneId( Slice<char>* pTarget, const VulkanBreadcrumbBuffer* pBreadcrumbBuffer, uint32 zoneIndex )
    {
        const VulkanBreadcrumbZone& zone = pBreadcrumbBuffer->zones[ zoneIndex ];
        if( zone.parentZoneIndex.isSet() )
        {
            formatZoneId( pTarget, pBreadcrumbBuffer, zone.parentZoneIndex.get() );
            pTarget->pushBack( '/' );
        }
        pTarget->append( zone.name );
    }

    StaticStringView getVulkanBreadcrumbTypeString( VulkanBreadcrumbType type )
    {
        switch( type )
        {
        case VulkanBreadcrumbType::Dispatch:                    return "Dispatch"_s;
        case VulkanBreadcrumbType::DispatchIndirect:            return "DispatchIndirect"_s;
        case VulkanBreadcrumbType::Draw:                        return "Draw"_s;
        case VulkanBreadcrumbType::DrawIndirectCount:           return "DrawIndirectCount"_s;
        case VulkanBreadcrumbType::DrawIndirect:                return "DrawIndirect"_s;
        case VulkanBreadcrumbType::DrawIndexedIndirectCount:    return "DrawIndexedIndirectCount"_s;
        case VulkanBreadcrumbType::DrawIndexedIndirect:         return "DrawIndexedIndirect"_s;
        case VulkanBreadcrumbType::DrawIndexed:                 return "DrawIndexed"_s;
        case VulkanBreadcrumbType::FillBuffer:                  return "FillBuffer"_s;
        case VulkanBreadcrumbType::CopyBuffer:                  return "CopyBuffer"_s;
        }
        KEEN_BREAK( "invalid code path" );
        return "<invalid>"_s;
    }

    void formatToString( WriteStream* pStream, const FormatStringOptions& options, const VulkanBreadcrumbType& type )
    {
        KEEN_UNUSED1( options );
        pStream->writeString( getVulkanBreadcrumbTypeString( type ) );  
    }

    VulkanBreadcrumbBuffer* createVulkanBreadcrumbBuffer( MemoryAllocator* pAllocator, VulkanApi* pVulkan, VkDevice device )
    {
        VulkanBreadcrumbBuffer* pBreadcrumbBuffer = newObjectZero<VulkanBreadcrumbBuffer>( pAllocator, "VulkanBreadcrumbBuffer"_debug );
        if( pBreadcrumbBuffer == nullptr )
        {
            return nullptr;
        }

        const uint32 maxBreadcrumbCount = 65536u;

        pBreadcrumbBuffer->allocator.create( pAllocator, 1_mib );
        pBreadcrumbBuffer->zones.create( pAllocator, 128u );
        pBreadcrumbBuffer->zoneStack.create( pAllocator, 128u );
        pBreadcrumbBuffer->breadcrumbs.create( pAllocator, maxBreadcrumbCount, false );

        const bool useManualBreadcrumbs = ( !pVulkan->AMD_buffer_marker && !pVulkan->NV_device_diagnostic_checkpoints ) || s_forceManualBreadcrumbs;

        if( useManualBreadcrumbs || pVulkan->AMD_buffer_marker )
        {
            KEEN_TRACE_INFO( "[graphics] Using %s for vulkan gpu breadcrumbs\n", useManualBreadcrumbs ? "Manual breadcrumbs" : "AMD_buffer_marker" );
            const uint32 markerBufferSize = ( maxBreadcrumbCount * 2u ) * sizeof( uint32 );

            VkBufferCreateInfo bufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferCreateInfo.size   = markerBufferSize;
            bufferCreateInfo.usage  = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VulkanResult result = pVulkan->vkCreateBuffer( device, &bufferCreateInfo, nullptr, &pBreadcrumbBuffer->buffer );
            KEEN_ASSERT( result.isOk() );
            vulkan::setObjectName( pVulkan, device, (VkObjectHandle)pBreadcrumbBuffer->buffer, VK_OBJECT_TYPE_BUFFER, "BreadcrumbBuffer"_debug );

            VkMemoryRequirements memoryRequirements{};
            pVulkan->vkGetBufferMemoryRequirements( device, pBreadcrumbBuffer->buffer, &memoryRequirements );

            const Result<uint32> findMemoryTypeResult = findMarkerBufferMemoryTypeIndex( pVulkan, memoryRequirements.memoryTypeBits );
            KEEN_ASSERT( findMemoryTypeResult.isOk() );

            VkMemoryAllocateInfo allocationInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocationInfo.allocationSize   = memoryRequirements.size;
            allocationInfo.memoryTypeIndex  = findMemoryTypeResult.getValue();

            result = pVulkan->vkAllocateMemory( device, &allocationInfo, nullptr, &pBreadcrumbBuffer->bufferMemory );
            KEEN_ASSERT( result.isOk() );
            vulkan::setObjectName( pVulkan, device, (VkObjectHandle)pBreadcrumbBuffer->bufferMemory, VK_OBJECT_TYPE_DEVICE_MEMORY, "BreadcrumbBufferMemory"_debug );

            void* pMappedMemory = nullptr;
            result = pVulkan->vkMapMemory( device, pBreadcrumbBuffer->bufferMemory, 0, markerBufferSize, 0, &pMappedMemory );
            KEEN_ASSERT( result.isOk() );

            pBreadcrumbBuffer->mappedData = createArrayViewFromMemoryBlock<uint32>( MemoryBlock{ (uint8*)pMappedMemory, markerBufferSize } );

            KEEN_ASSERT( pBreadcrumbBuffer->mappedData.getCount() == ( pBreadcrumbBuffer->breadcrumbs.getCapacity() * 2u ) );

            result = pVulkan->vkBindBufferMemory( device, pBreadcrumbBuffer->buffer, pBreadcrumbBuffer->bufferMemory, 0 );
            KEEN_ASSERT( result.isOk() );

            pBreadcrumbBuffer->technique = useManualBreadcrumbs ? VulkanBreadcrumbTechnique::Manual : VulkanBreadcrumbTechnique::AmdMarker;
        }
        else if( pVulkan->NV_device_diagnostic_checkpoints )
        {
            KEEN_TRACE_INFO( "[graphics] Using NV_device_diagnostic_checkpoints for vulkan gpu breadcrumbs\n" );
            KEEN_ASSERT( pVulkan->vkCmdSetCheckpointNV != nullptr );
            pBreadcrumbBuffer->technique = VulkanBreadcrumbTechnique::NvCheckpoint;
        }
        else
        {
            KEEN_TRACE_WARNING( "[graphics] No support for AMD_buffer_marker or NV_device_diagnostic_checkpoints found - gpu breadcrumbs couldn't be enabled\n" );
            pBreadcrumbBuffer->technique = VulkanBreadcrumbTechnique::None;
        }

        return pBreadcrumbBuffer;
    }

    void destroyVulkanBreadcrumbBuffer( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, MemoryAllocator* pAllocator, VulkanApi* pVulkan, VkDevice device )
    {
        KEEN_ASSERT( pBreadcrumbBuffer != nullptr );

        if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::AmdMarker || pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::Manual )
        {
            if( pBreadcrumbBuffer->mappedData.isValid() )
            {
                pVulkan->vkUnmapMemory( device, pBreadcrumbBuffer->bufferMemory );
            }

            if( pBreadcrumbBuffer->bufferMemory != VK_NULL_HANDLE )
            {
                pVulkan->vkFreeMemory( device, pBreadcrumbBuffer->bufferMemory, nullptr );
                pBreadcrumbBuffer->bufferMemory = VK_NULL_HANDLE;
            }

            if( pBreadcrumbBuffer->buffer != VK_NULL_HANDLE )
            {
                pVulkan->vkDestroyBuffer( device, pBreadcrumbBuffer->buffer, nullptr );
                pBreadcrumbBuffer->buffer = VK_NULL_HANDLE;
            }
        }
         
        deleteObject( pAllocator, pBreadcrumbBuffer );
    }

    void beginBreadcrumbFrame( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, GraphicsFrameId frameId )
    {
        KEEN_ASSERT( pBreadcrumbBuffer != nullptr );

        pBreadcrumbBuffer->allocator.clear();
        pBreadcrumbBuffer->zones.clear();
        pBreadcrumbBuffer->breadcrumbs.clear();

        pBreadcrumbBuffer->frameId      = frameId;
        pBreadcrumbBuffer->markerToken  = (uint16)( frameId & 0xffffu );

        if( pBreadcrumbBuffer->mappedData.isValid() && pBreadcrumbBuffer->technique != VulkanBreadcrumbTechnique::Manual )
        {
            // determine the marker for this frame
            const uint32 markerValue = (uint32)pBreadcrumbBuffer->markerToken << 16u;
            fillMemoryUint32( pBreadcrumbBuffer->mappedData.getMemory(), markerValue );
        }

        // :JK: this barrier is necessary for validation layers
        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1u, &barrier, 0u, nullptr, 0u, nullptr );

        DynamicArray<char,256u> frameZoneName;
        formatString( &frameZoneName, "Frame#%d", frameId );
        pushBreadcrumbZone( pBreadcrumbBuffer, frameZoneName );
    }

    void endBreadcrumbFrame( VulkanBreadcrumbBuffer* pBreadcrumbBuffer )
    {
        popBreadcrumbZone( pBreadcrumbBuffer );
    }

    void traceBreadcrumbBufferState( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkQueue queue )
    {
        KEEN_ASSERT( pBreadcrumbBuffer != nullptr );
                
        if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::AmdMarker || pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::Manual )
        {
            for( uint32 breadcrumbIndex = 0u; breadcrumbIndex < pBreadcrumbBuffer->breadcrumbs.getCount(); ++breadcrumbIndex )
            {
                // check if this is done..
                const uint32 markerIndex      = breadcrumbIndex / 2;
                const uint32 markerValueStart = pBreadcrumbBuffer->mappedData[ markerIndex + 0 ];
                const uint32 markerValueEnd   = pBreadcrumbBuffer->mappedData[ markerIndex + 1 ];
                KEEN_ASSERT( ( markerValueStart >> 16u ) == pBreadcrumbBuffer->markerToken );
                KEEN_ASSERT( ( markerValueEnd >> 16u ) == pBreadcrumbBuffer->markerToken );

                const uint32 markerStateEnd   = markerValueEnd & 0xffffu;
                const uint32 markerStateStart = markerValueStart & 0xffffu;

                if( markerStateEnd == 1u )
                {
                    // ok..
                }
                else if( markerStateStart == 1u )
                {
                    // started but not finished:
                    const VulkanBreadcrumb& breadcrumb = pBreadcrumbBuffer->breadcrumbs[ breadcrumbIndex ];

                    DynamicArray<char,1024u> zoneId;
                    formatZoneId( &zoneId, pBreadcrumbBuffer, breadcrumb.zoneIndex );

                    // started but not finished
                    KEEN_TRACE_ERROR( "Breadcrumb %d: has started execution and didn't finish. Zone:%k Type:%k Info:%k\n",
                        breadcrumbIndex, zoneId, breadcrumb.type, breadcrumb.commandInfo );
                }
                else if( markerStateStart == 0u )
                {
                    // not started
                }
                else
                {
                    KEEN_BREAK( "Invalid value in breadcrumb buffer (index:%d)\n", breadcrumbIndex );
                }
            }
        }
        else if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::NvCheckpoint )
        {
            StaticArray<VkCheckpointDataNV, 64u> checkpointData;
            fill( &checkpointData, { VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV } );

            uint32 checkpointDataCount;
            pVulkan->vkGetQueueCheckpointDataNV( queue, &checkpointDataCount, nullptr );

            if( checkpointDataCount > 0u )
            {
                if( checkpointDataCount > checkpointData.getCapacity() )
                {
                    KEEN_TRACE_ERROR( "too many checkpoints\n" );
                    return;
                }

                pVulkan->vkGetQueueCheckpointDataNV( queue, &checkpointDataCount, checkpointData.getStart() );

                KEEN_TRACE_ERROR( "NV_device_diagnostic_checkpoints data:\n" );
                uint32_t lastFailedBreadcrumbIndex = 0;
                for( uint32 checkpointDataIndex = 0u; checkpointDataIndex < checkpointDataCount; ++checkpointDataIndex )
                {
                    const VkCheckpointDataNV& checkpoint = checkpointData[ checkpointDataIndex ];

                    const uint32 breadcrumbIndex = (uint32)( copyMemoryCast<uint64>( checkpoint.pCheckpointMarker ) );
                    lastFailedBreadcrumbIndex = max( breadcrumbIndex, lastFailedBreadcrumbIndex );
                }

                for( uint32_t breadcrumbIndex = 0; breadcrumbIndex <= lastFailedBreadcrumbIndex; ++breadcrumbIndex )
                {
                    const VulkanBreadcrumb&     breadcrumb = pBreadcrumbBuffer->breadcrumbs[ breadcrumbIndex ];

                    DynamicArray< char, 1024u > zoneId;
                    formatZoneId( &zoneId, pBreadcrumbBuffer, breadcrumb.zoneIndex );

                    bool foundBreadcrumbReport = false;
                    for( uint32 checkpointDataIndex = 0u; checkpointDataIndex < checkpointDataCount; ++checkpointDataIndex )
                    {
                        const VkCheckpointDataNV& checkpoint = checkpointData[ checkpointDataIndex ];

                        const uint32              checkpointBreadcrumbIndex = (uint32)( copyMemoryCast< uint64 >( checkpoint.pCheckpointMarker ) );
                        if( checkpointBreadcrumbIndex == breadcrumbIndex )
                        {
                            foundBreadcrumbReport = true;
                            KEEN_TRACE_ERROR( "Breadcrumb %d: has started execution and didn't finish. Zone:%k Type:%k Info:%k Stage:%k\n", breadcrumbIndex, zoneId, breadcrumb.type, breadcrumb.commandInfo, vulkan::getVkPipelineStageFlagBitsString( checkpoint.stage ) );
                        }
                    }

                    if( !foundBreadcrumbReport )
                    {
                        KEEN_TRACE_ERROR( "Breadcrumb %d: has finished Execution. Zone:%k Type:%k Info:%k\n", breadcrumbIndex, zoneId, breadcrumb.type, breadcrumb.commandInfo );
                    }
                }
            }
        }
        else
        {
            KEEN_TRACE_ERROR( "[graphics] Vulkan breadcrumbs are disabled\n" );
        }
    }

    void pushBreadcrumbZone( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, StringView name )
    {
        if( pBreadcrumbBuffer == nullptr )
        {
            return;
        }

        // allocate the zone + copy the name
        VulkanBreadcrumbZone* pBreadcrumbZone = pBreadcrumbBuffer->zones.pushBack();

        if( pBreadcrumbBuffer->zoneStack.hasElements() )
        {
            pBreadcrumbZone->parentZoneIndex = pBreadcrumbBuffer->zoneStack.getLast();
        }
        pBreadcrumbZone->name = copyStringData( &pBreadcrumbBuffer->allocator, name );

        const uint32 zoneIndex = (uint32)pBreadcrumbBuffer->zones.getIndex( pBreadcrumbZone );

        pBreadcrumbBuffer->zoneStack.pushBack( zoneIndex );
    }

    void popBreadcrumbZone( VulkanBreadcrumbBuffer* pBreadcrumbBuffer )
    {
        if( pBreadcrumbBuffer == nullptr )
        {
            return;
        }
        KEEN_ASSERT( pBreadcrumbBuffer->zoneStack.hasElements() );
        pBreadcrumbBuffer->zoneStack.popBack();
    }

    void toggleBreadcrumbRecording( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, bool enable )
    {
        if( pBreadcrumbBuffer == nullptr )
        {
            return;
        }

        pBreadcrumbBuffer->recordingEnabled = enable;
    }

    void breadcrumbRenderpassHint( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, bool enterRenderPass )
    {
        if( pBreadcrumbBuffer == nullptr || pBreadcrumbBuffer->technique != VulkanBreadcrumbTechnique::Manual )
        {
            return;
        }

        pBreadcrumbBuffer->recordingEnabled = !enterRenderPass;
    }

    bool beginBreadcrumb( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, VulkanBreadcrumbType type, DebugName commandInfo )
    {
        if( pBreadcrumbBuffer == nullptr )
        {
            return false;
        }

        KEEN_ASSERT( pBreadcrumbBuffer->currentBreadcrumbIndex.isClear() );

        VulkanBreadcrumb* pBreadcrumb = pBreadcrumbBuffer->breadcrumbs.tryPushBackZero();
        if( pBreadcrumb == nullptr )
        {
            return false;
        }
        KEEN_ASSERT( pBreadcrumbBuffer->zones.hasElements() );

        if( !pBreadcrumbBuffer->recordingEnabled )
        {
            return false;
        }

        const uint32 zoneIndex = (uint32)pBreadcrumbBuffer->zones.getCount() - 1u;

        pBreadcrumb->zoneIndex      = zoneIndex;
        pBreadcrumb->type           = type;
        pBreadcrumb->commandInfo    = commandInfo;

        const uint32 breadcrumbIndex = (uint32)pBreadcrumbBuffer->breadcrumbs.getIndex( pBreadcrumb );

        pBreadcrumbBuffer->currentBreadcrumbIndex = breadcrumbIndex;

        // push new breadcrumb if still enough space + write the checkpoint / marker
        if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::AmdMarker )
        {
            const uint32 startValue = ( pBreadcrumbBuffer->markerToken << 16u ) | 1u;
            pVulkan->vkCmdWriteBufferMarkerAMD( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pBreadcrumbBuffer->buffer, ( breadcrumbIndex * 2u + 0u ) * sizeof( uint32 ), startValue );
        }
        else if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::Manual )
        {
            const uint32 startValue = ( pBreadcrumbBuffer->markerToken << 16u ) | 1u;
            if( s_forceSyncManualBreadcrumbs )
            {
                pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr );
            }
            pVulkan->vkCmdFillBuffer( commandBuffer, pBreadcrumbBuffer->buffer, ( breadcrumbIndex * 2u + 0u ) * sizeof( uint32 ), sizeof( uint32 ), startValue );
            if( s_forceSyncManualBreadcrumbs )
            {
                pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr );
            }
        }
        else if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::NvCheckpoint )
        {
            pVulkan->vkCmdSetCheckpointNV( commandBuffer, copyMemoryCast<const void*>( (uint64)breadcrumbIndex ) );
        }
        return true;
    }

    void endBreadcrumb( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer )
    {
        // write checkpoint / marker
        const uint32 breadcrumbIndex = pBreadcrumbBuffer->currentBreadcrumbIndex.get();

        // push new breadcrumb if still enough space + write the checkpoint / marker
        if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::AmdMarker )
        {
            const uint32 endValue = ( pBreadcrumbBuffer->markerToken << 16u ) | 1u;
            pVulkan->vkCmdWriteBufferMarkerAMD( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pBreadcrumbBuffer->buffer, ( breadcrumbIndex * 2u + 1u ) * sizeof( uint32 ), endValue );
        }
        else if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::Manual )
        {
            const uint32 endValue = ( pBreadcrumbBuffer->markerToken << 16u ) | 1u;
            if( s_forceSyncManualBreadcrumbs )
            {
                pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr );
            }
            pVulkan->vkCmdFillBuffer( commandBuffer, pBreadcrumbBuffer->buffer, ( breadcrumbIndex * 2u + 1u ) * sizeof( uint32 ), sizeof( uint32 ), endValue );
            if( s_forceSyncManualBreadcrumbs )
            {
                pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr );
            }
        }
        else if( pBreadcrumbBuffer->technique == VulkanBreadcrumbTechnique::NvCheckpoint )
        {
            // not required..
        }
        pBreadcrumbBuffer->currentBreadcrumbIndex.clear();
    }

}

#endif
