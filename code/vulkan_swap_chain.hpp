#ifndef KEEN_VULKAN_SWAP_CHAIN_HPP_INCLUDED
#define KEEN_VULKAN_SWAP_CHAIN_HPP_INCLUDED

#include "keen/base/base_types.hpp"
#include "keen/base/array.hpp"

#include "vulkan_types.hpp"

namespace keen
{
    class VulkanGraphicsDevice;

    struct VulkanSwapChainParameters
    {
        MemoryAllocator*            pAllocator;
        
        VulkanSwapChainWrapper*     pSwapChainWrapper;

        VulkanApi*                  pVulkan;
        VkPhysicalDevice            physicalDevice;
        VkDevice                    device;
        VkSurfaceKHR                surface;

        VulkanGraphicsObjects*      pObjects;
        VulkanSharedData*           pSharedData;

        uint2                       backBufferSize;
        PixelFormat                 backBufferColorFormat;
        PixelFormat                 alternativeBackBufferColorFormat;
        GraphicsTextureUsageMask    backBufferUsageMask;

        uint32                      presentationInterval;
    };

    // struct + free functions plz.
    class VulkanSwapChain
    {
    public:

        bool                        tryCreate( const VulkanSwapChainParameters& parameters );
        void                        destroy();

        bool                        resize( uint2 newSize );
        bool                        isValid() const { return m_swapChain != VK_NULL_HANDLE; }

        void                        setPresentationInterval( uint32 interval );

        GraphicsFrameId             getFrameId() const { return m_currentFrameId; }
        bool                        beginNextImage( VulkanUsedSwapChainInfo* pSwapChainInfo, GraphicsFrameId frameId );

        bool                        prepareForRendering( VkCommandBuffer commandBuffer );
        void                        prepareForPresent( VkCommandBuffer commandBuffer );

        VkSurfaceKHR                getSurface() { return m_surface; }

        uint2                       getSize() const { return m_swapChainSize; }
        const VulkanTexture*        getBackBufferTexture( size_t colorFormatIndex );

    private:
        struct BackBuffer
        {
            Array<VkImageView>      swapChainImageViews;
            VulkanTexture           colorTarget;
            PixelFormat             desiredColorFormat; // the desired color format for the back buffer..
            PixelFormat             format;
        };

        Result<void>                createSwapChain( uint2 size );
        void                        destroySwapChain();
        bool                        tryToRecreateSwapChain();

        void                        destroySwapChainObjects();

        PixelFormat                 findBestSurfaceFormat( PixelFormat requestedFormat );
        bool                        initializeBackBuffer( BackBuffer* pBackBuffer, PixelFormat desiredColorFormat );
        Result<void>                createBackBuffer( BackBuffer* pBackBuffer );
        void                        destroyBackBuffer( BackBuffer* pBackBuffer );
        void                        updateBackBufferImage( BackBuffer* pBackBuffer, uint32 imageIndex );

        MemoryAllocator*            m_pAllocator;

        VulkanApi*                  m_pVulkan;
        VkPhysicalDevice            m_physicalDevice;
        VkDevice                    m_device;
        VkSurfaceKHR                m_surface;

        VulkanSwapChainWrapper*     m_pSwapChainWrapper;

        VulkanGraphicsObjects*      m_pObjects;
        VulkanSharedData*           m_pSharedData;

        VkSwapchainKHR              m_swapChain;
        Array<VkImage>              m_swapChainImages;
        uint2                       m_swapChainSize;

        GraphicsFrameId             m_currentFrameId;
        uint32                      m_currentImageIndex;

        BackBuffer                  m_backBuffers[ GraphicsLimits_MaxRenderTargetPixelFormatCount ];

        Array<VkSemaphore>          m_imageAvailableSemaphores;
        uint32                      m_currentImageAvailableSemaphoreIndex;

        GraphicsTextureUsageMask    m_usageMask;
        uint32                      m_presentationInterval;
        bool                        m_recreateSwapChain;
        bool                        m_allowTearing;
        Time                        m_lastImageTime;
        bool                        m_wasTimedOut;
    };
}

#endif
