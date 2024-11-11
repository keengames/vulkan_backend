#include "vulkan_command_buffer.hpp"
#include "vulkan_types.hpp"
#include "vulkan_synchronization.hpp"
#include "keen/base/inivariables.hpp"
#include "keen/base/thread_local_storage.hpp"
#include "../global/graphics_command_buffer.hpp"
#include "vulkan_graphics_objects.hpp"

namespace keen
{

    namespace vulkan
    {

        static uint32 getVulkanQueueFamilyIndex( const VulkanQueueInfos& queueInfos, GraphicsQueueId queueId )
        {
            switch( queueId )
            {
            case GraphicsQueueId::Main:     return queueInfos.mainQueueFamilyIndex;
            case GraphicsQueueId::Transfer: return queueInfos.transferQueueFamilyIndex;
            }
            return 0u;
        }

        static void writeVulkanCommand( VulkanRecordCommandBufferState* pState, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const GraphicsCommand* pCommand );

    }

    void bindDescriptorSets( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, const GraphicsBindDescriptorSetsCommand* pBindDescriptorSetsCommand, VkDescriptorSet bindlessDescriptorSet, VkDescriptorSet emptyDescriptorSet )
    {
        const VulkanPipelineLayout* pPipelineLayout = (const VulkanPipelineLayout*)pBindDescriptorSetsCommand->pPipelineLayout;

        DynamicArray<VkDescriptorSet,GraphicsLimits_MaxDescriptorSetSlotCount + 1u> descriptorSets;
        for( size_t i = 0u; i < pBindDescriptorSetsCommand->descriptorSetCount; ++i )
        {
            const VulkanDescriptorSet* pDescriptorSet = (const VulkanDescriptorSet*)pBindDescriptorSetsCommand->descriptorSets[ i ];
            descriptorSets.pushBack( pDescriptorSet->set );
        }

        if( pPipelineLayout->useBindlessDescriptors )
        {
            while( descriptorSets.getCount() < GraphicsLimits_MaxDescriptorSetSlotCount )
            {
                descriptorSets.pushBack( emptyDescriptorSet );
            }

            descriptorSets.pushBack( bindlessDescriptorSet );
        }

        pVulkan->vkCmdBindDescriptorSets( commandBuffer, pipelineBindPoint, pPipelineLayout->pipelineLayout, 0u,
            descriptorSets.getCount32(), descriptorSets.getStart(), 0u, nullptr );
    }

    static void writePipelineBarrier( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const GraphicsPipelineBarrierCommand* pCommand, GraphicsOptionalShaderStageMask shaderStageMask )
    {
        VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        uint8* pCommandData = (uint8*)pCommand + alignUp( sizeof( GraphicsPipelineBarrierCommand ), sizeof( void* ) );

        const ArrayView<const GraphicsTexture*> commandTextures = createArrayView( pointer_cast<const GraphicsTexture*>( pCommandData ), pCommand->textureBarrierCount );
        pCommandData += commandTextures.getSizeInBytes();

        const ArrayView<const GraphicsTextureBarrierInfo> commandTextureBarrierInfos = createArrayView( pointer_cast<const GraphicsTextureBarrierInfo>( pCommandData ), pCommand->textureBarrierCount );
        pCommandData += commandTextureBarrierInfos.getSizeInBytes();

        const ArrayView<const GraphicsMemoryBarrier> commandGlobalBarrierInfos = createArrayView( pointer_cast<const GraphicsMemoryBarrier>( pCommandData ), pCommand->memoryBarrierCount );
        pCommandData += commandGlobalBarrierInfos.getSizeInBytes();

        KEEN_ASSERT( alignUp( (uintptr_t)pCommandData, sizeof( void* ) ) == ( (uintptr_t)pCommand ) + pCommand->sizeInBytes );

        DynamicArray<VkMemoryBarrier, GraphicsLimits_MaxTextureBarrierCount> globalMemoryBarriers;
        for( size_t globalBarrierIndex = 0u; globalBarrierIndex < commandGlobalBarrierInfos.getCount(); ++globalBarrierIndex )
        {
            const GraphicsMemoryBarrier& barrierInfo = commandGlobalBarrierInfos[ globalBarrierIndex ];

            GraphicsMemoryBarrier memoryBarrier;
            memoryBarrier.oldAccessMask = barrierInfo.oldAccessMask;
            memoryBarrier.newAccessMask = barrierInfo.newAccessMask;

            if( memoryBarrier.oldAccessMask.isAnySet() || memoryBarrier.newAccessMask.isAnySet() )
            {
                const VulkanMemoryBarrier vulkanBarrier = vulkan::getVulkanMemoryBarrier( memoryBarrier, shaderStageMask );

                srcStageMask |= vulkanBarrier.srcStageMask;
                dstStageMask |= vulkanBarrier.dstStageMask;

                globalMemoryBarriers.pushBack( vulkanBarrier.barrier );
            }
        }

        DynamicArray<VkImageMemoryBarrier,GraphicsLimits_MaxTextureBarrierCount> imageMemoryBarriers;
        for( size_t imageBarrierIndex = 0u; imageBarrierIndex < commandTextureBarrierInfos.getCount(); ++imageBarrierIndex )
        {
            const GraphicsTextureBarrierInfo& barrierInfo = commandTextureBarrierInfos[ imageBarrierIndex ];

            GraphicsTextureBarrier barrier{};
            barrier.oldAccessMask   = barrierInfo.oldAccessMask;
            barrier.newAccessMask   = barrierInfo.newAccessMask;
            barrier.oldLayout       = barrierInfo.oldLayout;
            barrier.newLayout       = barrierInfo.newLayout;
            barrier.pTexture        = commandTextures[ imageBarrierIndex ];

            barrier.subresourceRange.aspectMask         = barrierInfo.aspectMask;
            barrier.subresourceRange.arrayLayerCount    = barrierInfo.firstArrayLayer;
            barrier.subresourceRange.firstArrayLayer    = barrierInfo.firstArrayLayer;
            barrier.subresourceRange.arrayLayerCount    = barrierInfo.arrayLayerCount;
            barrier.subresourceRange.firstMipLevel      = barrierInfo.firstMipLevel;
            barrier.subresourceRange.mipLevelCount      = barrierInfo.mipLevelCount;

            const VulkanImageMemoryBarrier imageMemoryBarrier = vulkan::getVulkanImageMemoryBarrier( barrier, shaderStageMask );
            imageMemoryBarriers.pushBack( imageMemoryBarrier.barrier );

            srcStageMask |= imageMemoryBarrier.srcStageMask;
            dstStageMask |= imageMemoryBarrier.dstStageMask;
        }

        pVulkan->vkCmdPipelineBarrier( commandBuffer, srcStageMask, dstStageMask, 0, globalMemoryBarriers.getCount32(), globalMemoryBarriers.getStart(), 0u, nullptr, imageMemoryBarriers.getCount32(), imageMemoryBarriers.getStart() );
    }

    static void writeQueueOwnershipTransfer( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const GraphicsQueueOwnershipTransferCommand* pCommand, const VulkanQueueInfos& queueInfos, GraphicsOptionalShaderStageMask shaderStageMask )
    {
        VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        uint8* pCommandData = (uint8*)( pCommand + 1u );

        const ArrayView<const GraphicsTexture*> commandTextures = createArrayView( (const GraphicsTexture**)pCommandData, pCommand->imageBarrierCount );
        pCommandData += commandTextures.getSizeInBytes();

        const ArrayView<const GraphicsTextureBarrierInfo> commandTextureBarrierInfos = createArrayView( (const GraphicsTextureBarrierInfo*)pCommandData, pCommand->imageBarrierCount );
        pCommandData += commandTextureBarrierInfos.getSizeInBytes();

        const uint32 oldQueueFamilyIndex = vulkan::getVulkanQueueFamilyIndex( queueInfos, pCommand->oldQueueId );
        const uint32 newQueueFamilyIndex = vulkan::getVulkanQueueFamilyIndex( queueInfos, pCommand->newQueueId );

        DynamicArray<VkImageMemoryBarrier,GraphicsLimits_MaxTextureBarrierCount> imageMemoryBarriers;

        for( size_t imageBarrierIndex = 0u; imageBarrierIndex < commandTextureBarrierInfos.getCount(); ++imageBarrierIndex )
        {
            const GraphicsTextureBarrierInfo& barrierInfo = commandTextureBarrierInfos[ imageBarrierIndex ];

            GraphicsTextureBarrier barrier{};
            barrier.oldAccessMask   = barrierInfo.oldAccessMask;
            barrier.newAccessMask   = barrierInfo.newAccessMask;
            barrier.oldLayout       = barrierInfo.oldLayout;
            barrier.newLayout       = barrierInfo.newLayout;
            barrier.pTexture        = commandTextures[ imageBarrierIndex ];

            barrier.subresourceRange.aspectMask         = barrierInfo.aspectMask;
            barrier.subresourceRange.arrayLayerCount    = barrierInfo.firstArrayLayer;
            barrier.subresourceRange.firstArrayLayer    = barrierInfo.firstArrayLayer;
            barrier.subresourceRange.arrayLayerCount    = barrierInfo.arrayLayerCount;
            barrier.subresourceRange.firstMipLevel      = barrierInfo.firstMipLevel;
            barrier.subresourceRange.mipLevelCount      = barrierInfo.mipLevelCount;

            VulkanImageMemoryBarrier imageMemoryBarrier = vulkan::getVulkanImageMemoryBarrier( barrier, shaderStageMask );
            imageMemoryBarrier.barrier.srcQueueFamilyIndex = oldQueueFamilyIndex;
            imageMemoryBarrier.barrier.dstQueueFamilyIndex = newQueueFamilyIndex;

            imageMemoryBarriers.pushBack( imageMemoryBarrier.barrier );

            srcStageMask |= imageMemoryBarrier.srcStageMask;
            dstStageMask |= imageMemoryBarrier.dstStageMask;
        }

        pVulkan->vkCmdPipelineBarrier( commandBuffer, srcStageMask, dstStageMask, 0, 0u, nullptr, 0u, nullptr, imageMemoryBarriers.getCount32(), imageMemoryBarriers.getStart() );
    }

    void vulkan::beginCommandBufferReading( VulkanReadCommandBufferState* pState, const GraphicsCommandBuffer* pCommandBuffer )
    {
        *pState = { pCommandBuffer };

        if( pCommandBuffer->pFirstChunk->commandCount != 0u )
        {
            // first chunk
            pState->pChunk = pCommandBuffer->pFirstChunk;
        }
    }

    const GraphicsCommand* vulkan::readNextCommand( VulkanReadCommandBufferState* pState )
    {
        const GraphicsCommand* pResult = nullptr;

        if( pState->pCommand == nullptr && pState->pChunk != nullptr )
        {
            KEEN_ASSERT( pState->pChunk->commandCount != 0u );

            // first command of current chunk
            pState->pCommand        = pointer_cast<const GraphicsCommand>( ( (uint8*)pState->pChunk ) + sizeof( GraphicsCommandBufferChunk ) );
            pState->commandIndex    = 0u;
        }

        pResult = pState->pCommand;

        if( pState->pCommand != nullptr )
        {
            KEEN_ASSERT( pState->commandIndex < pState->pChunk->commandCount );

            pState->commandIndex += 1u;

            if( pState->commandIndex < pState->pChunk->commandCount )
            {
                // next command
                pState->pCommand    = pointer_cast<const GraphicsCommand>( ( (uint8*)pState->pCommand ) + pState->pCommand->sizeInBytes );
            }
            else
            {
                // next chunk
                pState->pChunk      = pState->pChunk->pNextChunk;
                pState->pCommand    = nullptr;
            }
        }

        return pResult;
    }

    void vulkan::beginCommandBufferRecording( VulkanRecordCommandBufferState* pState, const GraphicsCommandBuffer* pCommandBuffer, const VulkanRecordCommandBufferParameters& parameters )
    {
        KEEN_ASSERT( pState != nullptr );
        KEEN_ASSERT( pCommandBuffer != nullptr );

        VulkanRecordCommandBufferState state{};
        beginCommandBufferReading( &state.readState, pCommandBuffer );
        state.oldFpuExceptionState  = pf::disableExceptions();
        state.frameId               = parameters.frameId;
        state.bindlessDescriptorSet = parameters.bindlessDescriptorSet;
        state.emptyDescriptorSet    = parameters.emptyDescriptorSet;
        state.queueInfos            = parameters.queueInfos;
        state.shaderStageMask       = graphics::getDeviceInfo( pCommandBuffer->pGraphicsSystem ).optionalShaderStages;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        state.pBreadcrumbBuffer = parameters.pBreadcrumbBuffer;
#endif

        *pState = state;
    }

    bool vulkan::recordNextCommand( VulkanRecordCommandBufferState* pState, VulkanApi* pVulkan, VkCommandBuffer commandBuffer )
    {
        const GraphicsCommand* pCommand = readNextCommand( &pState->readState );
        if( pCommand == nullptr )
        {
            return false;
        }

        writeVulkanCommand( pState, pVulkan, commandBuffer, pCommand );

        return true;
    }

    void vulkan::endCommandBufferRecording( VulkanRecordCommandBufferState* pState )
    {
        pf::restoreExceptionState( pState->oldFpuExceptionState );
    }

    void vulkan::recordCommandBuffer( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const GraphicsCommandBuffer* pCommandBuffer, const VulkanRecordCommandBufferParameters& parameters )
    {
        VulkanRecordCommandBufferState recordState;

        beginCommandBufferRecording( &recordState, pCommandBuffer, parameters );

        vulkan::beginDebugLabel( pVulkan, commandBuffer, pCommandBuffer->debugName );
        vulkan::insertCheckPoint( pVulkan, commandBuffer, pCommandBuffer->debugName.getCName() );

        while( recordNextCommand( &recordState, pVulkan, commandBuffer ) )
        {
            // continue
        }

        vulkan::endDebugLabel( pVulkan, commandBuffer );

        endCommandBufferRecording( &recordState );
    }

    static void vulkan::writeVulkanCommand( VulkanRecordCommandBufferState* pState, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const GraphicsCommand* pCommand )
    {
        switch( pCommand->id )
        {
        case GraphicsCommandId_SetViewport:
            {
                const GraphicsSetViewportCommand* pSetViewportCommand = (const GraphicsSetViewportCommand*)pCommand;

                VkViewport viewport;
                viewport.x          = (float32)pSetViewportCommand->viewport.x;
                viewport.y          = (float32)pSetViewportCommand->viewport.y;
                viewport.width      = (float32)pSetViewportCommand->viewport.width;
                viewport.height     = (float32)pSetViewportCommand->viewport.height;
                viewport.minDepth   = pSetViewportCommand->viewport.minDepth;
                viewport.maxDepth   = pSetViewportCommand->viewport.maxDepth;
                pVulkan->vkCmdSetViewport( commandBuffer, 0u, 1u, &viewport );
            }
            break;

        case GraphicsCommandId_SetScissorRectangle:
            {
                const GraphicsSetScissorRectangleCommand* pSetScissorRectangleCommand = (const GraphicsSetScissorRectangleCommand*)pCommand;

                VkRect2D scissorRect;
                scissorRect.offset.x = pSetScissorRectangleCommand->scissorRectangle.x;
                scissorRect.offset.y = pSetScissorRectangleCommand->scissorRectangle.y;
                scissorRect.extent.width = pSetScissorRectangleCommand->scissorRectangle.width;
                scissorRect.extent.height = pSetScissorRectangleCommand->scissorRectangle.height;
                pVulkan->vkCmdSetScissor( commandBuffer, 0u, 1u, &scissorRect );
            }
            break;

        case GraphicsCommandId_SetStencilReference:
            {
                const GraphicsSetStencilReferenceCommand* pSetStencilReferenceCommand = (const GraphicsSetStencilReferenceCommand*)pCommand;

                const VkStencilFaceFlags faceMask = vulkan::getStencilFaceFlags( pSetStencilReferenceCommand->faceMask );
                pVulkan->vkCmdSetStencilReference( commandBuffer, faceMask, pSetStencilReferenceCommand->reference );
            }
            break;

        case GraphicsCommandId_SetStencilWriteMask:
            {
                const GraphicsSetStencilWriteMaskCommand* pSetStencilWriteMaskCommand = (const GraphicsSetStencilWriteMaskCommand*)pCommand;

                const VkStencilFaceFlags faceMask = vulkan::getStencilFaceFlags( pSetStencilWriteMaskCommand->faceMask );
                pVulkan->vkCmdSetStencilWriteMask( commandBuffer, faceMask, pSetStencilWriteMaskCommand->writeMask );
            }
            break;

        case GraphicsCommandId_SetStencilCompareMask:
            {
                const GraphicsSetStencilCompareMaskCommand* pSetStencilCompareMaskCommand = (const GraphicsSetStencilCompareMaskCommand*)pCommand;

                const VkStencilFaceFlags faceMask = vulkan::getStencilFaceFlags( pSetStencilCompareMaskCommand->faceMask );
                pVulkan->vkCmdSetStencilCompareMask( commandBuffer, faceMask, pSetStencilCompareMaskCommand->compareMask );
            }
            break;

        case GraphicsCommandId_BindRenderPipeline:
            {
                const GraphicsBindRenderPipelineCommand* pBindRenderPipelineCommand = (const GraphicsBindRenderPipelineCommand*)pCommand;

                const VulkanRenderPipeline* pRenderPipeline = (const VulkanRenderPipeline*)pBindRenderPipelineCommand->pRenderPipeline;
                pVulkan->vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pRenderPipeline->pipeline );

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                pState->pCurrentRenderPipeline = pRenderPipeline;
#endif
            }
            break;

        case GraphicsCommandId_BindComputePipeline:
            {
                const GraphicsBindComputePipelineCommand* pBindComputePipelineCommand = (const GraphicsBindComputePipelineCommand*)pCommand;

                const VulkanComputePipeline* pComputePipeline = (const VulkanComputePipeline*)pBindComputePipelineCommand->pComputePipeline;
                pVulkan->vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pComputePipeline->pipeline );

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                pState->pCurrentComputePipeline = pComputePipeline;
#endif
            }
            break;

        case GraphicsCommandId_Dispatch:
            {
                const GraphicsDispatchCommand* pDispatchCommand = (const GraphicsDispatchCommand*)pCommand;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::Dispatch, pState->pCurrentComputePipeline->getDebugName() );
#endif

                pVulkan->vkCmdDispatch( commandBuffer, pDispatchCommand->groupCountX, pDispatchCommand->groupCountY, pDispatchCommand->groupCountZ );
            }
            break;

        case GraphicsCommandId_DispatchIndirect:
            {
                const GraphicsDispatchIndirectCommand* pDispatchCommand = (const GraphicsDispatchIndirectCommand*)pCommand;
                const VulkanBuffer* pParameterBuffer = (const VulkanBuffer*)pDispatchCommand->pParametersBuffer;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::DispatchIndirect, pState->pCurrentComputePipeline->getDebugName() );
#endif

                pVulkan->vkCmdDispatchIndirect( commandBuffer, pParameterBuffer->buffer, pDispatchCommand->parameterBufferOffset );
            }
            break;

        case GraphicsCommandId_FillBuffer:
            {
                const GraphicsFillBufferCommand* pFillBufferCommand = (const GraphicsFillBufferCommand*)pCommand;
                const VulkanBuffer* pBuffer = (const VulkanBuffer*)pFillBufferCommand->pBuffer;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::FillBuffer, pBuffer->getDebugName() );
#endif

                pVulkan->vkCmdFillBuffer( commandBuffer, pBuffer->buffer, pFillBufferCommand->offset, pFillBufferCommand->size, pFillBufferCommand->value );
            }
            break;

        case GraphicsCommandId_CopyBuffer:
            {
                const GraphicsCopyBufferCommand* pCopyBufferCommand = (const GraphicsCopyBufferCommand*)pCommand;
                const VulkanBuffer* pSourceBuffer = (const VulkanBuffer*)pCopyBufferCommand->pSourceBuffer;
                const VulkanBuffer* pTargetBuffer = (const VulkanBuffer*)pCopyBufferCommand->pTargetBuffer;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::CopyBuffer, pSourceBuffer->getDebugName() );
#endif

                VkBufferCopy region;
                region.srcOffset    = pCopyBufferCommand->sourceOffset;
                region.dstOffset    = pCopyBufferCommand->targetOffset;
                region.size         = pCopyBufferCommand->size;

                pVulkan->vkCmdCopyBuffer( commandBuffer, pSourceBuffer->buffer, pTargetBuffer->buffer, 1u, &region );
            }
            break;

        case GraphicsCommandId_CopyTexture:
            {
                const GraphicsCopyTextureCommand* pCopyTextureCommand = (const GraphicsCopyTextureCommand*)pCommand;
                const VulkanTexture* pSourceTexture = (const VulkanTexture*)pCopyTextureCommand->pSourceTexture;
                const VulkanTexture* pTargetTexture = (const VulkanTexture*)pCopyTextureCommand->pTargetTexture;
                const GraphicsTextureRegion& sourceRegion = pCopyTextureCommand->sourceRegion;
                const GraphicsTextureRegion& targetRegion = pCopyTextureCommand->targetRegion;
                const VkImageLayout targetLayout = vulkan::getImageLayout( pCopyTextureCommand->targetLayout );
                const VkImageLayout sourceLayout = vulkan::getImageLayout( pCopyTextureCommand->sourceLayout );

                VkImageCopy region;
                KEEN_ASSERT( sourceRegion.size == targetRegion.size );
                region.extent = vulkan::createExtent3d( sourceRegion.size );
                region.srcOffset = vulkan::createOffset3d( sourceRegion.offset );
                region.dstOffset = vulkan::createOffset3d( targetRegion.offset );
                vulkan::fillVkImageSubresourceLayers( &region.srcSubresource, sourceRegion );
                vulkan::fillVkImageSubresourceLayers( &region.dstSubresource, targetRegion );

                pVulkan->vkCmdCopyImage( commandBuffer, pSourceTexture->image, sourceLayout, pTargetTexture->image, targetLayout, 1, &region );
            }
            break;

        case GraphicsCommandId_CopyBufferToTexture:
            {
                const GraphicsCopyBufferToTextureCommand* pCopyCommand = (const GraphicsCopyBufferToTextureCommand*)pCommand;
                const VulkanBuffer* pSourceBuffer = (const VulkanBuffer*)pCopyCommand->pSourceBuffer;
                const VulkanTexture* pTargetTexture = (const VulkanTexture*)pCopyCommand->pTargetTexture;
                const VkImageLayout targetLayout = vulkan::getImageLayout( pCopyCommand->targetLayout );
                const GraphicsBufferTextureCopyRegion* pCopyRegions = (const GraphicsBufferTextureCopyRegion*)( (const uint8*)pCommand + sizeof( GraphicsCopyBufferToTextureCommand ) );
                const size_t copyRegionCount = pCopyCommand->copyRegionCount;

                constexpr size_t BatchSize = 16u;
                StaticArray<VkBufferImageCopy, BatchSize> vulkanCopyRegions;
                for( size_t batchIndex = 0u; batchIndex < alignUp( copyRegionCount, BatchSize ) / BatchSize; ++batchIndex )
                {
                    size_t batchRegionIndex;
                    for( batchRegionIndex = 0u; batchRegionIndex < BatchSize; ++batchRegionIndex )
                    {
                        const size_t regionIndex = batchIndex * BatchSize + batchRegionIndex;
                        if( regionIndex == copyRegionCount )
                        {
                            break;
                        }

                        const GraphicsBufferTextureCopyRegion& copyRegion = pCopyRegions[ regionIndex ];
                        VkBufferImageCopy* pVulkanCopyRegion = &vulkanCopyRegions[ batchRegionIndex ];
                        vulkan::fillVkBufferImageCopy( pVulkanCopyRegion, copyRegion );
                    }

                    KEEN_ASSERT( batchRegionIndex > 0u );
                    pVulkan->vkCmdCopyBufferToImage( commandBuffer, pSourceBuffer->buffer, pTargetTexture->image, targetLayout, rangecheck_cast<uint32>( batchRegionIndex ), vulkanCopyRegions.getStart() );
                }
            }
            break;

        case GraphicsCommandId_CopyTextureToBuffer:
            {
                const GraphicsCopyTextureToBufferCommand* pCopyCommand = (const GraphicsCopyTextureToBufferCommand*)pCommand;
                const VulkanTexture* pSourceTexture = (const VulkanTexture*)pCopyCommand->pSourceTexture;
                const VkImageLayout sourceImageLayout = vulkan::getImageLayout( pCopyCommand->sourceTextureLayout );
                const VulkanBuffer* pTargetBuffer = (const VulkanBuffer*)pCopyCommand->pTargetBuffer;
                const GraphicsBufferTextureCopyRegion* pCopyRegions = (const GraphicsBufferTextureCopyRegion*)( (const uint8*)pCommand + sizeof( GraphicsCopyTextureToBufferCommand ) );
                const size_t copyRegionCount = pCopyCommand->copyRegionCount;

                constexpr size_t BatchSize = 16u;
                StaticArray<VkBufferImageCopy, BatchSize> vulkanCopyRegions;
                for( size_t batchIndex = 0u; batchIndex < alignUp( copyRegionCount, BatchSize ) / BatchSize; ++batchIndex )
                {
                    size_t batchRegionIndex;
                    for( batchRegionIndex = 0u; batchRegionIndex < BatchSize; ++batchRegionIndex )
                    {
                        const size_t regionIndex = batchIndex * BatchSize + batchRegionIndex;
                        if( regionIndex == copyRegionCount )
                        {
                            break;
                        }

                        const GraphicsBufferTextureCopyRegion& copyRegion = pCopyRegions[ regionIndex ];
                        VkBufferImageCopy* pVulkanCopyRegion = &vulkanCopyRegions[ batchRegionIndex ];

                        vulkan::fillVkBufferImageCopy( pVulkanCopyRegion, copyRegion );
                    }

                    KEEN_ASSERT( batchRegionIndex > 0u );
                    pVulkan->vkCmdCopyImageToBuffer( commandBuffer, pSourceTexture->image, sourceImageLayout, pTargetBuffer->buffer, rangecheck_cast<uint32>( batchRegionIndex ), vulkanCopyRegions.getStart() );
                }
            }
            break;

        case GraphicsCommandId_ClearColorTexture:
            {
                const GraphicsClearColorTextureCommand* pClearCommand = (const GraphicsClearColorTextureCommand*)pCommand;
                const VulkanTexture* pTexture = (const VulkanTexture*)pClearCommand->pTexture;
                const VkImageLayout textureLayout = vulkan::getImageLayout( pClearCommand->textureLayout );
                const VkImageSubresourceRange range = vulkan::getImageSubresourceRange( pClearCommand->range );
                const VkClearColorValue clearColorValue = vulkan::getColorClearValue( pClearCommand->clearValue );

                pVulkan->vkCmdClearColorImage( commandBuffer, pTexture->image, textureLayout, &clearColorValue, 1u, &range );
            }
            break;

        case GraphicsCommandId_ClearDepthTexture:
            {
                const GraphicsClearDepthTextureCommand* pClearCommand = (const GraphicsClearDepthTextureCommand*)pCommand;
                const VulkanTexture* pTexture = (const VulkanTexture*)pClearCommand->pTexture;
                const VkImageLayout textureLayout = vulkan::getImageLayout( pClearCommand->textureLayout );
                const VkImageSubresourceRange range = vulkan::getImageSubresourceRange( pClearCommand->range );
                const VkClearDepthStencilValue clearDepthValue = vulkan::getDepthStencilClearValue( pClearCommand->clearValue, 0 );

                pVulkan->vkCmdClearDepthStencilImage( commandBuffer, pTexture->image, textureLayout, &clearDepthValue, 1u, &range );
            }
            break;

        case GraphicsCommandId_PipelineBarrier:
            {
                const GraphicsPipelineBarrierCommand* pPipelineBarrierCommand = (const GraphicsPipelineBarrierCommand*)pCommand;

                writePipelineBarrier( pVulkan, commandBuffer, pPipelineBarrierCommand, pState->shaderStageMask );
            }
            break;

        case GraphicsCommandId_QueueOwnershipTransfer:
            {
                const GraphicsQueueOwnershipTransferCommand* pQueueOwnershipTransferCommand = (const GraphicsQueueOwnershipTransferCommand*)pCommand;

                writeQueueOwnershipTransfer( pVulkan, commandBuffer, pQueueOwnershipTransferCommand, pState->queueInfos, pState->shaderStageMask );
            }
            break;

        case GraphicsCommandId_BindRenderDescriptorSets:
            {
                const GraphicsBindDescriptorSetsCommand* pBindDescriptorSetsCommand = (const GraphicsBindDescriptorSetsCommand*)pCommand;

                bindDescriptorSets( pVulkan, commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pBindDescriptorSetsCommand, pState->bindlessDescriptorSet, pState->emptyDescriptorSet );
            }
            break;

        case GraphicsCommandId_BindComputeDescriptorSets:
            {
                const GraphicsBindDescriptorSetsCommand* pBindDescriptorSetsCommand = (const GraphicsBindDescriptorSetsCommand*)pCommand;

                bindDescriptorSets( pVulkan, commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pBindDescriptorSetsCommand, pState->bindlessDescriptorSet, pState->emptyDescriptorSet );
            }
            break;

        case GraphicsCommandId_PushConstants:
            {
                // get stage mask + pipeline layout:
                const GraphicsPushConstantsCommand* pPushConstantsCommand = (const GraphicsPushConstantsCommand*)pCommand;
                const VulkanPipelineLayout* pPipelineLayout = (const VulkanPipelineLayout*)pPushConstantsCommand->pPipelineLayout;
                const VkShaderStageFlags stageMask = vulkan::getStageFlags( pPushConstantsCommand->stageMask );
                const uint8* pData = ( (const uint8*)pPushConstantsCommand ) + sizeof( GraphicsPushConstantsCommand );

                pVulkan->vkCmdPushConstants( commandBuffer, pPipelineLayout->pipelineLayout, stageMask, 0u, pPushConstantsCommand->dataSize, pData );
            }
            break;

        case GraphicsCommandId_BindVertexBuffer:
            {
                const GraphicsBindVertexBufferCommand* pBindVertexBufferCommand = (const GraphicsBindVertexBufferCommand*)pCommand;
                const VulkanBuffer* pVulkanBuffer = (const VulkanBuffer*)pBindVertexBufferCommand->vertexBuffer.pBuffer;
                pVulkan->vkCmdBindVertexBuffers( commandBuffer, 0u, 1u, &pVulkanBuffer->buffer, &pBindVertexBufferCommand->vertexBuffer.offset );
            }
            break;

        case GraphicsCommandId_BindIndexBuffer:
            {
                const GraphicsBindIndexBufferCommand* pBindIndexBufferCommand = (const GraphicsBindIndexBufferCommand*)pCommand;
                const VulkanBuffer* pVulkanBuffer = (const VulkanBuffer*)pBindIndexBufferCommand->indexBuffer.pBuffer;
                const VkIndexType vulkanIndexType = vulkan::getIndexType( pBindIndexBufferCommand->indexFormat );

                pVulkan->vkCmdBindIndexBuffer( commandBuffer, pVulkanBuffer->buffer, pBindIndexBufferCommand->indexBuffer.offset, vulkanIndexType );
            }
            break;

        case GraphicsCommandId_Draw:
            {
                const GraphicsDrawCommand* pDrawCommand = (const GraphicsDrawCommand*)pCommand;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::Draw, pState->pCurrentRenderPipeline->getDebugName() );
#endif

                pVulkan->vkCmdDraw( commandBuffer, pDrawCommand->vertexCount, pDrawCommand->instanceCount, pDrawCommand->vertexOffset, pDrawCommand->instanceOffset );
            }
            break;

        case GraphicsCommandId_DrawIndexed:
            {
                const GraphicsDrawIndexedCommand* pDrawCommand = (const GraphicsDrawIndexedCommand*)pCommand;

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::DrawIndexed, pState->pCurrentRenderPipeline->getDebugName() );
#endif

                pVulkan->vkCmdDrawIndexed( commandBuffer, pDrawCommand->indexCount, pDrawCommand->instanceCount, pDrawCommand->indexOffset, pDrawCommand->vertexOffset, pDrawCommand->instanceOffset );
            }
            break;

        case GraphicsCommandId_DrawIndirect:
            {
                const GraphicsDrawIndirectCommand* pDrawCommand = (const GraphicsDrawIndirectCommand*)pCommand;
                const VulkanBuffer* pParameterBuffer = (const VulkanBuffer*)pDrawCommand->pParametersBuffer;

                if( pDrawCommand->isIndexed )
                {
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                    VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::DrawIndexedIndirect, pState->pCurrentRenderPipeline->getDebugName() );
#endif

                    pVulkan->vkCmdDrawIndexedIndirect( commandBuffer, pParameterBuffer->buffer, pDrawCommand->parameterBufferOffset, pDrawCommand->drawCount, pDrawCommand->parameterStride );
                }
                else
                {
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                    VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::DrawIndirect, pState->pCurrentRenderPipeline->getDebugName() );
#endif

                    pVulkan->vkCmdDrawIndirect( commandBuffer, pParameterBuffer->buffer, pDrawCommand->parameterBufferOffset, pDrawCommand->drawCount, pDrawCommand->parameterStride );
                }
            }
            break;

        case GraphicsCommandId_DrawIndirectCount:
            {
                const GraphicsDrawIndirectCountCommand* pDrawCommand = (const GraphicsDrawIndirectCountCommand*)pCommand;
                const VulkanBuffer* pParameterBuffer = (const VulkanBuffer*)pDrawCommand->pParametersBuffer;
                const VulkanBuffer* pCountBuffer = (const VulkanBuffer*)pDrawCommand->pCountBuffer;

                if( pDrawCommand->isIndexed )
                {
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                    VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::DrawIndexedIndirectCount, pState->pCurrentRenderPipeline->getDebugName() );
#endif

                    pVulkan->vkCmdDrawIndexedIndirectCount( commandBuffer, pParameterBuffer->buffer, pDrawCommand->parameterBufferOffset, pCountBuffer->buffer, pDrawCommand->countBufferOffset, pDrawCommand->maxDrawCount, pDrawCommand->parameterStride );
                }
                else
                {
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                    VulkanBreadcrumbScope breadcrumb( pState->pBreadcrumbBuffer, pVulkan, commandBuffer, VulkanBreadcrumbType::DrawIndirectCount, pState->pCurrentRenderPipeline->getDebugName() );
#endif

                    pVulkan->vkCmdDrawIndirectCount( commandBuffer, pParameterBuffer->buffer, pDrawCommand->parameterBufferOffset, pCountBuffer->buffer, pDrawCommand->countBufferOffset, pDrawCommand->maxDrawCount, pDrawCommand->parameterStride );
                }
            }
            break;

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CODE )
        case GraphicsCommandId_BeginDebugLabel:
            {
                const GraphicsBeginDebugLabelCommand* pBeginDebugLabelCommand = (const GraphicsBeginDebugLabelCommand*)pCommand;

                vulkan::beginDebugLabel( pVulkan, commandBuffer, pBeginDebugLabelCommand->name, pBeginDebugLabelCommand->color );

#if KEEN_USING( KEEN_VULKAN_CHECKPOINTS )
                vulkan::insertCheckPoint( pVulkan, commandBuffer, pBeginDebugLabelCommand->name.getCName() );
#endif
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                pushBreadcrumbZone( pState->pBreadcrumbBuffer, pBeginDebugLabelCommand->name.getName() );
#endif
            }
            break;

        case GraphicsCommandId_EndDebugLabel:
            {
#if KEEN_USING( KEEN_VULKAN_CHECKPOINTS )
                vulkan::insertCheckPoint( pVulkan, commandBuffer, "endDebugLabel" );
#endif
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                popBreadcrumbZone( pState->pBreadcrumbBuffer );
#endif

                vulkan::endDebugLabel( pVulkan, commandBuffer );
            }
            break;

        case GraphicsCommandId_InsertDebugLabel:
            {
                const GraphicsInsertDebugLabelCommand* pInsertDebugLabelCommand = (const GraphicsInsertDebugLabelCommand*)pCommand;

                vulkan::insertDebugLabel( pVulkan, commandBuffer, pInsertDebugLabelCommand->name, pInsertDebugLabelCommand->color );

#if KEEN_USING( KEEN_VULKAN_CHECKPOINTS )
                vulkan::insertCheckPoint( pVulkan, commandBuffer, pInsertDebugLabelCommand->name.getCName() );
#endif
            }
            break;
#endif
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        case GraphicsCommandId_BeginBreadcrumbBatch:
        {
                const GraphicsBeginBreadcrumbBatchCommand* pBeginBreadcrumbBatchCommand = (const GraphicsBeginBreadcrumbBatchCommand*)pCommand;
                pushBreadcrumbZone( pState->pBreadcrumbBuffer, pBeginBreadcrumbBatchCommand->name.getName() );
                toggleBreadcrumbRecording( pState->pBreadcrumbBuffer, false );
        }
        break;
        case GraphicsCommandId_EndBreadcrumbBatch:
        {
                const GraphicsEndBreadcrumbBatchCommand* pEndBreadcrumbBatchCommand = (const GraphicsEndBreadcrumbBatchCommand*)pCommand;
                KEEN_UNUSED1( pEndBreadcrumbBatchCommand );
                toggleBreadcrumbRecording( pState->pBreadcrumbBuffer, true );
                popBreadcrumbZone( pState->pBreadcrumbBuffer );
        }
        break;
#endif

        case GraphicsCommandId_ResetQueryPool:
            {
                const GraphicsResetQueryPoolCommand* pResetQueryPoolCommand = (const GraphicsResetQueryPoolCommand*)pCommand;

                VulkanQueryPool* pQueryPool = (VulkanQueryPool*)pResetQueryPoolCommand->pQueryPool;
                pVulkan->vkCmdResetQueryPool( commandBuffer, pQueryPool->queryPool, pResetQueryPoolCommand->firstQuery, pResetQueryPoolCommand->queryCount );
            }
            break;

        case GraphicsCommandId_WriteTimestampQuery:
            {
                const GraphicsWriteTimestampQueryCommand* pQueryCommand = (const GraphicsWriteTimestampQueryCommand*)pCommand;
                VulkanQueryPool* pQueryPool = (VulkanQueryPool*)pQueryCommand->pQueryPool;

                VkPipelineStageFlagBits pipelineStageFlagMask{};
                switch( pQueryCommand->queryType )
                {
                case GraphicsQueryType::TimeStamp_PipelineTop:      pipelineStageFlagMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; break;
                case GraphicsQueryType::TimeStamp_PipelineBottom:   pipelineStageFlagMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; break;
                default:
                    KEEN_BREAK( "Invalid code path" );
                    break;
                }

                pVulkan->vkCmdWriteTimestamp( commandBuffer, pipelineStageFlagMask, pQueryPool->queryPool, pQueryCommand->queryIndex );
            }
            break;

        case GraphicsCommandId_CopyQueryResults:
            {
                const GraphicsCopyQueryResultsCommand* pCopyQueryResultsCommand = (const GraphicsCopyQueryResultsCommand*)pCommand;
                VulkanQueryPool* pQueryPool = (VulkanQueryPool*)pCopyQueryResultsCommand->pQueryPool;
                const VulkanBuffer* pTargetBuffer = (const VulkanBuffer*)pCopyQueryResultsCommand->pTargetBuffer;

                const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT;
                pVulkan->vkCmdCopyQueryPoolResults( commandBuffer, pQueryPool->queryPool, pCopyQueryResultsCommand->queryOffset, pCopyQueryResultsCommand->queryCount, pTargetBuffer->buffer, pCopyQueryResultsCommand->targetOffset, pCopyQueryResultsCommand->targetStride, flags );
            }
            break;

        case GraphicsCommandId_BeginRendering:
            {
                const GraphicsBeginRenderingCommand* pBeginRenderingCommand = (const GraphicsBeginRenderingCommand*)pCommand;

                vulkan::beginDebugLabel( pVulkan, commandBuffer, pBeginRenderingCommand->debugName );

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                pushBreadcrumbZone( pState->pBreadcrumbBuffer, pBeginRenderingCommand->debugName.getName() );
#endif
                breadcrumbRenderpassHint( pState->pBreadcrumbBuffer, true );

                VkRenderingAttachmentInfo colorAttachmentInfos[ GraphicsLimits_MaxColorTargetCount ];
                for( uint32 i = 0u; i < pBeginRenderingCommand->colorAttachmentCount; ++i )
                {
                    const GraphicsRenderingAttachmentInfo& colorAttachment = pBeginRenderingCommand->colorAttachments[ i ];
                    const VulkanTexture* pTexture = (const VulkanTexture*)colorAttachment.pTexture;
                    const VulkanTexture* pResolveTexture = (const VulkanTexture*)colorAttachment.pResolveTexture;

                    colorAttachmentInfos[ i ].sType         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    colorAttachmentInfos[ i ].pNext         = nullptr;
                    colorAttachmentInfos[ i ].imageView     = pTexture->imageView;
                    colorAttachmentInfos[ i ].imageLayout   = vulkan::getImageLayout( colorAttachment.textureLayout );

                    if( pResolveTexture != nullptr )
                    {
                        colorAttachmentInfos[ i ].resolveMode           = VK_RESOLVE_MODE_AVERAGE_BIT;
                        colorAttachmentInfos[ i ].resolveImageView      = pResolveTexture->imageView;
                        colorAttachmentInfos[ i ].resolveImageLayout    = vulkan::getImageLayout( colorAttachment.resolveTextureLayout );
                    }
                    else
                    {
                        colorAttachmentInfos[ i ].resolveMode           = VK_RESOLVE_MODE_NONE;
                        colorAttachmentInfos[ i ].resolveImageView      = VK_NULL_HANDLE;
                        colorAttachmentInfos[ i ].resolveImageLayout    = VK_IMAGE_LAYOUT_UNDEFINED;
                    }
                    colorAttachmentInfos[ i ].loadOp    = vulkan::getLoadOp( colorAttachment.loadAction );
                    colorAttachmentInfos[ i ].storeOp   = vulkan::getStoreOp( colorAttachment.storeAction );

                    colorAttachmentInfos[ i ].clearValue.color = vulkan::getColorClearValue( colorAttachment.clearValue.color );
                }

                //const GraphicsRectangle renderArea = pBeginRenderingCommand->renderArea;

                VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                renderingInfo.renderArea.offset     = { 0u, 0u };
                renderingInfo.renderArea.extent     = { pBeginRenderingCommand->renderSize.x, pBeginRenderingCommand->renderSize.y };
                renderingInfo.layerCount            = 1u;
                renderingInfo.colorAttachmentCount  = pBeginRenderingCommand->colorAttachmentCount;
                renderingInfo.pColorAttachments     = colorAttachmentInfos;

                VkRenderingAttachmentInfo depthAttachmentInfo;
                if( pBeginRenderingCommand->depthAttachment.pTexture != nullptr )
                {
                    const VulkanTexture* pDepthImage = (const VulkanTexture*)pBeginRenderingCommand->depthAttachment.pTexture;
                    const VulkanTexture* pDepthResolveImage = (const VulkanTexture*)pBeginRenderingCommand->depthAttachment.pResolveTexture;

                    depthAttachmentInfo.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    depthAttachmentInfo.pNext       = nullptr;
                    depthAttachmentInfo.imageView   = pDepthImage->imageView;
                    depthAttachmentInfo.imageLayout = vulkan::getImageLayout( pBeginRenderingCommand->depthAttachment.textureLayout );

                    if( pDepthResolveImage != nullptr )
                    {
                        depthAttachmentInfo.resolveMode         = VK_RESOLVE_MODE_MIN_BIT;
                        depthAttachmentInfo.resolveImageView    = pDepthResolveImage->imageView;
                        depthAttachmentInfo.resolveImageLayout  = vulkan::getImageLayout( pBeginRenderingCommand->depthAttachment.resolveTextureLayout );
                    }
                    else
                    {
                        depthAttachmentInfo.resolveMode         = VK_RESOLVE_MODE_NONE;
                        depthAttachmentInfo.resolveImageView    = VK_NULL_HANDLE;
                        depthAttachmentInfo.resolveImageLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
                    }
                    depthAttachmentInfo.loadOp  = vulkan::getLoadOp( pBeginRenderingCommand->depthAttachment.loadAction );
                    depthAttachmentInfo.storeOp = vulkan::getStoreOp( pBeginRenderingCommand->depthAttachment.storeAction );

                    depthAttachmentInfo.clearValue.depthStencil = vulkan::getDepthStencilClearValue( pBeginRenderingCommand->depthAttachment.clearValue.depth, 0 );
                    renderingInfo.pDepthAttachment = &depthAttachmentInfo;
                }

                VkRenderingAttachmentInfo stencilAttachmentInfo;
                if( pBeginRenderingCommand->stencilAttachment.pTexture != nullptr )
                {
                    // :FK: :NOTE: No vulkan hardware supports separate depth stencil attachments
                    KEEN_ASSERT( pBeginRenderingCommand->depthAttachment.pTexture == nullptr || pBeginRenderingCommand->stencilAttachment.pTexture == pBeginRenderingCommand->depthAttachment.pTexture );

                    const VulkanTexture* pStencilImage = (const VulkanTexture*)pBeginRenderingCommand->stencilAttachment.pTexture;
                    const VulkanTexture* pStencilResolveImage = (const VulkanTexture*)pBeginRenderingCommand->stencilAttachment.pResolveTexture;

                    stencilAttachmentInfo.sType         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    stencilAttachmentInfo.pNext         = nullptr;
                    stencilAttachmentInfo.imageView     = pStencilImage->imageView;
                    stencilAttachmentInfo.imageLayout   = vulkan::getImageLayout( pBeginRenderingCommand->stencilAttachment.textureLayout );

                    if( pStencilResolveImage != nullptr )
                    {
                        stencilAttachmentInfo.resolveMode           = VK_RESOLVE_MODE_MIN_BIT;
                        stencilAttachmentInfo.resolveImageView      = pStencilResolveImage->imageView;
                        stencilAttachmentInfo.resolveImageLayout    = vulkan::getImageLayout( pBeginRenderingCommand->stencilAttachment.resolveTextureLayout );
                    }
                    else
                    {
                        stencilAttachmentInfo.resolveMode           = VK_RESOLVE_MODE_NONE;
                        stencilAttachmentInfo.resolveImageView      = VK_NULL_HANDLE;
                        stencilAttachmentInfo.resolveImageLayout    = VK_IMAGE_LAYOUT_UNDEFINED;
                    }
                    stencilAttachmentInfo.loadOp    = vulkan::getLoadOp( pBeginRenderingCommand->stencilAttachment.loadAction );
                    stencilAttachmentInfo.storeOp   = vulkan::getStoreOp( pBeginRenderingCommand->stencilAttachment.storeAction );

                    stencilAttachmentInfo.clearValue.depthStencil = vulkan::getDepthStencilClearValue( GraphicsDepthClearValue::Zero, pBeginRenderingCommand->stencilAttachment.clearValue.stencil );

                    renderingInfo.pStencilAttachment = &stencilAttachmentInfo;
                }

                pVulkan->vkCmdBeginRenderingKHR( commandBuffer, &renderingInfo );
            }
            break;

        case GraphicsCommandId_EndRendering:
            {
                const GraphicsEndRenderingCommand* pEndRenderingCommand = (const GraphicsEndRenderingCommand*)pCommand;

                KEEN_UNUSED1( pEndRenderingCommand );
                pVulkan->vkCmdEndRenderingKHR( commandBuffer );

                breadcrumbRenderpassHint( pState->pBreadcrumbBuffer, false );
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                popBreadcrumbZone( pState->pBreadcrumbBuffer );
#endif

                vulkan::endDebugLabel( pVulkan, commandBuffer );
            }
            break;

        default:
            KEEN_BREAK( "???" );
            break;
        }
    }

}
