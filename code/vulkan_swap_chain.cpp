#include "vulkan_swap_chain.hpp"

namespace keen
{

    bool VulkanSwapChain::tryCreate( const VulkanSwapChainParameters& parameters )
    {
        m_pAllocator            = parameters.pAllocator;

        m_pVulkan               = parameters.pVulkan;
        m_physicalDevice        = parameters.physicalDevice;
        m_device                = parameters.device;
        m_surface               = parameters.surface;

        m_pSwapChainWrapper     = parameters.pSwapChainWrapper;

        m_pObjects              = parameters.pObjects;
        m_pSharedData           = parameters.pSharedData;

        m_usageMask             = parameters.backBufferUsageMask;
        m_presentationInterval  = parameters.presentationInterval;
        m_allowTearing          = false;
        m_wasTimedOut           = false;

        m_lastImageTime         = time::getCurrentTime();

        if( !initializeBackBuffer( &m_backBuffers[ 0u ], parameters.backBufferColorFormat ) )
        {
            return false;
        }
        KEEN_TRACE_INFO( "[graphics] swapchain backbuffer format is '%s'\n", m_backBuffers[ 0u ].format );

        if( parameters.alternativeBackBufferColorFormat != PixelFormat::None )
        {
            if( !initializeBackBuffer( &m_backBuffers[ 1u ], parameters.alternativeBackBufferColorFormat ) )
            {
                return false;
            }
            KEEN_TRACE_INFO( "[graphics] swapchain backbuffer alternative format is '%s'\n", m_backBuffers[ 1u ].format );
        }

        uint2 size = parameters.backBufferSize;

        if( size.x < 1u )
        {
            size.x = 1u;
        }
        if( size.y < 1u )
        {
            size.y = 1u;
        }

        Result<void> createResult = createSwapChain( size );
        if( createResult.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not create vulkan swap chain! error='%s'\n", createResult.getError() );
            return false;
        }

        if( !m_imageAvailableSemaphores.tryCreate( m_pAllocator, m_swapChainImages.getCount() ) )
        {
            KEEN_TRACE_ERROR( "Could not allocate memory for imageAvailableSemaphores.\n" );
            return false;
        }

        for( uint32 i = 0; i < m_imageAvailableSemaphores.getCount(); ++i )
        {
            // create semaphores:
            VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VulkanResult result = m_pVulkan->vkCreateSemaphore( m_device, &semaphoreCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &m_imageAvailableSemaphores[ i ] );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateSemaphore failed with error '%s'\n", result );
                return false;
            }
        }
        m_currentImageAvailableSemaphoreIndex = 0;

        return true;
    }

    void VulkanSwapChain::destroy()
    {
        destroySwapChain();

        for( uint32 i = 0; i < m_imageAvailableSemaphores.getCount(); ++i )
        {
            if( m_imageAvailableSemaphores[ i ] != VK_NULL_HANDLE )
            {
                m_pVulkan->vkDestroySemaphore( m_device, m_imageAvailableSemaphores[ i ], m_pSharedData->pVulkanAllocationCallbacks );
            }
        }
        m_imageAvailableSemaphores.destroy();
    }

    bool VulkanSwapChain::resize( uint2 newSize )
    {
        // :JK: we always wait for idle here to avoid destroying frame buffers which are in use still.
        if( m_pVulkan != nullptr )
        {
            m_pVulkan->vkDeviceWaitIdle( m_device );
        }

        // :JK: createSwapChain() can handle an existing swap chain and destroys it automatically
        const Result<void> createResult = createSwapChain( newSize );
        if( createResult.hasError() )
        {
            return false;
        }

        return true;
    }

    void VulkanSwapChain::setPresentationInterval( uint32 interval )
    {
        if( m_presentationInterval != interval )
        {
            if( m_presentationInterval == 0u || interval == 0u )
            {
                // vsync changed: we have to recreate the swap chain:
                m_recreateSwapChain = true;
            }

            m_presentationInterval = interval;
        }
    }

    bool VulkanSwapChain::beginNextImage( VulkanUsedSwapChainInfo* pSwapChainInfo, GraphicsFrameId frameId )
    {
        KEEN_PROFILE_CPU( Vk_acquireNextImage );
        if( m_swapChain == VK_NULL_HANDLE || m_swapChainSize.x == 0u || m_swapChainSize.y == 0u || m_recreateSwapChain )
        {
            m_recreateSwapChain = false; // :JK: only try once
            if( !tryToRecreateSwapChain() )
            {
                return false;
            }
        }

        if( m_currentFrameId == frameId )
        {
            // :JK: only once per frame.. shouldn't happen anyway..
            return false;
        }

        VkSemaphore imageAvailableSemaphore = m_imageAvailableSemaphores[ m_currentImageAvailableSemaphoreIndex ];
        m_currentImageAvailableSemaphoreIndex = ( m_currentImageAvailableSemaphoreIndex + 1 ) % m_imageAvailableSemaphores.getCount32();
        // :JK: the idea is to use a high timeout normally (because especially with amd we saw spikes here of multiple seconds when switching between windowed and full-screen mode)
        // but when we get a timeout once with that time we are in some special state (maybe for example on a headless buildserver) and reduce the timeout until we got an image again
        const Time   timeOut = m_wasTimedOut ? 5_ms : 10_s;    // :JK: this shouldn't be too low because when pressing alt-tab on win32 this can take a while before vkAcquireNextImageKHR returns
        uint32       imageIndex;
        VulkanResult result = m_pVulkan->vkAcquireNextImageKHR( m_device, m_swapChain, timeOut.toNanoseconds(), imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex );

        if( result.hasError() && result.vkResult != VK_SUBOPTIMAL_KHR )
        {
            KEEN_TRACE_ERROR( "[graphics] vkAcquireNextImageKHR failed with error '%s'\n", result );

            if( !m_wasTimedOut && result.vkResult == VK_TIMEOUT )
            {
                KEEN_TRACE_INFO( "[graphics] Got a timeout acquiring the next swap chain image (waiting for %d) - switching mode to not wait as long!\n", timeOut );
                m_wasTimedOut = true;
                return false;
            }

            if( result.isDeviceLost() )
            {
                return false;
            }

            if( !tryToRecreateSwapChain() )
            {
                KEEN_TRACE_WARNING( "[graphics] Could not recreate swap chain!\n" );
                return false;
            }

            // try again:
            result = m_pVulkan->vkAcquireNextImageKHR( m_device, m_swapChain, timeOut.toNanoseconds(), imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex );
            if( result.hasError() )
            {
                return false;
            }
        }

        if( m_wasTimedOut && result.vkResult != VK_TIMEOUT )
        {
            KEEN_TRACE_INFO( "[graphics] Switching back to high swap chain time out value!\n" );
            m_wasTimedOut = false;
        }

        for( size_t i = 0u; i < GraphicsLimits_MaxRenderTargetPixelFormatCount; ++i )
        {
            if( m_backBuffers[ i ].format != PixelFormat::None )
            {
                updateBackBufferImage( &m_backBuffers[ i ], imageIndex );
            }
        }

        if( !pSwapChainInfo->swapChains.tryPushBack( m_swapChain ) ||
            !pSwapChainInfo->imageAvailableSemaphores.tryPushBack( imageAvailableSemaphore ) ||
            !pSwapChainInfo->waitStageMasks.tryPushBack( VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT ) ||
            !pSwapChainInfo->imageIndices.tryPushBack( imageIndex ) )
        {
            return false;
        }

        m_currentFrameId    = frameId;
        m_currentImageIndex = imageIndex;
        m_lastImageTime     = time::getCurrentTime();

        return true;
    }

    bool VulkanSwapChain::prepareForRendering( VkCommandBuffer commandBuffer )
    {
        if( m_swapChain == VK_NULL_HANDLE )
        {
            return false;
        }
        // change the back buffer image from "present read" to "draw write"
        VkImageSubresourceRange imageSubresourceRange;
        imageSubresourceRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
        imageSubresourceRange.baseMipLevel            = 0u;
        imageSubresourceRange.levelCount              = 1u;
        imageSubresourceRange.baseArrayLayer          = 0u;
        imageSubresourceRange.layerCount              = 1u;

        VkImageMemoryBarrier barrierFromPresentToDraw = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrierFromPresentToDraw.srcAccessMask        = 0u;
        barrierFromPresentToDraw.oldLayout            = VK_IMAGE_LAYOUT_UNDEFINED;
        barrierFromPresentToDraw.dstAccessMask        = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrierFromPresentToDraw.newLayout            = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrierFromPresentToDraw.srcQueueFamilyIndex  = m_pSharedData->presentQueueFamilyIndex;
        barrierFromPresentToDraw.dstQueueFamilyIndex  = m_pSharedData->graphicsQueueFamilyIndex;
        barrierFromPresentToDraw.image                = m_swapChainImages[ m_currentImageIndex ];
        barrierFromPresentToDraw.subresourceRange       = imageSubresourceRange;
        m_pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierFromPresentToDraw );        

        return true;
    }

    void VulkanSwapChain::prepareForPresent( VkCommandBuffer commandBuffer )
    {
        KEEN_ASSERT( m_swapChain != VK_NULL_HANDLE );

        // transition the back buffer image to present state:
        VkImageSubresourceRange imageSubresourceRange;
        imageSubresourceRange.aspectMask        = VK_IMAGE_ASPECT_COLOR_BIT;
        imageSubresourceRange.baseMipLevel      = 0u;
        imageSubresourceRange.levelCount        = 1u;
        imageSubresourceRange.baseArrayLayer    = 0u;
        imageSubresourceRange.layerCount        = 1u;

        VkImageMemoryBarrier barrierFromDrawToPresent = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrierFromDrawToPresent.srcAccessMask          = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrierFromDrawToPresent.dstAccessMask          = VK_ACCESS_NONE;
        barrierFromDrawToPresent.oldLayout              = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrierFromDrawToPresent.newLayout              = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrierFromDrawToPresent.srcQueueFamilyIndex    = m_pSharedData->graphicsQueueFamilyIndex;
        barrierFromDrawToPresent.dstQueueFamilyIndex    = m_pSharedData->presentQueueFamilyIndex;
        barrierFromDrawToPresent.image                  = m_swapChainImages[ m_currentImageIndex ];
        barrierFromDrawToPresent.subresourceRange       = imageSubresourceRange;
        m_pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierFromDrawToPresent );
    }

    const VulkanTexture* VulkanSwapChain::getBackBufferTexture( size_t colorFormatIndex )
    {
        if( m_backBuffers[ colorFormatIndex ].format == PixelFormat::None )
        {
            return nullptr;
        }

        return &m_backBuffers[ colorFormatIndex ].colorTarget;
    }

    PixelFormat VulkanSwapChain::findBestSurfaceFormat( PixelFormat requestedFormat )
    {
        TlsStackAllocatorScope stackAllocator;

        // Get the list of VkFormat's that are supported:
        uint32 formatCount;
        VulkanResult result = m_pVulkan->vkGetPhysicalDeviceSurfaceFormatsKHR( m_physicalDevice, m_surface, &formatCount, nullptr );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfaceFormatsKHR#1 failed with error '%s'\n", result );
            return PixelFormat::None;
        }
        Array<VkSurfaceFormatKHR> surfaceFormats;
        if( !surfaceFormats.tryCreate( &stackAllocator, formatCount ) )
        {
            KEEN_TRACE_ERROR( "[graphics] out of memory!\n" );
            return PixelFormat::None;
        }
        result = m_pVulkan->vkGetPhysicalDeviceSurfaceFormatsKHR( m_physicalDevice, m_surface, &formatCount, surfaceFormats.getStart() );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfaceFormatsKHR#2 failed with error '%s'\n", result );
            return PixelFormat::None;
        }

        KEEN_TRACE_INFO( "[graphics] Supported vulkan device formats:\n" );
        for( size_t i = 0u; i < surfaceFormats.getSize(); ++i )
        {
            const VkSurfaceFormatKHR& surfaceFormat = surfaceFormats[ i ];
            KEEN_TRACE_INFO( "format %d: %s (colorSpace:%s)\n", i, vulkan::getVkFormatString( surfaceFormat.format ), vulkan::getVkColorSpaceString( surfaceFormat.colorSpace ) );
        }

        // this is obsolete behavior which was removed with Vulkan 1.1.111
        if( surfaceFormats.getSize() == 1u && surfaceFormats[ 0u ].format == VK_FORMAT_UNDEFINED )
        {
            return requestedFormat;
        }

        for( size_t i = 0u; i < surfaceFormats.getSize(); ++i )
        {
            const VkSurfaceFormatKHR& surfaceFormat = surfaceFormats[ i ];
            if( surfaceFormat.format == vulkan::getVulkanFormat( requestedFormat ) )
            {
                return requestedFormat;
            }
        }

        // find a matching format:
        for( size_t i = 0u; i < surfaceFormats.getSize(); ++i )
        {
            const VkSurfaceFormatKHR& surfaceFormat = surfaceFormats[ i ];

            if( image::isGammaPixelFormat( requestedFormat ) )
            {
                if( surfaceFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
                {
                    continue;
                }
            }

            if( !vulkan::isCompatibleSurfaceFormat( surfaceFormat.format, requestedFormat ) )
            {
                continue;
            }

            const PixelFormat pixelFormat = vulkan::getPixelFormat( surfaceFormat.format );
            if( pixelFormat != PixelFormat::None )
            {
                return pixelFormat;
            }
        }

        return PixelFormat::None;
    }

    struct VulkanSwapChainCreateInfo
    {
        VkSurfaceFormatKHR          surfaceFormat;
        VkSwapchainCreateInfoKHR    swapChainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        VkImageFormatListCreateInfo formatListCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO };        
        VkFormat                    swapChainFormats[ 2u ];
    };

    struct VulkanSwapChainCreationParameters
    {
        VulkanApi*                  pVulkan = nullptr;
        VkPhysicalDevice            physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR                surface = VK_NULL_HANDLE;
        VkImageUsageFlags           usageMask = 0u;
        uint                        presentationInterval = 1u;
        bool                        allowTearing = false;
        uint2                       requestedSize = { 0u, 0u };
        VkFormat                    format = VK_FORMAT_UNDEFINED;
        VkFormat                    alternateFormat = VK_FORMAT_UNDEFINED;
        VkSwapchainKHR              oldSwapChain = VK_NULL_HANDLE;
    };

    static Result<void> prepareSwapChainCreation( VulkanSwapChainCreateInfo* pInfo, MemoryAllocator* pTempAllocator, const VulkanSwapChainCreationParameters& parameters )
    {
        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        VulkanResult result = parameters.pVulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR( parameters.physicalDevice, parameters.surface, &surfaceCapabilities );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with error '%s'\n", result );
            return result.getErrorId();
        }

        if( !isBitmaskSet( surfaceCapabilities.supportedUsageFlags, parameters.usageMask ) )
        {
            KEEN_TRACE_ERROR( "[graphics] vulkan surface does not support all required usage flags\n" );
            return ErrorId_NotSupported;
        }

        uint32 presentModeCount;
        result = parameters.pVulkan->vkGetPhysicalDeviceSurfacePresentModesKHR( parameters.physicalDevice, parameters.surface, &presentModeCount, nullptr );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfacePresentModesKHR#1 failed with error '%s'\n", result );
            return result.getErrorId();
        }
        if( presentModeCount == 0u )
        {
            KEEN_TRACE_ERROR( "[graphics] No present mode found!\n" );
            return ErrorId_NotSupported;
        }

        Array<VkPresentModeKHR, 16u> presentModes( pTempAllocator, presentModeCount );
        result = parameters.pVulkan->vkGetPhysicalDeviceSurfacePresentModesKHR( parameters.physicalDevice, parameters.surface, &presentModeCount, presentModes.getStart() );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfacePresentModesKHR#2 failed with error '%s'\n", result );
            return result.getErrorId();
        }

        // Set of images defined in a swap chain may not always be available for application to render to:
        // One may be displayed and one may wait in a queue to be presented
        // If application wants to use more images at the same time it must ask for more images
        uint32 desiredImageCount = 3u;
        if( surfaceCapabilities.maxImageCount > 0u && desiredImageCount > surfaceCapabilities.maxImageCount )
        {
            desiredImageCount = surfaceCapabilities.maxImageCount;
        }
        if( desiredImageCount < surfaceCapabilities.minImageCount )
        {
            desiredImageCount = surfaceCapabilities.minImageCount;
        }

        VkExtent2D desiredExtent;
        {
            uint2 swapChainExtent = parameters.requestedSize;
            desiredExtent.width     = clamp( swapChainExtent.x, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width );
            desiredExtent.height    = clamp( swapChainExtent.y, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height );
        }

        if( desiredExtent.width == 0u || desiredExtent.height == 0u )
        {
            KEEN_TRACE_ERROR( "[graphics] desired swap chain size is 0! (%dx%d)\n", desiredExtent.width, desiredExtent.height );
            return ErrorId_NotSupported;
        }

        VkSurfaceTransformFlagBitsKHR desiredTransform = surfaceCapabilities.currentTransform;
        if( surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR )
        {
            desiredTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }

        // find best present mode:
        VkPresentModeKHR desiredPresentMode = presentModes[ 0u ];

        if( parameters.presentationInterval > 0u )
        {
            if( parameters.allowTearing && containsValue( presentModes, VK_PRESENT_MODE_FIFO_RELAXED_KHR  ) )
            {
                desiredPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            }
            else if( containsValue( presentModes, VK_PRESENT_MODE_FIFO_KHR ) )
            {
                desiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;
            }
            else if( containsValue( presentModes, VK_PRESENT_MODE_MAILBOX_KHR ) )
            {
                desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
        }
        else
        {
            if( containsValue( presentModes, VK_PRESENT_MODE_IMMEDIATE_KHR ) )
            {
                desiredPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            else if( containsValue( presentModes, VK_PRESENT_MODE_MAILBOX_KHR ) )
            {
                desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
            else if( containsValue( presentModes, VK_PRESENT_MODE_FIFO_KHR ) )
            {
                desiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;
            }
        }
    
        pInfo->surfaceFormat.format     = parameters.format;
        pInfo->surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        
        pInfo->swapChainCreateInfo.surface                  = parameters.surface;
        pInfo->swapChainCreateInfo.minImageCount            = desiredImageCount;
        pInfo->swapChainCreateInfo.imageFormat              = pInfo->surfaceFormat.format;
        pInfo->swapChainCreateInfo.imageColorSpace          = pInfo->surfaceFormat.colorSpace;
        pInfo->swapChainCreateInfo.imageExtent              = desiredExtent;
        pInfo->swapChainCreateInfo.imageArrayLayers         = 1u;
        pInfo->swapChainCreateInfo.imageUsage               = parameters.usageMask;
        pInfo->swapChainCreateInfo.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
        pInfo->swapChainCreateInfo.queueFamilyIndexCount    = 0u;
        pInfo->swapChainCreateInfo.pQueueFamilyIndices      = nullptr;
        pInfo->swapChainCreateInfo.preTransform             = desiredTransform;

        if( surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR )
        {
            pInfo->swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }
        else if( surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR )
        {
            pInfo->swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
        else
        {
            KEEN_TRACE_ERROR( "[graphics] No Supported composite alpha mode found!\n" );
            return ErrorId_NotSupported;
        }

        pInfo->swapChainCreateInfo.presentMode      = desiredPresentMode;
        pInfo->swapChainCreateInfo.clipped          = VK_TRUE;
        pInfo->swapChainCreateInfo.oldSwapchain     = parameters.oldSwapChain;

        if( parameters.alternateFormat != VK_FORMAT_UNDEFINED )
        {
            pInfo->swapChainFormats[ 0u ] = parameters.format;
            pInfo->swapChainFormats[ 1u ] = parameters.alternateFormat;

            pInfo->formatListCreateInfo.pViewFormats    = pInfo->swapChainFormats;
            pInfo->formatListCreateInfo.viewFormatCount = 2u;

            pInfo->swapChainCreateInfo.flags    |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
            pInfo->swapChainCreateInfo.pNext    = &pInfo->formatListCreateInfo;
        }

        return ErrorId_Ok;
    }

    Result<void> VulkanSwapChain::createSwapChain( uint2 size )
    {
        KEEN_ASSERT( m_device != VK_NULL_HANDLE );

        if( size.x == 0u || size.y == 0u )
        {
            // empty swap chain is not supported: destroy the existing one + store the size
            KEEN_TRACE_ERROR( "[graphics] cannot create swapchain with size 0\n" );
            destroySwapChainObjects();
            m_swapChainSize = { 0u, 0u };
            return ErrorId_NotSupported;
        }

        TlsStackAllocatorScope stackAllocator;

        VulkanSwapChainCreationParameters swapChainCreationParameters;
        swapChainCreationParameters.pVulkan                 = m_pVulkan;
        swapChainCreationParameters.physicalDevice          = m_physicalDevice;
        swapChainCreationParameters.surface                 = m_surface;
        swapChainCreationParameters.usageMask               = vulkan::getImageUsageMask( m_usageMask );
        swapChainCreationParameters.presentationInterval    = m_presentationInterval;
        swapChainCreationParameters.allowTearing            = m_allowTearing;
        swapChainCreationParameters.requestedSize           = size;
        swapChainCreationParameters.format                  = vulkan::getVulkanFormat( m_backBuffers[ 0u ].format );
        swapChainCreationParameters.alternateFormat         = vulkan::getVulkanFormat( m_backBuffers[ 1u ].format );
        swapChainCreationParameters.oldSwapChain            = m_swapChain;

        VulkanSwapChainCreateInfo createInfo;
        const Result<void> prepareResult = prepareSwapChainCreation( &createInfo, &stackAllocator, swapChainCreationParameters );
        if( prepareResult.hasError() )
        {
            destroySwapChainObjects();
            return prepareResult.getError();
        }

        VkSwapchainKHR newSwapChain;
        VulkanResult result = m_pVulkan->vkCreateSwapchainKHR( m_device, &createInfo.swapChainCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &newSwapChain );

        // always destroy the old swap chain here
        destroySwapChainObjects();

        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateSwapchainKHR failed with error '%s'\n", result );

            // if it was created with the old swap chain it sometimes fails on some gpus to create the new swap chain
            // with error VK_ERROR_NATIVE_WINDOW_IN_USE_KHR (https://git.keengames.com/keen/holistic/issues/32574)
            if( result.vkResult == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR &&
                createInfo.swapChainCreateInfo.oldSwapchain != VK_NULL_HANDLE )
            {
                // try again without old swap chain:
                createInfo.swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
                
                result = m_pVulkan->vkCreateSwapchainKHR( m_device, &createInfo.swapChainCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &newSwapChain );

                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkCreateSwapchainKHR failed again with error '%s'\n", result );
                    destroySwapChainObjects();
                    return result.getErrorId();
                }

                // fall through to success path..
            }
            else
            {
                return result.getErrorId();
            }
        }

        m_swapChain     = newSwapChain;
        m_swapChainSize = { createInfo.swapChainCreateInfo.imageExtent.width, createInfo.swapChainCreateInfo.imageExtent.height };

        KEEN_TRACE_INFO( "[graphics] Vulkan swap chain created size:%dx%d desired present mode:%k\n", m_swapChainSize.x, m_swapChainSize.y, vulkan::getVkPresentModeString( createInfo.swapChainCreateInfo.presentMode ) );

        // get the swap chain images:
        uint32 imageCount = 0u;
        result = m_pVulkan->vkGetSwapchainImagesKHR( m_device, m_swapChain, &imageCount, nullptr );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetSwapchainImagesKHR#1 failed with error '%s'\n", result );
            return result.getErrorId();
        }
        KEEN_ASSERT( imageCount > 0u );
        if( !m_swapChainImages.tryCreate( m_pAllocator, imageCount ) )
        {
            return ErrorId_OutOfMemory;
        }

        result = m_pVulkan->vkGetSwapchainImagesKHR( m_device, m_swapChain, &imageCount, m_swapChainImages.getStart() );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetSwapchainImagesKHR#2 failed with error '%s'\n", result );
            return result.getErrorId();
        }

#if KEEN_USING( KEEN_VULKAN_OBJECT_NAMES )
        for( uint32 i = 0u; i < m_swapChainImages.getCount32(); ++i )
        {
            vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)m_swapChainImages[ i ], VK_OBJECT_TYPE_IMAGE, DebugName::createFormatted( "SwapchainImage%d", i ) );
        }
#endif

        // create the image views for the color images:
        for( size_t i = 0u; i < GraphicsLimits_MaxRenderTargetPixelFormatCount ; ++i )
        {
            if( m_backBuffers[ i ].format == PixelFormat::None )
            {
                continue;
            }
            Result<void> backBufferCreationResult = createBackBuffer( &m_backBuffers[ i ] );
            if( backBufferCreationResult.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] Could not create swap chain back buffer. error='%s'\n", backBufferCreationResult.error );
                return backBufferCreationResult.error;
            }
        }

        m_wasTimedOut = false;

        return ErrorId_Ok;
    }

    void VulkanSwapChain::destroySwapChain()
    {
        // ;JK: we always wait for idle here to avoid destroying frame buffers which are in use still.
        if( m_pVulkan != nullptr )
        {
            m_pVulkan->vkDeviceWaitIdle( m_device );
        }

        destroySwapChainObjects();
    }

    bool VulkanSwapChain::tryToRecreateSwapChain()
    {
        const uint2 oldSize = getSize();
        const bool wasValidBefore = isValid();

        if( wasValidBefore )
        {
            KEEN_TRACE_INFO( "[graphics] Trying to recreate vulkan swap chain.\n" );
        }

        VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
        VulkanResult result = m_pVulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR( m_physicalDevice, m_surface, &surfaceCapabilities );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with error '%s'\n", result );
            return false;
        }

        uint2 size;
        size.x = clamp( oldSize.x, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width );
        size.y = clamp( oldSize.y, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height );

        if( size != oldSize )
        {
            return resize( size );
        }

        // the size is the same - so we try to fully recreate the swap chain because it might be broken for some other reason:
        destroySwapChain();

        if( size.x == 0u || size.y == 0u )
        {
            return false;
        }

        const Result<void> createResult = createSwapChain( size );
        if( createResult.hasError() )
        {
            if( wasValidBefore )
            {
                KEEN_TRACE_ERROR( "[graphics] Unable to recreate swap chain. Error: %s\n", createResult.getError() );
            }
            return false;
        }
        return true;
    }

    bool VulkanSwapChain::initializeBackBuffer( BackBuffer* pBackBuffer, PixelFormat desiredColorFormat )
    {
        pBackBuffer->desiredColorFormat = desiredColorFormat;

        // create fake back buffer:
        graphics::initializeInternalDeviceObject( &pBackBuffer->colorTarget, GraphicsDeviceObjectType::Texture, "BackBufferColorTarget"_debug );
        pBackBuffer->colorTarget.depth          = 1u;
        pBackBuffer->colorTarget.levelCount     = 1u;
        pBackBuffer->colorTarget.layerCount     = 1u;
        pBackBuffer->colorTarget.sampleCount    = 1u;
        pBackBuffer->colorTarget.usageMask      = m_usageMask;
        pBackBuffer->colorTarget.type           = TextureType::Texture2D;

        // find a matching surface format:
        pBackBuffer->format = findBestSurfaceFormat( pBackBuffer->desiredColorFormat );
        if( pBackBuffer->format == PixelFormat::None )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not find compatible vulkan surface format!\n" );
            return false;
        }

        return true;
    }

    Result<void> VulkanSwapChain::createBackBuffer( BackBuffer* pBackBuffer )
    {
        VkSurfaceFormatKHR surfaceFormat;
        surfaceFormat.format        = vulkan::getVulkanFormat( pBackBuffer->format );
        surfaceFormat.colorSpace    = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        // set back buffer target size
        pBackBuffer->colorTarget.width  = m_swapChainSize.x;
        pBackBuffer->colorTarget.height = m_swapChainSize.y;
        pBackBuffer->colorTarget.format = pBackBuffer->format;

        const size_t imageCount = m_swapChainImages.getSize();

        if( !pBackBuffer->swapChainImageViews.tryCreate( m_pAllocator, imageCount ) )
        {
            return ErrorId_OutOfMemory;
        }
        fill( &pBackBuffer->swapChainImageViews, VK_NULL_HANDLE );

        for( size_t imageIndex = 0u; imageIndex < imageCount; ++imageIndex )
        {
            VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            imageViewCreateInfo.image                           = m_swapChainImages[ imageIndex ];
            imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format                          = surfaceFormat.format;
            imageViewCreateInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewCreateInfo.subresourceRange.baseMipLevel   = 0u;
            imageViewCreateInfo.subresourceRange.levelCount     = 1u;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = 0u;
            imageViewCreateInfo.subresourceRange.layerCount     = 1u;

            VulkanResult result = m_pVulkan->vkCreateImageView( m_device, &imageViewCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pBackBuffer->swapChainImageViews[ imageIndex ] );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateImageView failed with error '%s'\n", result );
                return result.getErrorId();
            }
        }

        return ErrorId_Ok;
    }

    void VulkanSwapChain::destroyBackBuffer( BackBuffer* pBackBuffer )
    {
        if( m_pVulkan != nullptr )
        {
            for( size_t imageIndex = 0u; imageIndex < pBackBuffer->swapChainImageViews.getSize(); ++imageIndex )
            {
                if( pBackBuffer->swapChainImageViews[ imageIndex ] != VK_NULL_HANDLE )
                {
                    m_pVulkan->vkDestroyImageView( m_device, pBackBuffer->swapChainImageViews[ imageIndex ], m_pSharedData->pVulkanAllocationCallbacks );
                }
            }
        }
        pBackBuffer->swapChainImageViews.destroy();
        graphics::shutdownInternalDeviceObject( &pBackBuffer->colorTarget );
    }

    void VulkanSwapChain::updateBackBufferImage( BackBuffer* pBackBuffer, uint32 imageIndex )
    {
        VkImageSubresourceRange imageSubresourceRange;
        imageSubresourceRange.aspectMask        = VK_IMAGE_ASPECT_COLOR_BIT;
        imageSubresourceRange.baseMipLevel      = 0u;
        imageSubresourceRange.levelCount        = 1u;
        imageSubresourceRange.baseArrayLayer    = 0u;
        imageSubresourceRange.layerCount        = 1u;

        pBackBuffer->colorTarget.image          = m_swapChainImages[ imageIndex ];
        pBackBuffer->colorTarget.imageView      = pBackBuffer->swapChainImageViews[ imageIndex ];
    }

    void VulkanSwapChain::destroySwapChainObjects()
    {
        for( size_t i = 0u; i < GraphicsLimits_MaxRenderTargetPixelFormatCount ; ++i )
        {
            if( m_backBuffers[ i ].format == PixelFormat::None )
            {
                continue;
            }
            destroyBackBuffer( &m_backBuffers[ i ] );
        }

        m_swapChainImages.destroy();

        m_swapChainSize = { 0u, 0u };

        if( m_pVulkan != nullptr )
        {
            if( m_swapChain != VK_NULL_HANDLE )
            {
                m_pVulkan->vkDestroySwapchainKHR( m_device, m_swapChain, m_pSharedData->pVulkanAllocationCallbacks );
                m_swapChain = VK_NULL_HANDLE;
            }
        }
    }

}
