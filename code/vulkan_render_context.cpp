#include "vulkan_render_context.hpp"
#include "vulkan_swap_chain.hpp"
#include "vulkan_synchronization.hpp"
#include "vulkan_descriptor_set_writer.hpp"
#include "vulkan_command_buffer.hpp"

#include "keen/base/defer.hpp"
#include "keen/os/os_crash.hpp"
#include "keen/os/process.hpp"

namespace keen
{
    namespace vulkan
    {
        KEEN_DEFINE_BOOL_VARIABLE( s_testGpuCrash,      "vulkan/testGpuCrash", false, "");
        KEEN_DEFINE_BOOL_VARIABLE( s_waitAfterSubmit,   "vulkan/waitAfterSubmit", false, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_waitForGpu,        "vulkan/WaitForGpu", false, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_enableBreadcrumbs, "vulkan/enableBreadcrumbs", false, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_verboseQueueSubmit,"vulkan/verboseQueueSubmit", false, "" );

#if !defined( KEEN_BUILD_MASTER )
        KEEN_DEFINE_BOOL_VARIABLE( s_splitSubmission,   "vulkan/splitSubmission", false, "" );
#endif
    }

    bool VulkanRenderContext::tryCreate( const VulkanRenderContextParameters& parameters )
    {
        m_pAllocator        = parameters.pAllocator;

        m_pVulkan           = parameters.pVulkan;
        m_device            = parameters.device;
        m_physicalDevice    = parameters.physicalDevice;

        m_pObjects          = parameters.pObjects;
        m_pSharedData       = parameters.pSharedData;

        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanDescriptorSetCount, 0u, "Vk_DescriptorSetCount", false );

        m_currentFrameId                = 0u;
        m_isNonInteractiveApplication   = parameters.isNonInteractiveApplication;

        size_t workerCount = 1u;

        KEEN_TRACE_INFO( "[graphics] Using %zu threads for vulkan command buffer recording!\n", workerCount );

        if( !m_frames.tryCreateZero( m_pAllocator, parameters.frameCount ) )
        {
            return false;
        }

        m_pSharedData->info.internalFrameCount = (uint32)m_frames.getSize();

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        const bool enableBreadcrumbs = vulkan::s_enableBreadcrumbs || parameters.enableBreadcrumbs;
        if( enableBreadcrumbs )
        {
            KEEN_TRACE_INFO( "[graphics] Enabling vulkan breadcrumbs.\n" );
        }
        else
        {
            KEEN_TRACE_INFO( "[graphics] Vulkan breadcrumbs disabled (enable them using the --enable-gpu-breadcrumbs command line argument)\n" );
        }
#endif

        // create the descriptor pool for the bindless descriptors:
        if( parameters.enableBindlessDescriptors )
        {
            // descriptor pool:
            const uint32 sampledImageDescriptorCount = ( m_frames.getCount32() + 1u ) * parameters.bindlessTextureCount;
            const uint32 samplerDescriptorCount = ( m_frames.getCount32() + 1u ) * parameters.bindlessSamplerCount;
            const VkDescriptorPoolSize poolSizes[] =
            {
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampledImageDescriptorCount },
                { VK_DESCRIPTOR_TYPE_SAMPLER, samplerDescriptorCount }
            };

            VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            descriptorPoolCreateInfo.maxSets        = m_frames.getCount32() + 1u;
            descriptorPoolCreateInfo.poolSizeCount  = KEEN_COUNTOF( poolSizes );
            descriptorPoolCreateInfo.pPoolSizes     = poolSizes;
            descriptorPoolCreateInfo.flags          = 0u;

            VulkanResult result = m_pVulkan->vkCreateDescriptorPool( m_device, &descriptorPoolCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &m_bindlessDescriptorSetPool );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateDescriptorPool failed with error '%s'\n", result );
                destroy();
                return false;
            }

            vulkan::setObjectName( m_pVulkan, m_device, m_bindlessDescriptorSetPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "BindlessDescriptorSetPool"_debug );

            {
                const VkDescriptorSetLayout emptyDescriptorSetLayout = m_pObjects->getEmptyDescriptorSetLayout();
                KEEN_ASSERT( emptyDescriptorSetLayout != VK_NULL_HANDLE );

                VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocateInfo.descriptorPool     = m_bindlessDescriptorSetPool;
                allocateInfo.descriptorSetCount = 1u;
                allocateInfo.pSetLayouts        = &emptyDescriptorSetLayout;

                result = m_pVulkan->vkAllocateDescriptorSets( m_device, &allocateInfo, &m_pSharedData->emptyDescriptorSet );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkAllocateDescriptorSets failed with error '%s'\n", result );
                    destroy();
                    return false;
                }

                vulkan::setObjectName( m_pVulkan, m_device, m_pSharedData->emptyDescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Empty"_debug );
            }
        }
        else
        {
            m_bindlessDescriptorSetPool = VK_NULL_HANDLE;
        }

        for( size_t frameIndex = 0u; frameIndex < m_frames.getSize(); ++frameIndex )
        {
            VulkanFrame* pFrame = &m_frames[ frameIndex ];

            VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VulkanResult result = m_pVulkan->vkCreateSemaphore( m_device, &semaphoreCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pFrame->renderingFinishedSemaphore );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateSemaphore failed with error '%s'\n", result );
                destroy();
                return false;
            }

            VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            result = m_pVulkan->vkCreateFence( m_device, &fenceCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pFrame->fence );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateFence failed with error '%s'\n", result );
                destroy();
                return false;
            }

            if( !pFrame->destroyObjects.tryCreate( parameters.pAllocator, 1024u ) ||
                !pFrame->commandPools.tryCreate( m_pAllocator, workerCount ) ||
                !pFrame->bindlessTexturesDirtyMask.tryCreateClear( m_pAllocator, parameters.bindlessTextureCount ) ||
                !pFrame->bindlessSamplersDirtyMask.tryCreateClear( m_pAllocator, parameters.bindlessSamplerCount ) )
            {
                destroy();
                return false;
            }

            // allocate the per-thread command pools:
            for( size_t workerIndex = 0u; workerIndex < pFrame->commandPools.getSize(); ++workerIndex )
            {
                VulkanCommandPool* pCommandPool = &pFrame->commandPools[ workerIndex ];

                // initialize
                fillMemoryWithZero( pCommandPool, sizeof( VulkanCommandPool ) );

                // create a graphics command pool:
                VkCommandPoolCreateInfo commandPoolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
                commandPoolCreateInfo.queueFamilyIndex  = m_pSharedData->graphicsQueueFamilyIndex;
                commandPoolCreateInfo.flags             = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                result = m_pVulkan->vkCreateCommandPool( m_device, &commandPoolCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pCommandPool->commandPool );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkCreateCommandPool failed with error '%s'\n", result );
                    destroy();
                    return false;
                }

                pCommandPool->commandBuffers.create( m_pAllocator );
                if( !resizeCommandPool( pCommandPool, 12u ) )
                {
                    destroy();
                    return false;
                }
            }

            // allocate the main command buffer for the frame:
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            commandBufferAllocateInfo.commandPool           = pFrame->commandPools[ 0u ].commandPool;
            commandBufferAllocateInfo.level                 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount    = 1u;

            result = m_pVulkan->vkAllocateCommandBuffers( m_device, &commandBufferAllocateInfo, &pFrame->mainCommandBuffer );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkAllocateCommandBuffers failed with error '%s'\n", result );
                destroy();
                return false;
            }

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
            if( enableBreadcrumbs )
            {
                KEEN_TRACE_INFO( "[graphics] Creating vulkan breadcrumbs buffer for frame #%d\n", frameIndex );
                pFrame->pBreadcrumbBuffer = createVulkanBreadcrumbBuffer( m_pAllocator, m_pVulkan, m_device );

                m_pSharedData->info.areBreadcrumbsEnabled = true;
            }
#endif

            // allocate the bindless descriptor set for this frame:
            if( parameters.enableBindlessDescriptors )
            {
                const VkDescriptorSetLayout bindlessDescriptorSetLayout = m_pObjects->getBindlessDescriptorSetLayout();
                KEEN_ASSERT( bindlessDescriptorSetLayout != VK_NULL_HANDLE );

                VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                allocateInfo.descriptorPool     = m_bindlessDescriptorSetPool;
                allocateInfo.descriptorSetCount = 1u;
                allocateInfo.pSetLayouts        = &bindlessDescriptorSetLayout;

                result = m_pVulkan->vkAllocateDescriptorSets( m_device, &allocateInfo, &pFrame->bindlessDescriptorSet );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkAllocateDescriptorSets failed with error '%s'\n", result );
                    destroy();
                    return false;
                }

                // set the first bit as dirty - to write the error descriptor in the first frame:
                pFrame->bindlessTexturesDirtyMask.set( 0u );
                pFrame->bindlessSamplersDirtyMask.set( 0u );
            }

            pFrame->pDescriptorPool = nullptr;
        }

        return true;
    }

    void VulkanRenderContext::destroy()
    {
        for( size_t frameIndex = 0u; frameIndex < m_frames.getSize(); ++frameIndex )
        {
            VulkanFrame* pFrame = &m_frames[ frameIndex ];

            KEEN_ASSERT( pFrame->isRunning == false );

            if( pFrame->pDescriptorPool != nullptr )
            {
                VulkanDescriptorPool* pDescriptorPool = pFrame->pDescriptorPool;
                while( pDescriptorPool != nullptr )
                {
                    VulkanDescriptorPool* pNextDescriptorPool = pDescriptorPool->pNext;
                    m_pObjects->freeDescriptorPool( pDescriptorPool );
                    pDescriptorPool = pNextDescriptorPool;
                }
                pFrame->pDescriptorPool = nullptr;
            }

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
            if( pFrame->pBreadcrumbBuffer != nullptr )
            {
                destroyVulkanBreadcrumbBuffer( pFrame->pBreadcrumbBuffer, m_pAllocator, m_pVulkan, m_device );
            }
#endif

            if( pFrame->commandPools.hasElements() )
            {
                if( pFrame->mainCommandBuffer != VK_NULL_HANDLE )
                {
                    m_pVulkan->vkFreeCommandBuffers( m_device, pFrame->commandPools[ 0u ].commandPool, 1u, &pFrame->mainCommandBuffer );
                    pFrame->mainCommandBuffer = VK_NULL_HANDLE;
                }

                for( size_t workerIndex = 0u; workerIndex < pFrame->commandPools.getSize(); ++workerIndex )
                {
                    VulkanCommandPool* pCommandPool = &pFrame->commandPools[ workerIndex ];

                    if( pCommandPool->commandBuffers.isValid() )
                    {
                        m_pVulkan->vkFreeCommandBuffers( m_device, pCommandPool->commandPool, (uint32)pCommandPool->commandBuffers.getSize(), pCommandPool->commandBuffers.getStart() );
                        pCommandPool->commandBuffers.destroy();
                    }

                    if( pCommandPool->commandPool != VK_NULL_HANDLE )
                    {
                        m_pVulkan->vkDestroyCommandPool( m_device, pCommandPool->commandPool, m_pSharedData->pVulkanAllocationCallbacks );
                    }
                }
                pFrame->commandPools.destroy();
            }

            if( pFrame->fence != VK_NULL_HANDLE )
            {
                m_pVulkan->vkDestroyFence( m_device, pFrame->fence, m_pSharedData->pVulkanAllocationCallbacks );
                pFrame->fence = VK_NULL_HANDLE;
            }

            if( pFrame->renderingFinishedSemaphore != VK_NULL_HANDLE )
            {
                m_pVulkan->vkDestroySemaphore( m_device, pFrame->renderingFinishedSemaphore, m_pSharedData->pVulkanAllocationCallbacks );
            }
        }

        m_frames.destroy();

        if( m_bindlessDescriptorSetPool != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyDescriptorPool( m_device, m_bindlessDescriptorSetPool, m_pSharedData->pVulkanAllocationCallbacks );
            m_bindlessDescriptorSetPool = VK_NULL_HANDLE;
        }

        KEEN_PROFILE_COUNTER_UNREGISTER( m_vulkanDescriptorSetCount );
    }

    VulkanFrame* VulkanRenderContext::beginFrame( ArrayView<GraphicsSwapChain*> swapChains )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VulkanFrame* pFrame;
        {
            pFrame = &m_frames[ m_currentFrameId % m_frames.getSize() ];
            // wait on the fence of this frame:
            waitForFrame( pFrame );
            prepareFrame( pFrame );
        }
        m_currentFrameId++;

        KEEN_ASSERT( pFrame != nullptr );

        pFrame->targetSwapChains.clear();

        pFrame->swapChainInfo.swapChains.clear();
        pFrame->swapChainInfo.imageAvailableSemaphores.clear();
        pFrame->swapChainInfo.waitStageMasks.clear();
        pFrame->swapChainInfo.imageIndices.clear();
        pFrame->hasBrokenSwapChains = false;

        for( size_t i = 0u; i < swapChains.getCount(); ++i )
        {
            VulkanSwapChainWrapper* pSwapChainWrapper = (VulkanSwapChainWrapper*)swapChains[ i ];
            VulkanSwapChain* pSwapChain = pSwapChainWrapper->pSwapChain;

            KEEN_ASSERT( pSwapChain->getFrameId() != m_currentFrameId );

            if( pSwapChain->beginNextImage( &pFrame->swapChainInfo, m_currentFrameId ) )
            {
                pFrame->targetSwapChains.pushBack( pSwapChain );
            }
            else
            {
                // :JK: we continue rendering..     
                KEEN_ASSERT( !pSwapChain->isValid() );
                pFrame->hasBrokenSwapChains = true;
            }           
        }

        return pFrame;
    }

    void VulkanRenderContext::submitFrame( VulkanFrame* pFrame, const GraphicsBindlessDescriptorSet& bindlessDescriptorSet )
    {
        executeFrame( pFrame, bindlessDescriptorSet );
    }

    void VulkanRenderContext::waitForAllFramesFinished()
    {
        for( size_t frameIndex = 0u; frameIndex < m_frames.getCount(); ++frameIndex )
        {
            VulkanFrame* pFrame = &m_frames[ frameIndex ];
            waitForFrame( pFrame );
        }
    }

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
    void VulkanRenderContext::executeValidationLayerDetectionCode( VulkanBuffer* pBuffer )
    {
        // create a graphics command pool:
        VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        commandPoolCreateInfo.queueFamilyIndex = m_pSharedData->graphicsQueueFamilyIndex;
        commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool commandPool;
        VulkanResult result = m_pVulkan->vkCreateCommandPool( m_device, &commandPoolCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &commandPool );
        if( result.hasError() )
        {
            return;
        }

        VkCommandBufferAllocateInfo commandBufferAllocateInfo ={ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocateInfo.commandPool           = commandPool;
        commandBufferAllocateInfo.level                 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount    = 1u;
        VkCommandBuffer commandBuffer;
        result = m_pVulkan->vkAllocateCommandBuffers( m_device, &commandBufferAllocateInfo, &commandBuffer );
        if( result.hasError() )
        {
            return;
        }

        VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = m_pVulkan->vkBeginCommandBuffer( commandBuffer, &commandBufferBeginInfo );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkBeginCommandBuffer failed with error '%s'\n", result );
            return;
        }

        {
            VkBufferCopy region;
            region.srcOffset    = 0u;
            region.dstOffset    = 16u;
            region.size         = 16u;
            m_pVulkan->vkCmdCopyBuffer( commandBuffer, pBuffer->buffer, pBuffer->buffer, 1u, &region );
        }
        {
            VkBufferCopy region;
            region.srcOffset    = 8u;
            region.dstOffset    = 16u;
            region.size         = 8u;
            m_pVulkan->vkCmdCopyBuffer( commandBuffer, pBuffer->buffer, pBuffer->buffer, 1u, &region );
        }

        result = m_pVulkan->vkEndCommandBuffer( commandBuffer );
        if( result.hasError() )
        {
            return;
        }

        m_pVulkan->vkFreeCommandBuffers( m_device, commandPool, 1u, &commandBuffer );
        m_pVulkan->vkDestroyCommandPool( m_device, commandPool, m_pSharedData->pVulkanAllocationCallbacks );
    }
#endif

    bool VulkanRenderContext::submitCommandBuffer( VulkanFrame* pFrame, VkCommandBuffer commandBuffer, SubmitCommandBufferFlags flags, DebugName debugName )
    {
        KEEN_PROFILE_CPU( Vk_Submit );

        if( flags.isSet( SubmitCommandBufferFlag::IsFirstCommandBuffer ) )
        {
            KEEN_ASSERT( !pFrame->isRunning );
            pFrame->isRunning = true;
        }
        else
        {
            KEEN_ASSERT( pFrame->isRunning );
        }

        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        if( flags.isSet( SubmitCommandBufferFlag::IsFirstCommandBuffer ) && pFrame->swapChainInfo.imageAvailableSemaphores.hasElements() )
        {
            submitInfo.waitSemaphoreCount   = pFrame->swapChainInfo.imageAvailableSemaphores.getCount32();
            submitInfo.pWaitSemaphores      = pFrame->swapChainInfo.imageAvailableSemaphores.getStart();
            submitInfo.pWaitDstStageMask    = pFrame->swapChainInfo.waitStageMasks.getStart();
        }
        if( flags.isSet( SubmitCommandBufferFlag::IsLastCommandBuffer ) && pFrame->swapChainInfo.swapChains.hasElements() )
        {
            submitInfo.signalSemaphoreCount = 1u;
            submitInfo.pSignalSemaphores    = &pFrame->renderingFinishedSemaphore;
        }

        submitInfo.commandBufferCount   = 1u;
        submitInfo.pCommandBuffers      = &commandBuffer;

#if KEEN_USING( KEEN_GPU_PROFILER )
        if( flags.isSet( SubmitCommandBufferFlag::IsFirstCommandBuffer ) )
        {
            pFrame->submissionTime = profiler::getCurrentCpuTime();
        }
#endif

        VkFence fence = VK_NULL_HANDLE;
        if( flags.isSet( SubmitCommandBufferFlag::IsLastCommandBuffer ) )
        {
            fence = pFrame->fence;
        }

        if( vulkan::s_verboseQueueSubmit )
        {
            KEEN_TRACE_INFO( "[graphics] vkQueueSubmit '%s'\n", debugName );
        }

        VulkanResult result = m_pVulkan->vkQueueSubmit( m_pSharedData->graphicsQueue, 1u, &submitInfo, fence );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkQueueSubmit '%s' failed with error '%s'\n", debugName, result );
        }
        if( handleDeviceLost( result ) )
        {
            return false;
        }
        if( result.hasError() )
        {
            traceFrameBreadcrumbs();
            KEEN_BREAKPOINT;
            return false;
        }

        if( flags.isSet( SubmitCommandBufferFlag::WaitAfterSubmit ) || vulkan::s_waitAfterSubmit )
        {
            KEEN_PROFILE_CPU( Vk_QueueWaitIdle );
            result = m_pVulkan->vkQueueWaitIdle( m_pSharedData->graphicsQueue );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkQueueWaitIdle '%s' failed with error '%s'\n", debugName, result );
            }
            if( handleDeviceLost( result ) )
            {
                return false;
            }
            if( result.hasError() )
            {
                traceFrameBreadcrumbs();
                KEEN_BREAKPOINT;
                return false;
            }
        }

        return true;
    }

    void VulkanRenderContext::recordStartOfFrameCommands( VulkanFrame* pFrame, VkCommandBuffer commandBuffer )
    {
#if KEEN_USING( KEEN_GPU_PROFILER )
        if( pFrame->pProfilePool != nullptr && pFrame->beginQueryId != 0xffffffffu )
        {
            const VulkanQueryPool* pFrameVulkanQueryPool = (VulkanQueryPool*)graphics::getQueryPool( pFrame->pProfilePool );
            m_pVulkan->vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pFrameVulkanQueryPool->queryPool, pFrame->beginQueryId );
        }
#endif

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        if( pFrame->pBreadcrumbBuffer != nullptr )
        {
            beginBreadcrumbFrame( pFrame->pBreadcrumbBuffer, m_pVulkan, commandBuffer, pFrame->id );
        }
#endif
    }

    void VulkanRenderContext::recordEndOfFrameCommands( VulkanFrame* pFrame, VkCommandBuffer commandBuffer )
    {
        for( size_t i = 0u; i < pFrame->targetSwapChains.getCount(); ++i )
        {
            VulkanSwapChain* pVulkanSwapChain = pFrame->targetSwapChains[ i ];
            pVulkanSwapChain->prepareForPresent( commandBuffer );
        }

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
        if( pFrame->pBreadcrumbBuffer != nullptr )
        {
            endBreadcrumbFrame( pFrame->pBreadcrumbBuffer );
        }
#endif

#if KEEN_USING( KEEN_GPU_PROFILER )
        if( pFrame->pProfilePool != nullptr && pFrame->endQueryId != 0xffffffffu )
        {
            const VulkanQueryPool* pFrameVulkanQueryPool = (VulkanQueryPool*)graphics::getQueryPool( pFrame->pProfilePool );
            m_pVulkan->vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pFrameVulkanQueryPool->queryPool, pFrame->endQueryId );
        }
#endif
    }

    void VulkanRenderContext::recordAndSubmitCommands( VulkanFrame* pFrame )
    {
        KEEN_PROFILE_CPU( Vk_RecordAndSubmit );

        const VkCommandBuffer commandBuffer = pFrame->mainCommandBuffer;

        VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VulkanResult result = m_pVulkan->vkBeginCommandBuffer( commandBuffer, &commandBufferBeginInfo );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkBeginCommandBuffer failed with error '%s'\n", result );
            return;
        }

        recordStartOfFrameCommands( pFrame, commandBuffer );

        // :JK: it would be trivial to do this in parallel (task system) again when this ever becomes a bottleneck
        const GraphicsCommandBuffer* pCommandBuffer = pFrame->pFirstCommandBuffer;
        while( pCommandBuffer != nullptr )
        {
            VulkanRecordCommandBufferParameters recordParameters{};
            recordParameters.frameId                = pFrame->id;
            recordParameters.queueInfos             = m_pSharedData->queueInfos;
            recordParameters.bindlessDescriptorSet  = pFrame->bindlessDescriptorSet;
            recordParameters.emptyDescriptorSet     = m_pSharedData->emptyDescriptorSet;
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
            recordParameters.pBreadcrumbBuffer  = pFrame->pBreadcrumbBuffer;
#endif
            vulkan::recordCommandBuffer( m_pVulkan, commandBuffer, pCommandBuffer, recordParameters );

            pCommandBuffer = pCommandBuffer->pNextCommandBuffer;
        }

        recordEndOfFrameCommands( pFrame, commandBuffer );

        result = m_pVulkan->vkEndCommandBuffer( commandBuffer );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkEndCommandBuffer failed with error '%s'\n", result );
            return;
        }

        submitCommandBuffer( pFrame, commandBuffer, { SubmitCommandBufferFlag::IsFirstCommandBuffer, SubmitCommandBufferFlag::IsLastCommandBuffer }, "Frame"_debug );
    }

#if !defined( KEEN_BUILD_MASTER )
    void VulkanRenderContext::recordAndSubmitCommandsSplit( VulkanFrame* pFrame )
    {
        KEEN_PROFILE_CPU( Vk_RecordAndSubmitSplit );

        const VkCommandBuffer commandBuffer = pFrame->mainCommandBuffer;

        const GraphicsCommandBuffer* pCommandBuffer = pFrame->pFirstCommandBuffer;
        while( pCommandBuffer != nullptr )
        {
            SubmitCommandBufferFlags submitFlags = SubmitCommandBufferFlag::WaitAfterSubmit;    // need to wait because we're reusing the command buffer for every submit
            submitFlags.setIf( SubmitCommandBufferFlag::IsFirstCommandBuffer, pCommandBuffer == pFrame->pFirstCommandBuffer );
            submitFlags.setIf( SubmitCommandBufferFlag::IsLastCommandBuffer, pCommandBuffer == pFrame->pLastCommandBuffer );
            KEEN_ASSERT( submitFlags.isClear( SubmitCommandBufferFlag::IsLastCommandBuffer ) || pCommandBuffer->pNextCommandBuffer == nullptr );

            VkCommandBufferBeginInfo commandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VulkanResult result = m_pVulkan->vkBeginCommandBuffer( commandBuffer, &commandBufferBeginInfo );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkBeginCommandBuffer failed with error '%s'\n", result );
                break;
            }

            if( submitFlags.isSet( SubmitCommandBufferFlag::IsFirstCommandBuffer ) )
            {
                recordStartOfFrameCommands( pFrame, commandBuffer );
            }

            VulkanRecordCommandBufferParameters recordParameters {};
            recordParameters.frameId            = pFrame->id;
            recordParameters.queueInfos         = m_pSharedData->queueInfos;
#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
            recordParameters.pBreadcrumbBuffer  = pFrame->pBreadcrumbBuffer;
#endif
            vulkan::recordCommandBuffer( m_pVulkan, commandBuffer, pCommandBuffer, recordParameters );

            if( submitFlags.isSet( SubmitCommandBufferFlag::IsLastCommandBuffer ) )
            {
                recordEndOfFrameCommands( pFrame, commandBuffer );
            }

            result = m_pVulkan->vkEndCommandBuffer( commandBuffer );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkEndCommandBuffer failed with error '%s'\n", result );
                break;
            }

            if( !submitCommandBuffer( pFrame, commandBuffer, submitFlags, pCommandBuffer->debugName ) )
            {
                break;
            }

            pCommandBuffer = pCommandBuffer->pNextCommandBuffer;
        }
    }
#endif

    static void transferSetBits( BitArrayCount* pTarget, const BitArrayCount& source )
    {
        KEEN_ASSERT( pTarget->getCount() == source.getCount() );

        if( !source.hasSetBits() )
        {
            return;
        }

        // :TODO: work on full uint64 values..
        for( uint32 i = source.findFirstSet(); i != source.getCount(); i = source.findNextSet( i ) )
        {
            pTarget->set( i );
        }
    }

    void VulkanRenderContext::executeFrame( VulkanFrame* pFrame, const GraphicsBindlessDescriptorSet& bindlessDescriptorSet )
    {
        KEEN_PROFILE_CPU( Vk_ExecuteFrame );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

#if KEEN_USING( KEEN_GRAPHICS_RENDERDOC )
        if( pFrame->pRenderDocApi != nullptr )
        {
            renderdoc::beginFrameCapture( pFrame->pRenderDocApi );
        }
#endif

        // update bindless descriptor sets:
        if( bindlessDescriptorSet.textures.hasElements() )
        {
            KEEN_PROFILE_CPU( Vk_BindlessDescriptorSet );

            if( bindlessDescriptorSet.textureDirtyMask.hasSetBits() || bindlessDescriptorSet.samplerDirtyMask.hasSetBits() )
            {
                // merge dirty mask into all frame dirty masks:
                for( size_t frameIndex = 0u; frameIndex < m_frames.getSize(); ++frameIndex )
                {
                    transferSetBits( &m_frames[ frameIndex ].bindlessTexturesDirtyMask, bindlessDescriptorSet.textureDirtyMask );
                    transferSetBits( &m_frames[ frameIndex ].bindlessSamplersDirtyMask, bindlessDescriptorSet.samplerDirtyMask );
                }
            }

            // update all dirty descriptors for this frame:
            if( pFrame->bindlessTexturesDirtyMask.hasSetBits() )
            {
                // write changed descriptors..
                constexpr uint32 BindlessDescriptorWriteBatchCount = 64u;
                DynamicArray<VkWriteDescriptorSet, BindlessDescriptorWriteBatchCount>   writes;
                DynamicArray<VkDescriptorImageInfo, BindlessDescriptorWriteBatchCount>  imageInfos;

                const VulkanTexture* pErrorTexture = (const VulkanTexture*)bindlessDescriptorSet.textures[ 0 ];
                
                // unfortunately there is no way to write an invalid descriptor value in vulkan - so we just write a dummy descriptor to a debug texture
                for( uint32 dirtyIndex = pFrame->bindlessTexturesDirtyMask.findFirstSet(); dirtyIndex != pFrame->bindlessTexturesDirtyMask.getCount(); dirtyIndex = pFrame->bindlessTexturesDirtyMask.findNextSet( dirtyIndex ) )
                {
                    if( imageInfos.getCount() == BindlessDescriptorWriteBatchCount )
                    {                       
                        m_pVulkan->vkUpdateDescriptorSets( m_device, writes.getCount32(), writes.getStart(), 0u, nullptr );
                        writes.clear();
                        imageInfos.clear();
                    }

                    VkDescriptorImageInfo imageDescriptorInfo;
                    if( bindlessDescriptorSet.textures[ dirtyIndex ] != nullptr )
                    {
                        const VulkanTexture* pTexture = (const VulkanTexture*)bindlessDescriptorSet.textures[ dirtyIndex ];
                        imageDescriptorInfo.imageView   = pTexture->imageView;
                        imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imageDescriptorInfo.sampler     = VK_NULL_HANDLE;
                    }
                    else
                    {
                        // write error texture descriptor
                        imageDescriptorInfo.imageView   = pErrorTexture->imageView;
                        imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imageDescriptorInfo.sampler     = VK_NULL_HANDLE;
                    }

                    const VkDescriptorImageInfo* pImageInfo = imageInfos.pushBack( imageDescriptorInfo );
                    KEEN_ASSERT( pImageInfo != nullptr );

                    VkWriteDescriptorSet write;
                    write.sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.pNext             = nullptr;
                    write.dstSet            = pFrame->bindlessDescriptorSet;
                    write.dstBinding        = 0u;
                    write.dstArrayElement   = dirtyIndex;
                    write.descriptorCount   = 1u;
                    write.descriptorType    = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    write.pImageInfo        = pImageInfo;
                    write.pBufferInfo       = nullptr;
                    write.pTexelBufferView  = nullptr;
                    writes.pushBack( write );

                    pFrame->bindlessTexturesDirtyMask.clear( dirtyIndex );
                }

                KEEN_ASSERT( pFrame->bindlessTexturesDirtyMask.getSetBitCount() == 0u );

                if( writes.hasElements() )
                {
                    m_pVulkan->vkUpdateDescriptorSets( m_device, writes.getCount32(), writes.getStart(), 0u, nullptr );
                }               
            }

            if( pFrame->bindlessSamplersDirtyMask.hasSetBits() )
            {
                // write changed descriptors..
                constexpr uint32 BindlessDescriptorWriteBatchCount = 64u;
                DynamicArray<VkWriteDescriptorSet, BindlessDescriptorWriteBatchCount>   writes;
                DynamicArray<VkDescriptorImageInfo, BindlessDescriptorWriteBatchCount>  samplerInfos;

                const VulkanSampler* pErrorSampler = (const VulkanSampler*)bindlessDescriptorSet.samplers[ 0 ];
                
                // unfortunately there is no way to write an invalid descriptor value in vulkan - so we just write a dummy descriptor to a debug texture
                for( uint32 dirtyIndex = pFrame->bindlessSamplersDirtyMask.findFirstSet(); dirtyIndex != pFrame->bindlessSamplersDirtyMask.getCount(); dirtyIndex = pFrame->bindlessSamplersDirtyMask.findNextSet( dirtyIndex ) )
                {
                    if( samplerInfos.getCount() == BindlessDescriptorWriteBatchCount )
                    {                       
                        m_pVulkan->vkUpdateDescriptorSets( m_device, writes.getCount32(), writes.getStart(), 0u, nullptr );
                        writes.clear();
                        samplerInfos.clear();
                    }

                    VkDescriptorImageInfo imageDescriptorInfo;
                    if( bindlessDescriptorSet.samplers[ dirtyIndex ] != nullptr )
                    {
                        const VulkanSampler* pSampler = (const VulkanSampler*)bindlessDescriptorSet.samplers[ dirtyIndex ];
                        imageDescriptorInfo.imageView   = VK_NULL_HANDLE;
                        imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        imageDescriptorInfo.sampler     = pSampler->sampler;
                    }
                    else
                    {
                        // write error texture descriptor
                        imageDescriptorInfo.imageView   = VK_NULL_HANDLE;
                        imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        imageDescriptorInfo.sampler     = pErrorSampler->sampler;
                    }

                    const VkDescriptorImageInfo* pSamplerInfo = samplerInfos.pushBack( imageDescriptorInfo );
                    KEEN_ASSERT( pSamplerInfo != nullptr );

                    VkWriteDescriptorSet write;
                    write.sType             = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.pNext             = nullptr;
                    write.dstSet            = pFrame->bindlessDescriptorSet;
                    write.dstBinding        = 1u;
                    write.dstArrayElement   = dirtyIndex;
                    write.descriptorCount   = 1u;
                    write.descriptorType    = VK_DESCRIPTOR_TYPE_SAMPLER;
                    write.pImageInfo        = pSamplerInfo;
                    write.pBufferInfo       = nullptr;
                    write.pTexelBufferView  = nullptr;
                    writes.pushBack( write );

                    pFrame->bindlessSamplersDirtyMask.clear( dirtyIndex );
                }

                KEEN_ASSERT( pFrame->bindlessSamplersDirtyMask.getSetBitCount() == 0u );

                if( writes.hasElements() )
                {
                    m_pVulkan->vkUpdateDescriptorSets( m_device, writes.getCount32(), writes.getStart(), 0u, nullptr );
                }               
            }
        }

        {
            KEEN_PROFILE_CPU( Vk_ResetCommandPool );

            for( size_t i = 0u; i < pFrame->commandPools.getCount(); ++i )
            {
                VulkanResult result = m_pVulkan->vkResetCommandPool( m_device, pFrame->commandPools[ i ].commandPool, 0u /*VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT*/ );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkResetCommandPool failed with error '%s'\n", result );
                }
            }

            m_pVulkan->vkResetCommandBuffer( pFrame->mainCommandBuffer, 0u );
        }

#if !defined( KEEN_BUILD_MASTER )
        if( vulkan::s_splitSubmission )
        {
            recordAndSubmitCommandsSplit( pFrame );
        }
        else
#endif
        {
            recordAndSubmitCommands( pFrame );
        }

        if( pFrame->swapChainInfo.swapChains.hasElements() )
        {
            KEEN_PROFILE_CPU( Vk_Present );

            KEEN_ASSERT( pFrame->swapChainInfo.swapChains.getSize() == pFrame->swapChainInfo.imageIndices.getSize() );

            VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            presentInfo.waitSemaphoreCount  = 1u;
            presentInfo.pWaitSemaphores     = &pFrame->renderingFinishedSemaphore;
            presentInfo.swapchainCount      = (uint32)pFrame->swapChainInfo.swapChains.getSize();
            presentInfo.pSwapchains         = pFrame->swapChainInfo.swapChains.getStart();
            presentInfo.pImageIndices       = pFrame->swapChainInfo.imageIndices.getStart();

            VulkanResult result = m_pVulkan->vkQueuePresentKHR( m_pSharedData->presentQueue, &presentInfo );
            if( result.hasError() )
            {
                if( result.isDeviceLost() && m_isNonInteractiveApplication )
                {
                    KEEN_ISSUE_ADD( "[graphics] vkQueuePresentKHR failed with error 'VK_ERROR_DEVICE_LOST' in non-interactive mode (we ignore this here)\n" );
                }
                else if( !handleDeviceLost( result ) )
                {
                    KEEN_TRACE_WARNING( "[graphics] vkQueuePresentKHR failed with error '%s'\n", result );
                }
            }
        }
        // present all used swap chains in one present call..

#if KEEN_USING( KEEN_GRAPHICS_RENDERDOC )
        if( pFrame->pRenderDocApi != nullptr )
        {
            renderdoc::endFrameCapture( pFrame->pRenderDocApi );
            pFrame->pRenderDocApi = nullptr;
        }
#endif

#if !defined( KEEN_BUILD_MASTER )
        if( vulkan::s_waitForGpu )
        {
            KEEN_PROFILE_CPU( Vk_WaitForGpu );
            m_pVulkan->vkDeviceWaitIdle( m_device );
        }
#endif
    }

    void VulkanRenderContext::waitForFrame( VulkanFrame* pFrame )
    {
        KEEN_PROFILE_CPU( Vk_waitForFrame );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( pFrame->isRunning )
        {
            TimeSpan timeOut = 10_s;

            const VulkanResult result = m_pVulkan->vkWaitForFences( m_device, 1u, &pFrame->fence, VK_TRUE, timeOut.toNanoseconds() );

            if( result.isOk() )
            {
                pFrame->isRunning = false;
                m_pVulkan->vkResetFences( m_device, 1u, &pFrame->fence );
            }
            else
            {
                if( result.vkResult == VK_TIMEOUT )
                {
                    // bad.. this is probably a driver crash..
                    KEEN_TRACE_ERROR( "[graphics] vkWaitForFences() timed out after %k.. probably a driver crash!\n", timeOut );
                }
                else
                {
                    KEEN_TRACE_ERROR( "[graphics] vkWaitForFences() failed with error '%s'\n", result );
                }

                // :JK: we just handle all of the errors here as a device lost error
                handleDeviceLost( VK_ERROR_DEVICE_LOST );
            }

            // delete all objects from the last time:
            m_pObjects->destroyFrameObjects( pFrame->destroyObjects );
            pFrame->destroyObjects.clear();

            // reset dynamic descriptor pools:
            {
                VulkanDescriptorPool* pDescriptorPool = pFrame->pDescriptorPool;
                while( pDescriptorPool != nullptr )
                {
                    VulkanDescriptorPool* pNextDescriptorPool = pDescriptorPool->pNext;

                    m_pObjects->freeDescriptorPool( pDescriptorPool );

                    pDescriptorPool = pNextDescriptorPool;
                }           
                pFrame->pDescriptorPool = nullptr;
            }
        }
    }

    void VulkanRenderContext::prepareFrame( VulkanFrame* pFrame )
    {
        KEEN_PROFILE_CPU( Vk_prepareFrame );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        // create new descriptor pool:
        pFrame->pDescriptorPool = m_pObjects->createDescriptorPool( VulkanDescriptorPoolType::Dynamic );
        KEEN_ASSERT( pFrame->pDescriptorPool != nullptr );

        // reset command buffer pools: the actual vkResetCommandPool call is done during execution..
        for( size_t workerIndex = 0u; workerIndex < pFrame->commandPools.getSize(); ++workerIndex )
        {
            VulkanCommandPool* pCommandPool = &pFrame->commandPools[ workerIndex ];
            pCommandPool->allocatedCommandBufferCount = 0u;
        }
    }

    bool VulkanRenderContext::resizeCommandPool( VulkanCommandPool* pCommandPool, size_t newSize )
    {
        const size_t oldSize = pCommandPool->commandBuffers.getSize();
        if( newSize <= oldSize )
        {
            return true;
        }

        KEEN_PROFILE_CPU( Vk_resizeCommandPool );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        pCommandPool->commandBuffers.setSize( newSize, VK_NULL_HANDLE );

        const size_t newBufferCount = newSize - oldSize;

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        commandBufferAllocateInfo.commandPool           = pCommandPool->commandPool;
        commandBufferAllocateInfo.level                 = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        commandBufferAllocateInfo.commandBufferCount    = (uint32)newBufferCount;

        VulkanResult result = m_pVulkan->vkAllocateCommandBuffers( m_device, &commandBufferAllocateInfo, pCommandPool->commandBuffers.getStart() + oldSize );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkAllocateCommandBuffers failed with error '%s'\n", result );
            return false;
        }

        return true;
    }

    VkCommandBuffer VulkanRenderContext::allocateCommandBuffer( VulkanFrame* pFrame, size_t workerIndex )
    {
        VulkanCommandPool* pCommandPool = &pFrame->commandPools[ workerIndex ];

        if( pCommandPool->allocatedCommandBufferCount < pCommandPool->commandBuffers.getSize() )
        {
            return pCommandPool->commandBuffers[ pCommandPool->allocatedCommandBufferCount++ ];
        }
        else
        {
            if( !resizeCommandPool( pCommandPool, max< size_t >( 4u, pCommandPool->commandBuffers.getSize() * 2u ) ) )
            {
                return VK_NULL_HANDLE;
            }

            KEEN_ASSERT( pCommandPool->allocatedCommandBufferCount < pCommandPool->commandBuffers.getSize() );
            return pCommandPool->commandBuffers[ pCommandPool->allocatedCommandBufferCount++ ];
        }
    }

    void VulkanRenderContext::traceFrameBreadcrumbs()
    {
#if KEEN_USING( KEEN_TRACE_FEATURE )
        // go through all frames to find the active one:
        for( uint32 frameIndex = 0u; frameIndex < m_frames.getCount(); ++frameIndex )
        {
            VulkanFrame* pFrame = &m_frames[ frameIndex ];
            if( pFrame->isRunning )
            {
#if KEEN_USING( KEEN_GPU_PROFILER )
                const Time elapsedTime = Time::fromFloatSeconds( profiler::getElapsedTimeInMilliseconds( pFrame->submissionTime, profiler::getCurrentCpuTime() ) / 1000.0f );
                KEEN_UNUSED1( elapsedTime );
                KEEN_TRACE_DEBUG( "Frame %d didn't finish after %k!\n", pFrame->id, elapsedTime );
#else
                KEEN_TRACE_DEBUG( "Frame %d didn't finish!\n", pFrame->id );
#endif

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
                if( pFrame->pBreadcrumbBuffer != nullptr )
                {
                    traceBreadcrumbBufferState( pFrame->pBreadcrumbBuffer, m_pVulkan, m_pSharedData->graphicsQueue );
                }
#endif
            }
            else
            {
                KEEN_TRACE_DEBUG( "Frame %d is not running!\n", pFrame->id );
            }
        }
#endif
    }

    void VulkanRenderContext::traceDeviceLossReason()
    {
        if( m_pVulkan->EXT_device_fault )
        {
#if KEEN_USING( KEEN_TRACE_FEATURE )
            VkDeviceFaultCountsEXT faultCounts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT };

            m_pVulkan->vkGetDeviceFaultInfoEXT( m_pVulkan->device, &faultCounts, nullptr );

            TlsDynamicArray< VkDeviceFaultAddressInfoEXT >  deviceFaultAddressInfos( faultCounts.addressInfoCount, false );
            TlsDynamicArray< VkDeviceFaultVendorInfoEXT >   deviceFaultVendorInfos( faultCounts.vendorInfoCount, false );
            TlsDynamicArray< uint8 >                        deviceFaultVendorBinarySize( faultCounts.vendorBinarySize, false );

            VkDeviceFaultInfoEXT faultInfos = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT };
            faultInfos.pAddressInfos        = deviceFaultAddressInfos.getStart();
            faultInfos.pVendorInfos         = deviceFaultVendorInfos.getStart();
            m_pVulkan->vkGetDeviceFaultInfoEXT( m_pVulkan->device, &faultCounts, &faultInfos );

            KEEN_TRACE_INFO( "[DeviceFaultEXT] %s\n", faultInfos.description );
            for( uint32 i = 0; i < faultCounts.addressInfoCount; ++i )
            {
                const VkDeviceFaultAddressInfoEXT* pInfo = &deviceFaultAddressInfos[ i ];
                VkDeviceSize lowerAddress = pInfo->reportedAddress & ~( pInfo->addressPrecision - 1 );
                VkDeviceSize upperAddress = pInfo->reportedAddress | ( pInfo->addressPrecision - 1 );

                KEEN_TRACE_INFO( "[DeviceFaultEXT] Caused by %k in address range [ %k, %k ]\n", pInfo->addressType, lowerAddress, upperAddress );
            }

            for( uint32 i = 0; i < faultCounts.vendorInfoCount; ++i )
            {
                const VkDeviceFaultVendorInfoEXT* pInfo = &deviceFaultVendorInfos[ i ];

                KEEN_TRACE_INFO( "[DeviceFaultEXT] Caused by %s with error code %k and data %k\n", pInfo->description, pInfo->vendorFaultCode, pInfo->vendorFaultData );
            }
#endif
        }
    }

    bool VulkanRenderContext::handleDeviceLost( VulkanResult result )
    {
        if( !vulkan::s_testGpuCrash && !result.isDeviceLost() )
        {
            return false;
        }

        KEEN_TRACE_ERROR( "[graphics] Vulkan device lost...\n" );

        traceFrameBreadcrumbs();

        traceDeviceLossReason();

        if( debug::isRunningInDebugger() )
        {
            /*
                      _ ._  _ , _ ._
                    (_ ' ( `  )_  .__)
                  ( (  (    )   `)  ) _)
                 (__ (_   (_ . _) _) ,__)
                     `~~`\ ' . /`~~`
                          ;   ;
                          /   \
            _____________/_ __ \_____________

                   Your GPU just died...
            */
            KEEN_BREAKPOINT;
        }
        else
        {
            KEEN_TRACE_ERROR( "Stopping process due to unrecoverable GPU crash!\n" );

            os::triggerCrash( "Unrecoverable GPU crash", "Unrecoverable GPU crash" );
        }

        return true;
    }
}
