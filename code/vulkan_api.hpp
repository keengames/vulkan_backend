#ifndef KEEN_VULKAN_API_HPP_INCLUDED
#define KEEN_VULKAN_API_HPP_INCLUDED

#include "keen/base/base_types.hpp"
#include "keen/base/pixel_format.hpp"
#include "keen/base/dynamic_array.hpp"
#include "keen/base/set.hpp"
#include "keen/geometry/vertex_attribute.hpp"

#include "keen/graphics/graphics_types.hpp"

#if defined( KEEN_PLATFORM_WIN32 )
#   define VK_USE_PLATFORM_WIN32_KHR
#elif defined( KEEN_PLATFORM_LINUX )
#   define VK_USE_PLATFORM_XLIB_KHR
#elif defined( KEEN_PLATFORM_NX )
#else
#   error "Platform not supported"
#endif

#define KEEN_VULKAN_BETA_FEATURES               KEEN_OFF

#define KEEN_VULKAN_SERIALIZE_COMMAND_EXECUTION KEEN_OFF

#define VK_NO_PROTOTYPES

#if KEEN_USING( KEEN_VULKAN_BETA_FEATURES )
#   define VK_ENABLE_BETA_EXTENSIONS
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
#   include <vulkan/vk_platform.h>
#   include <vulkan/vulkan_core.h>

typedef unsigned long DWORD;
typedef const wchar_t* LPCWSTR;
typedef void* HANDLE;
typedef struct HINSTANCE__* HINSTANCE;
typedef struct HWND__* HWND;
typedef struct HMONITOR__* HMONITOR;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

#   include <vulkan/vulkan_win32.h>

#   ifdef VK_ENABLE_BETA_EXTENSIONS
#       include <vulkan/vulkan_beta.h>
#   endif
#else
#   include <vulkan/vulkan.h>
#   include <vulkan/vulkan_wayland.h>
#   include "keen/base/linux/linux_x11.hpp"
#endif

#define KEEN_VULKAN_OBJECT_NAMES    KEEN_ON_IF_ALL( KEEN_DEBUG_NAME, KEEN_OFF_IN_MASTER_BUILD )
#define KEEN_VULKAN_DEBUG_LABELS    KEEN_ON_IF_ALL( KEEN_DEBUG_NAME, KEEN_OFF_IN_MASTER_BUILD )
#define KEEN_VULKAN_CHECKPOINTS     KEEN_ON_IF_ALL( KEEN_DEBUG_NAME, KEEN_OFF_IN_MASTER_BUILD )

namespace keen
{

#if defined( KEEN_64_BIT_POINTERS )
    typedef void* VkObjectHandle;
#else
    typedef uint64 VkObjectHandle;
#endif

    struct VulkanApi
    {
        VkPhysicalDevice                                    physicalDevice;
        VkDevice                                            device;

        void*                                               pLibrary;
        PFN_vkGetInstanceProcAddr                           vkGetInstanceProcAddr;

        PFN_vkCreateInstance                                vkCreateInstance;
        PFN_vkDestroyInstance                               vkDestroyInstance;
        PFN_vkEnumeratePhysicalDevices                      vkEnumeratePhysicalDevices;
        PFN_vkGetPhysicalDeviceFeatures2                    vkGetPhysicalDeviceFeatures2;
        PFN_vkGetPhysicalDeviceFormatProperties             vkGetPhysicalDeviceFormatProperties;
        PFN_vkGetPhysicalDeviceImageFormatProperties        vkGetPhysicalDeviceImageFormatProperties;
        PFN_vkGetPhysicalDeviceProperties                   vkGetPhysicalDeviceProperties; // for VMA
        PFN_vkGetPhysicalDeviceProperties2                  vkGetPhysicalDeviceProperties2;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties        vkGetPhysicalDeviceQueueFamilyProperties;
        PFN_vkGetPhysicalDeviceMemoryProperties             vkGetPhysicalDeviceMemoryProperties; // for VMA
        PFN_vkGetPhysicalDeviceMemoryProperties2            vkGetPhysicalDeviceMemoryProperties2;
        PFN_vkGetDeviceProcAddr                             vkGetDeviceProcAddr;
        PFN_vkCreateDevice                                  vkCreateDevice;
        PFN_vkDestroyDevice                                 vkDestroyDevice;
        PFN_vkEnumerateInstanceVersion                      vkEnumerateInstanceVersion;
        PFN_vkEnumerateInstanceExtensionProperties          vkEnumerateInstanceExtensionProperties;
        PFN_vkEnumerateDeviceExtensionProperties            vkEnumerateDeviceExtensionProperties;
        PFN_vkEnumerateInstanceLayerProperties              vkEnumerateInstanceLayerProperties;
        PFN_vkGetDeviceQueue                                vkGetDeviceQueue;
        PFN_vkQueueSubmit                                   vkQueueSubmit;
        PFN_vkQueueWaitIdle                                 vkQueueWaitIdle;
        PFN_vkDeviceWaitIdle                                vkDeviceWaitIdle;
        PFN_vkAllocateMemory                                vkAllocateMemory;
        PFN_vkFreeMemory                                    vkFreeMemory;
        PFN_vkMapMemory                                     vkMapMemory;
        PFN_vkUnmapMemory                                   vkUnmapMemory;
        PFN_vkFlushMappedMemoryRanges                       vkFlushMappedMemoryRanges;
        PFN_vkInvalidateMappedMemoryRanges                  vkInvalidateMappedMemoryRanges;
        PFN_vkGetDeviceMemoryCommitment                     vkGetDeviceMemoryCommitment;
        PFN_vkBindBufferMemory                              vkBindBufferMemory;
        PFN_vkBindBufferMemory2                             vkBindBufferMemory2;
        PFN_vkBindImageMemory                               vkBindImageMemory;
        PFN_vkBindImageMemory2                              vkBindImageMemory2;
        PFN_vkGetBufferMemoryRequirements                   vkGetBufferMemoryRequirements; // for VMA
        PFN_vkGetBufferMemoryRequirements2                  vkGetBufferMemoryRequirements2;
        PFN_vkGetImageMemoryRequirements                    vkGetImageMemoryRequirements; // for VMA
        PFN_vkGetImageMemoryRequirements2                   vkGetImageMemoryRequirements2;
        PFN_vkGetImageSparseMemoryRequirements              vkGetImageSparseMemoryRequirements;
        PFN_vkGetPhysicalDeviceSparseImageFormatProperties  vkGetPhysicalDeviceSparseImageFormatProperties;
        PFN_vkQueueBindSparse                               vkQueueBindSparse;
        PFN_vkCreateFence                                   vkCreateFence;
        PFN_vkDestroyFence                                  vkDestroyFence;
        PFN_vkResetFences                                   vkResetFences;
        PFN_vkGetFenceStatus                                vkGetFenceStatus;
        PFN_vkWaitForFences                                 vkWaitForFences;
        PFN_vkCreateSemaphore                               vkCreateSemaphore;
        PFN_vkDestroySemaphore                              vkDestroySemaphore;
        PFN_vkGetSemaphoreCounterValue                      vkGetSemaphoreCounterValue;
        PFN_vkWaitSemaphores                                vkWaitSemaphores;
        PFN_vkCreateEvent                                   vkCreateEvent;
        PFN_vkDestroyEvent                                  vkDestroyEvent;
        PFN_vkGetEventStatus                                vkGetEventStatus;
        PFN_vkSetEvent                                      vkSetEvent;
        PFN_vkResetEvent                                    vkResetEvent;
        PFN_vkCreateQueryPool                               vkCreateQueryPool;
        PFN_vkDestroyQueryPool                              vkDestroyQueryPool;
        PFN_vkGetQueryPoolResults                           vkGetQueryPoolResults;
        PFN_vkCreateBuffer                                  vkCreateBuffer;
        PFN_vkDestroyBuffer                                 vkDestroyBuffer;
        PFN_vkGetBufferDeviceAddress                        vkGetBufferDeviceAddress;
        PFN_vkCreateBufferView                              vkCreateBufferView;
        PFN_vkDestroyBufferView                             vkDestroyBufferView;
        PFN_vkCreateImage                                   vkCreateImage;
        PFN_vkDestroyImage                                  vkDestroyImage;
        PFN_vkGetImageSubresourceLayout                     vkGetImageSubresourceLayout;
        PFN_vkCreateImageView                               vkCreateImageView;
        PFN_vkDestroyImageView                              vkDestroyImageView;
        PFN_vkCreateShaderModule                            vkCreateShaderModule;
        PFN_vkDestroyShaderModule                           vkDestroyShaderModule;
        PFN_vkCreatePipelineCache                           vkCreatePipelineCache;
        PFN_vkDestroyPipelineCache                          vkDestroyPipelineCache;
        PFN_vkGetPipelineCacheData                          vkGetPipelineCacheData;
        PFN_vkMergePipelineCaches                           vkMergePipelineCaches;
        PFN_vkCreateGraphicsPipelines                       vkCreateGraphicsPipelines;
        PFN_vkCreateComputePipelines                        vkCreateComputePipelines;
        PFN_vkDestroyPipeline                               vkDestroyPipeline;
        PFN_vkCreatePipelineLayout                          vkCreatePipelineLayout;
        PFN_vkDestroyPipelineLayout                         vkDestroyPipelineLayout;
        PFN_vkCreateSampler                                 vkCreateSampler;
        PFN_vkDestroySampler                                vkDestroySampler;
        PFN_vkCreateDescriptorSetLayout                     vkCreateDescriptorSetLayout;
        PFN_vkDestroyDescriptorSetLayout                    vkDestroyDescriptorSetLayout;
        PFN_vkCreateDescriptorPool                          vkCreateDescriptorPool;
        PFN_vkDestroyDescriptorPool                         vkDestroyDescriptorPool;
        PFN_vkResetDescriptorPool                           vkResetDescriptorPool;
        PFN_vkAllocateDescriptorSets                        vkAllocateDescriptorSets;
        PFN_vkFreeDescriptorSets                            vkFreeDescriptorSets;
        PFN_vkUpdateDescriptorSets                          vkUpdateDescriptorSets;
        PFN_vkCreateFramebuffer                             vkCreateFramebuffer;
        PFN_vkDestroyFramebuffer                            vkDestroyFramebuffer;
        PFN_vkCreateRenderPass2                             vkCreateRenderPass2;
        PFN_vkDestroyRenderPass                             vkDestroyRenderPass;
        PFN_vkGetRenderAreaGranularity                      vkGetRenderAreaGranularity;
        PFN_vkCreateCommandPool                             vkCreateCommandPool;
        PFN_vkDestroyCommandPool                            vkDestroyCommandPool;
        PFN_vkResetCommandPool                              vkResetCommandPool;
        PFN_vkAllocateCommandBuffers                        vkAllocateCommandBuffers;
        PFN_vkFreeCommandBuffers                            vkFreeCommandBuffers;
        PFN_vkBeginCommandBuffer                            vkBeginCommandBuffer;
        PFN_vkEndCommandBuffer                              vkEndCommandBuffer;
        PFN_vkResetCommandBuffer                            vkResetCommandBuffer;
        PFN_vkCmdBindPipeline                               vkCmdBindPipeline;
        PFN_vkCmdSetViewport                                vkCmdSetViewport;
        PFN_vkCmdSetScissor                                 vkCmdSetScissor;
        PFN_vkCmdSetLineWidth                               vkCmdSetLineWidth;
        PFN_vkCmdSetDepthBias                               vkCmdSetDepthBias;
        PFN_vkCmdSetBlendConstants                          vkCmdSetBlendConstants;
        PFN_vkCmdSetDepthBounds                             vkCmdSetDepthBounds;
        PFN_vkCmdSetStencilCompareMask                      vkCmdSetStencilCompareMask;
        PFN_vkCmdSetStencilWriteMask                        vkCmdSetStencilWriteMask;
        PFN_vkCmdSetStencilReference                        vkCmdSetStencilReference;
        PFN_vkCmdBindDescriptorSets                         vkCmdBindDescriptorSets;
        PFN_vkCmdBindIndexBuffer                            vkCmdBindIndexBuffer;
        PFN_vkCmdBindVertexBuffers                          vkCmdBindVertexBuffers;
        PFN_vkCmdDraw                                       vkCmdDraw;
        PFN_vkCmdDrawIndexed                                vkCmdDrawIndexed;
        PFN_vkCmdDrawIndirect                               vkCmdDrawIndirect;
        PFN_vkCmdDrawIndirectCount                          vkCmdDrawIndirectCount;
        PFN_vkCmdDrawIndexedIndirect                        vkCmdDrawIndexedIndirect;
        PFN_vkCmdDrawIndexedIndirectCount                   vkCmdDrawIndexedIndirectCount;
        PFN_vkCmdDispatch                                   vkCmdDispatch;
        PFN_vkCmdDispatchIndirect                           vkCmdDispatchIndirect;
        PFN_vkCmdCopyBuffer                                 vkCmdCopyBuffer;
        PFN_vkCmdCopyImage                                  vkCmdCopyImage;
        PFN_vkCmdBlitImage                                  vkCmdBlitImage;
        PFN_vkCmdCopyBufferToImage                          vkCmdCopyBufferToImage;
        PFN_vkCmdCopyImageToBuffer                          vkCmdCopyImageToBuffer;
        PFN_vkCmdUpdateBuffer                               vkCmdUpdateBuffer;
        PFN_vkCmdFillBuffer                                 vkCmdFillBuffer;
        PFN_vkCmdClearColorImage                            vkCmdClearColorImage;
        PFN_vkCmdClearDepthStencilImage                     vkCmdClearDepthStencilImage;
        PFN_vkCmdClearAttachments                           vkCmdClearAttachments;
        PFN_vkCmdResolveImage                               vkCmdResolveImage;
        PFN_vkCmdSetEvent                                   vkCmdSetEvent;
        PFN_vkCmdResetEvent                                 vkCmdResetEvent;
        PFN_vkCmdWaitEvents                                 vkCmdWaitEvents;
        PFN_vkCmdPipelineBarrier                            vkCmdPipelineBarrier;
        PFN_vkCmdBeginQuery                                 vkCmdBeginQuery;
        PFN_vkCmdEndQuery                                   vkCmdEndQuery;
        PFN_vkCmdResetQueryPool                             vkCmdResetQueryPool;
        PFN_vkCmdWriteTimestamp                             vkCmdWriteTimestamp;
        PFN_vkCmdCopyQueryPoolResults                       vkCmdCopyQueryPoolResults;
        PFN_vkCmdPushConstants                              vkCmdPushConstants;
        PFN_vkCmdBeginRenderPass                            vkCmdBeginRenderPass;
        PFN_vkCmdNextSubpass                                vkCmdNextSubpass;
        PFN_vkCmdEndRenderPass                              vkCmdEndRenderPass;
        PFN_vkCmdExecuteCommands                            vkCmdExecuteCommands;

        // 1.2:
        PFN_vkResetQueryPool                                vkResetQueryPool;

        // instance extensions:
        bool                                                KHR_surface;
#if defined( VK_KHR_surface )
        PFN_vkDestroySurfaceKHR                             vkDestroySurfaceKHR;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR            vkGetPhysicalDeviceSurfaceSupportKHR;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR       vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR            vkGetPhysicalDeviceSurfaceFormatsKHR;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR       vkGetPhysicalDeviceSurfacePresentModesKHR;
#endif

        bool                                                KHR_win32_surface;
#if defined( VK_KHR_win32_surface )
        PFN_vkCreateWin32SurfaceKHR                         vkCreateWin32SurfaceKHR;
        PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR  vkGetPhysicalDeviceWin32PresentationSupportKHR;
#endif

        bool                                                KHR_xlib_surface;
#if defined( VK_KHR_xlib_surface )
        PFN_vkCreateXlibSurfaceKHR                          vkCreateXlibSurfaceKHR;
        PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR   vkGetPhysicalDeviceXlibPresentationSupportKHR;
#endif

        bool                                                KHR_wayland_surface;
#if defined( VK_KHR_wayland_surface )
        PFN_vkCreateWaylandSurfaceKHR                           vkCreateWaylandSurfaceKHR;
        PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR    vkGetPhysicalDeviceWaylandPresentationSupportKHR;
#endif

        bool                                                KHR_android_surface;
#if defined( VK_KHR_android_surface )
        PFN_vkCreateAndroidSurfaceKHR                       vkCreateAndroidSurfaceKHR;
#endif

        bool                                                EXT_debug_utils;
#if defined( VK_EXT_debug_utils )
        PFN_vkSetDebugUtilsObjectNameEXT                    vkSetDebugUtilsObjectNameEXT;
        PFN_vkSetDebugUtilsObjectTagEXT                     vkSetDebugUtilsObjectTagEXT;
        PFN_vkQueueBeginDebugUtilsLabelEXT                  vkQueueBeginDebugUtilsLabelEXT;
        PFN_vkQueueEndDebugUtilsLabelEXT                    vkQueueEndDebugUtilsLabelEXT;
        PFN_vkQueueInsertDebugUtilsLabelEXT                 vkQueueInsertDebugUtilsLabelEXT;
        PFN_vkCmdBeginDebugUtilsLabelEXT                    vkCmdBeginDebugUtilsLabelEXT;
        PFN_vkCmdEndDebugUtilsLabelEXT                      vkCmdEndDebugUtilsLabelEXT;
        PFN_vkCmdInsertDebugUtilsLabelEXT                   vkCmdInsertDebugUtilsLabelEXT;
        PFN_vkCreateDebugUtilsMessengerEXT                  vkCreateDebugUtilsMessengerEXT;
        PFN_vkDestroyDebugUtilsMessengerEXT                 vkDestroyDebugUtilsMessengerEXT;
        PFN_vkSubmitDebugUtilsMessageEXT                    vkSubmitDebugUtilsMessageEXT;
#endif

        // device extensions:
        bool                                                KHR_swapchain;
#if defined( VK_KHR_swapchain )
        PFN_vkCreateSwapchainKHR                            vkCreateSwapchainKHR;
        PFN_vkDestroySwapchainKHR                           vkDestroySwapchainKHR;
        PFN_vkGetSwapchainImagesKHR                         vkGetSwapchainImagesKHR;
        PFN_vkAcquireNextImageKHR                           vkAcquireNextImageKHR;
        PFN_vkQueuePresentKHR                               vkQueuePresentKHR;
#endif

        bool                                                KHR_dynamic_rendering;
#if defined( VK_KHR_dynamic_rendering )
        PFN_vkCmdBeginRenderingKHR                          vkCmdBeginRenderingKHR;
        PFN_vkCmdEndRenderingKHR                            vkCmdEndRenderingKHR;
#endif

        bool                                                EXT_memory_budget;

        bool                                                NV_device_diagnostic_checkpoints;
#if defined( VK_NV_device_diagnostic_checkpoints )
        PFN_vkCmdSetCheckpointNV                            vkCmdSetCheckpointNV;
        PFN_vkGetQueueCheckpointDataNV                      vkGetQueueCheckpointDataNV;
#endif

        bool                                                AMD_buffer_marker;
#if defined( VK_AMD_buffer_marker )
        PFN_vkCmdWriteBufferMarkerAMD                       vkCmdWriteBufferMarkerAMD;
#endif

        bool                                                EXT_device_fault;
#if defined( VK_EXT_device_fault )
        PFN_vkGetDeviceFaultInfoEXT                         vkGetDeviceFaultInfoEXT;
#endif

        bool                                                    KHR_pipeline_executable_properties;
#if defined( VK_KHR_pipeline_executable_properties )
        PFN_vkGetPipelineExecutableInternalRepresentationsKHR   vkGetPipelineExecutableInternalRepresentationsKHR;
        PFN_vkGetPipelineExecutablePropertiesKHR                vkGetPipelineExecutablePropertiesKHR;
        PFN_vkGetPipelineExecutableStatisticsKHR                vkGetPipelineExecutableStatisticsKHR;
#endif

        bool                                                    AMD_shader_info;
#if defined( VK_AMD_shader_info )
        PFN_vkGetShaderInfoAMD                                  vkGetShaderInfoAMD;
#endif
    };

    using VulkanExtensionStringSet = Set<HashKey64>;
    struct VulkanLayerExtensionInfo
    {
        VulkanExtensionStringSet    layerNameHashes;
        VulkanExtensionStringSet    extensionNameHashes;

        bool                        hasLayer( const char* pExtension ) const;
        bool                        hasExtension( const char* pExtension ) const;
    };

    struct VulkanQueueInfos
    {
        uint32  mainQueueFamilyIndex;
        uint32  transferQueueFamilyIndex;
    };

    struct VulkanResult
    {
        VulkanResult() {}
        VulkanResult( VkResult result ) : vkResult( result ) {}

        bool        isOk() const            { return vkResult == VK_SUCCESS; }
        bool        hasError() const        { return vkResult != VK_SUCCESS; }
        bool        isDeviceLost() const    { return vkResult == VK_ERROR_DEVICE_LOST; }

        ErrorId     getErrorId() const;

        VkResult vkResult = VK_SUCCESS;
    };

    void formatToString( WriteStream* pStream, const FormatStringOptions& options, const VulkanResult& result );

    namespace vulkan
    {

        Result<VulkanApi*>          createVulkanApi( MemoryAllocator* pAllocator );
        void                        destroyVulkanApi( MemoryAllocator* pAllocator, VulkanApi* pVulkanApi );

        ErrorId                     fillInstanceInfo( VulkanLayerExtensionInfo* pInfo, MemoryAllocator* pAllocator, VulkanApi* pVulkan );
        ErrorId                     fillInstanceLayerExtensionInfo( VulkanLayerExtensionInfo* pInfo, MemoryAllocator* pAllocator, VulkanApi* pVulkan, const char *pLayerName );
        ErrorId                     loadInstanceFunctions( VulkanApi* pVulkan, VkInstance instance, const ArrayView<const char*> activeExtensions );
        ErrorId                     fillDeviceInfo( VulkanLayerExtensionInfo* pInfo, MemoryAllocator* pAllocator, VulkanApi* pVulkan, VkPhysicalDevice physicalDevice );
        ErrorId                     loadDeviceFunctions( VulkanApi* pVulkan, VkPhysicalDevice physicalDevice, VkDevice device, const ArrayView<const char*> activeExtensions );

        template<typename T>
            inline void             appendToStructChain( const void*** pppNext, T* pStruct )
        {
            KEEN_ASSERT( **pppNext == nullptr );
            KEEN_ASSERT( pStruct->pNext == nullptr );
            **pppNext = pStruct;
            *pppNext = &pStruct->pNext;
        }

        template<typename T>
            inline void             appendToStructChain( void*** pppNext, T* pStruct )
        {
            KEEN_ASSERT( **pppNext == nullptr );
            KEEN_ASSERT( pStruct->pNext == nullptr );
            **pppNext = pStruct;
            *pppNext = &pStruct->pNext;
        }

#if KEEN_USING( KEEN_VULKAN_OBJECT_NAMES )
        void                        setObjectName( VulkanApi* pVulkan, VkDevice device, VkObjectHandle pObject, VkObjectType objectType, const DebugName& name );
#else
        inline void                 setObjectName( VulkanApi* pVulkan, VkDevice device, VkObjectHandle pObject, VkObjectType objectType, const DebugName& name ) { KEEN_UNUSED5( pVulkan, device, pObject, objectType, name ); }
#endif

#if KEEN_USING( KEEN_VULKAN_DEBUG_LABELS )
        void                        beginDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const DebugName& name, const float4& color = f4_zero );
        void                        endDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer );
        void                        insertDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const DebugName& name, const float4& color = f4_zero );
#else
        inline void                 beginDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const DebugName& name, const float4& color = f4_zero ) { KEEN_UNUSED4( pVulkan, commandBuffer, name, color ); }
        inline void                 endDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer ) { KEEN_UNUSED2( pVulkan, commandBuffer ); }
        inline void                 insertDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const DebugName& name, const float4& color = f4_zero ) { KEEN_UNUSED4( pVulkan, commandBuffer, name, color ); }
#endif

        VkDeviceAddress             getBufferDeviceAddress( VulkanApi* pVulkan, VkDevice device, VkBuffer buffer );

        const char*                 getDeviceTypeString( VkPhysicalDeviceType deviceType );
        const char*                 getVkFormatString( VkFormat format );
        const char*                 getVkColorSpaceString( VkColorSpaceKHR colorSpace );
        const char*                 getVkPresentModeString( VkPresentModeKHR presentMode );

        const char*                 getVkPipelineStageFlagBitsString( VkPipelineStageFlagBits stage );

        VkFormat                    getVertexAttributeFormat( VertexAttributeFormat format );
        VkIndexType                 getIndexType( GraphicsIndexFormat format );

        bool                        isCompatibleSurfaceFormat( VkFormat vkFormat, PixelFormat pixelFormat );

        VkPrimitiveTopology         getPrimitiveTopology( GraphicsPrimitiveType primitiveType );
        VkFormat                    getVulkanFormat( PixelFormat format );      
        PixelFormat                 getPixelFormat( VkFormat format );
        bool                        isDepthFormat( VkFormat format );
        bool                        isPackedDepthStencilFormat( VkFormat format );
        
        inline VkOffset2D           createOffset2d( sint32 x, sint32 y ) { VkOffset2D result = { x, y }; return result; }
        inline VkOffset3D           createOffset3d( sint32 x, sint32 y, sint32 z ) { VkOffset3D result = { x, y, z }; return result; }
        inline VkOffset3D           createOffset3d( int3 offset ) { VkOffset3D result = { offset.x, offset.y, offset.z }; return result; }
        inline VkOffset3D           createOffset3d( uint3 offset ) { return createOffset3d( i3_from_u3( offset ) ); }
        inline VkExtent2D           createExtent2d( uint32 width, uint32 height ) { VkExtent2D result = { width, height }; return result; }
        inline VkExtent3D           createExtent3d( uint32 width, uint32 height, uint32 depth ) { VkExtent3D result = { width, height, depth }; return result; }
        inline VkExtent3D           createExtent3d( uint3 size ) { VkExtent3D result = { size.x, size.y, size.z }; return result; }
        
        VkFilter                    getFilter( GraphicsSamplerFilterMode filterMode );
        VkSamplerMipmapMode         getSamplerMipmapMode( GraphicsSamplerFilterMode filterMode );
        VkSamplerAddressMode        getSamplerAddressMode( GraphicsSamplerAddressMode addressMode );
        VkSamplerReductionModeEXT   getSamplerReductionMode( GraphicsSamplerReductionMode reductionMode );
        VkCompareOp                 getCompareOp( GraphicsComparisonFunction comparisonFunction );
        VkStencilOp                 getStencilOp( GraphicsStencilOperation stencilOperation );
        VkPolygonMode               getPolygonMode( GraphicsFillMode fillMode );
        VkCullModeFlagBits          getCullModeFlagBits( GraphicsCullMode cullMode );
        VkFrontFace                 getFrontFace( GraphicsWindingOrder windingOrder );
        VkSampleCountFlagBits       getSampleCountFlagBits( uint8 sampleCount );
        VkImageUsageFlags           getImageUsageMask( GraphicsTextureUsageMask usageMask );
        VkBlendOp                   getBlendOp( GraphicsBlendOperation blendOperation );
        VkBlendFactor               getBlendFactor( GraphicsBlendFactor blendFactor );
        VkColorComponentFlagBits    getColorComponentFlagBits( GraphicsColorWriteMask colorMask );
        VkAttachmentLoadOp          getLoadOp( GraphicsLoadAction loadAction );
        VkAttachmentStoreOp         getStoreOp( GraphicsStoreAction storeAction );
        VkClearColorValue           getColorClearValue( GraphicsColorClearValue clearValue );
        VkClearDepthStencilValue    getDepthStencilClearValue( GraphicsDepthClearValue depthClearValue, GraphicsStencilClearValue stencilClearValue );
        VkShaderStageFlags          getStageFlags( GraphicsPipelineStageMask mask );
        VkBufferUsageFlags          getBufferUsageFlags( GraphicsBufferUsageMask flags );
        VkDescriptorType            getDescriptorType( GraphicsDescriptorType type );

        VkImageType                 getImageType( TextureType type );
        VkImageViewType             getImageViewType( TextureType type );

        VkImageLayout               getImageLayout( GraphicsTextureLayout layout );

        VkStencilFaceFlags          getStencilFaceFlags( GraphicsStencilFaceMask faceMask );

        VkImageSubresourceRange     getImageSubresourceRange( const GraphicsTextureSubresourceRange& subresourceRange );

        float                       getMemoryPriority( GraphicsDeviceMemoryPriority priority );

        GraphicsMemoryRequirements  createGraphicsMemoryRequirements( const VkMemoryRequirements& memoryRequirements, bool prefersDedicatedAllocation, bool requiresDedicatedAllocation );

        VkImageAspectFlags          getImageAspectFlags( const GraphicsTextureAspectFlagMask& aspectMask );
        void                        fillVkImageSubresourceLayers( VkImageSubresourceLayers* pVulkanImageSubresource, const GraphicsTextureRegion& region );
        void                        fillVkBufferImageCopy( VkBufferImageCopy* pVulkanBufferImageCopy, const GraphicsBufferTextureCopyRegion& region );

        void                        writeFullPipelineBarrier( VulkanApi* pVulkan, VkCommandBuffer commandBuffer );

        inline uint32               calculateSubresource( uint32 mipLevelIndex, uint32 arrayLayerIndex, uint32 planeIndex, uint32 mipLevels, uint32 arraySize ) { return mipLevelIndex + ( arrayLayerIndex * mipLevels ) + ( planeIndex * mipLevels * arraySize ); }

#if KEEN_USING( KEEN_VULKAN_CHECKPOINTS )
        inline void                 insertCheckPoint( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const char* pName )
        {
            if( pVulkan->NV_device_diagnostic_checkpoints )
            {
                pVulkan->vkCmdSetCheckpointNV( commandBuffer, pName );
            }
        }
#else
        inline void                 insertCheckPoint( VulkanApi*, VkCommandBuffer, const char* ) {}
#endif
    }

}

#endif
