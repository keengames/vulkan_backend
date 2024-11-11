#include "vulkan_api.hpp"
#include "keen/base/crc64.hpp"
#include "keen/base/search.hpp"
#include "keen/base/sort.hpp"
#include "keen/base/error.hpp"
#include "keen/base/format_string.hpp"
#include "keen/os/os_dynamic_library.hpp"

namespace keen
{

    namespace vulkan
    {

        static void insertHashString( VulkanExtensionStringSet* pStringHashSet, const char* pValue );
        static bool findHashString( const VulkanExtensionStringSet& stringHash, const char* pValue );

        static bool isExtensionActive( ArrayView<const char*> activeExtensions, const char* pExtensionName );

    }

    bool VulkanLayerExtensionInfo::hasLayer( const char* pLayer ) const
    {
        return vulkan::findHashString( layerNameHashes, pLayer );
    }

    bool VulkanLayerExtensionInfo::hasExtension( const char* pExtension ) const
    {
        return vulkan::findHashString( extensionNameHashes, pExtension );
    }

    static void vulkan::insertHashString( VulkanExtensionStringSet* pStringHashSet, const char* pValue )
    {
        const HashKey64 hash = calculateFnv1a64Hash( createStringView( pValue ) );
        KEEN_VERIFY( pStringHashSet->insert( hash ) );
    }

    static bool vulkan::findHashString( const VulkanExtensionStringSet& stringHashSet, const char* pValue )
    {
        const HashKey64 hash = calculateFnv1a64Hash( createStringView( pValue ) );
        return stringHashSet.find( hash );
    }

    static bool vulkan::isExtensionActive( ArrayView<const char*> activeExtensions, const char* pExtensionName )
    {
        for( size_t i = 0u; i < activeExtensions.getSize(); ++i )
        {
            if( cstring::isStringEqual( activeExtensions[ i ], pExtensionName ) )
            {
                return true;
            }
        }
        return false;
    }

    ErrorId VulkanResult::getErrorId() const
    {
        switch( vkResult )
        {
        case VK_SUCCESS:                            return ErrorId_Ok;
        case VK_NOT_READY:                          return ErrorId_Temporary_TimeOut;
        case VK_TIMEOUT:                            return ErrorId_Temporary_TimeOut;
        case VK_INCOMPLETE:                         return ErrorId_BufferTooSmall;
        case VK_ERROR_OUT_OF_HOST_MEMORY:           return ErrorId_OutOfMemory;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:         return ErrorId_OutOfMemory;
        case VK_ERROR_INITIALIZATION_FAILED:        return ErrorId_InitializationFailed;
        case VK_ERROR_DEVICE_LOST:                  return ErrorId_DeviceLost;
        case VK_ERROR_MEMORY_MAP_FAILED:            return ErrorId_MemoryMapFailed;
        case VK_ERROR_LAYER_NOT_PRESENT:            return ErrorId_NotFound;
        case VK_ERROR_EXTENSION_NOT_PRESENT:        return ErrorId_NotFound;
        case VK_ERROR_FEATURE_NOT_PRESENT:          return ErrorId_NotFound;
        case VK_ERROR_INCOMPATIBLE_DRIVER:          return ErrorId_WrongVersion;
        case VK_ERROR_TOO_MANY_OBJECTS:             return ErrorId_OutOfMemory;
        case VK_ERROR_FORMAT_NOT_SUPPORTED:         return ErrorId_NotSupported;
            // :JK: these are not really errors:
        case VK_EVENT_SET:                          return ErrorId_Generic;
        case VK_EVENT_RESET:                        return ErrorId_Generic;

            // map all other errors to ErrorId_Generic:
        default:                                    return ErrorId_Generic;
        }
    }

    void formatToString( WriteStream* pStream, const FormatStringOptions& options, const VulkanResult& result )
    {
        KEEN_UNUSED1( options );

        switch( result.vkResult )
        {
        case VK_SUCCESS:                                            pStream->writeString( "VK_SUCCESS"_s );                                             return;
        case VK_NOT_READY:                                          pStream->writeString( "VK_NOT_READY"_s );                                           return;
        case VK_TIMEOUT:                                            pStream->writeString( "VK_TIMEOUT"_s );                                             return;
        case VK_EVENT_SET:                                          pStream->writeString( "VK_EVENT_SET"_s );                                           return;
        case VK_EVENT_RESET:                                        pStream->writeString( "VK_EVENT_RESET"_s );                                         return;
        case VK_INCOMPLETE:                                         pStream->writeString( "VK_INCOMPLETE"_s );                                          return;
        case VK_ERROR_OUT_OF_HOST_MEMORY:                           pStream->writeString( "VK_ERROR_OUT_OF_HOST_MEMORY"_s );                            return;
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:                         pStream->writeString( "VK_ERROR_OUT_OF_DEVICE_MEMORY"_s );                          return;
        case VK_ERROR_INITIALIZATION_FAILED:                        pStream->writeString( "VK_ERROR_INITIALIZATION_FAILED"_s );                         return;
        case VK_ERROR_DEVICE_LOST:                                  pStream->writeString( "VK_ERROR_DEVICE_LOST"_s );                                   return;
        case VK_ERROR_MEMORY_MAP_FAILED:                            pStream->writeString( "VK_ERROR_MEMORY_MAP_FAILED"_s );                             return;
        case VK_ERROR_LAYER_NOT_PRESENT:                            pStream->writeString( "VK_ERROR_LAYER_NOT_PRESENT"_s );                             return;
        case VK_ERROR_EXTENSION_NOT_PRESENT:                        pStream->writeString( "VK_ERROR_EXTENSION_NOT_PRESENT"_s );                         return;
        case VK_ERROR_FEATURE_NOT_PRESENT:                          pStream->writeString( "VK_ERROR_FEATURE_NOT_PRESENT"_s );                           return;
        case VK_ERROR_INCOMPATIBLE_DRIVER:                          pStream->writeString( "VK_ERROR_INCOMPATIBLE_DRIVER"_s );                           return;
        case VK_ERROR_TOO_MANY_OBJECTS:                             pStream->writeString( "VK_ERROR_TOO_MANY_OBJECTS"_s );                              return;
        case VK_ERROR_FORMAT_NOT_SUPPORTED:                         pStream->writeString( "VK_ERROR_FORMAT_NOT_SUPPORTED"_s );                          return;
        case VK_ERROR_FRAGMENTED_POOL:                              pStream->writeString( "VK_ERROR_FRAGMENTED_POOL"_s );                               return;
        case VK_ERROR_UNKNOWN:                                      pStream->writeString( "VK_ERROR_UNKNOWN"_s );                                       return;
        case VK_ERROR_OUT_OF_POOL_MEMORY:                           pStream->writeString( "VK_ERROR_OUT_OF_POOL_MEMORY"_s );                            return;
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:                      pStream->writeString( "VK_ERROR_INVALID_EXTERNAL_HANDLE"_s );                       return;
        case VK_ERROR_FRAGMENTATION:                                pStream->writeString( "VK_ERROR_FRAGMENTATION"_s );                                 return;
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:               pStream->writeString( "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS"_s );                return;
        case VK_PIPELINE_COMPILE_REQUIRED:                          pStream->writeString( "VK_PIPELINE_COMPILE_REQUIRED"_s );                           return;
        case VK_ERROR_SURFACE_LOST_KHR:                             pStream->writeString( "VK_ERROR_SURFACE_LOST_KHR"_s );                              return;
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:                     pStream->writeString( "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR"_s );                      return;
        case VK_SUBOPTIMAL_KHR:                                     pStream->writeString( "VK_SUBOPTIMAL_KHR"_s );                                      return;
        case VK_ERROR_OUT_OF_DATE_KHR:                              pStream->writeString( "VK_ERROR_OUT_OF_DATE_KHR"_s );                               return;
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:                     pStream->writeString( "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR"_s );                      return;
        case VK_ERROR_VALIDATION_FAILED_EXT:                        pStream->writeString( "VK_ERROR_VALIDATION_FAILED_EXT"_s );                         return;
        case VK_ERROR_INVALID_SHADER_NV:                            pStream->writeString( "VK_ERROR_INVALID_SHADER_NV"_s );                             return;
        case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR:                pStream->writeString( "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR"_s );                 return;
        case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR:       pStream->writeString( "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR"_s );        return;
        case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR:    pStream->writeString( "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR"_s );     return;
        case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR:       pStream->writeString( "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR"_s );        return;
        case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR:        pStream->writeString( "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR"_s );         return;
        case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR:          pStream->writeString( "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR"_s );           return;
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: pStream->writeString( "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT"_s );  return;
        case VK_ERROR_NOT_PERMITTED_KHR:                            pStream->writeString( "VK_ERROR_NOT_PERMITTED_KHR"_s );                             return;
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:          pStream->writeString( "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT"_s );           return;
        case VK_THREAD_IDLE_KHR:                                    pStream->writeString( "VK_THREAD_IDLE_KHR"_s );                                     return;
        case VK_THREAD_DONE_KHR:                                    pStream->writeString( "VK_THREAD_DONE_KHR"_s );                                     return;
        case VK_OPERATION_DEFERRED_KHR:                             pStream->writeString( "VK_OPERATION_DEFERRED_KHR"_s );                              return;
        case VK_OPERATION_NOT_DEFERRED_KHR:                         pStream->writeString( "VK_OPERATION_NOT_DEFERRED_KHR"_s );                          return;
        case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:                    pStream->writeString( "VK_ERROR_COMPRESSION_EXHAUSTED_EXT"_s );                     return;
        case VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT:               pStream->writeString( "VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT"_s );                return;
        case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR:             pStream->writeString( "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR"_s );              return;
        case VK_RESULT_MAX_ENUM:                                    break;
        }

        formatString( pStream, "%d", result.vkResult );
    }

    static PFN_vkVoidFunction getVulkanInstanceProcAddress( StickyError* pError, VulkanApi* pVulkan, VkInstance instance, const char* pName )
    {
        KEEN_ASSERTE( pVulkan != nullptr );
        KEEN_ASSERTE( pVulkan->pLibrary != nullptr );
        KEEN_ASSERTE( pVulkan->vkGetInstanceProcAddr != nullptr );

        PFN_vkVoidFunction pAddress = pVulkan->vkGetInstanceProcAddr( instance, pName );
        if( pAddress == nullptr )
        {
            pError->setError( ErrorId_NotFound );
        }

        return pAddress;
    }

    static PFN_vkVoidFunction getVulkanDeviceProcAddress( StickyError* pError, VulkanApi* pVulkan, VkDevice device, const char* pName )
    {
        KEEN_ASSERTE( pVulkan != nullptr );
        KEEN_ASSERTE( pVulkan->pLibrary != nullptr );
        KEEN_ASSERTE( pVulkan->vkGetInstanceProcAddr != nullptr );

        PFN_vkVoidFunction pAddress = pVulkan->vkGetDeviceProcAddr( device, pName );
        if( pAddress == nullptr )
        {
            pError->setError( ErrorId_NotFound );
        }

        return pAddress;
    }

    Result<VulkanApi*> vulkan::createVulkanApi( MemoryAllocator* pAllocator )
    {
        VulkanApi* pVulkan = newObjectZero<VulkanApi>( pAllocator, "VulkanApi"_debug );
        if( pVulkan == nullptr )
        {
            return ErrorId_OutOfMemory;
        }

#if defined( KEEN_PLATFORM_WIN32 )
        const Result<OsDynamicLibraryHandle> dynamicLibraryResult = os::loadDynamicLibrary( "vulkan-1.dll"_s );
#elif defined( KEEN_PLATFORM_LINUX )
        const Result<OsDynamicLibraryHandle> dynamicLibraryResult = os::loadDynamicLibrary( "libvulkan.so.1"_s );
#endif

        if( dynamicLibraryResult.hasError() )
        {
            destroyVulkanApi( pAllocator, pVulkan );
            return ErrorId_NotSupported;
        }

        pVulkan->pLibrary = dynamicLibraryResult.getValue().pHandle;

        using vkGetInstanceProcAddrType = typename RemovePointer<PFN_vkGetInstanceProcAddr>::Type;
        pVulkan->vkGetInstanceProcAddr = os::getLibraryFunction<vkGetInstanceProcAddrType>( { pVulkan->pLibrary }, "vkGetInstanceProcAddr"_s );

        if( pVulkan->vkGetInstanceProcAddr == nullptr )
        {
            destroyVulkanApi( pAllocator, pVulkan );
            return ErrorId_NotSupported;
        }

        // get global vulkan functions:
        StickyError error;
        pVulkan->vkEnumerateInstanceVersion             = (PFN_vkEnumerateInstanceVersion)(void*)pVulkan->vkGetInstanceProcAddr( nullptr, "vkEnumerateInstanceVersion" ); // optional, it's Vulkan 1.0 when this is nullptr
        pVulkan->vkCreateInstance                       = (PFN_vkCreateInstance)(void*)getVulkanInstanceProcAddress( &error, pVulkan, nullptr, "vkCreateInstance" );
        pVulkan->vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, nullptr, "vkEnumerateInstanceExtensionProperties" );
        pVulkan->vkEnumerateInstanceLayerProperties     = (PFN_vkEnumerateInstanceLayerProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, nullptr, "vkEnumerateInstanceLayerProperties" );

        if( error.hasError() )
        {
            destroyVulkanApi( pAllocator, pVulkan );
            return error.getError();
        }

        return pVulkan;
    }

    ErrorId vulkan::fillInstanceInfo( VulkanLayerExtensionInfo* pInfo, MemoryAllocator* pAllocator, VulkanApi* pVulkan )
    {
        KEEN_ASSERT( pInfo != nullptr );
        KEEN_ASSERT( pVulkan != nullptr );

        uint32 layerCount = 512u;
        VulkanResult result = pVulkan->vkEnumerateInstanceLayerProperties( &layerCount, nullptr );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not enumerate instance layer properties! error=%s\n", result );
            return result.getErrorId();
        }

        Array<VkLayerProperties> layers;
        if( !layers.tryCreate( pAllocator, layerCount ) )
        {
            return ErrorId_OutOfMemory;
        }
        result = pVulkan->vkEnumerateInstanceLayerProperties( &layerCount, layers.getStart() );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not enumerate instance layer properties! error=%s\n", result );
            return result.getErrorId();
        }

        if( !pInfo->layerNameHashes.tryCreate( pAllocator, layerCount ) )
        {
            return ErrorId_OutOfMemory;
        }
        for( size_t i = 0u; i < layers.getSize(); ++i )
        {
            KEEN_TRACE_INFO( "[graphics] Found vulkan instance layer '%s'\n", layers[ i ].layerName );
            insertHashString( &pInfo->layerNameHashes, layers[ i ].layerName );
        }

        if( !pInfo->extensionNameHashes.tryCreate( pAllocator, 256u ) )
        {
            return ErrorId_OutOfMemory;
        }
        return fillInstanceLayerExtensionInfo( pInfo, pAllocator, pVulkan, nullptr );
    }

    ErrorId vulkan::fillInstanceLayerExtensionInfo( VulkanLayerExtensionInfo* pInfo, MemoryAllocator* pAllocator, VulkanApi* pVulkan, const char* pLayerName )
    {
        KEEN_ASSERT( pInfo != nullptr );
        KEEN_ASSERT( pVulkan != nullptr );

        uint32 extensionCount = 512u;
        VulkanResult result = pVulkan->vkEnumerateInstanceExtensionProperties( pLayerName, &extensionCount, nullptr );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not enumerate instance extensions#1! error=%s\n", result );
            return result.getErrorId();
        }

        Array<VkExtensionProperties> extensions;
        if( !extensions.tryCreate( pAllocator, extensionCount ) )
        {
            return ErrorId_OutOfMemory;
        }

        result = pVulkan->vkEnumerateInstanceExtensionProperties( pLayerName, &extensionCount, extensions.getStart() );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not enumerate instance extensions#2! error=%s\n", result );
            return result.getErrorId();
        }

        KEEN_ASSERT( pInfo->extensionNameHashes.isCreated() );
        pInfo->extensionNameHashes.ensureCapacity( pInfo->extensionNameHashes.getCount() + extensionCount );

        for( size_t i = 0u; i < extensions.getSize(); ++i )
        {
            KEEN_TRACE_INFO( "[graphics] Found vulkan instance extension '%s'\n", extensions[ i ].extensionName );
            insertHashString( &pInfo->extensionNameHashes, extensions[ i ].extensionName );
        }

        return ErrorId_Ok;
    }

    ErrorId vulkan::loadInstanceFunctions( VulkanApi* pVulkan, VkInstance instance, const ArrayView<const char*> activeExtensions )
    {
        StickyError error;
        pVulkan->vkEnumeratePhysicalDevices                     = (PFN_vkEnumeratePhysicalDevices)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkEnumeratePhysicalDevices" );
        pVulkan->vkGetPhysicalDeviceFeatures2                   = (PFN_vkGetPhysicalDeviceFeatures2)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceFeatures2" );
        pVulkan->vkDestroyInstance                              = (PFN_vkDestroyInstance)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkDestroyInstance" );
        pVulkan->vkGetPhysicalDeviceFormatProperties            = (PFN_vkGetPhysicalDeviceFormatProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceFormatProperties" );
        pVulkan->vkGetPhysicalDeviceImageFormatProperties       = (PFN_vkGetPhysicalDeviceImageFormatProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceImageFormatProperties" );
        pVulkan->vkGetPhysicalDeviceProperties                  = (PFN_vkGetPhysicalDeviceProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceProperties" );
        pVulkan->vkGetPhysicalDeviceProperties2                 = (PFN_vkGetPhysicalDeviceProperties2)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceProperties2" );
        pVulkan->vkGetPhysicalDeviceQueueFamilyProperties       = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceQueueFamilyProperties" );
        pVulkan->vkGetPhysicalDeviceMemoryProperties            = (PFN_vkGetPhysicalDeviceMemoryProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceMemoryProperties" );
        pVulkan->vkGetPhysicalDeviceMemoryProperties2           = (PFN_vkGetPhysicalDeviceMemoryProperties2)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceMemoryProperties2" );
        pVulkan->vkCreateDevice                                 = (PFN_vkCreateDevice)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCreateDevice" );
        pVulkan->vkEnumerateDeviceExtensionProperties           = (PFN_vkEnumerateDeviceExtensionProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkEnumerateDeviceExtensionProperties" );
        pVulkan->vkGetPhysicalDeviceSparseImageFormatProperties = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceSparseImageFormatProperties" );
        pVulkan->vkGetDeviceProcAddr                            = (PFN_vkGetDeviceProcAddr)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetDeviceProcAddr" );

        // load function pointer of supported instance extensions:
        pVulkan->KHR_surface = isExtensionActive( activeExtensions, VK_KHR_SURFACE_EXTENSION_NAME );
#if defined( VK_KHR_surface )
        if( pVulkan->KHR_surface )
        {
            pVulkan->vkDestroySurfaceKHR                        = (PFN_vkDestroySurfaceKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkDestroySurfaceKHR" );
            pVulkan->vkGetPhysicalDeviceSurfaceSupportKHR       = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceSurfaceSupportKHR" );
            pVulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR  = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR" );
            pVulkan->vkGetPhysicalDeviceSurfaceFormatsKHR       = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceSurfaceFormatsKHR" );
            pVulkan->vkGetPhysicalDeviceSurfacePresentModesKHR  = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceSurfacePresentModesKHR" );
        }
#endif

#if defined( VK_KHR_win32_surface )
        pVulkan->KHR_win32_surface = isExtensionActive( activeExtensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
        if( pVulkan->KHR_win32_surface )
        {
            pVulkan->vkCreateWin32SurfaceKHR                        = (PFN_vkCreateWin32SurfaceKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCreateWin32SurfaceKHR" );
            pVulkan->vkGetPhysicalDeviceWin32PresentationSupportKHR = (PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR" );
        }
#endif

#if defined( VK_KHR_xlib_surface )
        pVulkan->KHR_xlib_surface = isExtensionActive( activeExtensions, VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
        if( pVulkan->KHR_xlib_surface )
        {
            pVulkan->vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCreateXlibSurfaceKHR" );
            pVulkan->vkGetPhysicalDeviceXlibPresentationSupportKHR = (PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceXlibPresentationSupportKHR" );
        }
#endif

#if defined( VK_KHR_wayland_surface )
        pVulkan->KHR_wayland_surface = isExtensionActive( activeExtensions, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME );
        if( pVulkan->KHR_wayland_surface )
        {
            pVulkan->vkCreateWaylandSurfaceKHR  = (PFN_vkCreateWaylandSurfaceKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCreateWaylandSurfaceKHR" );
            pVulkan->vkGetPhysicalDeviceWaylandPresentationSupportKHR = (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkGetPhysicalDeviceWaylandPresentationSupportKHR" );
        }
#endif

#if defined( VK_KHR_android_surface )
        pVulkan->KHR_android_surface = isExtensionActive( activeExtensions, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME );
        if( pVulkan->KHR_android_surface )
        {
            pVulkan->vkCreateAndroidSurfaceKHR = (PFN_vkCreateAndroidSurfaceKHR)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCreateAndroidSurfaceKHR" );
        }
#endif

#if defined( VK_EXT_debug_utils )
        pVulkan->EXT_debug_utils = isExtensionActive( activeExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        if( pVulkan->EXT_debug_utils )
        {
            pVulkan->vkSetDebugUtilsObjectNameEXT       = (PFN_vkSetDebugUtilsObjectNameEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkSetDebugUtilsObjectNameEXT" );
            pVulkan->vkSetDebugUtilsObjectTagEXT        = (PFN_vkSetDebugUtilsObjectTagEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkSetDebugUtilsObjectTagEXT" );
            pVulkan->vkQueueBeginDebugUtilsLabelEXT     = (PFN_vkQueueBeginDebugUtilsLabelEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkQueueBeginDebugUtilsLabelEXT" );
            pVulkan->vkQueueEndDebugUtilsLabelEXT       = (PFN_vkQueueEndDebugUtilsLabelEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkQueueEndDebugUtilsLabelEXT" );
            pVulkan->vkQueueInsertDebugUtilsLabelEXT    = (PFN_vkQueueInsertDebugUtilsLabelEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkQueueInsertDebugUtilsLabelEXT" );
            pVulkan->vkCmdBeginDebugUtilsLabelEXT       = (PFN_vkCmdBeginDebugUtilsLabelEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCmdBeginDebugUtilsLabelEXT" );
            pVulkan->vkCmdEndDebugUtilsLabelEXT         = (PFN_vkCmdEndDebugUtilsLabelEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCmdEndDebugUtilsLabelEXT" );
            pVulkan->vkCmdInsertDebugUtilsLabelEXT      = (PFN_vkCmdInsertDebugUtilsLabelEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCmdInsertDebugUtilsLabelEXT" );
            pVulkan->vkCreateDebugUtilsMessengerEXT     = (PFN_vkCreateDebugUtilsMessengerEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkCreateDebugUtilsMessengerEXT" );
            pVulkan->vkDestroyDebugUtilsMessengerEXT    = (PFN_vkDestroyDebugUtilsMessengerEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkDestroyDebugUtilsMessengerEXT" );
            pVulkan->vkSubmitDebugUtilsMessageEXT       = (PFN_vkSubmitDebugUtilsMessageEXT)(void*)getVulkanInstanceProcAddress( &error, pVulkan, instance, "vkSubmitDebugUtilsMessageEXT" );
        }
#endif

        return error.getError();
    }

    ErrorId vulkan::fillDeviceInfo( VulkanLayerExtensionInfo* pInfo, MemoryAllocator* pAllocator, VulkanApi* pVulkan, VkPhysicalDevice physicalDevice )
    {
        KEEN_ASSERT( pInfo != nullptr );
        KEEN_ASSERT( pVulkan != nullptr );

        // check device extensions:
        uint32 extensionCount = 512u;
        VulkanResult result = pVulkan->vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &extensionCount, nullptr );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not enumerate device extensions#1! error=%s\n", result );
            return result.getErrorId();
        }

        Array<VkExtensionProperties> extensions;
        if( !extensions.tryCreate( pAllocator, extensionCount ) )
        {
            return ErrorId_OutOfMemory;
        }

        result = pVulkan->vkEnumerateDeviceExtensionProperties( physicalDevice, nullptr, &extensionCount, extensions.getStart() );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "Could not enumerate device extensions#2! error=%s\n", result );
            return result.getErrorId();
        }

        if( !pInfo->extensionNameHashes.tryCreate( pAllocator, extensionCount ) )
        {
            return ErrorId_OutOfMemory;
        }
        for( size_t i = 0u; i < extensions.getSize(); ++i )
        {
            KEEN_TRACE_INFO( "[graphics] Found vulkan device extension '%s'\n", extensions[ i ].extensionName );
            insertHashString( &pInfo->extensionNameHashes, extensions[ i ].extensionName );
        }

        return ErrorId_Ok;
    }

    ErrorId vulkan::loadDeviceFunctions( VulkanApi* pVulkan, VkPhysicalDevice physicalDevice, VkDevice device, const ArrayView<const char*> activeExtensions )
    {
        pVulkan->physicalDevice = physicalDevice;
        pVulkan->device         = device;

        StickyError error;
        pVulkan->vkDestroyDevice                    = (PFN_vkDestroyDevice)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyDevice" );
        pVulkan->vkGetDeviceQueue                   = (PFN_vkGetDeviceQueue)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetDeviceQueue" );
        pVulkan->vkQueueSubmit                      = (PFN_vkQueueSubmit)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkQueueSubmit" );
        pVulkan->vkQueueWaitIdle                    = (PFN_vkQueueWaitIdle)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkQueueWaitIdle" );
        pVulkan->vkDeviceWaitIdle                   = (PFN_vkDeviceWaitIdle)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDeviceWaitIdle" );
        pVulkan->vkAllocateMemory                   = (PFN_vkAllocateMemory)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkAllocateMemory" );
        pVulkan->vkFreeMemory                       = (PFN_vkFreeMemory)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkFreeMemory" );
        pVulkan->vkMapMemory                        = (PFN_vkMapMemory)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkMapMemory" );
        pVulkan->vkUnmapMemory                      = (PFN_vkUnmapMemory)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkUnmapMemory" );
        pVulkan->vkFlushMappedMemoryRanges          = (PFN_vkFlushMappedMemoryRanges)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkFlushMappedMemoryRanges" );
        pVulkan->vkInvalidateMappedMemoryRanges     = (PFN_vkInvalidateMappedMemoryRanges)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkInvalidateMappedMemoryRanges" );
        pVulkan->vkGetDeviceMemoryCommitment        = (PFN_vkGetDeviceMemoryCommitment)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetDeviceMemoryCommitment" );
        pVulkan->vkBindBufferMemory                 = (PFN_vkBindBufferMemory)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkBindBufferMemory" );
        pVulkan->vkBindBufferMemory2                = (PFN_vkBindBufferMemory2)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkBindBufferMemory2" );
        pVulkan->vkBindImageMemory                  = (PFN_vkBindImageMemory)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkBindImageMemory" );
        pVulkan->vkBindImageMemory2                 = (PFN_vkBindImageMemory2)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkBindImageMemory2" );
        pVulkan->vkGetBufferMemoryRequirements      = (PFN_vkGetBufferMemoryRequirements)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetBufferMemoryRequirements" );
        pVulkan->vkGetBufferMemoryRequirements2     = (PFN_vkGetBufferMemoryRequirements2)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetBufferMemoryRequirements2" );
        pVulkan->vkGetImageMemoryRequirements       = (PFN_vkGetImageMemoryRequirements)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetImageMemoryRequirements" );
        pVulkan->vkGetImageMemoryRequirements2      = (PFN_vkGetImageMemoryRequirements2)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetImageMemoryRequirements2" );
        pVulkan->vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetImageSparseMemoryRequirements" );
        pVulkan->vkQueueBindSparse                  = (PFN_vkQueueBindSparse)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkQueueBindSparse" );
        pVulkan->vkCreateFence                      = (PFN_vkCreateFence)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateFence" );
        pVulkan->vkDestroyFence                     = (PFN_vkDestroyFence)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyFence" );
        pVulkan->vkResetFences                      = (PFN_vkResetFences)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkResetFences" );
        pVulkan->vkGetFenceStatus                   = (PFN_vkGetFenceStatus)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetFenceStatus" );
        pVulkan->vkWaitForFences                    = (PFN_vkWaitForFences)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkWaitForFences" );
        pVulkan->vkCreateSemaphore                  = (PFN_vkCreateSemaphore)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateSemaphore" );
        pVulkan->vkDestroySemaphore                 = (PFN_vkDestroySemaphore)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroySemaphore" );
        pVulkan->vkGetSemaphoreCounterValue         = (PFN_vkGetSemaphoreCounterValue)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetSemaphoreCounterValue" );
        pVulkan->vkWaitSemaphores                   = (PFN_vkWaitSemaphores)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkWaitSemaphores" );
        pVulkan->vkCreateEvent                      = (PFN_vkCreateEvent)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateEvent" );
        pVulkan->vkDestroyEvent                     = (PFN_vkDestroyEvent)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyEvent" );
        pVulkan->vkGetEventStatus                   = (PFN_vkGetEventStatus)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetEventStatus" );
        pVulkan->vkSetEvent                         = (PFN_vkSetEvent)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkSetEvent" );
        pVulkan->vkResetEvent                       = (PFN_vkResetEvent)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkResetEvent" );
        pVulkan->vkCreateQueryPool                  = (PFN_vkCreateQueryPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateQueryPool" );
        pVulkan->vkDestroyQueryPool                 = (PFN_vkDestroyQueryPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyQueryPool" );
        pVulkan->vkGetQueryPoolResults              = (PFN_vkGetQueryPoolResults)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetQueryPoolResults" );
        pVulkan->vkCreateBuffer                     = (PFN_vkCreateBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateBuffer" );
        pVulkan->vkDestroyBuffer                    = (PFN_vkDestroyBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyBuffer" );
        pVulkan->vkGetBufferDeviceAddress           = (PFN_vkGetBufferDeviceAddress)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetBufferDeviceAddress" );
        pVulkan->vkCreateBufferView                 = (PFN_vkCreateBufferView)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateBufferView" );
        pVulkan->vkDestroyBufferView                = (PFN_vkDestroyBufferView)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyBufferView" );
        pVulkan->vkCreateImage                      = (PFN_vkCreateImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateImage" );
        pVulkan->vkDestroyImage                     = (PFN_vkDestroyImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyImage" );
        pVulkan->vkGetImageSubresourceLayout        = (PFN_vkGetImageSubresourceLayout)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetImageSubresourceLayout" );
        pVulkan->vkCreateImageView                  = (PFN_vkCreateImageView)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateImageView" );
        pVulkan->vkDestroyImageView                 = (PFN_vkDestroyImageView)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyImageView" );
        pVulkan->vkCreateShaderModule               = (PFN_vkCreateShaderModule)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateShaderModule" );
        pVulkan->vkDestroyShaderModule              = (PFN_vkDestroyShaderModule)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyShaderModule" );
        pVulkan->vkCreatePipelineCache              = (PFN_vkCreatePipelineCache)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreatePipelineCache" );
        pVulkan->vkDestroyPipelineCache             = (PFN_vkDestroyPipelineCache)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyPipelineCache" );
        pVulkan->vkGetPipelineCacheData             = (PFN_vkGetPipelineCacheData)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetPipelineCacheData" );
        pVulkan->vkMergePipelineCaches              = (PFN_vkMergePipelineCaches)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkMergePipelineCaches" );
        pVulkan->vkCreateGraphicsPipelines          = (PFN_vkCreateGraphicsPipelines)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateGraphicsPipelines" );
        pVulkan->vkCreateComputePipelines           = (PFN_vkCreateComputePipelines)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateComputePipelines" );
        pVulkan->vkDestroyPipeline                  = (PFN_vkDestroyPipeline)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyPipeline" );
        pVulkan->vkCreatePipelineLayout             = (PFN_vkCreatePipelineLayout)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreatePipelineLayout" );
        pVulkan->vkDestroyPipelineLayout            = (PFN_vkDestroyPipelineLayout)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyPipelineLayout" );
        pVulkan->vkCreateSampler                    = (PFN_vkCreateSampler)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateSampler" );
        pVulkan->vkDestroySampler                   = (PFN_vkDestroySampler)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroySampler" );
        pVulkan->vkCreateDescriptorSetLayout        = (PFN_vkCreateDescriptorSetLayout)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateDescriptorSetLayout" );
        pVulkan->vkDestroyDescriptorSetLayout       = (PFN_vkDestroyDescriptorSetLayout)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyDescriptorSetLayout" );
        pVulkan->vkCreateDescriptorPool             = (PFN_vkCreateDescriptorPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateDescriptorPool" );
        pVulkan->vkDestroyDescriptorPool            = (PFN_vkDestroyDescriptorPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyDescriptorPool" );
        pVulkan->vkResetDescriptorPool              = (PFN_vkResetDescriptorPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkResetDescriptorPool" );
        pVulkan->vkAllocateDescriptorSets           = (PFN_vkAllocateDescriptorSets)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkAllocateDescriptorSets" );
        pVulkan->vkFreeDescriptorSets               = (PFN_vkFreeDescriptorSets)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkFreeDescriptorSets" );
        pVulkan->vkUpdateDescriptorSets             = (PFN_vkUpdateDescriptorSets)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkUpdateDescriptorSets" );
        pVulkan->vkCreateFramebuffer                = (PFN_vkCreateFramebuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateFramebuffer" );
        pVulkan->vkDestroyFramebuffer               = (PFN_vkDestroyFramebuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyFramebuffer" );
        pVulkan->vkCreateRenderPass2                = (PFN_vkCreateRenderPass2)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateRenderPass2" );
        pVulkan->vkDestroyRenderPass                = (PFN_vkDestroyRenderPass)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyRenderPass" );
        pVulkan->vkGetRenderAreaGranularity         = (PFN_vkGetRenderAreaGranularity)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetRenderAreaGranularity" );
        pVulkan->vkCreateCommandPool                = (PFN_vkCreateCommandPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateCommandPool" );
        pVulkan->vkDestroyCommandPool               = (PFN_vkDestroyCommandPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroyCommandPool" );
        pVulkan->vkResetCommandPool                 = (PFN_vkResetCommandPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkResetCommandPool" );
        pVulkan->vkAllocateCommandBuffers           = (PFN_vkAllocateCommandBuffers)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkAllocateCommandBuffers" );
        pVulkan->vkFreeCommandBuffers               = (PFN_vkFreeCommandBuffers)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkFreeCommandBuffers" );
        pVulkan->vkBeginCommandBuffer               = (PFN_vkBeginCommandBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkBeginCommandBuffer" );
        pVulkan->vkEndCommandBuffer                 = (PFN_vkEndCommandBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkEndCommandBuffer" );
        pVulkan->vkResetCommandBuffer               = (PFN_vkResetCommandBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkResetCommandBuffer" );
        pVulkan->vkCmdBindPipeline                  = (PFN_vkCmdBindPipeline)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBindPipeline" );
        pVulkan->vkCmdSetViewport                   = (PFN_vkCmdSetViewport)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetViewport" );
        pVulkan->vkCmdSetScissor                    = (PFN_vkCmdSetScissor)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetScissor" );
        pVulkan->vkCmdSetLineWidth                  = (PFN_vkCmdSetLineWidth)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetLineWidth" );
        pVulkan->vkCmdSetDepthBias                  = (PFN_vkCmdSetDepthBias)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetDepthBias" );
        pVulkan->vkCmdSetBlendConstants             = (PFN_vkCmdSetBlendConstants)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetBlendConstants" );
        pVulkan->vkCmdSetDepthBounds                = (PFN_vkCmdSetDepthBounds)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetDepthBounds" );
        pVulkan->vkCmdSetStencilCompareMask         = (PFN_vkCmdSetStencilCompareMask)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetStencilCompareMask" );
        pVulkan->vkCmdSetStencilWriteMask           = (PFN_vkCmdSetStencilWriteMask)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetStencilWriteMask" );
        pVulkan->vkCmdSetStencilReference           = (PFN_vkCmdSetStencilReference)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetStencilReference" );
        pVulkan->vkCmdBindDescriptorSets            = (PFN_vkCmdBindDescriptorSets)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBindDescriptorSets" );
        pVulkan->vkCmdBindIndexBuffer               = (PFN_vkCmdBindIndexBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBindIndexBuffer" );
        pVulkan->vkCmdBindVertexBuffers             = (PFN_vkCmdBindVertexBuffers)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBindVertexBuffers" );
        pVulkan->vkCmdDraw                          = (PFN_vkCmdDraw)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDraw" );
        pVulkan->vkCmdDrawIndexed                   = (PFN_vkCmdDrawIndexed)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDrawIndexed" );
        pVulkan->vkCmdDrawIndirect                  = (PFN_vkCmdDrawIndirect)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDrawIndirect" );
        pVulkan->vkCmdDrawIndirectCount             = (PFN_vkCmdDrawIndirectCount)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDrawIndirectCount" );
        pVulkan->vkCmdDrawIndexedIndirect           = (PFN_vkCmdDrawIndexedIndirect)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDrawIndexedIndirect" );
        pVulkan->vkCmdDrawIndexedIndirectCount      = (PFN_vkCmdDrawIndexedIndirectCount)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDrawIndexedIndirectCount" );
        pVulkan->vkCmdDispatch                      = (PFN_vkCmdDispatch)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDispatch" );
        pVulkan->vkCmdDispatchIndirect              = (PFN_vkCmdDispatchIndirect)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdDispatchIndirect" );
        pVulkan->vkCmdCopyBuffer                    = (PFN_vkCmdCopyBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdCopyBuffer" );
        pVulkan->vkCmdCopyImage                     = (PFN_vkCmdCopyImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdCopyImage" );
        pVulkan->vkCmdBlitImage                     = (PFN_vkCmdBlitImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBlitImage" );
        pVulkan->vkCmdCopyBufferToImage             = (PFN_vkCmdCopyBufferToImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdCopyBufferToImage" );
        pVulkan->vkCmdCopyImageToBuffer             = (PFN_vkCmdCopyImageToBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdCopyImageToBuffer" );
        pVulkan->vkCmdUpdateBuffer                  = (PFN_vkCmdUpdateBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdUpdateBuffer" );
        pVulkan->vkCmdFillBuffer                    = (PFN_vkCmdFillBuffer)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdFillBuffer" );
        pVulkan->vkCmdClearColorImage               = (PFN_vkCmdClearColorImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdClearColorImage" );
        pVulkan->vkCmdClearDepthStencilImage        = (PFN_vkCmdClearDepthStencilImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdClearDepthStencilImage" );
        pVulkan->vkCmdClearAttachments              = (PFN_vkCmdClearAttachments)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdClearAttachments" );
        pVulkan->vkCmdResolveImage                  = (PFN_vkCmdResolveImage)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdResolveImage" );
        pVulkan->vkCmdSetEvent                      = (PFN_vkCmdSetEvent)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetEvent" );
        pVulkan->vkCmdResetEvent                    = (PFN_vkCmdResetEvent)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdResetEvent" );
        pVulkan->vkCmdWaitEvents                    = (PFN_vkCmdWaitEvents)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdWaitEvents" );
        pVulkan->vkCmdPipelineBarrier               = (PFN_vkCmdPipelineBarrier)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdPipelineBarrier" );
        pVulkan->vkCmdBeginQuery                    = (PFN_vkCmdBeginQuery)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBeginQuery" );
        pVulkan->vkCmdEndQuery                      = (PFN_vkCmdEndQuery)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdEndQuery" );
        pVulkan->vkCmdResetQueryPool                = (PFN_vkCmdResetQueryPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdResetQueryPool" );
        pVulkan->vkCmdWriteTimestamp                = (PFN_vkCmdWriteTimestamp)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdWriteTimestamp" );
        pVulkan->vkCmdCopyQueryPoolResults          = (PFN_vkCmdCopyQueryPoolResults)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdCopyQueryPoolResults" );
        pVulkan->vkCmdPushConstants                 = (PFN_vkCmdPushConstants)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdPushConstants" );
        pVulkan->vkCmdBeginRenderPass               = (PFN_vkCmdBeginRenderPass)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBeginRenderPass" );
        pVulkan->vkCmdNextSubpass                   = (PFN_vkCmdNextSubpass)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdNextSubpass" );
        pVulkan->vkCmdEndRenderPass                 = (PFN_vkCmdEndRenderPass)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdEndRenderPass" );
        pVulkan->vkCmdExecuteCommands               = (PFN_vkCmdExecuteCommands)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdExecuteCommands" );

        pVulkan->vkResetQueryPool                   = (PFN_vkResetQueryPool)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkResetQueryPool" );

#if defined( VK_KHR_swapchain )
        pVulkan->KHR_swapchain = isExtensionActive( activeExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME );
        if( pVulkan->KHR_swapchain )
        {
            pVulkan->vkCreateSwapchainKHR           = (PFN_vkCreateSwapchainKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCreateSwapchainKHR" );
            pVulkan->vkDestroySwapchainKHR          = (PFN_vkDestroySwapchainKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkDestroySwapchainKHR" );
            pVulkan->vkGetSwapchainImagesKHR        = (PFN_vkGetSwapchainImagesKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetSwapchainImagesKHR" );
            pVulkan->vkAcquireNextImageKHR          = (PFN_vkAcquireNextImageKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkAcquireNextImageKHR" );
            pVulkan->vkQueuePresentKHR              = (PFN_vkQueuePresentKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkQueuePresentKHR" );
        }
#endif

#if defined( VK_KHR_dynamic_rendering )
        pVulkan->KHR_dynamic_rendering  = isExtensionActive( activeExtensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME );
        pVulkan->vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdBeginRenderingKHR" );
        pVulkan->vkCmdEndRenderingKHR   = (PFN_vkCmdEndRenderingKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdEndRenderingKHR" );
#endif


#if defined( VK_EXT_memory_budget )
        pVulkan->EXT_memory_budget = isExtensionActive( activeExtensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
#endif

#if defined( VK_NV_device_diagnostic_checkpoints )
        pVulkan->NV_device_diagnostic_checkpoints = isExtensionActive( activeExtensions, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME );
        if( pVulkan->NV_device_diagnostic_checkpoints )
        {
            pVulkan->vkCmdSetCheckpointNV           = (PFN_vkCmdSetCheckpointNV)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdSetCheckpointNV" );
            pVulkan->vkGetQueueCheckpointDataNV     = (PFN_vkGetQueueCheckpointDataNV)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetQueueCheckpointDataNV" );
        }
#endif

#if defined( VK_AMD_buffer_marker )
        pVulkan->AMD_buffer_marker = isExtensionActive( activeExtensions, VK_AMD_BUFFER_MARKER_EXTENSION_NAME );
        if( pVulkan->AMD_buffer_marker )
        {
            pVulkan->vkCmdWriteBufferMarkerAMD = (PFN_vkCmdWriteBufferMarkerAMD)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkCmdWriteBufferMarkerAMD" );
        }
#endif

#if defined( VK_EXT_device_fault )
        pVulkan->EXT_device_fault = isExtensionActive( activeExtensions, VK_EXT_DEVICE_FAULT_EXTENSION_NAME );
        if( pVulkan->EXT_device_fault )
        {
            pVulkan->vkGetDeviceFaultInfoEXT = (PFN_vkGetDeviceFaultInfoEXT)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetDeviceFaultInfoEXT" );
        }
#endif

#if defined( VK_KHR_pipeline_executable_properties )
        pVulkan->KHR_pipeline_executable_properties = isExtensionActive( activeExtensions, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME );
        if( pVulkan->KHR_pipeline_executable_properties )
        {
            pVulkan->vkGetPipelineExecutableInternalRepresentationsKHR  = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetPipelineExecutableInternalRepresentationsKHR" );
            pVulkan->vkGetPipelineExecutablePropertiesKHR               = (PFN_vkGetPipelineExecutablePropertiesKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetPipelineExecutablePropertiesKHR" );
            pVulkan->vkGetPipelineExecutableStatisticsKHR               = (PFN_vkGetPipelineExecutableStatisticsKHR)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetPipelineExecutableStatisticsKHR" );
        }
#endif

#if defined( VK_AMD_shader_info )
        pVulkan->AMD_shader_info = isExtensionActive( activeExtensions, VK_AMD_SHADER_INFO_EXTENSION_NAME );
        if( pVulkan->AMD_shader_info )
        {
            pVulkan->vkGetShaderInfoAMD = (PFN_vkGetShaderInfoAMD)(void*)getVulkanDeviceProcAddress( &error, pVulkan, device, "vkGetShaderInfoAMD" );
        }
#endif

        return error.getError();
    }

#if KEEN_USING( KEEN_VULKAN_OBJECT_NAMES )
    void vulkan::setObjectName( VulkanApi* pVulkan, VkDevice device, VkObjectHandle pObject, VkObjectType objectType, const DebugName& name )
    {
        KEEN_UNUSED5( pVulkan, device, pObject, objectType, name );

        if( pVulkan->EXT_debug_utils )
        {
#if defined( VK_EXT_debug_utils )
            VkDebugUtilsObjectNameInfoEXT nameInfo ={ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType     = objectType;
            nameInfo.objectHandle   = (uint64_t)pObject;
            nameInfo.pObjectName    = name.hasElements() ? name.getCName() : "";
            pVulkan->vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
#endif
        }
    }
#endif

#if KEEN_USING( KEEN_VULKAN_DEBUG_LABELS )
    void vulkan::beginDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const DebugName& name, const float4& color )
    {
        KEEN_UNUSED4( pVulkan, commandBuffer, name, color );

        if( pVulkan->EXT_debug_utils )
        {
#if defined( VK_EXT_debug_utils )
            VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
            label.pLabelName = name.hasElements() ? name.getCName() : "";
            label.color[ 0u ] = color.x;
            label.color[ 1u ] = color.y;
            label.color[ 2u ] = color.z;
            label.color[ 3u ] = color.w;
            pVulkan->vkCmdBeginDebugUtilsLabelEXT( commandBuffer, &label );
#endif
        }
    }

    void vulkan::endDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer )
    {
        KEEN_UNUSED2( pVulkan, commandBuffer );
        if( pVulkan->EXT_debug_utils )
        {
#if defined( VK_EXT_debug_utils )
            pVulkan->vkCmdEndDebugUtilsLabelEXT( commandBuffer );
#endif
        }
    }

    void vulkan::insertDebugLabel( VulkanApi* pVulkan, VkCommandBuffer commandBuffer, const DebugName& name, const float4& color )
    {
        KEEN_UNUSED4( pVulkan, commandBuffer, name, color );
        if( pVulkan->EXT_debug_utils )
        {
#if defined( VK_EXT_debug_utils )
            VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
            label.pLabelName = name.hasElements() ? name.getCName() : "";
            label.color[ 0u ] = color.x;
            label.color[ 1u ] = color.y;
            label.color[ 2u ] = color.z;
            label.color[ 3u ] = color.w;
            pVulkan->vkCmdInsertDebugUtilsLabelEXT( commandBuffer, &label );
#endif
        }
    }
#endif

    void vulkan::destroyVulkanApi( MemoryAllocator* pAllocator, VulkanApi* pVulkan )
    {
        if( pVulkan->pLibrary != nullptr )
        {
            os::freeDynamicLibrary( { pVulkan->pLibrary } );
        }

        deleteObject( pAllocator, pVulkan );
    }

    VkDeviceAddress vulkan::getBufferDeviceAddress( VulkanApi* pVulkan, VkDevice device, VkBuffer buffer )
    {
        VkBufferDeviceAddressInfo bufferDeviceAI{};
        bufferDeviceAI.sType    = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bufferDeviceAI.buffer   = buffer;
        return pVulkan->vkGetBufferDeviceAddress( device, &bufferDeviceAI );
    }

    const char* vulkan::getVkFormatString( VkFormat format )
    {
        switch( format )
        {
        case VK_FORMAT_UNDEFINED: return "VK_FORMAT_UNDEFINED";
        case VK_FORMAT_R4G4_UNORM_PACK8: return "VK_FORMAT_R4G4_UNORM_PACK8";
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16: return "VK_FORMAT_R4G4B4A4_UNORM_PACK16";
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return "VK_FORMAT_B4G4R4A4_UNORM_PACK16";
        case VK_FORMAT_R5G6B5_UNORM_PACK16: return "VK_FORMAT_R5G6B5_UNORM_PACK16";
        case VK_FORMAT_B5G6R5_UNORM_PACK16: return "VK_FORMAT_B5G6R5_UNORM_PACK16";
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return "VK_FORMAT_R5G5B5A1_UNORM_PACK16";
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return "VK_FORMAT_B5G5R5A1_UNORM_PACK16";
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return "VK_FORMAT_A1R5G5B5_UNORM_PACK16";
        case VK_FORMAT_R8_UNORM: return "VK_FORMAT_R8_UNORM";
        case VK_FORMAT_R8_SNORM: return "VK_FORMAT_R8_SNORM";
        case VK_FORMAT_R8_USCALED: return "VK_FORMAT_R8_USCALED";
        case VK_FORMAT_R8_SSCALED: return "VK_FORMAT_R8_SSCALED";
        case VK_FORMAT_R8_UINT: return "VK_FORMAT_R8_UINT";
        case VK_FORMAT_R8_SINT: return "VK_FORMAT_R8_SINT";
        case VK_FORMAT_R8_SRGB: return "VK_FORMAT_R8_SRGB";
        case VK_FORMAT_R8G8_UNORM: return "VK_FORMAT_R8G8_UNORM";
        case VK_FORMAT_R8G8_SNORM: return "VK_FORMAT_R8G8_SNORM";
        case VK_FORMAT_R8G8_USCALED: return "VK_FORMAT_R8G8_USCALED";
        case VK_FORMAT_R8G8_SSCALED: return "VK_FORMAT_R8G8_SSCALED";
        case VK_FORMAT_R8G8_UINT: return "VK_FORMAT_R8G8_UINT";
        case VK_FORMAT_R8G8_SINT: return "VK_FORMAT_R8G8_SINT";
        case VK_FORMAT_R8G8_SRGB: return "VK_FORMAT_R8G8_SRGB";
        case VK_FORMAT_R8G8B8_UNORM: return "VK_FORMAT_R8G8B8_UNORM";
        case VK_FORMAT_R8G8B8_SNORM: return "VK_FORMAT_R8G8B8_SNORM";
        case VK_FORMAT_R8G8B8_USCALED: return "VK_FORMAT_R8G8B8_USCALED";
        case VK_FORMAT_R8G8B8_SSCALED: return "VK_FORMAT_R8G8B8_SSCALED";
        case VK_FORMAT_R8G8B8_UINT: return "VK_FORMAT_R8G8B8_UINT";
        case VK_FORMAT_R8G8B8_SINT: return "VK_FORMAT_R8G8B8_SINT";
        case VK_FORMAT_R8G8B8_SRGB: return "VK_FORMAT_R8G8B8_SRGB";
        case VK_FORMAT_B8G8R8_UNORM: return "VK_FORMAT_B8G8R8_UNORM";
        case VK_FORMAT_B8G8R8_SNORM: return "VK_FORMAT_B8G8R8_SNORM";
        case VK_FORMAT_B8G8R8_USCALED: return "VK_FORMAT_B8G8R8_USCALED";
        case VK_FORMAT_B8G8R8_SSCALED: return "VK_FORMAT_B8G8R8_SSCALED";
        case VK_FORMAT_B8G8R8_UINT: return "VK_FORMAT_B8G8R8_UINT";
        case VK_FORMAT_B8G8R8_SINT: return "VK_FORMAT_B8G8R8_SINT";
        case VK_FORMAT_B8G8R8_SRGB: return "VK_FORMAT_B8G8R8_SRGB";
        case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SNORM: return "VK_FORMAT_R8G8B8A8_SNORM";
        case VK_FORMAT_R8G8B8A8_USCALED: return "VK_FORMAT_R8G8B8A8_USCALED";
        case VK_FORMAT_R8G8B8A8_SSCALED: return "VK_FORMAT_R8G8B8A8_SSCALED";
        case VK_FORMAT_R8G8B8A8_UINT: return "VK_FORMAT_R8G8B8A8_UINT";
        case VK_FORMAT_R8G8B8A8_SINT: return "VK_FORMAT_R8G8B8A8_SINT";
        case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
        case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SNORM: return "VK_FORMAT_B8G8R8A8_SNORM";
        case VK_FORMAT_B8G8R8A8_USCALED: return "VK_FORMAT_B8G8R8A8_USCALED";
        case VK_FORMAT_B8G8R8A8_SSCALED: return "VK_FORMAT_B8G8R8A8_SSCALED";
        case VK_FORMAT_B8G8R8A8_UINT: return "VK_FORMAT_B8G8R8A8_UINT";
        case VK_FORMAT_B8G8R8A8_SINT: return "VK_FORMAT_B8G8R8A8_SINT";
        case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32: return "VK_FORMAT_A8B8G8R8_SNORM_PACK32";
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32: return "VK_FORMAT_A8B8G8R8_USCALED_PACK32";
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32: return "VK_FORMAT_A8B8G8R8_SSCALED_PACK32";
        case VK_FORMAT_A8B8G8R8_UINT_PACK32: return "VK_FORMAT_A8B8G8R8_UINT_PACK32";
        case VK_FORMAT_A8B8G8R8_SINT_PACK32: return "VK_FORMAT_A8B8G8R8_SINT_PACK32";
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return "VK_FORMAT_A8B8G8R8_SRGB_PACK32";
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32: return "VK_FORMAT_A2R10G10B10_SNORM_PACK32";
        case VK_FORMAT_A2R10G10B10_USCALED_PACK32: return "VK_FORMAT_A2R10G10B10_USCALED_PACK32";
        case VK_FORMAT_A2R10G10B10_SSCALED_PACK32: return "VK_FORMAT_A2R10G10B10_SSCALED_PACK32";
        case VK_FORMAT_A2R10G10B10_UINT_PACK32: return "VK_FORMAT_A2R10G10B10_UINT_PACK32";
        case VK_FORMAT_A2R10G10B10_SINT_PACK32: return "VK_FORMAT_A2R10G10B10_SINT_PACK32";
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32: return "VK_FORMAT_A2B10G10R10_SNORM_PACK32";
        case VK_FORMAT_A2B10G10R10_USCALED_PACK32: return "VK_FORMAT_A2B10G10R10_USCALED_PACK32";
        case VK_FORMAT_A2B10G10R10_SSCALED_PACK32: return "VK_FORMAT_A2B10G10R10_SSCALED_PACK32";
        case VK_FORMAT_A2B10G10R10_UINT_PACK32: return "VK_FORMAT_A2B10G10R10_UINT_PACK32";
        case VK_FORMAT_A2B10G10R10_SINT_PACK32: return "VK_FORMAT_A2B10G10R10_SINT_PACK32";
        case VK_FORMAT_R16_UNORM: return "VK_FORMAT_R16_UNORM";
        case VK_FORMAT_R16_SNORM: return "VK_FORMAT_R16_SNORM";
        case VK_FORMAT_R16_USCALED: return "VK_FORMAT_R16_USCALED";
        case VK_FORMAT_R16_SSCALED: return "VK_FORMAT_R16_SSCALED";
        case VK_FORMAT_R16_UINT: return "VK_FORMAT_R16_UINT";
        case VK_FORMAT_R16_SINT: return "VK_FORMAT_R16_SINT";
        case VK_FORMAT_R16_SFLOAT: return "VK_FORMAT_R16_SFLOAT";
        case VK_FORMAT_R16G16_UNORM: return "VK_FORMAT_R16G16_UNORM";
        case VK_FORMAT_R16G16_SNORM: return "VK_FORMAT_R16G16_SNORM";
        case VK_FORMAT_R16G16_USCALED: return "VK_FORMAT_R16G16_USCALED";
        case VK_FORMAT_R16G16_SSCALED: return "VK_FORMAT_R16G16_SSCALED";
        case VK_FORMAT_R16G16_UINT: return "VK_FORMAT_R16G16_UINT";
        case VK_FORMAT_R16G16_SINT: return "VK_FORMAT_R16G16_SINT";
        case VK_FORMAT_R16G16_SFLOAT: return "VK_FORMAT_R16G16_SFLOAT";
        case VK_FORMAT_R16G16B16_UNORM: return "VK_FORMAT_R16G16B16_UNORM";
        case VK_FORMAT_R16G16B16_SNORM: return "VK_FORMAT_R16G16B16_SNORM";
        case VK_FORMAT_R16G16B16_USCALED: return "VK_FORMAT_R16G16B16_USCALED";
        case VK_FORMAT_R16G16B16_SSCALED: return "VK_FORMAT_R16G16B16_SSCALED";
        case VK_FORMAT_R16G16B16_UINT: return "VK_FORMAT_R16G16B16_UINT";
        case VK_FORMAT_R16G16B16_SINT: return "VK_FORMAT_R16G16B16_SINT";
        case VK_FORMAT_R16G16B16_SFLOAT: return "VK_FORMAT_R16G16B16_SFLOAT";
        case VK_FORMAT_R16G16B16A16_UNORM: return "VK_FORMAT_R16G16B16A16_UNORM";
        case VK_FORMAT_R16G16B16A16_SNORM: return "VK_FORMAT_R16G16B16A16_SNORM";
        case VK_FORMAT_R16G16B16A16_USCALED: return "VK_FORMAT_R16G16B16A16_USCALED";
        case VK_FORMAT_R16G16B16A16_SSCALED: return "VK_FORMAT_R16G16B16A16_SSCALED";
        case VK_FORMAT_R16G16B16A16_UINT: return "VK_FORMAT_R16G16B16A16_UINT";
        case VK_FORMAT_R16G16B16A16_SINT: return "VK_FORMAT_R16G16B16A16_SINT";
        case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
        case VK_FORMAT_R32_UINT: return "VK_FORMAT_R32_UINT";
        case VK_FORMAT_R32_SINT: return "VK_FORMAT_R32_SINT";
        case VK_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
        case VK_FORMAT_R32G32_UINT: return "VK_FORMAT_R32G32_UINT";
        case VK_FORMAT_R32G32_SINT: return "VK_FORMAT_R32G32_SINT";
        case VK_FORMAT_R32G32_SFLOAT: return "VK_FORMAT_R32G32_SFLOAT";
        case VK_FORMAT_R32G32B32_UINT: return "VK_FORMAT_R32G32B32_UINT";
        case VK_FORMAT_R32G32B32_SINT: return "VK_FORMAT_R32G32B32_SINT";
        case VK_FORMAT_R32G32B32_SFLOAT: return "VK_FORMAT_R32G32B32_SFLOAT";
        case VK_FORMAT_R32G32B32A32_UINT: return "VK_FORMAT_R32G32B32A32_UINT";
        case VK_FORMAT_R32G32B32A32_SINT: return "VK_FORMAT_R32G32B32A32_SINT";
        case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
        case VK_FORMAT_R64_UINT: return "VK_FORMAT_R64_UINT";
        case VK_FORMAT_R64_SINT: return "VK_FORMAT_R64_SINT";
        case VK_FORMAT_R64_SFLOAT: return "VK_FORMAT_R64_SFLOAT";
        case VK_FORMAT_R64G64_UINT: return "VK_FORMAT_R64G64_UINT";
        case VK_FORMAT_R64G64_SINT: return "VK_FORMAT_R64G64_SINT";
        case VK_FORMAT_R64G64_SFLOAT: return "VK_FORMAT_R64G64_SFLOAT";
        case VK_FORMAT_R64G64B64_UINT: return "VK_FORMAT_R64G64B64_UINT";
        case VK_FORMAT_R64G64B64_SINT: return "VK_FORMAT_R64G64B64_SINT";
        case VK_FORMAT_R64G64B64_SFLOAT: return "VK_FORMAT_R64G64B64_SFLOAT";
        case VK_FORMAT_R64G64B64A64_UINT: return "VK_FORMAT_R64G64B64A64_UINT";
        case VK_FORMAT_R64G64B64A64_SINT: return "VK_FORMAT_R64G64B64A64_SINT";
        case VK_FORMAT_R64G64B64A64_SFLOAT: return "VK_FORMAT_R64G64B64A64_SFLOAT";
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "VK_FORMAT_B10G11R11_UFLOAT_PACK32";
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32";
        case VK_FORMAT_D16_UNORM: return "VK_FORMAT_D16_UNORM";
        case VK_FORMAT_X8_D24_UNORM_PACK32: return "VK_FORMAT_X8_D24_UNORM_PACK32";
        case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT";
        case VK_FORMAT_S8_UINT: return "VK_FORMAT_S8_UINT";
        case VK_FORMAT_D16_UNORM_S8_UINT: return "VK_FORMAT_D16_UNORM_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT: return "VK_FORMAT_D24_UNORM_S8_UINT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return "VK_FORMAT_BC1_RGB_UNORM_BLOCK";
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return "VK_FORMAT_BC1_RGB_SRGB_BLOCK";
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return "VK_FORMAT_BC1_RGBA_UNORM_BLOCK";
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return "VK_FORMAT_BC1_RGBA_SRGB_BLOCK";
        case VK_FORMAT_BC2_UNORM_BLOCK: return "VK_FORMAT_BC2_UNORM_BLOCK";
        case VK_FORMAT_BC2_SRGB_BLOCK: return "VK_FORMAT_BC2_SRGB_BLOCK";
        case VK_FORMAT_BC3_UNORM_BLOCK: return "VK_FORMAT_BC3_UNORM_BLOCK";
        case VK_FORMAT_BC3_SRGB_BLOCK: return "VK_FORMAT_BC3_SRGB_BLOCK";
        case VK_FORMAT_BC4_UNORM_BLOCK: return "VK_FORMAT_BC4_UNORM_BLOCK";
        case VK_FORMAT_BC4_SNORM_BLOCK: return "VK_FORMAT_BC4_SNORM_BLOCK";
        case VK_FORMAT_BC5_UNORM_BLOCK: return "VK_FORMAT_BC5_UNORM_BLOCK";
        case VK_FORMAT_BC5_SNORM_BLOCK: return "VK_FORMAT_BC5_SNORM_BLOCK";
        case VK_FORMAT_BC6H_UFLOAT_BLOCK: return "VK_FORMAT_BC6H_UFLOAT_BLOCK";
        case VK_FORMAT_BC6H_SFLOAT_BLOCK: return "VK_FORMAT_BC6H_SFLOAT_BLOCK";
        case VK_FORMAT_BC7_UNORM_BLOCK: return "VK_FORMAT_BC7_UNORM_BLOCK";
        case VK_FORMAT_BC7_SRGB_BLOCK: return "VK_FORMAT_BC7_SRGB_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK";
        case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK";
        case VK_FORMAT_EAC_R11_UNORM_BLOCK: return "VK_FORMAT_EAC_R11_UNORM_BLOCK";
        case VK_FORMAT_EAC_R11_SNORM_BLOCK: return "VK_FORMAT_EAC_R11_SNORM_BLOCK";
        case VK_FORMAT_EAC_R11G11_UNORM_BLOCK: return "VK_FORMAT_EAC_R11G11_UNORM_BLOCK";
        case VK_FORMAT_EAC_R11G11_SNORM_BLOCK: return "VK_FORMAT_EAC_R11G11_SNORM_BLOCK";
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return "VK_FORMAT_ASTC_4x4_UNORM_BLOCK";
        case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return "VK_FORMAT_ASTC_4x4_SRGB_BLOCK";
        case VK_FORMAT_ASTC_5x4_UNORM_BLOCK: return "VK_FORMAT_ASTC_5x4_UNORM_BLOCK";
        case VK_FORMAT_ASTC_5x4_SRGB_BLOCK: return "VK_FORMAT_ASTC_5x4_SRGB_BLOCK";
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_5x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_5x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_6x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_6x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_6x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_6x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_6x6_UNORM_BLOCK";
        case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_6x6_SRGB_BLOCK";
        case VK_FORMAT_ASTC_8x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_8x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_8x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x6_UNORM_BLOCK";
        case VK_FORMAT_ASTC_8x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x6_SRGB_BLOCK";
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x8_UNORM_BLOCK";
        case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x8_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x5_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x5_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x6_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x6_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x8_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x8_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x8_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x8_SRGB_BLOCK";
        case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x10_UNORM_BLOCK";
        case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x10_SRGB_BLOCK";
        case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: return "VK_FORMAT_ASTC_12x10_UNORM_BLOCK";
        case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: return "VK_FORMAT_ASTC_12x10_SRGB_BLOCK";
        case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: return "VK_FORMAT_ASTC_12x12_UNORM_BLOCK";
        case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: return "VK_FORMAT_ASTC_12x12_SRGB_BLOCK";
        case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG: return "VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG";
        case VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG";
        case VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG: return "VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG";
        default:    return "unknown";
        }
    }

    const char* vulkan::getVkColorSpaceString( VkColorSpaceKHR colorSpace )
    {
        switch( colorSpace )
        {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT: return "VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT";
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return "VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT";
        case VK_COLOR_SPACE_DCI_P3_LINEAR_EXT: return "VK_COLOR_SPACE_DCI_P3_LINEAR_EXT";
        case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT: return "VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT";
        case VK_COLOR_SPACE_BT709_LINEAR_EXT: return "VK_COLOR_SPACE_BT709_LINEAR_EXT";
        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT: return "VK_COLOR_SPACE_BT709_NONLINEAR_EXT";
        case VK_COLOR_SPACE_BT2020_LINEAR_EXT: return "VK_COLOR_SPACE_BT2020_LINEAR_EXT";
        case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
        case VK_COLOR_SPACE_DOLBYVISION_EXT: return "VK_COLOR_SPACE_DOLBYVISION_EXT";
        case VK_COLOR_SPACE_HDR10_HLG_EXT: return "VK_COLOR_SPACE_HDR10_HLG_EXT";
        case VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT: return "VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT";
        case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT: return "VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT";
        default:    return "unknown";
        }
    }

    const char* vulkan::getVkPresentModeString( VkPresentModeKHR presentMode )
    {
        switch( presentMode )
        {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
        case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
        default:    return "unknown";
        }
    }

    const char* vulkan::getVkPipelineStageFlagBitsString( VkPipelineStageFlagBits stage )
    {
        switch( stage )
        {
        case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:                         return "VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT";
        case VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT:                       return "VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT";
        case VK_PIPELINE_STAGE_VERTEX_INPUT_BIT:                        return "VK_PIPELINE_STAGE_VERTEX_INPUT_BIT";
        case VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:                       return "VK_PIPELINE_STAGE_VERTEX_SHADER_BIT";
        case VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT:         return "VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT";
        case VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT:      return "VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT";
        case VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT:                     return "VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT";
        case VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:                     return "VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT";
        case VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:                return "VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT";
        case VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:                 return "VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT";
        case VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:             return "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT";
        case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:                      return "VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT";
        case VK_PIPELINE_STAGE_TRANSFER_BIT:                            return "VK_PIPELINE_STAGE_TRANSFER_BIT";
        case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:                      return "VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT";
        case VK_PIPELINE_STAGE_HOST_BIT:                                return "VK_PIPELINE_STAGE_HOST_BIT";
        case VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT:                        return "VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT";
        case VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:                        return "VK_PIPELINE_STAGE_ALL_COMMANDS_BIT";
        case VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT:              return "VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT";
        case VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT:           return "VK_PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT";
        case VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR:              return "VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR";
        case VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR:    return "VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR";
        case VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV:               return "VK_PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV";
        case VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV:                      return "VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV";
        case VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV:                      return "VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV";
        default:                                                        return "unknown";
        }
    }

    const char* vulkan::getDeviceTypeString( VkPhysicalDeviceType deviceType )
    {
        switch( deviceType )
        {
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:             return "other";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:    return "integrated gpu";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:      return "discrete gpu";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       return "virtual gpu";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:               return "cpu";
        case VK_PHYSICAL_DEVICE_TYPE_MAX_ENUM:          break;
        }
        return "unknown";
    }

    VkFormat vulkan::getVertexAttributeFormat( VertexAttributeFormat format )
    {
        switch( format )
        {
        case VertexAttributeFormat_x32_float:               return VK_FORMAT_R32_SFLOAT;
        case VertexAttributeFormat_x32y32_float:            return VK_FORMAT_R32G32_SFLOAT;
        case VertexAttributeFormat_x32y32z32_float:         return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexAttributeFormat_x32y32z32w32_float:      return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexAttributeFormat_x16y16z16w16_float:      return VK_FORMAT_R16G16B16A16_SFLOAT;
        case VertexAttributeFormat_x16y16z16w16_sint:       return VK_FORMAT_R16G16B16A16_SINT;
        case VertexAttributeFormat_x16y16z16w16_uint:       return VK_FORMAT_R16G16B16A16_UINT;
        case VertexAttributeFormat_x16y16z16w16_snorm:      return VK_FORMAT_R16G16B16A16_SNORM;
        case VertexAttributeFormat_x16y16z16w16_unorm:      return VK_FORMAT_R16G16B16A16_UNORM;
        case VertexAttributeFormat_x8y8z8w8_unorm:          return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexAttributeFormat_x8y8z8w8_uint:           return VK_FORMAT_R8G8B8A8_UINT;
        case VertexAttributeFormat_x8y8z8w8_snorm:          return VK_FORMAT_R8G8B8A8_SNORM;
        case VertexAttributeFormat_x8y8z8w8_sint:           return VK_FORMAT_R8G8B8A8_SINT;
        case VertexAttributeFormat_x16y16_float:            return VK_FORMAT_R16G16_SFLOAT;
        case VertexAttributeFormat_x16y16_sint:             return VK_FORMAT_R16G16_SINT;
        case VertexAttributeFormat_x16y16_snorm:            return VK_FORMAT_R16G16_SNORM;
        case VertexAttributeFormat_x16y16_uint:             return VK_FORMAT_R16G16_UINT;
        case VertexAttributeFormat_x16y16_unorm:            return VK_FORMAT_R16G16_UNORM;
        case VertexAttributeFormat_x10y10z10_snorm:         return VK_FORMAT_A2B10G10R10_SNORM_PACK32; // not required to be supported according to the spec !!! (the _UNORM variant is though...)
        case VertexAttributeFormat_x10y10z10w2_snorm:       return VK_FORMAT_A2B10G10R10_SNORM_PACK32; // not required to be supported according to the spec !!! (the _UNORM variant is though...)
        case VertexAttributeFormat_x16y16z16_float:         return VK_FORMAT_R16G16B16_SFLOAT; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x16y16z16_sint:          return VK_FORMAT_R16G16B16_SINT; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x16y16z16_snorm:         return VK_FORMAT_R16G16B16_SNORM; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x16y16z16_uint:          return VK_FORMAT_R16G16B16_UINT; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x16y16z16_unorm:         return VK_FORMAT_R16G16B16_UNORM; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x8_sint:                 return VK_FORMAT_R8_SINT;
        case VertexAttributeFormat_x8_uint:                 return VK_FORMAT_R8_UINT;
        case VertexAttributeFormat_x8_snorm:                return VK_FORMAT_R8_SNORM;
        case VertexAttributeFormat_x8_unorm:                return VK_FORMAT_R8_UNORM;
        case VertexAttributeFormat_x8y8_sint:               return VK_FORMAT_R8G8_SINT;
        case VertexAttributeFormat_x8y8_uint:               return VK_FORMAT_R8G8_UINT;
        case VertexAttributeFormat_x8y8_snorm:              return VK_FORMAT_R8G8_SNORM;
        case VertexAttributeFormat_x8y8_unorm:              return VK_FORMAT_R8G8_UNORM;
        case VertexAttributeFormat_x8y8z8_sint:             return VK_FORMAT_R8G8B8_SINT; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x8y8z8_uint:             return VK_FORMAT_R8G8B8_UINT; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x8y8z8_snorm:            return VK_FORMAT_R8G8B8_SNORM; // not required to be supported according to the spec !!!
        case VertexAttributeFormat_x8y8z8_unorm:            return VK_FORMAT_R8G8B8_UNORM; // not required to be supported according to the spec !!!
        default:                                            break;
        }

        return VK_FORMAT_UNDEFINED;
    }

    VkIndexType vulkan::getIndexType( GraphicsIndexFormat format )
    {
        switch( format )
        {
        case GraphicsIndexFormat::Uint16:   return VK_INDEX_TYPE_UINT16;
        case GraphicsIndexFormat::Uint32:   return VK_INDEX_TYPE_UINT32;
        default:
            KEEN_BREAK( "invalid index type" );
            return VK_INDEX_TYPE_NONE_KHR;
        }
    }

    static bool isVkFormatInList( VkFormat format, const VkFormat* pFormatList, size_t listSize )
    {
        for( size_t i = 0u; i < listSize; ++i )
        {
            if( pFormatList[ i ] == format )
            {
                return true;
            }
        }
        return false;
    }

    static const VkFormat s_linearSurfaceFormats[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_A8B8G8R8_UNORM_PACK32 };
    static const VkFormat s_srgbSurfaceFormats[] = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_A8B8G8R8_SRGB_PACK32 };

    bool vulkan::isCompatibleSurfaceFormat( VkFormat vkFormat, PixelFormat pixelFormat )
    {
        switch( pixelFormat )
        {
        case PixelFormat::R8G8B8A8_unorm:
        case PixelFormat::A8B8G8R8_unorm_pack32:
        case PixelFormat::B8G8R8A8_unorm:
            return isVkFormatInList( vkFormat, s_linearSurfaceFormats, KEEN_COUNTOF( s_linearSurfaceFormats ) );

        case PixelFormat::R8G8B8A8_srgb:
        case PixelFormat::A8B8G8R8_srgb_pack32:
        case PixelFormat::B8G8R8A8_srgb:
            return isVkFormatInList( vkFormat, s_srgbSurfaceFormats, KEEN_COUNTOF( s_srgbSurfaceFormats ) );

        default:
            return false;
        }
    }

    VkPrimitiveTopology vulkan::getPrimitiveTopology( GraphicsPrimitiveType primitiveType )
    {
        switch( primitiveType )
        {
        case GraphicsPrimitiveType::TriangleList:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case GraphicsPrimitiveType::TriangleStrip:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case GraphicsPrimitiveType::LineList:       return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case GraphicsPrimitiveType::PatchList:      return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:                                    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        }
    }

    VkFormat vulkan::getVulkanFormat( PixelFormat format )
    {
        // important info (just for reference)
        // https://www.khronos.org/registry/vulkan/specs/1.1/html/vkspec.html#features-required-format-support

        switch( format )
        {
        case PixelFormat::None:                         return VK_FORMAT_UNDEFINED;
        case PixelFormat::R4G4_unorm_pack8:             return VK_FORMAT_R4G4_UNORM_PACK8;
        case PixelFormat::R4G4B4A4_unorm_pack16:        return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        case PixelFormat::B4G4R4A4_unorm_pack16:        return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
        case PixelFormat::R5G6B5_unorm_pack16:          return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case PixelFormat::B5G6R5_unorm_pack16:          return VK_FORMAT_B5G6R5_UNORM_PACK16;
        case PixelFormat::R5G5B5A1_unorm_pack16:        return VK_FORMAT_R5G5B5A1_UNORM_PACK16;
        case PixelFormat::B5G5R5A1_unorm_pack16:        return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
        case PixelFormat::A1R5G5B5_unorm_pack16:        return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        case PixelFormat::R8_unorm:                     return VK_FORMAT_R8_UNORM;
        case PixelFormat::R8_snorm:                     return VK_FORMAT_R8_SNORM;
        case PixelFormat::R8_uscaled:                   return VK_FORMAT_R8_USCALED;
        case PixelFormat::R8_sscaled:                   return VK_FORMAT_R8_SSCALED;
        case PixelFormat::R8_uint:                      return VK_FORMAT_R8_UINT;
        case PixelFormat::R8_sint:                      return VK_FORMAT_R8_SINT;
        case PixelFormat::R8_srgb:                      return VK_FORMAT_R8_SRGB;
        case PixelFormat::R8G8_unorm:                   return VK_FORMAT_R8G8_UNORM;
        case PixelFormat::R8G8_snorm:                   return VK_FORMAT_R8G8_SNORM;
        case PixelFormat::R8G8_uscaled:                 return VK_FORMAT_R8G8_USCALED;
        case PixelFormat::R8G8_sscaled:                 return VK_FORMAT_R8G8_SSCALED;
        case PixelFormat::R8G8_uint:                    return VK_FORMAT_R8G8_UINT;
        case PixelFormat::R8G8_sint:                    return VK_FORMAT_R8G8_SINT;
        case PixelFormat::R8G8_srgb:                    return VK_FORMAT_R8G8_SRGB;
        case PixelFormat::R8G8B8_unorm:                 return VK_FORMAT_R8G8B8_UNORM;
        case PixelFormat::R8G8B8_snorm:                 return VK_FORMAT_R8G8B8_SNORM;
        case PixelFormat::R8G8B8_uscaled:               return VK_FORMAT_R8G8B8_USCALED;
        case PixelFormat::R8G8B8_sscaled:               return VK_FORMAT_R8G8B8_SSCALED;
        case PixelFormat::R8G8B8_uint:                  return VK_FORMAT_R8G8B8_UINT;
        case PixelFormat::R8G8B8_sint:                  return VK_FORMAT_R8G8B8_SINT;
        case PixelFormat::R8G8B8_srgb:                  return VK_FORMAT_R8G8B8_SRGB;
        case PixelFormat::B8G8R8_unorm:                 return VK_FORMAT_B8G8R8_UNORM;
        case PixelFormat::B8G8R8_snorm:                 return VK_FORMAT_B8G8R8_SNORM;
        case PixelFormat::B8G8R8_uscaled:               return VK_FORMAT_B8G8R8_USCALED;
        case PixelFormat::B8G8R8_sscaled:               return VK_FORMAT_B8G8R8_SSCALED;
        case PixelFormat::B8G8R8_uint:                  return VK_FORMAT_B8G8R8_UINT;
        case PixelFormat::B8G8R8_sint:                  return VK_FORMAT_B8G8R8_SINT;
        case PixelFormat::B8G8R8_srgb:                  return VK_FORMAT_B8G8R8_SRGB;
        case PixelFormat::R8G8B8A8_unorm:               return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::R8G8B8A8_snorm:               return VK_FORMAT_R8G8B8A8_SNORM;
        case PixelFormat::R8G8B8A8_uscaled:             return VK_FORMAT_R8G8B8A8_USCALED;
        case PixelFormat::R8G8B8A8_sscaled:             return VK_FORMAT_R8G8B8A8_SSCALED;
        case PixelFormat::R8G8B8A8_uint:                return VK_FORMAT_R8G8B8A8_UINT;
        case PixelFormat::R8G8B8A8_sint:                return VK_FORMAT_R8G8B8A8_SINT;
        case PixelFormat::R8G8B8A8_srgb:                return VK_FORMAT_R8G8B8A8_SRGB;
        case PixelFormat::B8G8R8A8_unorm:               return VK_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::B8G8R8A8_snorm:               return VK_FORMAT_B8G8R8A8_SNORM;
        case PixelFormat::B8G8R8A8_uscaled:             return VK_FORMAT_B8G8R8A8_USCALED;
        case PixelFormat::B8G8R8A8_sscaled:             return VK_FORMAT_B8G8R8A8_SSCALED;
        case PixelFormat::B8G8R8A8_uint:                return VK_FORMAT_B8G8R8A8_UINT;
        case PixelFormat::B8G8R8A8_sint:                return VK_FORMAT_B8G8R8A8_SINT;
        case PixelFormat::B8G8R8A8_srgb:                return VK_FORMAT_B8G8R8A8_SRGB;
        case PixelFormat::A8B8G8R8_unorm_pack32:        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
        case PixelFormat::A8B8G8R8_snorm_pack32:        return VK_FORMAT_A8B8G8R8_SNORM_PACK32;
        case PixelFormat::A8B8G8R8_uscaled_pack32:      return VK_FORMAT_A8B8G8R8_USCALED_PACK32;
        case PixelFormat::A8B8G8R8_sscaled_pack32:      return VK_FORMAT_A8B8G8R8_SSCALED_PACK32;
        case PixelFormat::A8B8G8R8_uint_pack32:         return VK_FORMAT_A8B8G8R8_UINT_PACK32;
        case PixelFormat::A8B8G8R8_sint_pack32:         return VK_FORMAT_A8B8G8R8_SINT_PACK32;
        case PixelFormat::A8B8G8R8_srgb_pack32:         return VK_FORMAT_A8B8G8R8_SRGB_PACK32;
        case PixelFormat::A2R10G10B10_unorm_pack32:     return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case PixelFormat::A2R10G10B10_snorm_pack32:     return VK_FORMAT_A2R10G10B10_SNORM_PACK32;
        case PixelFormat::A2R10G10B10_uscaled_pack32:   return VK_FORMAT_A2R10G10B10_USCALED_PACK32;
        case PixelFormat::A2R10G10B10_sscaled_pack32:   return VK_FORMAT_A2R10G10B10_SSCALED_PACK32;
        case PixelFormat::A2R10G10B10_uint_pack32:      return VK_FORMAT_A2R10G10B10_UINT_PACK32;
        case PixelFormat::A2R10G10B10_sint_pack32:      return VK_FORMAT_A2R10G10B10_SINT_PACK32;
        case PixelFormat::A2B10G10R10_unorm_pack32:     return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case PixelFormat::A2B10G10R10_snorm_pack32:     return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
        case PixelFormat::A2B10G10R10_uscaled_pack32:   return VK_FORMAT_A2B10G10R10_USCALED_PACK32;
        case PixelFormat::A2B10G10R10_sscaled_pack32:   return VK_FORMAT_A2B10G10R10_SSCALED_PACK32;
        case PixelFormat::A2B10G10R10_uint_pack32:      return VK_FORMAT_A2B10G10R10_UINT_PACK32;
        case PixelFormat::A2B10G10R10_sint_pack32:      return VK_FORMAT_A2B10G10R10_SINT_PACK32;
        case PixelFormat::R16_unorm:                    return VK_FORMAT_R16_UNORM;
        case PixelFormat::R16_snorm:                    return VK_FORMAT_R16_SNORM;
        case PixelFormat::R16_uscaled:                  return VK_FORMAT_R16_USCALED;
        case PixelFormat::R16_sscaled:                  return VK_FORMAT_R16_SSCALED;
        case PixelFormat::R16_uint:                     return VK_FORMAT_R16_UINT;
        case PixelFormat::R16_sint:                     return VK_FORMAT_R16_SINT;
        case PixelFormat::R16_sfloat:                   return VK_FORMAT_R16_SFLOAT;
        case PixelFormat::R16G16_unorm:                 return VK_FORMAT_R16G16_UNORM;
        case PixelFormat::R16G16_snorm:                 return VK_FORMAT_R16G16_SNORM;
        case PixelFormat::R16G16_uscaled:               return VK_FORMAT_R16G16_USCALED;
        case PixelFormat::R16G16_sscaled:               return VK_FORMAT_R16G16_SSCALED;
        case PixelFormat::R16G16_uint:                  return VK_FORMAT_R16G16_UINT;
        case PixelFormat::R16G16_sint:                  return VK_FORMAT_R16G16_SINT;
        case PixelFormat::R16G16_sfloat:                return VK_FORMAT_R16G16_SFLOAT;
        case PixelFormat::R16G16B16_unorm:              return VK_FORMAT_R16G16B16_UNORM;
        case PixelFormat::R16G16B16_snorm:              return VK_FORMAT_R16G16B16_SNORM;
        case PixelFormat::R16G16B16_uscaled:            return VK_FORMAT_R16G16B16_USCALED;
        case PixelFormat::R16G16B16_sscaled:            return VK_FORMAT_R16G16B16_SSCALED;
        case PixelFormat::R16G16B16_uint:               return VK_FORMAT_R16G16B16_UINT;
        case PixelFormat::R16G16B16_sint:               return VK_FORMAT_R16G16B16_SINT;
        case PixelFormat::R16G16B16_sfloat:             return VK_FORMAT_R16G16B16_SFLOAT;
        case PixelFormat::R16G16B16A16_unorm:           return VK_FORMAT_R16G16B16A16_UNORM;
        case PixelFormat::R16G16B16A16_snorm:           return VK_FORMAT_R16G16B16A16_SNORM;
        case PixelFormat::R16G16B16A16_uscaled:         return VK_FORMAT_R16G16B16A16_USCALED;
        case PixelFormat::R16G16B16A16_sscaled:         return VK_FORMAT_R16G16B16A16_SSCALED;
        case PixelFormat::R16G16B16A16_uint:            return VK_FORMAT_R16G16B16A16_UINT;
        case PixelFormat::R16G16B16A16_sint:            return VK_FORMAT_R16G16B16A16_SINT;
        case PixelFormat::R16G16B16A16_sfloat:          return VK_FORMAT_R16G16B16A16_SFLOAT;
        case PixelFormat::R32_uint:                     return VK_FORMAT_R32_UINT;
        case PixelFormat::R32_sint:                     return VK_FORMAT_R32_SINT;
        case PixelFormat::R32_sfloat:                   return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::R32G32_uint:                  return VK_FORMAT_R32G32_UINT;
        case PixelFormat::R32G32_sint:                  return VK_FORMAT_R32G32_SINT;
        case PixelFormat::R32G32_sfloat:                return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::R32G32B32_uint:               return VK_FORMAT_R32G32B32_UINT;
        case PixelFormat::R32G32B32_sint:               return VK_FORMAT_R32G32B32_SINT;
        case PixelFormat::R32G32B32_sfloat:             return VK_FORMAT_R32G32B32_SFLOAT;
        case PixelFormat::R32G32B32A32_uint:            return VK_FORMAT_R32G32B32A32_UINT;
        case PixelFormat::R32G32B32A32_sint:            return VK_FORMAT_R32G32B32A32_SINT;
        case PixelFormat::R32G32B32A32_sfloat:          return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::R64_uint:                     return VK_FORMAT_R64_UINT;
        case PixelFormat::R64_sint:                     return VK_FORMAT_R64_SINT;
        case PixelFormat::R64_sfloat:                   return VK_FORMAT_R64_SFLOAT;
        case PixelFormat::R64G64_uint:                  return VK_FORMAT_R64G64_UINT;
        case PixelFormat::R64G64_sint:                  return VK_FORMAT_R64G64_SINT;
        case PixelFormat::R64G64_sfloat:                return VK_FORMAT_R64G64_SFLOAT;
        case PixelFormat::R64G64B64_uint:               return VK_FORMAT_R64G64B64_UINT;
        case PixelFormat::R64G64B64_sint:               return VK_FORMAT_R64G64B64_SINT;
        case PixelFormat::R64G64B64_sfloat:             return VK_FORMAT_R64G64B64_SFLOAT;
        case PixelFormat::R64G64B64A64_uint:            return VK_FORMAT_R64G64B64A64_UINT;
        case PixelFormat::R64G64B64A64_sint:            return VK_FORMAT_R64G64B64A64_SINT;
        case PixelFormat::R64G64B64A64_sfloat:          return VK_FORMAT_R64G64B64A64_SFLOAT;
        case PixelFormat::B10G11R11_ufloat_pack32:      return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case PixelFormat::E5B9G9R9_ufloat_pack32:       return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
        case PixelFormat::D16_unorm:                    return VK_FORMAT_D16_UNORM;
        case PixelFormat::X8_D24_unorm_pack32:          return VK_FORMAT_X8_D24_UNORM_PACK32;
        case PixelFormat::D32_sfloat:                   return VK_FORMAT_D32_SFLOAT;
        case PixelFormat::S8_uint:                      return VK_FORMAT_S8_UINT;
        case PixelFormat::D16_unorm_S8_uint:            return VK_FORMAT_D16_UNORM_S8_UINT;
        case PixelFormat::D24_unorm_S8_uint:            return VK_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::D32_sfloat_S8_uint:           return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case PixelFormat::BC1_RGB_unorm_block:          return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case PixelFormat::BC1_RGB_srgb_block:           return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case PixelFormat::BC1_RGBA_unorm_block:         return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::BC1_RGBA_srgb_block:          return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case PixelFormat::BC2_unorm_block:              return VK_FORMAT_BC2_UNORM_BLOCK;
        case PixelFormat::BC2_srgb_block:               return VK_FORMAT_BC2_SRGB_BLOCK;
        case PixelFormat::BC3_unorm_block:              return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::BC3_srgb_block:               return VK_FORMAT_BC3_SRGB_BLOCK;
        case PixelFormat::BC4_unorm_block:              return VK_FORMAT_BC4_UNORM_BLOCK;
        case PixelFormat::BC4_snorm_block:              return VK_FORMAT_BC4_SNORM_BLOCK;
        case PixelFormat::BC5_unorm_block:              return VK_FORMAT_BC5_UNORM_BLOCK;
        case PixelFormat::BC5_snorm_block:              return VK_FORMAT_BC5_SNORM_BLOCK;
        case PixelFormat::BC6H_ufloat_block:            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case PixelFormat::BC6H_sfloat_block:            return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case PixelFormat::BC7_unorm_block:              return VK_FORMAT_BC7_UNORM_BLOCK;
        case PixelFormat::BC7_srgb_block:               return VK_FORMAT_BC7_SRGB_BLOCK;
        }

        KEEN_BREAK( "Unsupported pixel format" );

        return VK_FORMAT_UNDEFINED;
    }

    PixelFormat vulkan::getPixelFormat( VkFormat format )
    {
        switch( format )
        {
        case VK_FORMAT_UNDEFINED:                   return PixelFormat::None;
        case VK_FORMAT_R4G4_UNORM_PACK8:            return PixelFormat::R4G4_unorm_pack8;
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16:       return PixelFormat::R4G4B4A4_unorm_pack16;
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16:       return PixelFormat::B4G4R4A4_unorm_pack16;
        case VK_FORMAT_R5G6B5_UNORM_PACK16:         return PixelFormat::R5G6B5_unorm_pack16;
        case VK_FORMAT_B5G6R5_UNORM_PACK16:         return PixelFormat::B5G6R5_unorm_pack16;
        case VK_FORMAT_R5G5B5A1_UNORM_PACK16:       return PixelFormat::R5G5B5A1_unorm_pack16;
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16:       return PixelFormat::B5G5R5A1_unorm_pack16;
        case VK_FORMAT_A1R5G5B5_UNORM_PACK16:       return PixelFormat::A1R5G5B5_unorm_pack16;
        case VK_FORMAT_R8_UNORM:                    return PixelFormat::R8_unorm;
        case VK_FORMAT_R8_SNORM:                    return PixelFormat::R8_snorm;
        case VK_FORMAT_R8_USCALED:                  return PixelFormat::R8_uscaled;
        case VK_FORMAT_R8_SSCALED:                  return PixelFormat::R8_sscaled;
        case VK_FORMAT_R8_UINT:                     return PixelFormat::R8_uint;
        case VK_FORMAT_R8_SINT:                     return PixelFormat::R8_sint;
        case VK_FORMAT_R8_SRGB:                     return PixelFormat::R8_srgb;
        case VK_FORMAT_R8G8_UNORM:                  return PixelFormat::R8G8_unorm;
        case VK_FORMAT_R8G8_SNORM:                  return PixelFormat::R8G8_snorm;
        case VK_FORMAT_R8G8_USCALED:                return PixelFormat::R8G8_uscaled;
        case VK_FORMAT_R8G8_SSCALED:                return PixelFormat::R8G8_sscaled;
        case VK_FORMAT_R8G8_UINT:                   return PixelFormat::R8G8_uint;
        case VK_FORMAT_R8G8_SINT:                   return PixelFormat::R8G8_sint;
        case VK_FORMAT_R8G8_SRGB:                   return PixelFormat::R8G8_srgb;
        case VK_FORMAT_R8G8B8_UNORM:                return PixelFormat::R8G8B8_unorm;
        case VK_FORMAT_R8G8B8_SNORM:                return PixelFormat::R8G8B8_snorm;
        case VK_FORMAT_R8G8B8_USCALED:              return PixelFormat::R8G8B8_uscaled;
        case VK_FORMAT_R8G8B8_SSCALED:              return PixelFormat::R8G8B8_sscaled;
        case VK_FORMAT_R8G8B8_UINT:                 return PixelFormat::R8G8B8_uint;
        case VK_FORMAT_R8G8B8_SINT:                 return PixelFormat::R8G8B8_sint;
        case VK_FORMAT_R8G8B8_SRGB:                 return PixelFormat::R8G8B8_srgb;
        case VK_FORMAT_B8G8R8_UNORM:                return PixelFormat::B8G8R8_unorm;
        case VK_FORMAT_B8G8R8_SNORM:                return PixelFormat::B8G8R8_snorm;
        case VK_FORMAT_B8G8R8_USCALED:              return PixelFormat::B8G8R8_uscaled;
        case VK_FORMAT_B8G8R8_SSCALED:              return PixelFormat::B8G8R8_sscaled;
        case VK_FORMAT_B8G8R8_UINT:                 return PixelFormat::B8G8R8_uint;
        case VK_FORMAT_B8G8R8_SINT:                 return PixelFormat::B8G8R8_sint;
        case VK_FORMAT_B8G8R8_SRGB:                 return PixelFormat::B8G8R8_srgb;
        case VK_FORMAT_R8G8B8A8_UNORM:              return PixelFormat::R8G8B8A8_unorm;
        case VK_FORMAT_R8G8B8A8_SNORM:              return PixelFormat::R8G8B8A8_snorm;
        case VK_FORMAT_R8G8B8A8_USCALED:            return PixelFormat::R8G8B8A8_uscaled;
        case VK_FORMAT_R8G8B8A8_SSCALED:            return PixelFormat::R8G8B8A8_sscaled;
        case VK_FORMAT_R8G8B8A8_UINT:               return PixelFormat::R8G8B8A8_uint;
        case VK_FORMAT_R8G8B8A8_SINT:               return PixelFormat::R8G8B8A8_sint;
        case VK_FORMAT_R8G8B8A8_SRGB:               return PixelFormat::R8G8B8A8_srgb;
        case VK_FORMAT_B8G8R8A8_UNORM:              return PixelFormat::B8G8R8A8_unorm;
        case VK_FORMAT_B8G8R8A8_SNORM:              return PixelFormat::B8G8R8A8_snorm;
        case VK_FORMAT_B8G8R8A8_USCALED:            return PixelFormat::B8G8R8A8_uscaled;
        case VK_FORMAT_B8G8R8A8_SSCALED:            return PixelFormat::B8G8R8A8_sscaled;
        case VK_FORMAT_B8G8R8A8_UINT:               return PixelFormat::B8G8R8A8_uint;
        case VK_FORMAT_B8G8R8A8_SINT:               return PixelFormat::B8G8R8A8_sint;
        case VK_FORMAT_B8G8R8A8_SRGB:               return PixelFormat::B8G8R8A8_srgb;
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:       return PixelFormat::A8B8G8R8_unorm_pack32;
        case VK_FORMAT_A8B8G8R8_SNORM_PACK32:       return PixelFormat::A8B8G8R8_snorm_pack32;
        case VK_FORMAT_A8B8G8R8_USCALED_PACK32:     return PixelFormat::A8B8G8R8_uscaled_pack32;
        case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:     return PixelFormat::A8B8G8R8_sscaled_pack32;
        case VK_FORMAT_A8B8G8R8_UINT_PACK32:        return PixelFormat::A8B8G8R8_uint_pack32;
        case VK_FORMAT_A8B8G8R8_SINT_PACK32:        return PixelFormat::A8B8G8R8_sint_pack32;
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:        return PixelFormat::A8B8G8R8_srgb_pack32;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:    return PixelFormat::A2R10G10B10_unorm_pack32;
        case VK_FORMAT_A2R10G10B10_SNORM_PACK32:    return PixelFormat::A2R10G10B10_snorm_pack32;
        case VK_FORMAT_A2R10G10B10_USCALED_PACK32:  return PixelFormat::A2R10G10B10_uscaled_pack32;
        case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:  return PixelFormat::A2R10G10B10_sscaled_pack32;
        case VK_FORMAT_A2R10G10B10_UINT_PACK32:     return PixelFormat::A2R10G10B10_uint_pack32;
        case VK_FORMAT_A2R10G10B10_SINT_PACK32:     return PixelFormat::A2R10G10B10_sint_pack32;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:    return PixelFormat::A2B10G10R10_unorm_pack32;
        case VK_FORMAT_A2B10G10R10_SNORM_PACK32:    return PixelFormat::A2B10G10R10_snorm_pack32;
        case VK_FORMAT_A2B10G10R10_USCALED_PACK32:  return PixelFormat::A2B10G10R10_uscaled_pack32;
        case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:  return PixelFormat::A2B10G10R10_sscaled_pack32;
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:     return PixelFormat::A2B10G10R10_uint_pack32;
        case VK_FORMAT_A2B10G10R10_SINT_PACK32:     return PixelFormat::A2B10G10R10_sint_pack32;
        case VK_FORMAT_R16_UNORM:                   return PixelFormat::R16_unorm;
        case VK_FORMAT_R16_SNORM:                   return PixelFormat::R16_snorm;
        case VK_FORMAT_R16_USCALED:                 return PixelFormat::R16_uscaled;
        case VK_FORMAT_R16_SSCALED:                 return PixelFormat::R16_sscaled;
        case VK_FORMAT_R16_UINT:                    return PixelFormat::R16_uint;
        case VK_FORMAT_R16_SINT:                    return PixelFormat::R16_sint;
        case VK_FORMAT_R16_SFLOAT:                  return PixelFormat::R16_sfloat;
        case VK_FORMAT_R16G16_UNORM:                return PixelFormat::R16G16_unorm;
        case VK_FORMAT_R16G16_SNORM:                return PixelFormat::R16G16_snorm;
        case VK_FORMAT_R16G16_USCALED:              return PixelFormat::R16G16_uscaled;
        case VK_FORMAT_R16G16_SSCALED:              return PixelFormat::R16G16_sscaled;
        case VK_FORMAT_R16G16_UINT:                 return PixelFormat::R16G16_uint;
        case VK_FORMAT_R16G16_SINT:                 return PixelFormat::R16G16_sint;
        case VK_FORMAT_R16G16_SFLOAT:               return PixelFormat::R16G16_sfloat;
        case VK_FORMAT_R16G16B16_UNORM:             return PixelFormat::R16G16B16_unorm;
        case VK_FORMAT_R16G16B16_SNORM:             return PixelFormat::R16G16B16_snorm;
        case VK_FORMAT_R16G16B16_USCALED:           return PixelFormat::R16G16B16_uscaled;
        case VK_FORMAT_R16G16B16_SSCALED:           return PixelFormat::R16G16B16_sscaled;
        case VK_FORMAT_R16G16B16_UINT:              return PixelFormat::R16G16B16_uint;
        case VK_FORMAT_R16G16B16_SINT:              return PixelFormat::R16G16B16_sint;
        case VK_FORMAT_R16G16B16_SFLOAT:            return PixelFormat::R16G16B16_sfloat;
        case VK_FORMAT_R16G16B16A16_UNORM:          return PixelFormat::R16G16B16A16_unorm;
        case VK_FORMAT_R16G16B16A16_SNORM:          return PixelFormat::R16G16B16A16_snorm;
        case VK_FORMAT_R16G16B16A16_USCALED:        return PixelFormat::R16G16B16A16_uscaled;
        case VK_FORMAT_R16G16B16A16_SSCALED:        return PixelFormat::R16G16B16A16_sscaled;
        case VK_FORMAT_R16G16B16A16_UINT:           return PixelFormat::R16G16B16A16_uint;
        case VK_FORMAT_R16G16B16A16_SINT:           return PixelFormat::R16G16B16A16_sint;
        case VK_FORMAT_R16G16B16A16_SFLOAT:         return PixelFormat::R16G16B16A16_sfloat;
        case VK_FORMAT_R32_UINT:                    return PixelFormat::R32_uint;
        case VK_FORMAT_R32_SINT:                    return PixelFormat::R32_sint;
        case VK_FORMAT_R32_SFLOAT:                  return PixelFormat::R32_sfloat;
        case VK_FORMAT_R32G32_UINT:                 return PixelFormat::R32G32_uint;
        case VK_FORMAT_R32G32_SINT:                 return PixelFormat::R32G32_sint;
        case VK_FORMAT_R32G32_SFLOAT:               return PixelFormat::R32G32_sfloat;
        case VK_FORMAT_R32G32B32_UINT:              return PixelFormat::R32G32B32_uint;
        case VK_FORMAT_R32G32B32_SINT:              return PixelFormat::R32G32B32_sint;
        case VK_FORMAT_R32G32B32_SFLOAT:            return PixelFormat::R32G32B32_sfloat;
        case VK_FORMAT_R32G32B32A32_UINT:           return PixelFormat::R32G32B32A32_uint;
        case VK_FORMAT_R32G32B32A32_SINT:           return PixelFormat::R32G32B32A32_sint;
        case VK_FORMAT_R32G32B32A32_SFLOAT:         return PixelFormat::R32G32B32A32_sfloat;
        case VK_FORMAT_R64_UINT:                    return PixelFormat::R64_uint;
        case VK_FORMAT_R64_SINT:                    return PixelFormat::R64_sint;
        case VK_FORMAT_R64_SFLOAT:                  return PixelFormat::R64_sfloat;
        case VK_FORMAT_R64G64_UINT:                 return PixelFormat::R64G64_uint;
        case VK_FORMAT_R64G64_SINT:                 return PixelFormat::R64G64_sint;
        case VK_FORMAT_R64G64_SFLOAT:               return PixelFormat::R64G64_sfloat;
        case VK_FORMAT_R64G64B64_UINT:              return PixelFormat::R64G64B64_uint;
        case VK_FORMAT_R64G64B64_SINT:              return PixelFormat::R64G64B64_sint;
        case VK_FORMAT_R64G64B64_SFLOAT:            return PixelFormat::R64G64B64_sfloat;
        case VK_FORMAT_R64G64B64A64_UINT:           return PixelFormat::R64G64B64A64_uint;
        case VK_FORMAT_R64G64B64A64_SINT:           return PixelFormat::R64G64B64A64_sint;
        case VK_FORMAT_R64G64B64A64_SFLOAT:         return PixelFormat::R64G64B64A64_sfloat;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:     return PixelFormat::B10G11R11_ufloat_pack32;
        case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:      return PixelFormat::E5B9G9R9_ufloat_pack32;
        case VK_FORMAT_D16_UNORM:                   return PixelFormat::D16_unorm;
        case VK_FORMAT_X8_D24_UNORM_PACK32:         return PixelFormat::X8_D24_unorm_pack32;
        case VK_FORMAT_D32_SFLOAT:                  return PixelFormat::D32_sfloat;
        case VK_FORMAT_S8_UINT:                     return PixelFormat::S8_uint;
        case VK_FORMAT_D16_UNORM_S8_UINT:           return PixelFormat::D16_unorm_S8_uint;
        case VK_FORMAT_D24_UNORM_S8_UINT:           return PixelFormat::D24_unorm_S8_uint;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:          return PixelFormat::D32_sfloat_S8_uint;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:         return PixelFormat::BC1_RGB_unorm_block;
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:          return PixelFormat::BC1_RGB_srgb_block;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:        return PixelFormat::BC1_RGBA_unorm_block;
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:         return PixelFormat::BC1_RGBA_srgb_block;
        case VK_FORMAT_BC2_UNORM_BLOCK:             return PixelFormat::BC2_unorm_block;
        case VK_FORMAT_BC2_SRGB_BLOCK:              return PixelFormat::BC2_srgb_block;
        case VK_FORMAT_BC3_UNORM_BLOCK:             return PixelFormat::BC3_unorm_block;
        case VK_FORMAT_BC3_SRGB_BLOCK:              return PixelFormat::BC3_srgb_block;
        case VK_FORMAT_BC4_UNORM_BLOCK:             return PixelFormat::BC4_unorm_block;
        case VK_FORMAT_BC4_SNORM_BLOCK:             return PixelFormat::BC4_snorm_block;
        case VK_FORMAT_BC5_UNORM_BLOCK:             return PixelFormat::BC5_unorm_block;
        case VK_FORMAT_BC5_SNORM_BLOCK:             return PixelFormat::BC5_snorm_block;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:           return PixelFormat::BC6H_ufloat_block;
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:           return PixelFormat::BC6H_sfloat_block;
        case VK_FORMAT_BC7_UNORM_BLOCK:             return PixelFormat::BC7_unorm_block;
        case VK_FORMAT_BC7_SRGB_BLOCK:              return PixelFormat::BC7_srgb_block;

        default:
            return PixelFormat::None;
        }
    }

    bool vulkan::isDepthFormat( VkFormat format )
    {
        switch( format )
        {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;

        default:
            return false;
        }
    }

    bool vulkan::isPackedDepthStencilFormat( VkFormat format )
    {
        switch( format )
        {
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;

        default:
            return false;
        }
    }

    VkFilter vulkan::getFilter( GraphicsSamplerFilterMode filterMode )
    {
        switch( filterMode )
        {
        case GraphicsSamplerFilterMode::Nearest:    return VK_FILTER_NEAREST;
        case GraphicsSamplerFilterMode::Linear:     return VK_FILTER_LINEAR;
        }
        KEEN_BREAK( "invalid GraphicsSamplerFilterMode" );
        return VK_FILTER_NEAREST;
    }

    VkSamplerMipmapMode vulkan::getSamplerMipmapMode( GraphicsSamplerFilterMode filterMode )
    {
        switch( filterMode )
        {
        case GraphicsSamplerFilterMode::Nearest:    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        case GraphicsSamplerFilterMode::Linear:     return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }
        KEEN_BREAK( "invalid GraphicsSamplerFilterMode" );
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    VkSamplerAddressMode vulkan::getSamplerAddressMode( GraphicsSamplerAddressMode addressMode )
    {
        switch( addressMode )
        {
        case GraphicsSamplerAddressMode::Wrap:          return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case GraphicsSamplerAddressMode::Mirror:        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case GraphicsSamplerAddressMode::Clamp:         return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case GraphicsSamplerAddressMode::ClampToZero:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        }
        KEEN_BREAK( "invalid GraphicsSamplerAddressMode" );
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    VkSamplerReductionModeEXT vulkan::getSamplerReductionMode( GraphicsSamplerReductionMode reductionMode )
    {
        switch( reductionMode )
        {
        case GraphicsSamplerReductionMode::Min:     return VK_SAMPLER_REDUCTION_MODE_MIN_EXT;
        case GraphicsSamplerReductionMode::Max:     return VK_SAMPLER_REDUCTION_MODE_MAX_EXT;
        default:
            KEEN_BREAK( "Invalid sampler reduction mode %d", (uint8)reductionMode );
            return VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE_EXT;
        }
    }

    VkCompareOp vulkan::getCompareOp( GraphicsComparisonFunction comparisonFunction )
    {
        switch( comparisonFunction )
        {
        case GraphicsComparisonFunction::Never:         return VK_COMPARE_OP_NEVER;
        case GraphicsComparisonFunction::Less:          return VK_COMPARE_OP_LESS;
        case GraphicsComparisonFunction::Equal:         return VK_COMPARE_OP_EQUAL;
        case GraphicsComparisonFunction::LessEqual:     return VK_COMPARE_OP_LESS_OR_EQUAL;
        case GraphicsComparisonFunction::Greater:       return VK_COMPARE_OP_GREATER;
        case GraphicsComparisonFunction::NotEqual:      return VK_COMPARE_OP_NOT_EQUAL;
        case GraphicsComparisonFunction::GreaterEqual:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case GraphicsComparisonFunction::Always:        return VK_COMPARE_OP_ALWAYS;
        default:                                        return VK_COMPARE_OP_ALWAYS;
        }
    }

    VkStencilOp vulkan::getStencilOp( GraphicsStencilOperation stencilOperation )
    {
        switch( stencilOperation )
        {
        case GraphicsStencilOperation::Keep:            return VK_STENCIL_OP_KEEP;
        case GraphicsStencilOperation::Zero:            return VK_STENCIL_OP_ZERO;
        case GraphicsStencilOperation::Replace:         return VK_STENCIL_OP_REPLACE;
        case GraphicsStencilOperation::Increment:       return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case GraphicsStencilOperation::Decrement:       return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case GraphicsStencilOperation::IncrementWrap:   return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case GraphicsStencilOperation::DecrementWrap:   return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case GraphicsStencilOperation::Invert:          return VK_STENCIL_OP_INVERT;
        default:
            KEEN_BREAK( "Invalid stencil operation %d", (uint8)stencilOperation );
            return VK_STENCIL_OP_ZERO;
        }
    }

    VkPolygonMode vulkan::getPolygonMode( GraphicsFillMode fillMode )
    {
        switch( fillMode )
        {
        case GraphicsFillMode::Wireframe:   return VK_POLYGON_MODE_LINE;
        case GraphicsFillMode::Solid:       return VK_POLYGON_MODE_FILL;
        default:
            KEEN_TRACE_ERROR( "Invalid fill mode %d!\n", (uint8)fillMode );
            return VK_POLYGON_MODE_FILL;
        }
    }

    VkCullModeFlagBits vulkan::getCullModeFlagBits( GraphicsCullMode cullMode )
    {
        switch( cullMode )
        {
        case GraphicsCullMode::None:    return VK_CULL_MODE_NONE;
        case GraphicsCullMode::Front:   return VK_CULL_MODE_FRONT_BIT;
        case GraphicsCullMode::Back:    return VK_CULL_MODE_BACK_BIT;
        default:
            KEEN_TRACE_ERROR( "Invalid cull mode %d!\n", (uint8)cullMode );
            return VK_CULL_MODE_NONE;
        }
    }

    VkFrontFace vulkan::getFrontFace( GraphicsWindingOrder windingOrder )
    {
        switch( windingOrder )
        {
        case GraphicsWindingOrder::Ccw: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case GraphicsWindingOrder::Cw:  return VK_FRONT_FACE_CLOCKWISE;
        default:
            KEEN_TRACE_ERROR( "Invalid winding order %d!\n", (uint8)windingOrder );
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        }
    }

    VkSampleCountFlagBits vulkan::getSampleCountFlagBits( uint8 sampleCount )
    {
        switch( sampleCount )
        {
        case 1u:    return VK_SAMPLE_COUNT_1_BIT;
        case 2u:    return VK_SAMPLE_COUNT_2_BIT;
        case 4u:    return VK_SAMPLE_COUNT_4_BIT;
        case 8u:    return VK_SAMPLE_COUNT_8_BIT;
        case 16u:   return VK_SAMPLE_COUNT_16_BIT;
        case 32u:   return VK_SAMPLE_COUNT_32_BIT;
        case 64u:   return VK_SAMPLE_COUNT_64_BIT;
        default:
            KEEN_TRACE_ERROR( "Invalid sample count %d!\n", sampleCount );
            return VK_SAMPLE_COUNT_1_BIT;
        }
    }

    VkImageUsageFlags vulkan::getImageUsageMask( GraphicsTextureUsageMask usageMask )
    {
        VkImageUsageFlags result{};
        if( usageMask.isSet( GraphicsTextureUsageFlag::TransferSource ) )
        {
            result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        if( usageMask.isSet( GraphicsTextureUsageFlag::TransferTarget ) )
        {
            result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        if( usageMask.isSet( GraphicsTextureUsageFlag::Render_ShaderResource ) )
        {
            result |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }
        if( usageMask.isSet( GraphicsTextureUsageFlag::Render_ColorTarget ) )
        {
            result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if( usageMask.isSet( GraphicsTextureUsageFlag::Render_DepthTarget ) ||
            usageMask.isSet( GraphicsTextureUsageFlag::Render_StencilTarget ) )
        {
            result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        if( usageMask.isSet( GraphicsTextureUsageFlag::Render_ShaderStorage ) )
        {
            result |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        return result;
    }

    VkBlendOp vulkan::getBlendOp( GraphicsBlendOperation blendOperation )
    {
        switch( blendOperation )
        {
        case GraphicsBlendOperation::None:          return VK_BLEND_OP_ADD;
        case GraphicsBlendOperation::Add:           return VK_BLEND_OP_ADD;
        case GraphicsBlendOperation::Subtract:      return VK_BLEND_OP_SUBTRACT;
        case GraphicsBlendOperation::RevSubtract:   return VK_BLEND_OP_REVERSE_SUBTRACT;
        case GraphicsBlendOperation::Min:           return VK_BLEND_OP_MIN;
        case GraphicsBlendOperation::Max:           return VK_BLEND_OP_MAX;
        default:
            KEEN_TRACE_ERROR( "Invalid blend operation %d!\n", (uint8)blendOperation );
            return VK_BLEND_OP_ADD;
        }
    }

    VkBlendFactor vulkan::getBlendFactor( GraphicsBlendFactor blendFactor )
    {
        switch( blendFactor )
        {
        case GraphicsBlendFactor::Zero:                     return VK_BLEND_FACTOR_ZERO;
        case GraphicsBlendFactor::One:                      return VK_BLEND_FACTOR_ONE;
        case GraphicsBlendFactor::SrcColor:                 return VK_BLEND_FACTOR_SRC_COLOR;
        case GraphicsBlendFactor::InvSrcColor:              return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case GraphicsBlendFactor::SrcAlpha:                 return VK_BLEND_FACTOR_SRC_ALPHA;
        case GraphicsBlendFactor::InvSrcAlpha:              return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case GraphicsBlendFactor::DestAlpha:                return VK_BLEND_FACTOR_DST_ALPHA;
        case GraphicsBlendFactor::InvDestAlpha:             return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case GraphicsBlendFactor::DestColor:                return VK_BLEND_FACTOR_DST_COLOR;
        case GraphicsBlendFactor::InvDestColor:             return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case GraphicsBlendFactor::SrcAlphaSat:              return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default:
            KEEN_TRACE_ERROR( "Invalid blend factor %d!\n", (uint8)blendFactor );
            return VK_BLEND_FACTOR_ONE;
        }
    }

    VkColorComponentFlagBits vulkan::getColorComponentFlagBits( GraphicsColorWriteMask colorMask )
    {
        KEEN_COMPILE_TIME_ASSERT( (int)VK_COLOR_COMPONENT_R_BIT == (int)GraphicsColorWriteMask( GraphicsColorWriteFlag::Red ).value );
        KEEN_COMPILE_TIME_ASSERT( (int)VK_COLOR_COMPONENT_G_BIT == (int)GraphicsColorWriteMask( GraphicsColorWriteFlag::Green ).value );
        KEEN_COMPILE_TIME_ASSERT( (int)VK_COLOR_COMPONENT_B_BIT == (int)GraphicsColorWriteMask( GraphicsColorWriteFlag::Blue ).value );
        KEEN_COMPILE_TIME_ASSERT( (int)VK_COLOR_COMPONENT_A_BIT == (int)GraphicsColorWriteMask( GraphicsColorWriteFlag::Alpha ).value );
        return (VkColorComponentFlagBits)colorMask.value;
    }

    VkAttachmentLoadOp vulkan::getLoadOp( GraphicsLoadAction loadAction )
    {
        switch( loadAction )
        {
        case GraphicsLoadAction::DontCare:  return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case GraphicsLoadAction::Load:      return VK_ATTACHMENT_LOAD_OP_LOAD;
        case GraphicsLoadAction::Clear:     return VK_ATTACHMENT_LOAD_OP_CLEAR;     
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    VkAttachmentStoreOp vulkan::getStoreOp( GraphicsStoreAction storeAction )
    {
        switch( storeAction )
        {
        case GraphicsStoreAction::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case GraphicsStoreAction::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
        case GraphicsStoreAction::None:     return VK_ATTACHMENT_STORE_OP_NONE_KHR; // provided by VK_KHR_dynamic_rendering (core in Vulkan 1.3)
        }
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    VkClearColorValue vulkan::getColorClearValue( GraphicsColorClearValue clearValue )  
    {
        float4 color = graphics::getColorFromColorClearValue( clearValue );
        VkClearColorValue result{};
        result.float32[ 0u ] = color.x;
        result.float32[ 1u ] = color.y;
        result.float32[ 2u ] = color.z;
        result.float32[ 3u ] = color.w;
        return result;
    }

    VkClearDepthStencilValue vulkan::getDepthStencilClearValue( GraphicsDepthClearValue depthClearValue, GraphicsStencilClearValue stencilClearValue )
    {
        VkClearDepthStencilValue result{};
        switch( depthClearValue )
        {
        case GraphicsDepthClearValue::Zero:
            result.depth = 0.0f;
            break;

        case GraphicsDepthClearValue::One:
            result.depth = 1.0f;
            break;
        }
        result.stencil = stencilClearValue;
        return result;
    }

    VkShaderStageFlags vulkan::getStageFlags( GraphicsPipelineStageMask mask )
    {
        VkShaderStageFlags result = 0u;
        if( mask.isSet( GraphicsPipelineStage::Vertex ) )
        {
            result |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if( mask.isSet( GraphicsPipelineStage::Fragment ) )
        {
            result |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if( mask.isSet( GraphicsPipelineStage::TS_Control ) )
        {
            result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        }
        if( mask.isSet( GraphicsPipelineStage::TS_Evaluation ) )
        {
            result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        }
        if( mask.isSet( GraphicsPipelineStage::Compute ) )
        {
            result |= VK_SHADER_STAGE_COMPUTE_BIT;
        }
        return result;
    }

    VkBufferUsageFlags vulkan::getBufferUsageFlags( GraphicsBufferUsageMask flags )
    {
        VkBufferUsageFlags result = 0u;
        if( flags.isSet( GraphicsBufferUsageFlag::TransferSource ) )
        {
            result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }
        if( flags.isSet( GraphicsBufferUsageFlag::TransferTarget ) )
        {
            result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }
        if( flags.isSet( GraphicsBufferUsageFlag::UniformBuffer ) )
        {
            result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        }
        if( flags.isSet( GraphicsBufferUsageFlag::StorageBuffer ) )
        {
            result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        }
        if( flags.isSet( GraphicsBufferUsageFlag::IndexBuffer ) )
        {
            result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        }
        if( flags.isSet( GraphicsBufferUsageFlag::VertexBuffer ) )
        {
            result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        }
        if( flags.isSet( GraphicsBufferUsageFlag::ArgumentBuffer ) )
        {
            result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        }
        return result;
    }

    VkDescriptorType vulkan::getDescriptorType( GraphicsDescriptorType type )
    {
        switch( type )
        {
        case GraphicsDescriptorType::Sampler:               return VK_DESCRIPTOR_TYPE_SAMPLER;
        case GraphicsDescriptorType::SamplerArray:          return VK_DESCRIPTOR_TYPE_SAMPLER;
        case GraphicsDescriptorType::SampledImage:          return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case GraphicsDescriptorType::SampledImageArray:     return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case GraphicsDescriptorType::StorageImage:          return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case GraphicsDescriptorType::StorageImageArray:     return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case GraphicsDescriptorType::UniformBuffer:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case GraphicsDescriptorType::ByteAddressBuffer:     return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case GraphicsDescriptorType::StructuredBuffer:      return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case GraphicsDescriptorType::RWByteAddressBuffer:   return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case GraphicsDescriptorType::RWStructuredBuffer:    return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
#if KEEN_USING( KEEN_GRAPHICS_OLD_STORAGE_BUFFER_DESCRIPTORS )
        case GraphicsDescriptorType::StorageBuffer:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
#endif
        case GraphicsDescriptorType::Invalid:               break;
        }

        KEEN_BREAK( "invalid descriptor type" );
        return (VkDescriptorType)0;
    }

    VkImageType vulkan::getImageType( TextureType type )
    {
        switch( type )
        {
        case TextureType::Texture1D:    return VK_IMAGE_TYPE_1D;
        case TextureType::Texture2D:    return VK_IMAGE_TYPE_2D;
        case TextureType::Texture3D:    return VK_IMAGE_TYPE_3D;
        case TextureType::Cube:         return VK_IMAGE_TYPE_2D;
        case TextureType::Array1D:      return VK_IMAGE_TYPE_1D;
        case TextureType::Array2D:      return VK_IMAGE_TYPE_2D;
        case TextureType::ArrayCube:    return VK_IMAGE_TYPE_2D;
        }

        KEEN_TRACE_ERROR( "Invalid texture type %d!\n", (uint32)type );
        return VK_IMAGE_TYPE_MAX_ENUM;
    }

    VkImageViewType vulkan::getImageViewType( TextureType type )
    {
        switch( type )
        {
        case TextureType::Texture1D:    return VK_IMAGE_VIEW_TYPE_1D;
        case TextureType::Texture2D:    return VK_IMAGE_VIEW_TYPE_2D;
        case TextureType::Texture3D:    return VK_IMAGE_VIEW_TYPE_3D;
        case TextureType::Cube:         return VK_IMAGE_VIEW_TYPE_CUBE;
        case TextureType::Array1D:      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case TextureType::Array2D:      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case TextureType::ArrayCube:    return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        }

        KEEN_TRACE_ERROR( "Invalid texture type %d!\n", (uint32)type );
        return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }

    VkImageLayout vulkan::getImageLayout( GraphicsTextureLayout layout )
    {
        switch( layout )
        {
        case GraphicsTextureLayout::Undefined:                      return VK_IMAGE_LAYOUT_UNDEFINED;
        case GraphicsTextureLayout::General:                        return VK_IMAGE_LAYOUT_GENERAL;
        case GraphicsTextureLayout::ColorAttachmentOptimal:         return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case GraphicsTextureLayout::DepthAttachmentOptimal:         return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case GraphicsTextureLayout::DepthStencilReadOnlyOptimal:    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case GraphicsTextureLayout::StencilAttachmentOptimal:       return VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        case GraphicsTextureLayout::DepthStencilAttachmentOptimal:  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case GraphicsTextureLayout::ShaderReadOnlyOptimal:          return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case GraphicsTextureLayout::TransferSourceOptimal:          return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case GraphicsTextureLayout::TransferTargetOptimal:          return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
        case GraphicsTextureLayout::InvalidatedByAlias:             return VK_IMAGE_LAYOUT_UNDEFINED;
#endif
        }

        KEEN_TRACE_ERROR( "Invalid texture layout %d!\n", (uint32)layout );
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }

    VkStencilFaceFlags vulkan::getStencilFaceFlags( GraphicsStencilFaceMask faceMask )
    {
        VkStencilFaceFlags result = 0;
        if( faceMask.isSet( GraphicsStencilFace::Front ) )
        {
            result |= VK_STENCIL_FACE_FRONT_BIT;
        }
        if( faceMask.isSet( GraphicsStencilFace::Back ) )
        {
            result |= VK_STENCIL_FACE_BACK_BIT;
        }
        return result;
    }

    VkImageSubresourceRange vulkan::getImageSubresourceRange( const GraphicsTextureSubresourceRange& subresourceRange )
    {
        VkImageSubresourceRange result{};

        if( subresourceRange.aspectMask.isSet( GraphicsTextureAspectFlag::Color ) )
        {
            result.aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
        }
        if( subresourceRange.aspectMask.isSet( GraphicsTextureAspectFlag::Depth ) )
        {
            result.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if( subresourceRange.aspectMask.isSet( GraphicsTextureAspectFlag::Stencil ) )
        {
            result.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        result.baseArrayLayer   = subresourceRange.firstArrayLayer;
        result.baseMipLevel     = subresourceRange.firstMipLevel;
        result.layerCount       = subresourceRange.arrayLayerCount;
        result.levelCount       = subresourceRange.mipLevelCount;

        return result;
    }

    float vulkan::getMemoryPriority( GraphicsDeviceMemoryPriority priority )
    {
        // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkMemoryPriorityAllocateInfoEXT.html
        // priority is a floating-point value between 0 and 1, indicating the priority of the allocation relative to other memory allocations. Larger values are higher priority.
        switch( priority )
        {
        case GraphicsDeviceMemoryPriority::Lowest:  return 0.0f;
        case GraphicsDeviceMemoryPriority::Low:     return 0.25f;
        case GraphicsDeviceMemoryPriority::Normal:  return 0.5f;
        case GraphicsDeviceMemoryPriority::High:    return 0.75f;
        case GraphicsDeviceMemoryPriority::Highest: return 1.0f;
        }

        return 0.5f;
    }

    GraphicsMemoryRequirements vulkan::createGraphicsMemoryRequirements( const VkMemoryRequirements& memoryRequirements, bool prefersDedicatedAllocation, bool requiresDedicatedAllocation )
    {
        GraphicsMemoryRequirements result;
        result.size                             = memoryRequirements.size;
        result.alignment                        = memoryRequirements.alignment;
        result.prefersDedicatedAllocation       = prefersDedicatedAllocation;
        result.requiresDedicatedAllocation      = requiresDedicatedAllocation;
        result.supportedDeviceMemoryTypeIndices = memoryRequirements.memoryTypeBits;
        return result;
    }

    VkImageAspectFlags vulkan::getImageAspectFlags( const GraphicsTextureAspectFlagMask& aspectMask )
    {
        VkImageAspectFlags result{};

        if( aspectMask.isSet( GraphicsTextureAspectFlag::Color ) )
        {
            result |= VK_IMAGE_ASPECT_COLOR_BIT;
        }
        if( aspectMask.isSet( GraphicsTextureAspectFlag::Depth ) )
        {
            result |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if( aspectMask.isSet( GraphicsTextureAspectFlag::Stencil ) )
        {
            result |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        return result;
    }

    void vulkan::fillVkImageSubresourceLayers( VkImageSubresourceLayers* pVulkanImageSubresource, const GraphicsTextureRegion& region )
    {
        pVulkanImageSubresource->mipLevel       = region.level;
        pVulkanImageSubresource->baseArrayLayer = region.baseLayer;
        pVulkanImageSubresource->layerCount     = region.layerCount;
        pVulkanImageSubresource->aspectMask     = getImageAspectFlags( region.aspectMask );
    }

    void vulkan::fillVkBufferImageCopy( VkBufferImageCopy* pVulkanBufferImageCopy, const GraphicsBufferTextureCopyRegion& region )
    {
        pVulkanBufferImageCopy->bufferOffset        = region.bufferOffset;
        pVulkanBufferImageCopy->bufferRowLength     = 0u;
        pVulkanBufferImageCopy->bufferImageHeight   = 0u;
        vulkan::fillVkImageSubresourceLayers( &pVulkanBufferImageCopy->imageSubresource, region.textureRegion );
        pVulkanBufferImageCopy->imageOffset         = vulkan::createOffset3d( region.textureRegion.offset );
        pVulkanBufferImageCopy->imageExtent         = vulkan::createExtent3d( region.textureRegion.size );      
    }

    void vulkan::writeFullPipelineBarrier( VulkanApi* pVulkan, VkCommandBuffer commandBuffer )
    {
        VkMemoryBarrier memoryBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        memoryBarrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                   VK_ACCESS_INDEX_READ_BIT |
                   VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                   VK_ACCESS_UNIFORM_READ_BIT |
                   VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                   VK_ACCESS_SHADER_READ_BIT |
                   VK_ACCESS_SHADER_WRITE_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                   VK_ACCESS_TRANSFER_READ_BIT |
                   VK_ACCESS_TRANSFER_WRITE_BIT |
                   VK_ACCESS_HOST_READ_BIT |
                   VK_ACCESS_HOST_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                   VK_ACCESS_INDEX_READ_BIT |
                   VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                   VK_ACCESS_UNIFORM_READ_BIT |
                   VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                   VK_ACCESS_SHADER_READ_BIT |
                   VK_ACCESS_SHADER_WRITE_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                   VK_ACCESS_TRANSFER_READ_BIT |
                   VK_ACCESS_TRANSFER_WRITE_BIT |
                   VK_ACCESS_HOST_READ_BIT |
                   VK_ACCESS_HOST_WRITE_BIT;

        pVulkan->vkCmdPipelineBarrier( commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1u, &memoryBarrier, 0u, nullptr, 0u, nullptr );
    }

}
