#ifndef KEEN_VULKAN_BREADCRUMBS_HPP_INCLUDED
#define KEEN_VULKAN_BREADCRUMBS_HPP_INCLUDED

#include "keen/base/platform.hpp"
#include "keen/base/array_view.hpp"
#include "keen/base/chunked_zone_allocator.hpp"
#include "vulkan_api.hpp"

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )

namespace keen
{

    class MemoryAllocator;

    struct VulkanBreadcrumbZone
    {
        Optional<uint32>    parentZoneIndex;
        StringView          name;
    };

    enum class VulkanBreadcrumbType
    {
        Dispatch,
        DispatchIndirect,
        Draw,
        DrawIndirectCount,
        DrawIndirect,
        DrawIndexedIndirectCount,
        DrawIndexedIndirect,
        DrawIndexed,
        FillBuffer,
        CopyBuffer,
    };

    struct VulkanBreadcrumb
    {
        uint32                  zoneIndex;
        VulkanBreadcrumbType    type;
        DebugName               commandInfo;        // depends on the command type (pipeline name, buffer name, ...)
    };

    enum class VulkanBreadcrumbTechnique
    {
        None,
        Manual,
        AmdMarker,
        NvCheckpoint
    };

    struct VulkanBreadcrumbBuffer
    {
        // breadcrumb state
        ChunkedZoneAllocator                allocator;
        DynamicArray<VulkanBreadcrumbZone>  zones;
        DynamicArray<uint32>                zoneStack;
        DynamicArray<VulkanBreadcrumb>      breadcrumbs;
        Optional<uint32>                    currentBreadcrumbIndex;
        GraphicsFrameId                     frameId;
        uint16                              markerToken;

        VulkanBreadcrumbTechnique           technique;
        // amd only:
        VkDeviceMemory                      bufferMemory;
        ArrayView<uint32>                   mappedData;
        VkBuffer                            buffer;

        bool                                recordingEnabled = true;
    };

    VulkanBreadcrumbBuffer*     createVulkanBreadcrumbBuffer( MemoryAllocator* pAllocator, VulkanApi* pVulkan, VkDevice device );
    void                        destroyVulkanBreadcrumbBuffer( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, MemoryAllocator* pAllocator, VulkanApi* pVulkan, VkDevice device );

    void                        traceBreadcrumbBufferState( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkQueue queue );

    void                        beginBreadcrumbFrame( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, GraphicsFrameId frameId );
    void                        endBreadcrumbFrame( VulkanBreadcrumbBuffer* pBreadcrumbBuffer );

    void                        pushBreadcrumbZone( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, StringView name );
    void                        popBreadcrumbZone( VulkanBreadcrumbBuffer* pBreadcrumbBuffer );

    void                        toggleBreadcrumbRecording( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, bool enable );
    void                        breadcrumbRenderpassHint( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, bool enterBreadcrumb );

    bool                        beginBreadcrumb( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, VulkanBreadcrumbType type, DebugName commandInfo );
    void                        endBreadcrumb( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer );

    class VulkanBreadcrumbScope
    {
    public:
        VulkanBreadcrumbScope( VulkanBreadcrumbBuffer* pBreadcrumbBuffer, VulkanApi* pVulkan, VkCommandBuffer commandBuffer, VulkanBreadcrumbType type, DebugName commandInfo )
        {
            if( beginBreadcrumb( pBreadcrumbBuffer, pVulkan, commandBuffer, type, commandInfo ) )
            {
                m_pBreadcrumbBuffer = pBreadcrumbBuffer;
                m_pVulkan           = pVulkan;
                m_commandBuffer     = commandBuffer;
            }
        }

        ~VulkanBreadcrumbScope()
        {
            if( m_pBreadcrumbBuffer != nullptr )
            {
                endBreadcrumb( m_pBreadcrumbBuffer, m_pVulkan, m_commandBuffer );
            }
        }

    private:
        VulkanBreadcrumbBuffer* m_pBreadcrumbBuffer = nullptr;
        VulkanApi*              m_pVulkan = nullptr;
        VkCommandBuffer         m_commandBuffer = VK_NULL_HANDLE;
    };

}

#endif

#endif
