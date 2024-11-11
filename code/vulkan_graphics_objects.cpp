#include "vulkan_graphics_objects.hpp"

#include "../global/graphics_system_private.hpp"

#include "keen/base/atomic.hpp"
#include "keen/base/inivariables.hpp"
#include "keen/base/path_name.hpp"
#include "keen/base/static_array.hpp"
#include "keen/base/tls_array.hpp"
#include "keen/base/profiler.hpp"
#include "keen/os_gui/window_system.hpp"
#include "keen/os/os_file.hpp"
#include "keen/graphics/graphics_system.hpp"
#include "keen/base/inivariables.hpp"

#include "vulkan_swap_chain.hpp"
#include "vulkan_command_buffer.hpp"
#include "vulkan_descriptor_set_writer.hpp"
#include "vulkan_render_context.hpp"
#include "vulkan_api.hpp"
#include "vulkan_graphics_device.hpp"

namespace keen
{
    constexpr fourcc VulkanPipelineCacheHeaderMagic = "VKPC"_4cc;
    constexpr uint64 VulkanPipelineCacheHeaderVersion = 3ull;

    static const VkFormat s_D16_Candidates[]    = { VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT };
    static const VkFormat s_D24S8_Candidates[]  = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D16_UNORM };

    KEEN_DEFINE_BOOL_VARIABLE( s_enableVulkanObjectTracking,            "enableVulkanObjectTracking", false, "" );
    KEEN_DEFINE_BOOL_VARIABLE( s_enableVulkanPipelineCache,             "enableVulkanPipelineCache", false, "" );

    struct VulkanPipelineCacheHeader
    {
        uint32                              magic;              // VulkanPipelineCacheHeaderMagic
        uint32                              dataSize;           // equal to *pDataSize returned by vkGetPipelineCacheData
        uint64                              headerVersion;      // random seed VulkanPipelineCacheHeaderVersion
        uint64                              dataHash;           // a hash of pipeline cache data

        uint32                              vendorId;           // equal to VkPhysicalDeviceProperties::vendorID
        uint32                              deviceId;           // equal to VkPhysicalDeviceProperties::deviceID
        uint32                              driverVersion;      // equal to VkPhysicalDeviceProperties::driverVersion
        uint32                              driverABI;          // equal to sizeof(void*)

        StaticArray<uint8, VK_UUID_SIZE>    uuid;               // equal to VkPhysicalDeviceProperties::pipelineCacheUUID
    };

    VulkanDescriptorPoolSizes vulkan::fillDefaultVulkanDescriptorPoolSizes( uint32 descriptorSetCount )
    {
        VulkanDescriptorPoolSizes result{};
        
        result.descriptorSetCount           = descriptorSetCount;
        result.uniformBufferDescriptorCount = descriptorSetCount * 2u;
        result.storageBufferDescriptorCount = descriptorSetCount * 2u;
        result.samplerDescriptorCount       = descriptorSetCount * 4u;
        result.sampledImageDescriptorCount  = descriptorSetCount * 16u;
        result.storageImageDescriptorCount  = descriptorSetCount * 16u;

        return result;
    }

    VkImageSubresourceRange vulkan::getImageSubresourceRange( const VulkanTexture* pTexture )
    {
        VkImageAspectFlags aspectMask = 0u;
        if( pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_DepthTarget ) ||
            pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_StencilTarget ) )
        {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if( image::hasStencil( pTexture->format ) )
            {
                aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else
        {
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkImageSubresourceRange result{};
        result.aspectMask       = aspectMask;
        result.baseMipLevel     = pTexture->firstLevel;
        result.levelCount       = pTexture->levelCount;
        result.baseArrayLayer   = pTexture->firstArrayLayer;
        result.layerCount       = pTexture->layerCount;
        return result;
    }

    template<typename T>
        void VulkanGraphicsObjects::createObjectPoolAllocator( size_t chunkSize, size_t alignment, const DebugName& debugName )
    {
        const MemoryAllocatorFlags objectAllocatorFlags = s_enableVulkanObjectTracking ? MemoryAllocatorFlags() : MemoryAllocatorFlag::DontTrack;

        const size_t objectTypeIndex = (size_t)T::ObjectType;
        m_objectAllocators[ objectTypeIndex ].create( m_pAllocator, sizeof( T ), chunkSize, alignment, debugName, objectAllocatorFlags );
    }

    ErrorId VulkanGraphicsObjects::create( const VulkanGraphicsObjectsParameters& parameters )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_pAllocator        = parameters.pAllocator;

        m_pVulkan           = parameters.pVulkan;
        m_physicalDevice    = parameters.physicalDevice;
        m_instance          = parameters.instance;
        m_device            = parameters.device;

        m_pSharedData       = parameters.pSharedData;
        m_pWindowSystem     = parameters.pWindowSystem;

        m_frameCount        = parameters.frameCount;

        VulkanGpuAllocatorParameters gpuAllocatorParameters;
        gpuAllocatorParameters.pAllocator       = m_pAllocator;
        gpuAllocatorParameters.pVulkan          = m_pVulkan;
        gpuAllocatorParameters.pAllocCallbacks  = m_pSharedData->pVulkanAllocationCallbacks;
        gpuAllocatorParameters.physicalDevice   = m_physicalDevice;
        gpuAllocatorParameters.device           = m_device;
        gpuAllocatorParameters.instance         = m_instance;
        gpuAllocatorParameters.blockSizeInBytes = parameters.allocationBlockSizeInBytes;
        gpuAllocatorParameters.memoryProperties = m_pSharedData->deviceMemoryProperties;

        m_pGpuAllocator = vulkan::createGpuAllocator( gpuAllocatorParameters );

        if( m_pGpuAllocator == nullptr )
        {
            destroy();
            return ErrorId_Generic;
        }

        m_allocatorMutex.create( "VulkanObjectAllocator"_debug );
        m_descriptorPoolMutex.create( "VulkanDescriptorPool"_debug );
        m_staticDescriptorPoolMutex.create( "VulkanStaticDescriptorSetPool"_debug );
        m_staticSamplerMap.create( m_pAllocator, 128u );

        m_staticDescriptorPoolSizes     = parameters.staticDescriptorPoolSizes;
        m_dynamicDescriptorPoolSizes    = parameters.dynamicDescriptorPoolSizes;

        createObjectPoolAllocator<VulkanSwapChainWrapper>( 16u, 16u, "Vk_SwapChain"_debug );
        createObjectPoolAllocator<VulkanPipelineLayout>( 128u, 8u, "Vk_PipelineLayout"_debug );
        createObjectPoolAllocator<VulkanRenderPipeline>( 128u, 8u, "Vk_RdrPipeline"_debug );
        createObjectPoolAllocator<VulkanDeviceMemory>( 16u, 8u, "Vk_DeviceMemory"_debug );
        createObjectPoolAllocator<VulkanBuffer>( 1024u, 8u, "Vk_Buffer"_debug );
        createObjectPoolAllocator<VulkanTexture>( 1024u, 8u, "Vk_Texture"_debug );
        createObjectPoolAllocator<VulkanSampler>( 32u, 8u, "Vk_Sampler"_debug );
        createObjectPoolAllocator<VulkanDescriptorSetLayout>( 128u, 8u, "Vk_DescriptorSetLayout"_debug );
        createObjectPoolAllocator<StaticVulkanDescriptorSet>( 1024u, 8u, "Vk_DescriptorSet"_debug );
        createObjectPoolAllocator<VulkanQueryPool>( 16, 8u, "Vk_QueryPool"_debug );
        createObjectPoolAllocator<VulkanComputePipeline>( 32u, 8u, "Vk_CsPipeline"_debug );

        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanDeviceMemoryCount, 0u, "Vk_DeviceMemory", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanPipelineLayoutCount, 0u, "Vk_PipelineLayout", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanTextureCount, 0u, "Vk_TextureCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanBufferCount, 0u, "Vk_StaticBufferCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanSamplerCount, 0u, "Vk_SamplerCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanRenderPipelineCount, 0u, "Vk_RenderPipelineCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanCompilationContextCount, 0u, "Vk_CompilationContextCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanSwapChainCount, 0u, "Vk_SwapChainCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanDescriptorSetLayoutCount, 0u, "Vk_DescriptorSetLayoutCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanQueryPoolCount, 0u, "Vk_QueryPoolCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanComputePipelineCount, 0u, "Vk_ComputePipelineCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanDescriptorPoolCount, 0u, "Vk_DescriptorPoolCount", false );

        if( s_enableVulkanPipelineCache && parameters.pipelineCacheDirectory.hasElements() )
        {
            Array< uint8 > pipelineCacheData;
            VkPipelineCacheCreateInfo pipelineCacheCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };

            if( m_pipelineCachePath.tryCreateCombinedFilePath( m_pAllocator, parameters.pipelineCacheDirectory, "vk_pipeline_cache.bin" ) )
            {
                const Result<void> readResult = os::readWholeFile( &pipelineCacheData, m_pAllocator, m_pipelineCachePath );
                if( readResult.isOk() && pipelineCacheData.getCount() >= sizeof( VulkanPipelineCacheHeader ) )
                {
                    const VulkanPipelineCacheHeader* pHeader = pointer_cast<const VulkanPipelineCacheHeader>( pipelineCacheData.getStart() );

                    if( pHeader->magic != VulkanPipelineCacheHeaderMagic )
                    {
                        KEEN_TRACE_WARNING( "[graphics] Pipeline cache found is no longer valid, mismatch in magic number (probably a file corruption). Rebuilding now!\n" );
                    }
                    else if( pHeader->headerVersion != VulkanPipelineCacheHeaderVersion )
                    {
                        KEEN_TRACE_WARNING( "[graphics] Pipeline cache found is no longer valid, cache version was updated ( was %d, current %d ). Rebuilding now!\n", pHeader->headerVersion, VulkanPipelineCacheHeaderVersion );
                    }
                    else if( pHeader->vendorId != m_pSharedData->deviceProperties.vendorID || pHeader->deviceId != m_pSharedData->deviceProperties.deviceID )
                    {
                        KEEN_TRACE_WARNING( "[graphics] Pipeline cache found is no longer valid, device is not the same that the pipeline cache was build for. Rebuilding now!\n" );
                    }
                    else if( pHeader->driverVersion != m_pSharedData->deviceProperties.driverVersion )
                    {
                        KEEN_TRACE_WARNING( "[graphics] Pipeline cache found is no longer valid, driver is not the same that the pipeline cache was build for. Rebuilding now!\n" );
                    }
                    else if( !isMemoryBlockEqual( pHeader->uuid.getConstMemory(), createConstMemoryBlockFromArray( m_pSharedData->deviceProperties.pipelineCacheUUID ) ) )
                    {
                        KEEN_TRACE_WARNING( "[graphics] Pipeline cache found is no longer valid, pipelineCacheUUID is not the same that the pipeline cache was build for (probably due to a driver update). Rebuilding now!\n" );
                    }
                    else if( pHeader->driverABI != sizeof( void* ) )
                    {
                        KEEN_TRACE_ERROR( "[graphics] Pipeline cache is invalid, was build for x64 systems. Enshrouded was not build to run on non x64 systems\n" );
                        KEEN_ASSERT( false ); // should never happen
                    }
                    else
                    {
                        pipelineCacheCreateInfo.initialDataSize = pipelineCacheData.getCount() - sizeof( VulkanPipelineCacheHeader );
                        pipelineCacheCreateInfo.pInitialData    = pipelineCacheData.getStart() + sizeof( VulkanPipelineCacheHeader );
                        KEEN_TRACE_INFO( "[graphics] Found compatible vulkan pipeline cache data path:%s size:%,k\n", m_pipelineCachePath, pipelineCacheCreateInfo.initialDataSize );
                    }
                }
                else
                {
                    KEEN_TRACE_INFO( "[graphics] Failed to load shader pipeline cache ( ErrorId = %k ). Rebuilding now!\n", readResult.getError() );
                }
            }
            else
            {
                KEEN_TRACE_ERROR( "[graphics] failed to allocate shader pipeline cache path name.\n" );
            }

            VulkanResult pipelineCacheCreateResult;
            pipelineCacheCreateResult = m_pVulkan->vkCreatePipelineCache( m_device, &pipelineCacheCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &m_pipelineCache );
            if( pipelineCacheCreateResult.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] Could not create pipeline cache! error=%s. Falling back to an empty pipeline cache.\n", pipelineCacheCreateResult );

                // :FK: Just create a new pipeline cache. The old pipeline cache will be overwritten on shutdown.
                pipelineCacheCreateInfo.initialDataSize = 0;
                pipelineCacheCreateInfo.pInitialData    = nullptr;

                pipelineCacheCreateResult = m_pVulkan->vkCreatePipelineCache( m_device, &pipelineCacheCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &m_pipelineCache );
                if( pipelineCacheCreateResult.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] Could not create empty fallback pipeline cache! error=%s. Running without pipeline cache (this might incur a performance penalty)\n", pipelineCacheCreateResult );
                    m_pipelineCache = VK_NULL_HANDLE;
                }
            }
        }
        else if( parameters.pipelineCacheDirectory.hasElements() )
        {
            if( m_pipelineCachePath.tryCreateCombinedFilePath( m_pAllocator, parameters.pipelineCacheDirectory, "vk_pipeline_cache.bin" ) )
            {
                KEEN_TRACE_INFO( "[graphics] Deleting vulkan pipeline cache at %k!\n", m_pipelineCachePath );
                os::deleteFile( m_pipelineCachePath, FileDeleteFlag::Force );
            }
        }

        if( !m_freeObjectListMutex.tryCreate( "Vk_FreeObjectListMutex"_debug ) )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create free object list mutex!\n" );
            destroy();
            return ErrorId_OutOfMemory;
        }

        m_depthFormats[ VulkanDepthFormat_Depth16 ] = findFirstMatchingDepthStencilFormat( createArrayView( s_D16_Candidates ), false );
        m_depthFormats[ VulkanDepthFormat_Depth16_ShaderInput ] = findFirstMatchingDepthStencilFormat( createArrayView( s_D16_Candidates ), true );
        m_depthFormats[ VulkanDepthFormat_Depth24S8 ] = findFirstMatchingDepthStencilFormat( createArrayView( s_D24S8_Candidates ), false );
        m_depthFormats[ VulkanDepthFormat_Depth24S8_ShaderInput ] = findFirstMatchingDepthStencilFormat( createArrayView( s_D24S8_Candidates ), true );

        if( parameters.enableBindlessDescriptors )
        {
            const uint32 bindlessTextureCount = parameters.bindlessTextureCount;
            const uint32 bindlessSamplerCount = parameters.bindlessSamplerCount;

            const VkShaderStageFlags bindlessShaderStageMask = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutBinding bindlessBindings[] =
            {
                { 0u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessTextureCount, bindlessShaderStageMask, nullptr },
                { 1u, VK_DESCRIPTOR_TYPE_SAMPLER, bindlessSamplerCount, bindlessShaderStageMask, nullptr },
            };

            const VkDescriptorBindingFlags bindlessBindingFlags[] =
            {
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
            };

            KEEN_STATIC_ASSERT( KEEN_COUNTOF( bindlessBindings ) == KEEN_COUNTOF( bindlessBindingFlags ) );

            VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorBindingFlags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
            descriptorBindingFlags.bindingCount     = KEEN_COUNTOF( bindlessBindingFlags );
            descriptorBindingFlags.pBindingFlags    = bindlessBindingFlags;

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            descriptorSetLayoutCreateInfo.bindingCount  = KEEN_COUNTOF( bindlessBindings );
            descriptorSetLayoutCreateInfo.pBindings     = bindlessBindings;
            descriptorSetLayoutCreateInfo.pNext         = &descriptorBindingFlags;
            VulkanResult result = m_pVulkan->vkCreateDescriptorSetLayout( m_device, &descriptorSetLayoutCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &m_bindlessDescriptorSetLayout );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateDescriptorSetLayout failed with error '%s'\n", result );
                destroy();
                return result.getErrorId();
            }

            {
                VkDescriptorSetLayoutCreateInfo emptyDescriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                descriptorSetLayoutCreateInfo.bindingCount  = 0u;
                descriptorSetLayoutCreateInfo.pBindings     = nullptr;
                descriptorSetLayoutCreateInfo.pNext         = nullptr;
                result = m_pVulkan->vkCreateDescriptorSetLayout( m_device, &emptyDescriptorSetLayoutCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &m_emptyDescriptorSetLayout );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkCreateDescriptorSetLayout failed with error '%s'\n", result );
                    destroy();
                    return result.getErrorId();
                }
            }
        }
        else
        {
            m_bindlessDescriptorSetLayout = VK_NULL_HANDLE;
        }

        m_nextBufferId  = 1u;
        m_nextTextureId = 1u;

        return ErrorId_Ok;
    }

    void VulkanGraphicsObjects::destroy()
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( m_bindlessDescriptorSetLayout != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyDescriptorSetLayout( m_device, m_bindlessDescriptorSetLayout, m_pSharedData->pVulkanAllocationCallbacks );
            m_bindlessDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if( m_emptyDescriptorSetLayout != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyDescriptorSetLayout( m_device, m_emptyDescriptorSetLayout, m_pSharedData->pVulkanAllocationCallbacks );
            m_emptyDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if( m_pFirstFreeDynamicDescriptorPool != nullptr )
        {
            VulkanDescriptorPool* pDescriptorPool = m_pFirstFreeDynamicDescriptorPool;
            while( pDescriptorPool != nullptr )
            {
                KEEN_ASSERT( pDescriptorPool->type == VulkanDescriptorPoolType::Dynamic );
                VulkanDescriptorPool* pNextDescriptorPool = pDescriptorPool->pNext;
                destroyDescriptorPool( pDescriptorPool );

                pDescriptorPool = pNextDescriptorPool;
            }
            m_pFirstFreeDynamicDescriptorPool = nullptr;
        }

        if( m_pFirstStaticDescriptorPool != nullptr )
        {
            VulkanDescriptorPool* pDescriptorPool = m_pFirstStaticDescriptorPool;
            while( pDescriptorPool != nullptr )
            {
                VulkanDescriptorPool* pNextDescriptorPool = pDescriptorPool->pNext;
                destroyDescriptorPool( pDescriptorPool );

                pDescriptorPool = pNextDescriptorPool;
            }
            m_pFirstStaticDescriptorPool = nullptr;
        }

        if( m_pipelineCache != VK_NULL_HANDLE && !m_pipelineCachePath.isEmpty() )
        {
            size_t dataSize = 0;
            VulkanResult fetchResult = m_pVulkan->vkGetPipelineCacheData( m_device, m_pipelineCache, &dataSize, NULL );
            if( fetchResult.isOk() )
            {
                Array< uint8 > rawBuffer;
                if( rawBuffer.tryCreate( m_pAllocator, dataSize + sizeof( VulkanPipelineCacheHeader ) ) )
                {
                    fetchResult = m_pVulkan->vkGetPipelineCacheData( m_device, m_pipelineCache, &dataSize, rawBuffer.getStart() + sizeof( VulkanPipelineCacheHeader ) );
                    if( fetchResult.isOk() )
                    {
                        VulkanPipelineCacheHeader* pHeader = pointer_cast<VulkanPipelineCacheHeader>( rawBuffer.getStart() );
                        pHeader->magic          = VulkanPipelineCacheHeaderMagic;
                        pHeader->headerVersion  = VulkanPipelineCacheHeaderVersion;
                        pHeader->dataSize       = rangecheck_cast<uint32>( dataSize );
                        pHeader->dataHash       = calculateFnv1a32Hash( rawBuffer.getStart() + sizeof( VulkanPipelineCacheHeader ), dataSize ).value;

                        pHeader->driverVersion  = m_pSharedData->deviceProperties.driverVersion;
                        pHeader->deviceId       = m_pSharedData->deviceProperties.deviceID;
                        pHeader->vendorId       = m_pSharedData->deviceProperties.vendorID;
                        pHeader->driverABI      = sizeof( void* );
                        copyMemoryBlockNonOverlapping( pHeader->uuid.getMemory(), createMemoryBlockFromArray( m_pSharedData->deviceProperties.pipelineCacheUUID ) );

                        const Result<void> directoryResult = os::createDirectoryTree( m_pipelineCachePath.getDirectoryPath() );
                        if( directoryResult.hasError() )
                        {
                            KEEN_TRACE_ERROR( "[graphics] Could not create directory to store shader pipeline cache '%s', error=%k\n", m_pipelineCachePath, directoryResult.getError() );
                        }
                        else
                        {
                            const Result<void> writeResult = os::writeWholeFile( m_pipelineCachePath, rawBuffer.getMemory() );
                            if( writeResult.hasError() )
                            {
                                KEEN_TRACE_ERROR( "[graphics] Could not store shader pipeline cache '%s', error=%k\n", m_pipelineCachePath, directoryResult.getError() );
                            }
                            else
                            {
                                KEEN_TRACE_INFO( "[graphics] Successfully stored shader pipeline cache '%s'\n", m_pipelineCachePath );
                            }
                        }
                    }
                    else
                    {
                        KEEN_TRACE_ERROR( "[graphics] vkGetPipelineCacheData failed with error %k\n", fetchResult );
                    }
                }
                else
                {
                    KEEN_TRACE_ERROR( "[graphics] failed to allocate enough space ( %k bytes ) to serialize shader pipeline cache.\n", dataSize );
                }
            }
            else
            {
                KEEN_TRACE_ERROR( "[graphics] vkGetPipelineCacheData failed while retrieving the size ( error %k )\n", fetchResult );
            }

            m_pVulkan->vkDestroyPipelineCache( m_device, m_pipelineCache, m_pSharedData->pVulkanAllocationCallbacks );
            m_pipelineCache = VK_NULL_HANDLE;
        }

        if( m_pGpuAllocator != nullptr )
        {
            vulkan::destroyGpuAllocator( m_pAllocator, m_pGpuAllocator );
            m_pGpuAllocator = nullptr;
        }

        m_freeObjectListMutex.destroy();

        m_staticDescriptorPoolMutex.destroy();
        m_descriptorPoolMutex.destroy();

        for( size_t i = 0u; i < (size_t)GraphicsDeviceObjectType::Count; ++i )
        {
            if( m_objectAllocators[ i ].getSize() > 0u )
            {
                KEEN_TRACE_ERROR( "[graphics] Leaked %d %s of type (%s)\n",
                    m_objectAllocators[ i ].getSize(),
                    m_objectAllocators[ i ].getSize() == 1u ? "object" : "objects",
                    graphics::getDeviceObjectTypeName( (GraphicsDeviceObjectType)i ) );
            }
            m_objectAllocators[ i ].destroy();
        }
        m_allocatorMutex.destroy();
    }

    MemoryBlock VulkanGraphicsObjects::allocateDeviceObjectBase( GraphicsDeviceObjectType type )
    {
        KEEN_PROFILE_CPU( Vk_allocateDeviceObjectBase );
        MutexLock lock( &m_allocatorMutex );

        const size_t typeIndex = (size_t)type;

        MemoryBlock result;
        result.pStart   = (uint8*)m_objectAllocators[ typeIndex ].allocate();
        result.size     = m_objectAllocators[ typeIndex ].getElementSize();
        if( result.pStart != nullptr )
        {
            fillMemoryWithZero( result.pStart, result.size );
        }
        return result;
    }

    void VulkanGraphicsObjects::freeDeviceObjectBase( GraphicsDeviceObjectType type, void* pObject )
    {
        MutexLock lock( &m_allocatorMutex );
        m_objectAllocators[ (size_t)type ].free( pObject );
    }

    VulkanSwapChainWrapper* VulkanGraphicsObjects::createSwapChain( const GraphicsSwapChainParameters& parameters )
    {
        VulkanSwapChainWrapper* pSwapChainWrapper = allocateDeviceObject<VulkanSwapChainWrapper>();
        if( pSwapChainWrapper == nullptr )
        {
            return nullptr;
        }

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        KEEN_PROFILE_COUNTER_INC( m_vulkanSwapChainCount );

        pSwapChainWrapper->pSwapChain = newObjectZero<VulkanSwapChain>( m_pAllocator, "VulkanSwapChain"_debug );
        if( pSwapChainWrapper->pSwapChain == nullptr )
        {
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }

        VkSurfaceKHR surface{};

#if KEEN_USING( KEEN_OS_WINDOW_SUPPORT )
        OsWindowSystemInfo windowSystemInfo;
        OsWindowState windowState;
        if( !os::getWindowSystemInfo( &windowSystemInfo, m_pWindowSystem ) ||
            !os::getWindowState( &windowState, m_pWindowSystem, parameters.windowHandle ) )
        {
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }

        VulkanResult result;

#if defined( KEEN_PLATFORM_WIN32 )
        // Create a WSI surface for the window:
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        surfaceCreateInfo.hinstance = (HINSTANCE)windowSystemInfo.hInstance;
        surfaceCreateInfo.hwnd      = (HWND)windowState.nativeHandle;

        result = m_pVulkan->vkCreateWin32SurfaceKHR( m_instance, &surfaceCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &surface );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateWin32SurfaceKHR failed with error '%s'\n", result );
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }
#elif defined( KEEN_PLATFORM_LINUX )
#if KEEN_USING( KEEN_OS_USE_WAYLAND )
        VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR };
        surfaceCreateInfo.display = (wl_display*)windowSystemInfo.pDisplay;
        surfaceCreateInfo.surface = (wl_surface*)windowState.nativeHandle;
        result = m_pVulkan->vkCreateWaylandSurfaceKHR( m_instance, &surfaceCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &surface );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create vulkan wayland surface!\n" );
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }
#else
        VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
        surfaceCreateInfo.dpy       = (Display*)windowSystemInfo.pDisplay;
        surfaceCreateInfo.window    = windowState.nativeHandle;
        result = m_pVulkan->vkCreateXlibSurfaceKHR( m_instance, &surfaceCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &surface );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateXlibSurfaceKHR failed with error '%s'\n", result );
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }
#endif
#else

#error "Platform not supported"

#endif
#endif

        // now that we have the surface we have to check if the device can really present into it:
        VkBool32 presentSupported;
        result = m_pVulkan->vkGetPhysicalDeviceSurfaceSupportKHR( m_physicalDevice, m_pSharedData->presentQueueFamilyIndex, surface, &presentSupported );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceSurfaceSupportKHR failed with error '%s'\n", result );
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }

        if( !presentSupported )
        {
            KEEN_TRACE_ERROR( "[graphics] presenting is not supported from the present queue family!\n" );
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }

        VulkanSwapChainParameters swapChainParameters = {};
        swapChainParameters.pAllocator                          = m_pAllocator;
        swapChainParameters.pVulkan                             = m_pVulkan;
        swapChainParameters.pSwapChainWrapper                   = pSwapChainWrapper;
        swapChainParameters.physicalDevice                      = m_physicalDevice;
        swapChainParameters.device                              = m_device;
        swapChainParameters.surface                             = surface;
        swapChainParameters.pObjects                            = this;
        swapChainParameters.pSharedData                         = m_pSharedData;
        swapChainParameters.backBufferSize                      = parameters.size;
        swapChainParameters.backBufferColorFormat               = parameters.colorFormat;
        swapChainParameters.alternativeBackBufferColorFormat    = parameters.alternativeColorFormat;
        swapChainParameters.backBufferUsageMask                 = parameters.usageMask;
        swapChainParameters.presentationInterval                = parameters.presentationInterval;

        if( !pSwapChainWrapper->pSwapChain->tryCreate( swapChainParameters ) )
        {
            destroySwapChain( pSwapChainWrapper );
            return nullptr;
        }

        for( size_t i = 0u; i < GraphicsLimits_MaxRenderTargetPixelFormatCount; ++i )
        {
            const VulkanTexture* pBackBuffer = pSwapChainWrapper->pSwapChain->getBackBufferTexture( i );
            pSwapChainWrapper->backBuffers[ i ] = pBackBuffer;
        }

        pSwapChainWrapper->info.colorFormat             = pSwapChainWrapper->backBuffers[ 0u ]->format;
        pSwapChainWrapper->info.size                    = pSwapChainWrapper->pSwapChain->getSize();
        pSwapChainWrapper->info.presentationInterval    = parameters.presentationInterval;

#if KEEN_USING( KEEN_OS_WINDOW_SUPPORT )
        pSwapChainWrapper->info.windowHandle    = parameters.windowHandle;
#endif

        graphics::initializeDeviceObject( pSwapChainWrapper, GraphicsDeviceObjectType::SwapChain, parameters.debugName );

        return pSwapChainWrapper;
    }

    VulkanPipelineLayout* VulkanGraphicsObjects::createPipelineLayout( const GraphicsPipelineLayoutParameters& parameters )
    {
        VulkanPipelineLayout* pPipelineLayout = allocateDeviceObject<VulkanPipelineLayout>();
        if( pPipelineLayout == nullptr )
        {
            return nullptr;
        }

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        graphics::initializeDeviceObject( pPipelineLayout, GraphicsDeviceObjectType::PipelineLayout, parameters.debugName );
        KEEN_PROFILE_COUNTER_INC( m_vulkanPipelineLayoutCount );

        DynamicArray<VkDescriptorSetLayout, GraphicsLimits_MaxDescriptorSetSlotCount + 1u> setLayouts;
        for( size_t i = 0u; i < parameters.descriptorSetLayoutCount; ++i )
        {
            const VulkanDescriptorSetLayout* pDescriptorSetLayout = ( const VulkanDescriptorSetLayout* )parameters.descriptorSetLayouts[ i ];
            setLayouts.pushBack( pDescriptorSetLayout->layout );
        }

        if( parameters.useBindlessDescriptors )
        {
            KEEN_ASSERT( m_bindlessDescriptorSetLayout != VK_NULL_HANDLE );

            while( setLayouts.getCount() < GraphicsLimits_MaxDescriptorSetSlotCount )
            {
                setLayouts.pushBack( m_emptyDescriptorSetLayout );
            }

            setLayouts.pushBack( m_bindlessDescriptorSetLayout );
            pPipelineLayout->useBindlessDescriptors = true;
        }

        VkPipelineLayoutCreateInfo layoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutCreateInfo.pSetLayouts = setLayouts.getStart();
        layoutCreateInfo.setLayoutCount = setLayouts.getCount32();

        VkPushConstantRange pushConstantRange{};
        if( parameters.pushConstantsSize > 0u )
        {
            pushConstantRange.offset                = 0u;
            pushConstantRange.size                  = parameters.pushConstantsSize;
            pushConstantRange.stageFlags            = vulkan::getStageFlags( parameters.pushConstantsStageMask );
            layoutCreateInfo.pPushConstantRanges    = &pushConstantRange;
            layoutCreateInfo.pushConstantRangeCount = 1u;
        }

        VulkanResult result = m_pVulkan->vkCreatePipelineLayout( m_device, &layoutCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pPipelineLayout->pipelineLayout );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreatePipelineLayout failed with error '%s'\n", result );
            destroyPipelineLayout( pPipelineLayout );
            return nullptr;
        }

        vulkan::setObjectName( m_pVulkan, m_device, pPipelineLayout->pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, parameters.debugName );

        return pPipelineLayout;
    }

    GraphicsRenderPipeline* VulkanGraphicsObjects::createRenderPipeline( const GraphicsRenderPipelineParameters& parameters )
    {
        // :JK: note: this function will be called from various threads in parallel!
        KEEN_PROFILE_CPU( Vk_CreateRenderPipeline );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        KEEN_ASSERT( parameters.pPipelineLayout != nullptr );

        VulkanRenderPipeline* pRenderPipeline = allocateDeviceObject<VulkanRenderPipeline>();
        if( pRenderPipeline == nullptr )
        {
            return nullptr;
        }

        graphics::initializeDeviceObject( pRenderPipeline, GraphicsDeviceObjectType::RenderPipeline, parameters.debugName );

        KEEN_PROFILE_COUNTER_INC( m_vulkanRenderPipelineCount );

        RenderPipelineShaderModules shaderModules {};

        Result<void> prepareResult = prepareRenderPipelineCompileParameters( &shaderModules, parameters );
        if( prepareResult.hasError() )
        {
            destroyRenderPipeline( pRenderPipeline );
            destroyShaderModules( shaderModules );
            return nullptr;
        }

        Result<void> compileResult = compileRenderPipeline( pRenderPipeline, parameters, shaderModules );

        destroyShaderModules( shaderModules );

        if( compileResult.hasError() )
        {
            destroyRenderPipeline( pRenderPipeline );
            return nullptr;
        }

        pRenderPipeline->scissorTestEnabled = parameters.enableScissorTest;

        return pRenderPipeline;
    }

    VulkanDeviceMemory* VulkanGraphicsObjects::createDeviceMemory( const GraphicsDeviceMemoryParameters& parameters )
    {
        KEEN_PROFILE_CPU( Vk_createDeviceMemory );

        KEEN_ASSERT_RELEASE( parameters.memoryTypeIndex < m_pSharedData->deviceMemoryProperties.memoryTypeCount );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VulkanDeviceMemory* pDeviceMemory = allocateDeviceObject<VulkanDeviceMemory>();
        if( pDeviceMemory == nullptr )
        {
            return nullptr;
        }

        vulkan::AllocateGpuDeviceMemoryParameters allocationParameters;
        allocationParameters.sizeInBytes        = parameters.sizeInBytes;
        allocationParameters.memoryTypeIndex    = parameters.memoryTypeIndex;
        allocationParameters.debugName          = parameters.debugName;
        if( m_pSharedData->info.isMemoryPrioritySupported )
        {
            allocationParameters.priority = parameters.priority;
        }
        if( parameters.dedicatedBuffer.isSet() )
        {
            allocationParameters.dedicatedBuffer = ( (const VulkanBuffer*)parameters.dedicatedBuffer.get() )->buffer;
        }
        if( parameters.dedicatedTexture.isSet() )
        {
            allocationParameters.dedicatedImage = ( (const VulkanTexture*)parameters.dedicatedTexture.get() )->image;
        }

        const Result<VulkanGpuDeviceMemoryAllocation> allocationResult = vulkan::allocateGpuDeviceMemory( m_pGpuAllocator, allocationParameters );
        if( allocationResult.hasError() )
        {
            freeDeviceObject( pDeviceMemory );
            return nullptr;
        }
        pDeviceMemory->allocation = allocationResult.value;

        if( parameters.mapMemory )
        {
            KEEN_ASSERT( isBitmaskSet( m_pSharedData->deviceMemoryProperties.memoryTypes[ parameters.memoryTypeIndex ].propertyFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) );

            void* pMappedMemory;
            VulkanResult result = m_pVulkan->vkMapMemory( m_pVulkan->device, pDeviceMemory->allocation.memory, 0u, VK_WHOLE_SIZE, 0u, &pMappedMemory );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkMapMemory failed with error '%s'\n", result );
                destroyDeviceMemory( pDeviceMemory );
                return nullptr;
            }
            pDeviceMemory->mappedMemory = createMemoryBlock( pMappedMemory, parameters.sizeInBytes );

            pDeviceMemory->isCoherent = isBitmaskSet( m_pSharedData->deviceMemoryProperties.memoryTypes[ parameters.memoryTypeIndex ].propertyFlags, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
        }

        graphics::initializeDeviceObject( pDeviceMemory, GraphicsDeviceObjectType::DeviceMemory, parameters.debugName );

        KEEN_PROFILE_COUNTER_INC( m_vulkanDeviceMemoryCount );
        return pDeviceMemory;
    }

    VulkanBuffer* VulkanGraphicsObjects::createBuffer( const GraphicsBufferParameters& parameters )
    {
        KEEN_PROFILE_CPU( Vk_createBuffer );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VulkanBuffer* pBuffer = allocateDeviceObject<VulkanBuffer>();
        if( pBuffer == nullptr )
        {
            return nullptr;
        }

        VkBufferCreateInfo bufferCreateInfo;
        if( fillVkBufferCreateInfo( &bufferCreateInfo, parameters ).hasError() )
        {
            freeDeviceObject( pBuffer );
            return nullptr;
        }

        if( parameters.allocateMemory )
        {
            // determine type:
            VulkanGpuMemoryUsage memoryUsage{};
            VulkanGpuMemoryFlagMask memoryFlags{};

            uint32 minAlignment = 0u;
            if( parameters.cpuAccess.isSet( GraphicsAccessModeFlag::Write ) && !parameters.cpuAccess.isSet( GraphicsAccessModeFlag::Read ) )
            {
                // write only cpu access: 
                memoryUsage = VulkanGpuMemoryUsage::Auto_PreferHost;
                memoryFlags = { VulkanGpuMemoryFlag::PersistentlyMapped, VulkanGpuMemoryFlag::HostSequentialWrite };

                minAlignment = DefaultAlignment;
            }
            else if( parameters.cpuAccess.isAnySet() )
            {
                // read access from cpu
                memoryUsage = VulkanGpuMemoryUsage::Auto_PreferHost;
                memoryFlags = { VulkanGpuMemoryFlag::PersistentlyMapped, VulkanGpuMemoryFlag::HostRandomAccess };

                minAlignment = DefaultAlignment;
            }
            else
            {
                // no cpu access:
                memoryUsage = VulkanGpuMemoryUsage::Auto_PreferDevice;
            }

#ifndef KEEN_BUILD_MASTER
            memoryFlags.setIf( VulkanGpuMemoryFlag::DedicatedDeviceMemory, parameters.allocateDedicatedDeviceMemory );
#endif

            VulkanGpuBufferResult allocationResult;
            if( !vulkan::allocateGpuBuffer( &allocationResult, m_pGpuAllocator, memoryUsage, memoryFlags, minAlignment, bufferCreateInfo, parameters.debugName ) )
            {
                freeDeviceObject( pBuffer );
                return nullptr;
            }

            pBuffer->buffer         = allocationResult.buffer;
            pBuffer->allocation     = allocationResult.allocationInfo;

            KEEN_ASSERT( allocationResult.mappedMemory.isInvalid() || allocationResult.mappedMemory.size == parameters.sizeInBytes );
            pBuffer->pMappedMemory  = allocationResult.mappedMemory.pStart;

            if( isAnyBitSet( bufferCreateInfo.usage, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) )
            {
                pBuffer->deviceAddress = vulkan::getBufferDeviceAddress( m_pVulkan, m_device, pBuffer->buffer );
                KEEN_ASSERT( pBuffer->deviceAddress != 0u );
            }

            KEEN_ASSERT( parameters.cpuAccess.isZero() || pBuffer->pMappedMemory != nullptr );
        }
        else
        {
            const VulkanResult result = m_pVulkan->vkCreateBuffer( m_device, &bufferCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pBuffer->buffer );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateBuffer failed with error '%s'\n", result );
                freeDeviceObject( pBuffer );
                return nullptr;
            }

            if( parameters.deviceMemoryRange.pDeviceMemory != nullptr )
            {
                const GraphicsBufferMemoryBinding binding = { pBuffer, parameters.deviceMemoryRange };
                bindMemory( createArrayView( binding ), {} );
            }
        }

        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pBuffer->buffer, VK_OBJECT_TYPE_BUFFER, parameters.debugName );

        graphics::initializeDeviceObject( pBuffer, GraphicsDeviceObjectType::Buffer, parameters.debugName );

        KEEN_PROFILE_COUNTER_INC( m_vulkanBufferCount );
        return pBuffer;
    }

    VulkanTexture* VulkanGraphicsObjects::createTexture( const GraphicsTextureParameters& parameters )
    {
        KEEN_PROFILE_CPU( Vk_createTexture );
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VulkanTexture* pTexture = allocateDeviceObject<VulkanTexture>();
        if( pTexture == nullptr )
        {
            return nullptr;
        }

        VkImageCreateInfo imageCreateInfo{};
        if( fillVkImageCreateInfo( &imageCreateInfo, parameters ).hasError() )
        {
            freeDeviceObject( pTexture );
            return nullptr;
        }

        VkImageFormatListCreateInfo formatListCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO };
        DynamicArray<VkFormat,8u> viewFormats;      

        if( parameters.viewFormats.hasElements() )
        {
            KEEN_ASSERT( parameters.viewFormats.getCount() <= viewFormats.getCapacity() );
            for( size_t i = 0u; i < parameters.viewFormats.getCount(); ++i )
            {
                const VkFormat viewFormat = vulkan::getVulkanFormat( parameters.viewFormats[ i ] );
                viewFormats.pushBack( viewFormat );
            }

            formatListCreateInfo.pViewFormats       = viewFormats.getStart();
            formatListCreateInfo.viewFormatCount    = rangecheck_cast<uint32>( viewFormats.getCount() );
            imageCreateInfo.pNext = &formatListCreateInfo;
            imageCreateInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        }

        const uint16 cubeFaceCount = image::isCubeTextureType( parameters.type ) ? 6u : 1u;
        pTexture->width         = parameters.width;
        pTexture->height        = parameters.height;
        pTexture->depth         = parameters.depth;
        pTexture->levelCount    = parameters.levelCount;
        pTexture->sampleCount   = parameters.sampleCount;
        pTexture->layerCount    = parameters.layerCount * cubeFaceCount;
        pTexture->usageMask     = parameters.usageMask;
        pTexture->type          = parameters.type;
        pTexture->format        = parameters.format;

KEEN_ASSERT( parameters.debugName.hasElements() );

        if( parameters.allocateMemory )
        {
            VulkanGpuMemoryUsage usage = VulkanGpuMemoryUsage::Auto_PreferDevice;
            if( parameters.flags.isSet( GraphicsTextureFlag::PreferHostMemory ) )
            {
                usage = VulkanGpuMemoryUsage::Auto_PreferHost;
            }

            VulkanGpuImageResult allocationResult;
            if( !vulkan::allocateGpuImage( &allocationResult, m_pGpuAllocator, usage, {}, imageCreateInfo, parameters.debugName ) )
            {
                destroyTexture( pTexture );
                return nullptr;
            }

            pTexture->image         = allocationResult.image;
            pTexture->allocation    = allocationResult.allocationInfo;

            if( !createDefaultTextureView( pTexture ) )
            {
                destroyTexture( pTexture );
                return nullptr;
            }
        }
        else
        {
            const VulkanResult result = m_pVulkan->vkCreateImage( m_device, &imageCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pTexture->image );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateImage failed with error '%s'\n", result );
                destroyTexture( pTexture );
                return nullptr;
            }

            if( parameters.deviceMemoryRange.pDeviceMemory != nullptr )
            {
                const GraphicsTextureMemoryBinding binding = { pTexture, parameters.deviceMemoryRange };
                bindMemory( {}, createArrayView( binding ) );
            }
        }

        graphics::initializeDeviceObject( pTexture, GraphicsDeviceObjectType::Texture, parameters.debugName );

        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pTexture->image, VK_OBJECT_TYPE_IMAGE, parameters.debugName );
        if( pTexture->imageView )
        {
            vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pTexture->imageView, VK_OBJECT_TYPE_IMAGE_VIEW, parameters.debugName );
        }

        KEEN_PROFILE_COUNTER_INC( m_vulkanTextureCount );
        return pTexture;
    }

    bool VulkanGraphicsObjects::createDefaultTextureView( VulkanTexture* pTexture )
    {
        VkImageSubresourceRange imageSubresourceRange = vulkan::getImageSubresourceRange( pTexture );

        if( pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_ShaderResource ) ||
            pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_ColorTarget ) ||
            pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_DepthTarget ) ||
            pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_StencilTarget ) ||
            pTexture->usageMask.isSet( GraphicsTextureUsageFlag::Render_ShaderStorage ) )
        {
            const VkFormat vulkanFormat = vulkan::getVulkanFormat( pTexture->format );

            VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            imageViewCreateInfo.image               = pTexture->image;
            imageViewCreateInfo.viewType            = vulkan::getImageViewType( pTexture->type );
            imageViewCreateInfo.format              = vulkanFormat;
            imageViewCreateInfo.components.r        = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.g        = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.b        = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.a        = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.subresourceRange    = imageSubresourceRange;

            if( imageViewCreateInfo.subresourceRange.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT )
            {
                // we can't sample the stencil buffer right now..
                imageViewCreateInfo.subresourceRange.aspectMask &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            const VulkanResult result = m_pVulkan->vkCreateImageView( m_device, &imageViewCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, (VkImageView*)&pTexture->imageView );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateImageView failed with error '%s'\n", result );
                return false;
            }
            vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pTexture->imageView, VK_OBJECT_TYPE_IMAGE_VIEW, pTexture->getDebugName() );
        }
        return true;
    }

    VulkanTexture* VulkanGraphicsObjects::createTextureView( const GraphicsTextureViewParameters& parameters )
    {
        KEEN_PROFILE_CPU( Vk_createTexture );
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        const VulkanTexture* pViewedTexture = (const VulkanTexture*)parameters.pTexture;

        const VkFormat vulkanFormat = vulkan::getVulkanFormat( parameters.format );
        if( vulkanFormat == VK_FORMAT_UNDEFINED )
        {
            KEEN_TRACE_ERROR( "[graphics] Unsupported texture format %s\n", image::getPixelFormatName( parameters.format ) );
            return nullptr;
        }

        const VkImageViewType vulkanImageViewType = vulkan::getImageViewType( parameters.type );
        KEEN_ASSERT( vulkanImageViewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM );

        VulkanTexture* pTexture = allocateDeviceObject<VulkanTexture>();
        if( pTexture == nullptr )
        {
            return nullptr;
        }

        graphics::initializeDeviceObject( pTexture, GraphicsDeviceObjectType::Texture, parameters.debugName );
        pTexture->pViewedTexture    = parameters.pTexture;
        pTexture->width             = pViewedTexture->width;
        pTexture->height            = pViewedTexture->height;
        pTexture->depth             = pViewedTexture->depth;
        pTexture->firstLevel        = rangecheck_cast<uint8>( parameters.subresourceRange.firstMipLevel );
        pTexture->levelCount        = rangecheck_cast<uint8>( parameters.subresourceRange.mipLevelCount );
        pTexture->sampleCount       = pViewedTexture->sampleCount;
        pTexture->firstArrayLayer   = rangecheck_cast<uint16>( parameters.subresourceRange.firstArrayLayer );
        pTexture->layerCount        = rangecheck_cast<uint16>( parameters.subresourceRange.arrayLayerCount );
        pTexture->usageMask         = pViewedTexture->usageMask;
        pTexture->type              = parameters.type;
        pTexture->format            = parameters.format;
        pTexture->image             = VK_NULL_HANDLE;

        const VkImageSubresourceRange imageSubresourceRange = vulkan::getImageSubresourceRange( pTexture );

        VkImageViewUsageCreateInfo imageViewUsageCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO };
        imageViewUsageCreateInfo.usage  = vulkan::getImageUsageMask( parameters.usageMask );

        VkImageViewCreateInfo imageViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        imageViewCreateInfo.pNext               = &imageViewUsageCreateInfo;
        imageViewCreateInfo.image               = pViewedTexture->image;
        imageViewCreateInfo.viewType            = vulkanImageViewType;
        imageViewCreateInfo.format              = vulkanFormat;
        imageViewCreateInfo.components.r        = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.g        = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.b        = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.components.a        = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewCreateInfo.subresourceRange    = imageSubresourceRange;

        if( imageViewCreateInfo.subresourceRange.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT )
        {
            // we can't sample the stencil buffer right now..
            imageViewCreateInfo.subresourceRange.aspectMask &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        const VulkanResult result = m_pVulkan->vkCreateImageView( m_device, &imageViewCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pTexture->imageView );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateImageView failed with error '%s'\n", result );
            destroyTexture( pTexture );
            return nullptr;
        }
        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pTexture->imageView, VK_OBJECT_TYPE_IMAGE_VIEW, parameters.debugName );

        return pTexture;
    }

    VulkanSampler* VulkanGraphicsObjects::createStaticSampler( const GraphicsSamplerParameters& parameters )
    {
        const HashKey32 key = graphics::computeSamplerParametersHash( parameters );

        const StaticSamplerMap::InsertResult insertResult = m_staticSamplerMap.insertKey( key );
        KEEN_ASSERT( insertResult.pValue != nullptr );

        if( !insertResult.isNew )
        {
            VulkanSampler* pSampler = *insertResult.pValue;
            pSampler->referenceCount++;
            return pSampler;
        }

        VulkanSampler* pSampler = createSampler( parameters, "StaticSampler"_debug );
        *insertResult.pValue = pSampler;
        return pSampler;
    }

    VulkanSampler* VulkanGraphicsObjects::createSampler( const GraphicsSamplerParameters& parameters, DebugName debugName )
    {
        KEEN_PROFILE_CPU( Vk_createSampler );
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VulkanSampler* pSampler = allocateDeviceObject<VulkanSampler>();
        if( pSampler == nullptr )
        {
            return nullptr;
        }

        VkSamplerCreateInfo samplerCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerCreateInfo.magFilter         = vulkan::getFilter( parameters.magFilter );
        samplerCreateInfo.minFilter         = vulkan::getFilter( parameters.minFilter );
        samplerCreateInfo.mipmapMode        = vulkan::getSamplerMipmapMode( parameters.mipFilter );
        samplerCreateInfo.addressModeU      = vulkan::getSamplerAddressMode( parameters.addressU );
        samplerCreateInfo.addressModeV      = vulkan::getSamplerAddressMode( parameters.addressV );
        samplerCreateInfo.addressModeW      = vulkan::getSamplerAddressMode( parameters.addressW );
        samplerCreateInfo.mipLodBias        = parameters.mipLodBias;
        samplerCreateInfo.maxAnisotropy     = parameters.maxAnisotropyLevel;
        samplerCreateInfo.anisotropyEnable  = (VkBool32)parameters.maxAnisotropyLevel > 1.0f;
        samplerCreateInfo.compareEnable     = (VkBool32)((parameters.comparisonFunction != GraphicsComparisonFunction::Never) ? VK_TRUE : VK_FALSE);
        samplerCreateInfo.compareOp         = vulkan::getCompareOp( parameters.comparisonFunction );
        samplerCreateInfo.minLod            = parameters.minLod;
        samplerCreateInfo.maxLod            = parameters.maxLod;
        samplerCreateInfo.borderColor       = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        VkSamplerReductionModeCreateInfoEXT reductionModeCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO_EXT };
        if( parameters.reductionMode != GraphicsSamplerReductionMode::Disabled )
        {
            reductionModeCreateInfo.reductionMode = vulkan::getSamplerReductionMode( parameters.reductionMode );
            samplerCreateInfo.pNext         = &reductionModeCreateInfo;
        }

        const VulkanResult result = m_pVulkan->vkCreateSampler( m_device, &samplerCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pSampler->sampler );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateSampler failed with error '%s'\n", result );
            destroySampler( pSampler );
            return nullptr;
        }

        graphics::initializeDeviceObject( pSampler, GraphicsDeviceObjectType::Sampler, debugName );

        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pSampler->sampler, VK_OBJECT_TYPE_SAMPLER, debugName );

        KEEN_PROFILE_COUNTER_INC( m_vulkanSamplerCount );
        return pSampler;
    }

    VulkanDescriptorSetLayout* VulkanGraphicsObjects::createDescriptorSetLayout( const GraphicsDescriptorSetLayoutParameters& parameters )
    {
        VulkanDescriptorSetLayout* pLayout = allocateDeviceObject<VulkanDescriptorSetLayout>();
        if( pLayout == nullptr )
        {
            return nullptr;
        }

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        const uint32 totalBindingCount = parameters.bindings.getCount32() + parameters.staticSamplers.getCount32();

        DynamicArray<VkDescriptorSetLayoutBinding,128u> vulkanBindings; // :JK: what is the proper limit?
        if( !vulkanBindings.trySetCapacity( totalBindingCount ) )
        {
            destroyDescriptorSetLayout( pLayout );
            return nullptr;
        }

        DynamicArray<VkDescriptorBindingFlags, 128u> vulkanBindingFlags;    // :JK: what is the proper limit?
        if( !vulkanBindingFlags.trySetCapacity( totalBindingCount ) )
        {
            destroyDescriptorSetLayout( pLayout );
            return nullptr;
        }

        VkShaderStageFlags mergedStageFlags = 0u;

        uint32 lastBindingSlotIndex = 0u;
        for( uint32 bindingIndex = 0u; bindingIndex < parameters.bindings.getCount32(); ++bindingIndex )
        {
            // fill binding info..
            const GraphicsDescriptorSetLayoutBinding& binding = parameters.bindings[ bindingIndex ];

            VkDescriptorSetLayoutBinding* pVulkanBinding = vulkanBindings.pushBackZero();
            KEEN_ASSERT( pVulkanBinding != nullptr );

            VkDescriptorBindingFlags* pVulkanBindingFlags = vulkanBindingFlags.pushBackZero();
            KEEN_ASSERT( pVulkanBindingFlags != nullptr );

            pVulkanBinding->binding             = bindingIndex;
            pVulkanBinding->descriptorType      = vulkan::getDescriptorType( binding.type );

            if( graphics::isArrayDescriptor( binding.type ) )
            {
                pVulkanBinding->descriptorCount = binding.arraySizeOrBufferStride;
            }
            else
            {
                pVulkanBinding->descriptorCount = 1u;
            }
            pVulkanBinding->stageFlags          = vulkan::getStageFlags( binding.stageMask );
            pVulkanBinding->pImmutableSamplers  = nullptr; // later..

            mergedStageFlags |= pVulkanBinding->stageFlags;

            if( binding.flags.isSet( GraphicsDescriptorSetLayoutBindingFlag::AllowInvalidDescriptor ) )
            {
                *pVulkanBindingFlags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            }

            lastBindingSlotIndex = max<uint32>( lastBindingSlotIndex, bindingIndex );
        }

        if( mergedStageFlags == 0u )
        {
            // fallback.. 
            mergedStageFlags = VK_SHADER_STAGE_ALL;
        }

        // add static samplers:
        if( parameters.staticSamplers.hasElements() )
        {
            pLayout->staticSamplers.create( m_pAllocator, parameters.staticSamplers.getCount() );
            const uint32 firstStaticSamplerBindingSlot = lastBindingSlotIndex + 1u;
            for( uint32 staticSamplerIndex = 0u; staticSamplerIndex < parameters.staticSamplers.getCount32(); ++staticSamplerIndex )
            {
                // create the sampler object for this
                VulkanSampler* pSampler = createStaticSampler( parameters.staticSamplers[ staticSamplerIndex ] );
                KEEN_ASSERT( pSampler != nullptr );

                pLayout->staticSamplers[ staticSamplerIndex ] = pSampler;

                VkDescriptorSetLayoutBinding* pVulkanBinding = vulkanBindings.pushBackZero();
                KEEN_ASSERT( pVulkanBinding != nullptr );
                pVulkanBinding->binding             = firstStaticSamplerBindingSlot + staticSamplerIndex;
                pVulkanBinding->descriptorType      = VK_DESCRIPTOR_TYPE_SAMPLER;
                pVulkanBinding->descriptorCount     = 1u;
                pVulkanBinding->stageFlags          = mergedStageFlags;
                pVulkanBinding->pImmutableSamplers  = &pSampler->sampler;

                vulkanBindingFlags.pushBackZero();              
            }
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        descriptorSetLayoutCreateInfo.bindingCount  = (uint32)vulkanBindings.getSize();
        descriptorSetLayoutCreateInfo.pBindings     = vulkanBindings.getStart();

        VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorBindingFlags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        descriptorBindingFlags.bindingCount     = (uint32)vulkanBindingFlags.getSize();
        descriptorBindingFlags.pBindingFlags    = vulkanBindingFlags.getStart();
        descriptorSetLayoutCreateInfo.pNext = &descriptorBindingFlags;

        const VulkanResult result = m_pVulkan->vkCreateDescriptorSetLayout( m_device, &descriptorSetLayoutCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pLayout->layout );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateDescriptorSetLayout failed with error '%s'\n", result );
            destroyDescriptorSetLayout( pLayout );
            return nullptr;
        }

        graphics::initializeDeviceObject( pLayout, GraphicsDeviceObjectType::DescriptorSetLayout, parameters.debugName );

        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)pLayout->layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, parameters.debugName );

        KEEN_PROFILE_COUNTER_INC( m_vulkanDescriptorSetLayoutCount );

        return pLayout;
    }

    static Result<VkDescriptorSet> allocateDescriptorSet( VulkanApi* pVulkan, VkDevice device, VkDescriptorPool pool, const GraphicsDescriptorSetParameters& parameters )
    {
        VulkanDescriptorSetLayout* pLayout = (VulkanDescriptorSetLayout*)parameters.pDescriptorSetLayout;

        VkDescriptorSetAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocateInfo.descriptorPool     = pool;
        allocateInfo.descriptorSetCount = 1u;
        allocateInfo.pSetLayouts        = &pLayout->layout;

        VkDescriptorSet descriptorSet;
        VulkanResult result = pVulkan->vkAllocateDescriptorSets( device, &allocateInfo, &descriptorSet );
        if( result.hasError() )
        {
            return result.getErrorId();
        }

        // write descriptor set data
        {
            VulkanDescriptorSetWriter descriptorSetWriter( pVulkan );

            for( size_t i = 0u; i < parameters.descriptorData.getSize(); ++i )
            {
                const GraphicsDescriptorData& descriptorData = parameters.descriptorData[ i ];

                if( descriptorData.isInvalid() )
                {
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
                    KEEN_ASSERT( pLayout->bindings[ i ].flags.isSet( GraphicsDescriptorSetLayoutBindingFlag::AllowInvalidDescriptor ) );
#endif
                    continue;
                }

                uint32 startDescriptorIndex = 0u;
                uint32 descriptorCount = 1u;

                if( descriptorData.type == GraphicsDescriptorType::SampledImageArray ||
                    descriptorData.type == GraphicsDescriptorType::StorageImageArray )
                {
                    startDescriptorIndex    = descriptorData.imageArray.targetOffset;
                    descriptorCount         = descriptorData.imageArray.imageCount;
                }
                else if( descriptorData.type == GraphicsDescriptorType::SamplerArray )
                {
                    startDescriptorIndex    = descriptorData.samplerArray.targetOffset;
                    descriptorCount         = descriptorData.samplerArray.samplerCount;
                }

                if( descriptorCount > 0 )
                {
                    descriptorSetWriter.startWriteDescriptors( descriptorSet, pLayout, rangecheck_cast<uint32>( i ), descriptorData.type, startDescriptorIndex, descriptorCount );
                    descriptorSetWriter.writeDescriptor( descriptorData );
                }
            }
        }

        return descriptorSet;
    }

    VulkanDescriptorSet* VulkanGraphicsObjects::createStaticDescriptorSet( const GraphicsDescriptorSetParameters& parameters )
    {
        StaticVulkanDescriptorSet* pDescriptorSet = allocateDeviceObject<StaticVulkanDescriptorSet>();
        if( pDescriptorSet == nullptr )
        {
            return nullptr;
        }

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        // find a static chunk with enough space
        MutexLock lock( &m_staticDescriptorPoolMutex );

        graphics::initializeDeviceObject( pDescriptorSet, GraphicsDeviceObjectType::DescriptorSet, parameters.debugName );

        {
            VulkanDescriptorPool* pDescriptorPool = m_pFirstStaticDescriptorPool;
            while( pDescriptorPool != nullptr )
            {
                // try to allocate a set:
                KEEN_ASSERT( pDescriptorPool->type == VulkanDescriptorPoolType::Static );
                Result<VkDescriptorSet> allocationResult = keen::allocateDescriptorSet( m_pVulkan, m_device, pDescriptorPool->pool, parameters );
                if( allocationResult.isOk() )
                {
                    pDescriptorSet->set     = allocationResult.value;
                    pDescriptorSet->pPool   = pDescriptorPool;

                    vulkan::setObjectName( m_pVulkan, m_device, allocationResult.getValue(), VK_OBJECT_TYPE_DESCRIPTOR_SET, parameters.debugName );

                    return pDescriptorSet;
                }
                pDescriptorPool = pDescriptorPool->pNext;
            }
        }

        // new pool:
        VulkanDescriptorPool* pNewDescriptorPool = createDescriptorPool( VulkanDescriptorPoolType::Static );
        if( pNewDescriptorPool == nullptr )
        {
            KEEN_TRACE_ERROR( "[graphics] could not allocate new Vulkan Descriptor Pool.\n" );
            destroyDescriptorSet( pDescriptorSet );
            return nullptr;
        }
        KEEN_ASSERT( pNewDescriptorPool->type == VulkanDescriptorPoolType::Static );

        pNewDescriptorPool->pNext       = m_pFirstStaticDescriptorPool;
        m_pFirstStaticDescriptorPool    = pNewDescriptorPool;

        Result<VkDescriptorSet> allocationResult = keen::allocateDescriptorSet( m_pVulkan, m_device, pNewDescriptorPool->pool, parameters );
        KEEN_ASSERT( allocationResult.isOk() );
        pDescriptorSet->set     = allocationResult.value;
        pDescriptorSet->pPool   = pNewDescriptorPool;

        vulkan::setObjectName( m_pVulkan, m_device, allocationResult.getValue(), VK_OBJECT_TYPE_DESCRIPTOR_SET, parameters.debugName );

        return pDescriptorSet;
    }

    VulkanDescriptorSet* VulkanGraphicsObjects::createDynamicDescriptorSet( VulkanFrame* pFrame, const GraphicsDescriptorSetParameters& parameters )
    {
        VulkanDescriptorPool* pDescriptorPool = pFrame->pDescriptorPool;

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        KEEN_ASSERT( pDescriptorPool->type == VulkanDescriptorPoolType::Dynamic );

        while( pDescriptorPool != nullptr )
        {
            Result<VkDescriptorSet> allocationResult = keen::allocateDescriptorSet( m_pVulkan, m_device, pDescriptorPool->pool, parameters );
            if( allocationResult.isOk() )
            {
                VulkanDescriptorSet* pDescriptorSet = callDefaultConstructor( allocateZero<VulkanDescriptorSet>( &pDescriptorPool->setAllocator ) );
                KEEN_ASSERT( pDescriptorSet != nullptr );

                graphics::initializeDeviceObject( pDescriptorSet, GraphicsDeviceObjectType::DescriptorSet, parameters.debugName );

                pDescriptorSet->set         = allocationResult.getValue();
                pDescriptorSet->flags       |= GraphicsDescriptorSetFlagMask::Dynamic;

                vulkan::setObjectName( m_pVulkan, m_device, allocationResult.getValue(), VK_OBJECT_TYPE_DESCRIPTOR_SET, parameters.debugName );

#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
                pDescriptorPool->dynamicDescriptorSets.pushBack( pDescriptorSet );
#endif

                return pDescriptorSet;
            }

            if( pDescriptorPool->pNext == nullptr )
            {
                VulkanDescriptorPool* pNewDescriptorPool = createDescriptorPool( VulkanDescriptorPoolType::Dynamic );
                if( pNewDescriptorPool == nullptr )
                {
                    KEEN_TRACE_ERROR( "[graphics] could not allocate new Vulkan Descriptor Pool.\n" );
                    break;
                }

                pDescriptorPool->pNext = pNewDescriptorPool;
            }
            pDescriptorPool = pDescriptorPool->pNext;
        }

        return nullptr;
    }

    VulkanQueryPool* VulkanGraphicsObjects::createQueryPool( const GraphicsQueryPoolParameters& parameters )
    {
        VulkanQueryPool* pQueryPool = allocateDeviceObject<VulkanQueryPool>();
        if( pQueryPool == nullptr )
        {
            return nullptr;
        }
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        graphics::initializeDeviceObject( pQueryPool, GraphicsDeviceObjectType::QueryPool, parameters.debugName );
        KEEN_PROFILE_COUNTER_INC( m_vulkanQueryPoolCount );

        VkQueryPoolCreateInfo createQueryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        createQueryPoolInfo.queryType   = VK_QUERY_TYPE_TIMESTAMP;
        createQueryPoolInfo.queryCount  = (uint32)parameters.queryCount;
        const VulkanResult result = m_pVulkan->vkCreateQueryPool( m_device, &createQueryPoolInfo, m_pSharedData->pVulkanAllocationCallbacks, &pQueryPool->queryPool );
        if( result.hasError() )
        {
            KEEN_TRACE_WARNING( "[graphics] vkCreateQueryPool failed with error '%s'\n", result );
            pQueryPool->queryPool = VK_NULL_HANDLE;
            destroyQueryPool( pQueryPool );
            return nullptr;
        }
        return pQueryPool;
    }

    GraphicsMemoryRequirements VulkanGraphicsObjects::queryTextureMemoryRequirements( const VulkanTexture* pTexture )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VkImageMemoryRequirementsInfo2 imageRequirementsInfo{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr, pTexture->image };

        VkMemoryDedicatedRequirements dedicatedRequirements{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
        VkMemoryRequirements2 memoryRequirements{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedRequirements };

        m_pVulkan->vkGetImageMemoryRequirements2( m_device, &imageRequirementsInfo, &memoryRequirements );
        return vulkan::createGraphicsMemoryRequirements( memoryRequirements.memoryRequirements, dedicatedRequirements.prefersDedicatedAllocation == VK_TRUE, dedicatedRequirements.requiresDedicatedAllocation == VK_TRUE );
    }

    GraphicsMemoryRequirements VulkanGraphicsObjects::queryBufferMemoryRequirements( const VulkanBuffer* pBuffer )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VkBufferMemoryRequirementsInfo2 bufferRequirementsInfo{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, nullptr, pBuffer->buffer };

        VkMemoryDedicatedRequirements dedicatedRequirements{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
        VkMemoryRequirements2 memoryRequirements{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedRequirements };

        m_pVulkan->vkGetBufferMemoryRequirements2( m_device, &bufferRequirementsInfo, &memoryRequirements );
        return vulkan::createGraphicsMemoryRequirements( memoryRequirements.memoryRequirements, dedicatedRequirements.prefersDedicatedAllocation == VK_TRUE, dedicatedRequirements.requiresDedicatedAllocation == VK_TRUE );
    }

    void VulkanGraphicsObjects::bindMemory( const ArrayView<const GraphicsBufferMemoryBinding>& buffers, const ArrayView<const GraphicsTextureMemoryBinding>& textures )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( buffers.hasElements() )
        {
            TlsStackArray<VkBindBufferMemoryInfo, 16u> bufferInfos( buffers.getCount() );
            for( size_t bufferIndex = 0u; bufferIndex < buffers.getCount(); ++bufferIndex )
            {
                const GraphicsBufferMemoryBinding* pBinding = &buffers[ bufferIndex ];
                VkBindBufferMemoryInfo* pBindingInfo        = &bufferInfos[ bufferIndex ];

                const VulkanBuffer* pBuffer             = static_cast<const VulkanBuffer*>( pBinding->pBuffer );
                const VulkanDeviceMemory* pDeviceMemory = static_cast<const VulkanDeviceMemory*>( pBinding->memoryRange.pDeviceMemory );

                *pBindingInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO };
                pBindingInfo->buffer        = pBuffer->buffer;
                pBindingInfo->memory        = pDeviceMemory->allocation.memory;
                pBindingInfo->memoryOffset  = pBinding->memoryRange.offset;

                VulkanBuffer* pNonConstBuffer = const_cast<VulkanBuffer*>( pBuffer );
                KEEN_ASSERT( pNonConstBuffer->pBoundDeviceMemory == nullptr );
                KEEN_ASSERT( pNonConstBuffer->sizeInBytes <= pBinding->memoryRange.size );
                if( pDeviceMemory->mappedMemory.isValid() )
                {
                    pNonConstBuffer->pMappedMemory  = pDeviceMemory->mappedMemory.pStart + pBinding->memoryRange.offset;
                }
                pNonConstBuffer->pBoundDeviceMemory = pDeviceMemory;
                pNonConstBuffer->boundMemoryOffset  = pBinding->memoryRange.offset;
            }

            const VulkanResult result = m_pVulkan->vkBindBufferMemory2( m_device, rangecheck_cast<uint32>( bufferInfos.getCount() ), bufferInfos.getStart() );
            if( result.hasError() )
            {
                KEEN_BREAK( "vkBindBufferMemory2 failed with %k", result );
            }
        }

        if( textures.hasElements() )
        {
            TlsStackArray<VkBindImageMemoryInfo, 16u> imageInfos( textures.getCount() );
            for( size_t textureIndex = 0u; textureIndex < textures.getCount(); ++textureIndex )
            {
                const GraphicsTextureMemoryBinding* pBinding    = &textures[ textureIndex ];
                VkBindImageMemoryInfo* pBindingInfo             = &imageInfos[ textureIndex ];

                const VulkanTexture* pTexture           = static_cast<const VulkanTexture*>( pBinding->pTexture );
                const VulkanDeviceMemory* pDeviceMemory = static_cast<const VulkanDeviceMemory*>( pBinding->memoryRange.pDeviceMemory );

                *pBindingInfo = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO };
                pBindingInfo->image         = pTexture->image;
                pBindingInfo->memory        = pDeviceMemory->allocation.memory;
                pBindingInfo->memoryOffset  = pBinding->memoryRange.offset;
            }

            const VulkanResult result = m_pVulkan->vkBindImageMemory2( m_device, rangecheck_cast<uint32>( imageInfos.getCount() ), imageInfos.getStart() );
            if( result.hasError() )
            {
                KEEN_BREAK( "vkBindImageMemory2 failed with %k", result );
            }

            for( size_t textureIndex = 0u; textureIndex < textures.getCount(); ++textureIndex )
            {
                KEEN_VERIFY( createDefaultTextureView( (VulkanTexture*)textures[ textureIndex ].pTexture ) );
            }
        }
    }

    VulkanDescriptorPool* VulkanGraphicsObjects::createDescriptorPool( VulkanDescriptorPoolType type )
    {
        MutexLock lock( &m_descriptorPoolMutex );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        const VulkanDescriptorPoolSizes descriptorPoolSizes = type == VulkanDescriptorPoolType::Dynamic ? m_dynamicDescriptorPoolSizes : m_staticDescriptorPoolSizes;

        if( m_pFirstFreeDynamicDescriptorPool != nullptr )
        {
            if( type == VulkanDescriptorPoolType::Dynamic )
            {
                VulkanDescriptorPool* pDescriptorPool = m_pFirstFreeDynamicDescriptorPool;
                m_pFirstFreeDynamicDescriptorPool = pDescriptorPool->pNext;
                pDescriptorPool->pNext = nullptr;
                return pDescriptorPool;
            }
        }

        VulkanDescriptorPool* pDescriptorPool = newObjectZero<VulkanDescriptorPool>( m_pAllocator, "VulkanDescriptorPool"_debug );
        if( pDescriptorPool == nullptr )
        {
            return nullptr;
        }

        if( type == VulkanDescriptorPoolType::Dynamic )
        {
            pDescriptorPool->setAllocator.create( m_pAllocator, descriptorPoolSizes.descriptorSetCount * sizeof( VulkanDescriptorSet ), "DynamicVulkanDescriptorPool"_debug );

#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
            pDescriptorPool->dynamicDescriptorSets.create( m_pAllocator, descriptorPoolSizes.descriptorSetCount, false );
#endif
        }

        DynamicArray< VkDescriptorPoolSize, 8u > poolSizes;
        if( descriptorPoolSizes.uniformBufferDescriptorCount > 0u )
        {
            poolSizes.pushBack( VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorPoolSizes.uniformBufferDescriptorCount } );
        }
        if( descriptorPoolSizes.storageBufferDescriptorCount > 0u )
        {
            poolSizes.pushBack( VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorPoolSizes.storageBufferDescriptorCount } );
        }
        if( descriptorPoolSizes.samplerDescriptorCount > 0u )
        {
            poolSizes.pushBack( VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, descriptorPoolSizes.samplerDescriptorCount } );
        }
        if( descriptorPoolSizes.sampledImageDescriptorCount > 0u )
        {
            poolSizes.pushBack( VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descriptorPoolSizes.sampledImageDescriptorCount } );
        }
        if( descriptorPoolSizes.storageImageDescriptorCount > 0u )
        {
            poolSizes.pushBack( VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorPoolSizes.storageImageDescriptorCount } );
        }

        if( poolSizes.isEmpty() )
        {
            const VkDescriptorPoolSize dummyPoolSize = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u };
            poolSizes.pushBack( dummyPoolSize );
        }

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        descriptorPoolCreateInfo.maxSets        = (uint32)descriptorPoolSizes.descriptorSetCount;
        descriptorPoolCreateInfo.poolSizeCount  = (uint32)poolSizes.getSize();
        descriptorPoolCreateInfo.pPoolSizes     = poolSizes.getStart();
        descriptorPoolCreateInfo.flags          = type == VulkanDescriptorPoolType::Static ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0u;

        const VulkanResult result = m_pVulkan->vkCreateDescriptorPool( m_device, &descriptorPoolCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pDescriptorPool->pool );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateDescriptorPool failed with error '%s'\n", result );
            deleteObject( m_pAllocator, pDescriptorPool );
            return nullptr;
        }

        pDescriptorPool->type = type;
        pDescriptorPool->pNext = nullptr;

        KEEN_PROFILE_COUNTER_INC( m_vulkanDescriptorPoolCount );

        return pDescriptorPool;
    }

    void VulkanGraphicsObjects::freeDescriptorPool( VulkanDescriptorPool* pDescriptorPool )
    {
        MutexLock lock( &m_descriptorPoolMutex );
        KEEN_ASSERT( pDescriptorPool->pool != VK_NULL_HANDLE );
        KEEN_ASSERT( pDescriptorPool->type == VulkanDescriptorPoolType::Dynamic );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_pVulkan->vkResetDescriptorPool( m_device, pDescriptorPool->pool, 0u );

#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
        for( size_t i = 0u; i < pDescriptorPool->dynamicDescriptorSets.getCount(); ++i )
        {
            callDestructor( pDescriptorPool->dynamicDescriptorSets[ i ] );
        }
        pDescriptorPool->dynamicDescriptorSets.clear();
#endif

        pDescriptorPool->setAllocator.clear();

        pDescriptorPool->pNext = m_pFirstFreeDynamicDescriptorPool;
        m_pFirstFreeDynamicDescriptorPool = pDescriptorPool;
    }

    void VulkanGraphicsObjects::destroyDescriptorPool( VulkanDescriptorPool* pDescriptorPool )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        MutexLock lock( &m_descriptorPoolMutex );
        if( pDescriptorPool->pool != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyDescriptorPool( m_device, pDescriptorPool->pool, m_pSharedData->pVulkanAllocationCallbacks );

            KEEN_PROFILE_COUNTER_DEC( m_vulkanDescriptorPoolCount );
        }

        if( pDescriptorPool->type == VulkanDescriptorPoolType::Dynamic )
        {
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
            pDescriptorPool->dynamicDescriptorSets.destroy();
#endif
            pDescriptorPool->setAllocator.destroy( m_pAllocator );
        }

        deleteObject( m_pAllocator, pDescriptorPool );
    }

    VulkanComputePipeline* VulkanGraphicsObjects::createComputePipeline( const GraphicsComputePipelineParameters& parameters )
    {
        KEEN_PROFILE_CPU( Vk_CreateComputePipeline );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( parameters.shaderCode.size == 0u )
        {
            return nullptr;
        }

        VulkanComputePipeline* pComputePipeline = allocateDeviceObject<VulkanComputePipeline>();
        if( pComputePipeline == nullptr )
        {
            return nullptr;
        }

        graphics::initializeDeviceObject( pComputePipeline, GraphicsDeviceObjectType::ComputePipeline, parameters.debugName );

        KEEN_PROFILE_COUNTER_INC( m_vulkanComputePipelineCount );

        VkShaderModule computeShader;
        {
            VkShaderModuleCreateInfo computeShaderModuleCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            computeShaderModuleCreateInfo.codeSize = parameters.shaderCode.size;
            computeShaderModuleCreateInfo.pCode = ( const uint32_t* )parameters.shaderCode.pStart;

            const VulkanResult result = m_pVulkan->vkCreateShaderModule( m_device, &computeShaderModuleCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &computeShader );
            if( result.hasError() )
            {
                KEEN_TRACE_WARNING( "[graphics] vkCreateShaderModule failed with error '%s'\n", result );
                freeDeviceObject( pComputePipeline );
                return nullptr;
            }

            KEEN_ASSERT( computeShader != VK_NULL_HANDLE );

            vulkan::setObjectName( m_pVulkan, m_device, computeShader, VK_OBJECT_TYPE_SHADER_MODULE, DebugName::createFormatted( "%k_cs", parameters.debugName ) );
        }

        const Result<void> compileResult = compileComputePipeline( pComputePipeline, parameters, computeShader );
        m_pVulkan->vkDestroyShaderModule( m_device, computeShader, m_pSharedData->pVulkanAllocationCallbacks );

        if( compileResult.hasError() )
        {
            destroyComputePipeline( pComputePipeline );
            return nullptr;
        }
        return pComputePipeline;
    }

    ErrorId VulkanGraphicsObjects::compileComputePipeline( VulkanComputePipeline* pPipeline, const GraphicsComputePipelineParameters& parameters, VkShaderModule computeShader )
    {
        KEEN_PROFILE_CPU( Vk_CompileComputePipeline );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        // :JK: WARNING: this is called in parallel by multiple threads!! Please make sure all access to shared data is correctly synchronized
        const VulkanPipelineLayout* pPipelineLayout = (const VulkanPipelineLayout*)parameters.pPipelineLayout;
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
        KEEN_VERIFY( graphics::validateDeviceObject( pPipelineLayout, GraphicsDeviceObjectType::PipelineLayout ) );
#endif

        VkDevice device = m_device;
        VulkanApi* pVulkan = m_pVulkan;

        VkComputePipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineCreateInfo.stage.sType          = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineCreateInfo.stage.stage          = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineCreateInfo.stage.module         = computeShader;
        pipelineCreateInfo.stage.pName          = graphics::getEntryPointName( parameters.entryPointId, parameters.entryPoint, GraphicsPipelineStage::Compute ).getStart();
        pipelineCreateInfo.layout               = pPipelineLayout->pipelineLayout;
        pipelineCreateInfo.basePipelineHandle   = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex    = -1;

#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )
        if( m_pVulkan->KHR_pipeline_executable_properties )
        {
            pipelineCreateInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
        }
#endif

#ifndef KEEN_BUILD_MASTER
        const SystemTimer timer;
#endif

        VkPipeline pipeline = VK_NULL_HANDLE;
        VulkanResult result = pVulkan->vkCreateComputePipelines( device, m_pipelineCache, 1u, &pipelineCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pipeline );
        if( result.hasError() || pipeline == VK_NULL_HANDLE )   // :JK: pipeline shouldn't be null if vkCreateComputePipelines succeeds - but we had crashdumps suggesting that this happens
        {
            KEEN_TRACE_WARNING( "[graphics] vkCompileComputePipeline of pipeline '%s' failed with error '%s'. Trying again without pipeline cache.\n", parameters.debugName, result );
            result = pVulkan->vkCreateComputePipelines( device, VK_NULL_HANDLE, 1u, &pipelineCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pipeline );
            if( result.hasError() || pipeline == VK_NULL_HANDLE )
            {
                KEEN_TRACE_WARNING( "[graphics] vkCompileComputePipeline of pipeline '%s' failed with error '%s' (pipeline cache was skipped).\n", parameters.debugName, result );
                return result.getErrorId();
            }
        }

#ifndef KEEN_BUILD_MASTER
        const Time elapsedTime = timer.getElapsedTime();
        if( elapsedTime > 1_s )
        {
            KEEN_TRACE_WARNING( "[vulkan] compiling compute pipeline '%k' took %k!\n", parameters.debugName, elapsedTime );
        }
#endif

        KEEN_ASSERT( pipeline != VK_NULL_HANDLE );

        // :TB: use a atomic way to assign pipeline to avoid thread issues
        pPipeline->pipeline = pipeline;

        vulkan::setObjectName( pVulkan, device, (VkObjectHandle)pPipeline->pipeline, VK_OBJECT_TYPE_PIPELINE, parameters.debugName );

        return ErrorId_Ok;
    }

    ErrorId VulkanGraphicsObjects::compileRenderPipeline( VulkanRenderPipeline* pPipeline, const GraphicsRenderPipelineParameters& parameters, const RenderPipelineShaderModules& shaderModules )
    {
        KEEN_PROFILE_CPU( Vk_CompileRenderPipeline );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        // :JK: WARNING: this is called in parallel by multiple threads!! Please make suer all access to shared data is correctly synchronized
        const VulkanPipelineLayout* pPipelineLayout = (const VulkanPipelineLayout*)parameters.pPipelineLayout;
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
        KEEN_VERIFY( graphics::validateDeviceObject( pPipelineLayout, GraphicsDeviceObjectType::PipelineLayout ) );
#endif

        VkDevice device = m_device;
        VulkanApi* pVulkan = m_pVulkan;

        const bool hasTessellation = shaderModules.tcShader != VK_NULL_HANDLE;
        
        DynamicArray<VkPipelineShaderStageCreateInfo, 5u> shaderStageCreateInfos;

        VkPipelineShaderStageCreateInfo* pVertexShaderStageCreateInfo = shaderStageCreateInfos.pushBack();
        zeroValue( pVertexShaderStageCreateInfo );
        pVertexShaderStageCreateInfo->sType     = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pVertexShaderStageCreateInfo->stage     = VK_SHADER_STAGE_VERTEX_BIT;
        pVertexShaderStageCreateInfo->module    = shaderModules.vertexShader;
        pVertexShaderStageCreateInfo->pName     = graphics::getEntryPointName( parameters.entryPointId, parameters.vsEntryPoint, GraphicsPipelineStage::Vertex ).getStart();

        if( hasTessellation )
        {
            VkPipelineShaderStageCreateInfo* pTcShaderStageCreateInfo = shaderStageCreateInfos.pushBack();
            zeroValue( pTcShaderStageCreateInfo );
            pTcShaderStageCreateInfo->sType     = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pTcShaderStageCreateInfo->stage     = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            pTcShaderStageCreateInfo->module    = shaderModules.tcShader;
            pTcShaderStageCreateInfo->pName     = graphics::getEntryPointName( parameters.entryPointId, parameters.tcEntryPoint, GraphicsPipelineStage::TS_Control ).getStart();

            VkPipelineShaderStageCreateInfo* pTeShaderStageCreateInfo = shaderStageCreateInfos.pushBack();
            zeroValue( pTeShaderStageCreateInfo );
            pTeShaderStageCreateInfo->sType     = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pTeShaderStageCreateInfo->stage     = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            pTeShaderStageCreateInfo->module    = shaderModules.teShader;
            pTeShaderStageCreateInfo->pName     = graphics::getEntryPointName( parameters.entryPointId, parameters.teEntryPoint, GraphicsPipelineStage::TS_Evaluation ).getStart();
        }

        if( shaderModules.fragmentShader != VK_NULL_HANDLE )
        {
            VkPipelineShaderStageCreateInfo* pFragmentShaderStageCreateInfo = shaderStageCreateInfos.pushBack();
            zeroValue( pFragmentShaderStageCreateInfo );
            pFragmentShaderStageCreateInfo->sType   = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pFragmentShaderStageCreateInfo->stage   = VK_SHADER_STAGE_FRAGMENT_BIT;
            pFragmentShaderStageCreateInfo->module  = shaderModules.fragmentShader;
            pFragmentShaderStageCreateInfo->pName   = graphics::getEntryPointName( parameters.entryPointId, parameters.fsEntryPoint, GraphicsPipelineStage::Fragment ).getStart();
        }

        VkVertexInputAttributeDescription vertexAttributeDescriptions[ VertexAttributeId_Count ];
        VkVertexInputBindingDescription vertexInputBindingDescription;
        uint32 vertexAttributeDescriptionCount = 0u;
        uint32 vertexInputBindingDescriptionCount = 0u;

        const VertexFormat* pVertexFormat = parameters.pVertexFormat;
        if( pVertexFormat != nullptr )
        {
            for( size_t attributeId = 0u; attributeId < VertexAttributeId_Count; ++attributeId )
            {
                if( !pVertexFormat->hasAttribute( attributeId ) )
                {
                    continue;
                }
                const VertexFormat::AttributeInfo& attribute = pVertexFormat->attributeInfos[ attributeId ];

                KEEN_ASSERT( attribute.format < VertexAttributeFormat_Count );

                const VkFormat vulkanFormat = vulkan::getVertexAttributeFormat( attribute.format );
                KEEN_ASSERT( vulkanFormat != VK_FORMAT_UNDEFINED );

                vertexAttributeDescriptions[ vertexAttributeDescriptionCount ].location = (uint32)attributeId;
                vertexAttributeDescriptions[ vertexAttributeDescriptionCount ].binding  = 0u;
                vertexAttributeDescriptions[ vertexAttributeDescriptionCount ].format   = vulkanFormat;
                vertexAttributeDescriptions[ vertexAttributeDescriptionCount ].offset   = attribute.offset;

                vertexAttributeDescriptionCount++;
            }

            vertexInputBindingDescription.binding   = 0u;
            vertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            vertexInputBindingDescription.stride    = pVertexFormat->stride;
            vertexInputBindingDescriptionCount = 1u;
        }

        VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputStateCreateInfo.pVertexBindingDescriptions       = &vertexInputBindingDescription;
        vertexInputStateCreateInfo.vertexBindingDescriptionCount    = vertexInputBindingDescriptionCount;
        vertexInputStateCreateInfo.pVertexAttributeDescriptions     = vertexAttributeDescriptions;
        vertexInputStateCreateInfo.vertexAttributeDescriptionCount  = vertexAttributeDescriptionCount;

        VkPipelineTessellationStateCreateInfo tessellationStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO };
        tessellationStateCreateInfo.patchControlPoints = parameters.patchSize;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssemblyStateCreateInfo.topology = vulkan::getPrimitiveTopology( parameters.primitiveType );

        VkViewport viewport;
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = 0.0f;
        viewport.height     = 0.0f;
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;

        VkRect2D scissor;
        scissor.offset.x        = 0;
        scissor.offset.y        = 0;
        scissor.extent.width    = 0u;
        scissor.extent.height   = 0u;

        VkPipelineViewportStateCreateInfo viewportStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportStateCreateInfo.viewportCount   = 1u;
        viewportStateCreateInfo.pViewports      = &viewport;
        viewportStateCreateInfo.scissorCount    = 1u;
        viewportStateCreateInfo.pScissors       = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizerStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizerStateCreateInfo.polygonMode               = vulkan::getPolygonMode( parameters.fillMode );
        rasterizerStateCreateInfo.cullMode                  = vulkan::getCullModeFlagBits( parameters.cullMode );
        rasterizerStateCreateInfo.frontFace                 = vulkan::getFrontFace( parameters.windingOrder );
        rasterizerStateCreateInfo.lineWidth                 = 1.0f;
        rasterizerStateCreateInfo.depthBiasEnable           = ( ( parameters.slopeDepthBias != 0.0f ) || ( parameters.constDepthBias != 0.0f ) ) ? VK_TRUE : VK_FALSE;
        rasterizerStateCreateInfo.depthBiasConstantFactor   = parameters.constDepthBias;
        rasterizerStateCreateInfo.depthBiasSlopeFactor      = parameters.slopeDepthBias;

        const bool depthTestEnabled = parameters.depthComparisonFunction != GraphicsComparisonFunction::Always;
        const bool depthWriteEnabled = parameters.depthWriteEnabled;

        VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencilStateCreateInfo.depthTestEnable         = (VkBool32)(depthTestEnabled || depthWriteEnabled);
        depthStencilStateCreateInfo.depthWriteEnable        = (VkBool32)depthWriteEnabled;
        depthStencilStateCreateInfo.depthCompareOp          = vulkan::getCompareOp( parameters.depthComparisonFunction );
        depthStencilStateCreateInfo.depthBoundsTestEnable   = VK_FALSE;
        depthStencilStateCreateInfo.stencilTestEnable       = (VkBool32)( parameters.frontStencil.testEnabled || parameters.backStencil.testEnabled );

        depthStencilStateCreateInfo.front.failOp            = vulkan::getStencilOp( parameters.frontStencil.opFail );
        depthStencilStateCreateInfo.front.passOp            = vulkan::getStencilOp( parameters.frontStencil.opDepthPass );
        depthStencilStateCreateInfo.front.depthFailOp       = vulkan::getStencilOp( parameters.frontStencil.opDepthFail );
        depthStencilStateCreateInfo.front.compareOp         = vulkan::getCompareOp( parameters.frontStencil.testFunc );
        depthStencilStateCreateInfo.front.compareMask       = parameters.frontStencil.testMask;
        depthStencilStateCreateInfo.front.writeMask         = parameters.frontStencil.writeMask;
        depthStencilStateCreateInfo.front.reference         = 0u;

        depthStencilStateCreateInfo.back.failOp             = vulkan::getStencilOp( parameters.backStencil.opFail );
        depthStencilStateCreateInfo.back.passOp             = vulkan::getStencilOp( parameters.backStencil.opDepthPass );
        depthStencilStateCreateInfo.back.depthFailOp        = vulkan::getStencilOp( parameters.backStencil.opDepthFail );
        depthStencilStateCreateInfo.back.compareOp          = vulkan::getCompareOp( parameters.backStencil.testFunc );
        depthStencilStateCreateInfo.back.compareMask        = parameters.frontStencil.testMask;
        depthStencilStateCreateInfo.back.writeMask          = parameters.frontStencil.writeMask;
        depthStencilStateCreateInfo.back.reference          = 0u;

        VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampleStateCreateInfo.rasterizationSamples     = vulkan::getSampleCountFlagBits( parameters.sampleCount );
        if( parameters.sampleShading )
        {
            multisampleStateCreateInfo.sampleShadingEnable  = VK_TRUE;
            multisampleStateCreateInfo.minSampleShading     = 1.f;
        }
        multisampleStateCreateInfo.alphaToCoverageEnable    = parameters.alphaToCoverage;

        const uint8 colorTargetCount = parameters.renderTargetFormat.getColorTargetCount();

        const bool blendingEnabled = parameters.blendOp != GraphicsBlendOperation::None && colorTargetCount > 0u;

        VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
        colorBlendAttachmentState.blendEnable           = (VkBool32)( blendingEnabled ? VK_TRUE : VK_FALSE );
        colorBlendAttachmentState.srcColorBlendFactor   = vulkan::getBlendFactor( parameters.blendSourceFactor );
        colorBlendAttachmentState.dstColorBlendFactor   = vulkan::getBlendFactor( parameters.blendDestFactor );
        colorBlendAttachmentState.colorBlendOp          = vulkan::getBlendOp( parameters.blendOp );
        colorBlendAttachmentState.srcAlphaBlendFactor   = vulkan::getBlendFactor( parameters.blendSourceFactor );
        colorBlendAttachmentState.dstAlphaBlendFactor   = vulkan::getBlendFactor( parameters.blendDestFactor );
        colorBlendAttachmentState.alphaBlendOp          = vulkan::getBlendOp( parameters.blendOp );

        VkPipelineColorBlendAttachmentState colorBlendAttachmentStates[ GraphicsLimits_MaxColorTargetCount ];
        for( size_t i = 0u; i < colorTargetCount; ++i )
        {
            colorBlendAttachmentStates[ i ]                 = colorBlendAttachmentState;
            colorBlendAttachmentStates[ i ].colorWriteMask  = vulkan::getColorComponentFlagBits( parameters.colorWriteMask[ i ] );
        }

        VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlendStateCreateInfo.logicOpEnable     = VK_FALSE;
        colorBlendStateCreateInfo.attachmentCount   = colorTargetCount;
        colorBlendStateCreateInfo.pAttachments      = colorBlendAttachmentStates;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineCreateInfo.stageCount           = rangecheck_cast<uint32>( shaderStageCreateInfos.getSize() );
        pipelineCreateInfo.pStages              = shaderStageCreateInfos.getStart();
        pipelineCreateInfo.pVertexInputState    = &vertexInputStateCreateInfo;
        pipelineCreateInfo.pInputAssemblyState  = &inputAssemblyStateCreateInfo;
        if( hasTessellation )
        {
            pipelineCreateInfo.pTessellationState = &tessellationStateCreateInfo;
        }
        pipelineCreateInfo.pViewportState       = &viewportStateCreateInfo;
        pipelineCreateInfo.pRasterizationState  = &rasterizerStateCreateInfo;
        pipelineCreateInfo.pDepthStencilState   = &depthStencilStateCreateInfo;
        pipelineCreateInfo.pMultisampleState    = &multisampleStateCreateInfo;
        pipelineCreateInfo.pColorBlendState     = &colorBlendStateCreateInfo;       // pass nullptr to disable rasterization

        DynamicArray<VkDynamicState, 16u> dynamicStates;

        if( parameters.dynamicState.isSet( GraphicsDynamicStateFlag::Viewport ) )
        {
            dynamicStates.pushBack( VK_DYNAMIC_STATE_VIEWPORT );
        }

        if( parameters.dynamicState.isSet( GraphicsDynamicStateFlag::Scissor ) )
        {
            dynamicStates.pushBack( VK_DYNAMIC_STATE_SCISSOR );
        }

        if( parameters.dynamicState.isSet( GraphicsDynamicStateFlag::StencilReference ) )
        {
            dynamicStates.pushBack( VK_DYNAMIC_STATE_STENCIL_REFERENCE );
        }

        if( parameters.dynamicState.isSet( GraphicsDynamicStateFlag::StencilWriteMask ) )
        {
            dynamicStates.pushBack( VK_DYNAMIC_STATE_STENCIL_WRITE_MASK );
        }

        if( parameters.dynamicState.isSet( GraphicsDynamicStateFlag::StencilCompareMask ) )
        {
            dynamicStates.pushBack( VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK );
        }

        VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicStateCreateInfo.dynamicStateCount    = (uint32)dynamicStates.getSize();
        dynamicStateCreateInfo.pDynamicStates       = dynamicStates.getStart();

        if( !dynamicStates.isEmpty() )
        {
            pipelineCreateInfo.pDynamicState    = &dynamicStateCreateInfo;
        }

        VkPipelineRenderingCreateInfo renderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        VkFormat colorAttachmentFormats[ GraphicsLimits_MaxColorTargetCount ];

        {
            for( size_t i = 0u; i < colorTargetCount; ++i )
            {
                colorAttachmentFormats[ i ] = vulkan::getVulkanFormat( parameters.renderTargetFormat.colorTargetFormats[ i ] );
            }

            const bool hasStencil                       = image::hasStencil( parameters.renderTargetFormat.depthStencilTargetFormat );
            const bool hasDepth                         = image::hasDepth( parameters.renderTargetFormat.depthStencilTargetFormat );
            const VkFormat depthStencilFormat           = vulkan::getVulkanFormat( parameters.renderTargetFormat.depthStencilTargetFormat );
            renderingCreateInfo.viewMask                = 0u;
            renderingCreateInfo.colorAttachmentCount    = colorTargetCount;
            renderingCreateInfo.pColorAttachmentFormats = colorAttachmentFormats;
            renderingCreateInfo.depthAttachmentFormat   = hasDepth ? depthStencilFormat : VK_FORMAT_UNDEFINED;
            renderingCreateInfo.stencilAttachmentFormat = hasStencil ? depthStencilFormat : VK_FORMAT_UNDEFINED;

            pipelineCreateInfo.pNext = &renderingCreateInfo;
        }

        pipelineCreateInfo.layout               = pPipelineLayout->pipelineLayout;
        pipelineCreateInfo.subpass              = 0u;
        pipelineCreateInfo.basePipelineHandle   = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex    = -1;

#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )
        if( m_pVulkan->KHR_pipeline_executable_properties )
        {
            pipelineCreateInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
        }
#endif

#ifndef KEEN_BUILD_MASTER
        const SystemTimer timer;
#endif

        VkPipeline pipeline = VK_NULL_HANDLE;
        VulkanResult result = pVulkan->vkCreateGraphicsPipelines( device, m_pipelineCache, 1u, &pipelineCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pipeline );
        if( result.hasError() || pipeline == VK_NULL_HANDLE )
        {
            KEEN_TRACE_WARNING( "[graphics] vkCreateGraphicsPipelines of pipeline '%s' failed with error '%s'. Trying again without pipeline cache.\n", parameters.debugName, result );
            result = pVulkan->vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1u, &pipelineCreateInfo, m_pSharedData->pVulkanAllocationCallbacks, &pipeline );
            if( result.hasError() || pipeline == VK_NULL_HANDLE )
            {
                KEEN_TRACE_WARNING( "[graphics] vkCreateGraphicsPipelines of pipeline '%s' failed with error '%s' (pipeline cache was skipped).\n", parameters.debugName, result );
                return result.getErrorId();
            }
        }

        KEEN_ASSERT( pipeline != VK_NULL_HANDLE );

#ifndef KEEN_BUILD_MASTER
        const Time elapsedTime = timer.getElapsedTime();
        if( elapsedTime > 1_s )
        {
            KEEN_TRACE_WARNING( "[vulkan] compiling render pipeline '%k' took %k!\n", parameters.debugName, elapsedTime );
        }
#endif

        pPipeline->pipeline = pipeline;

        vulkan::setObjectName( pVulkan, device, (VkObjectHandle)pPipeline->pipeline, VK_OBJECT_TYPE_PIPELINE, parameters.debugName );

        return ErrorId_Ok;
    }

    void VulkanGraphicsObjects::destroyDeviceObject( GraphicsDeviceObject* pObject )
    {
        KEEN_ASSERT( pObject != nullptr );
        graphics::shutdownDeviceObject( pObject );

        switch( pObject->objectType )
        {
        case GraphicsDeviceObjectType::SwapChain:
            destroySwapChain( (VulkanSwapChainWrapper*)pObject );
            break;

        case GraphicsDeviceObjectType::PipelineLayout:
            destroyPipelineLayout( (VulkanPipelineLayout*)pObject );
            break;

        case GraphicsDeviceObjectType::RenderPipeline:
            destroyRenderPipeline( (VulkanRenderPipeline*)pObject );
            break;

        case GraphicsDeviceObjectType::DeviceMemory:
            destroyDeviceMemory( (VulkanDeviceMemory*)pObject );
            break;

        case GraphicsDeviceObjectType::Buffer:
            destroyBuffer( (VulkanBuffer*)pObject );
            break;

        case GraphicsDeviceObjectType::Texture:
            destroyTexture( (VulkanTexture*)pObject );
            break;

        case GraphicsDeviceObjectType::Sampler:
            destroySampler( (VulkanSampler*)pObject );
            break;

        case GraphicsDeviceObjectType::DescriptorSetLayout:
            destroyDescriptorSetLayout( (VulkanDescriptorSetLayout*)pObject );
            break;

        case GraphicsDeviceObjectType::DescriptorSet:
            destroyDescriptorSet( (StaticVulkanDescriptorSet*)pObject );
            break;

        case GraphicsDeviceObjectType::QueryPool:
            destroyQueryPool( (VulkanQueryPool*)pObject );
            break;

        case GraphicsDeviceObjectType::ComputePipeline:
            destroyComputePipeline( (VulkanComputePipeline*)pObject );
            break;

        case GraphicsDeviceObjectType::Count:
            KEEN_TRACE_ERROR( "Invalid object type!\n" );
            break;
        }
    }

    void VulkanGraphicsObjects::destroyFrameObjects( ArrayView<GraphicsDeviceObject*> objects )
    {
        KEEN_PROFILE_CPU( Vk_destroyFrameObjects );

        for( size_t i = 0u; i < objects.getSize(); ++i )
        {
            destroyDeviceObject( objects[ i ] );
        }
    }

    void VulkanGraphicsObjects::flushCpuMemoryCache( ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes )
    {
        vulkan::flushCpuMemoryCache( m_pGpuAllocator, allocations, offsets, sizes );
    }

    void VulkanGraphicsObjects::invalidateCpuMemoryCache( ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes )
    {
        vulkan::invalidateCpuMemoryCache( m_pGpuAllocator, allocations, offsets, sizes );
    }

    ErrorId VulkanGraphicsObjects::createShaderModule( VkShaderModule* pShaderModule, ConstMemoryBlock shaderCode, const DebugName& debugName )
    {
        if( !isConstMemoryBlockValid( shaderCode ) )
        {
            // silently ignore
            return ErrorId_Ok;
        }
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        const uint32* pBinaryData = ( const uint32* )shaderCode.pStart;
        const size_t binarySize = shaderCode.size;

        VkShaderModuleCreateInfo shaderModule = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        shaderModule.codeSize   = binarySize;
        shaderModule.pCode      = pBinaryData;

        const VulkanResult result = m_pVulkan->vkCreateShaderModule( m_device, &shaderModule, m_pSharedData->pVulkanAllocationCallbacks, pShaderModule );
        if( result.hasError() )
        {
            KEEN_TRACE_WARNING( "[graphics] vkCreateShaderModule failed with error '%s'\n", result );
            return result.getErrorId();
        }

        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)*pShaderModule, VK_OBJECT_TYPE_SHADER_MODULE, debugName );

        return ErrorId_Ok;
    }

    void VulkanGraphicsObjects::destroyShaderModules( const RenderPipelineShaderModules& shaderModules )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( shaderModules.fragmentShader != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyShaderModule( m_device, shaderModules.fragmentShader, m_pSharedData->pVulkanAllocationCallbacks );
        }
        if( shaderModules.vertexShader != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyShaderModule( m_device, shaderModules.vertexShader, m_pSharedData->pVulkanAllocationCallbacks );
        }
        if( shaderModules.tcShader != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyShaderModule( m_device, shaderModules.tcShader, m_pSharedData->pVulkanAllocationCallbacks );
        }
        if( shaderModules.teShader != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyShaderModule( m_device, shaderModules.teShader, m_pSharedData->pVulkanAllocationCallbacks );
        }
    }

    Result<void> VulkanGraphicsObjects::prepareRenderPipelineCompileParameters( RenderPipelineShaderModules* pShaderModules, const GraphicsRenderPipelineParameters& parameters )
    {
        KEEN_PROFILE_CPU( Vk_createGraphicsPipeline );

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        {
            StickyError error;
            error = createShaderModule( &pShaderModules->vertexShader, parameters.vertexShaderCode, DebugName::createFormatted( "%k_vs", parameters.debugName ) );
            error = createShaderModule( &pShaderModules->tcShader, parameters.tcShaderCode, DebugName::createFormatted( "%k_tcs", parameters.debugName ) );
            error = createShaderModule( &pShaderModules->teShader, parameters.teShaderCode, DebugName::createFormatted( "%k_tes", parameters.debugName ) );
            error = createShaderModule( &pShaderModules->fragmentShader, parameters.fragmentShaderCode, DebugName::createFormatted( "%k_fs", parameters.debugName ) );
            if( error.hasError() )
            {
                return error.getError();
            }
        }

        return ErrorId_Ok;
    }

    void VulkanGraphicsObjects::destroySwapChain( VulkanSwapChainWrapper* pSwapChainWrapper )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        VkSurfaceKHR surface = VK_NULL_HANDLE;

        if( pSwapChainWrapper->pSwapChain != nullptr )
        {
            surface = pSwapChainWrapper->pSwapChain->getSurface();
            pSwapChainWrapper->pSwapChain->destroy();
            deleteObject( m_pAllocator, pSwapChainWrapper->pSwapChain );
        }
        if( surface != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroySurfaceKHR( m_instance, surface, m_pSharedData->pVulkanAllocationCallbacks );       
        }
        freeDeviceObject( pSwapChainWrapper );
        KEEN_PROFILE_COUNTER_DEC( m_vulkanSwapChainCount );
    }

    void VulkanGraphicsObjects::destroyRenderPipeline( VulkanRenderPipeline* pRenderPipeline )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( pRenderPipeline->pipeline != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyPipeline( m_device, pRenderPipeline->pipeline, m_pSharedData->pVulkanAllocationCallbacks );
        }

        freeDeviceObject( pRenderPipeline );
        KEEN_PROFILE_COUNTER_DEC( m_vulkanRenderPipelineCount );
    }

    void VulkanGraphicsObjects::destroyPipelineLayout( VulkanPipelineLayout* pPipelineLayout )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( pPipelineLayout->pipelineLayout != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyPipelineLayout( m_device, pPipelineLayout->pipelineLayout, m_pSharedData->pVulkanAllocationCallbacks );
        }

        freeDeviceObject( pPipelineLayout );
        KEEN_PROFILE_COUNTER_DEC( m_vulkanPipelineLayoutCount );
    }

    void VulkanGraphicsObjects::traceGpuAllocations()
    {
        vulkan::traceGpuAllocations( m_pGpuAllocator );
    }

    void VulkanGraphicsObjects::destroyDeviceMemory( VulkanDeviceMemory* pDeviceMemory )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        vulkan::freeGpuDeviceMemory( m_pGpuAllocator, pDeviceMemory->allocation );
        freeDeviceObject( pDeviceMemory );

        KEEN_PROFILE_COUNTER_DEC( m_vulkanDeviceMemoryCount );
    }

    void VulkanGraphicsObjects::destroyBuffer( VulkanBuffer* pBuffer )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( pBuffer->allocation.pAllocation != nullptr )
        {
            vulkan::freeGpuBuffer( m_pGpuAllocator, pBuffer->buffer, pBuffer->allocation );
        }
        else if( pBuffer->buffer != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyBuffer( m_device, pBuffer->buffer, m_pSharedData->pVulkanAllocationCallbacks );
        }
        freeDeviceObject( pBuffer );

        KEEN_PROFILE_COUNTER_DEC( m_vulkanBufferCount );
    }

    void VulkanGraphicsObjects::destroyTexture( VulkanTexture* pTexture )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        if( pTexture->imageView != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyImageView( m_device, pTexture->imageView, m_pSharedData->pVulkanAllocationCallbacks );
        }

        if( pTexture->image != VK_NULL_HANDLE )
        {
            if( pTexture->allocation.pAllocation != nullptr )
            {
                vulkan::freeGpuImage( m_pGpuAllocator, pTexture->image, pTexture->allocation );
            }
            else
            {
                m_pVulkan->vkDestroyImage( m_device, pTexture->image, m_pSharedData->pVulkanAllocationCallbacks );
            }
        }
        freeDeviceObject( pTexture );

        KEEN_PROFILE_COUNTER_DEC( m_vulkanTextureCount );
    }

    void VulkanGraphicsObjects::destroySampler( VulkanSampler* pSampler )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_pVulkan->vkDestroySampler( m_device, pSampler->sampler, m_pSharedData->pVulkanAllocationCallbacks );
        freeDeviceObject( pSampler );

        KEEN_PROFILE_COUNTER_DEC( m_vulkanSamplerCount );
    }

    void VulkanGraphicsObjects::destroyDescriptorSetLayout( VulkanDescriptorSetLayout* pLayout )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_pVulkan->vkDestroyDescriptorSetLayout( m_device, pLayout->layout, m_pSharedData->pVulkanAllocationCallbacks );

        if( pLayout->staticSamplers.hasElements() )
        {
            for( uint32 i = 0u; i < pLayout->staticSamplers.getCount32(); ++i )
            {
                VulkanSampler* pSampler = pLayout->staticSamplers[ i ];

                KEEN_ASSERT( pSampler->referenceCount > 0u );
                pSampler->referenceCount--;

                if( pSampler->referenceCount == 0u )
                {
                    destroySampler( pSampler );
                }
            }
        }

        freeDeviceObject( pLayout );

        KEEN_PROFILE_COUNTER_DEC( m_vulkanDescriptorSetLayoutCount );
    }

    void VulkanGraphicsObjects::destroyDescriptorSet( StaticVulkanDescriptorSet* pDesciptorSet )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        KEEN_ASSERT( pDesciptorSet->pPool->type == VulkanDescriptorPoolType::Static );
        m_pVulkan->vkFreeDescriptorSets( m_device, pDesciptorSet->pPool->pool, 1u, &pDesciptorSet->set );
        freeDeviceObject( pDesciptorSet );
    }

    void VulkanGraphicsObjects::destroyQueryPool( VulkanQueryPool* pQueryPool )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_pVulkan->vkDestroyQueryPool( m_device, pQueryPool->queryPool, m_pSharedData->pVulkanAllocationCallbacks );
        freeDeviceObject( pQueryPool );
        KEEN_PROFILE_COUNTER_DEC( m_vulkanQueryPoolCount );
    }

    void VulkanGraphicsObjects::destroyComputePipeline( VulkanComputePipeline* pComputePipeline )
    {
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_pVulkan->vkDestroyPipeline( m_device, pComputePipeline->pipeline, m_pSharedData->pVulkanAllocationCallbacks );
        freeDeviceObject( pComputePipeline );

        KEEN_PROFILE_COUNTER_DEC( m_vulkanComputePipelineCount );
    }

    VkFormat VulkanGraphicsObjects::findFirstMatchingDepthStencilFormat( const ArrayView<const VkFormat>& candidates, bool usedAsShaderInput ) const
    {
        for( size_t i = 0u; i < candidates.getCount(); ++i )
        {
            const VkFormat candidateFormat = candidates[ i ];

            VkFormatProperties formatProperties;
            m_pVulkan->vkGetPhysicalDeviceFormatProperties( m_physicalDevice, candidateFormat, &formatProperties );

            if( ( formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) == 0u )
            {
                // not compatible.
                KEEN_TRACE_INFO( "[graphics] Skipping unsupported depth format %d!\n", vulkan::getVkFormatString( candidateFormat ) );
                continue;
            }

            // Format must support depth stencil attachment for optimal tiling
            if( usedAsShaderInput && ( ( formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) == 0u ) )
            {
                // not usable as shader input..
                KEEN_TRACE_INFO( "[graphics] Skipping depth format %d because it can't be bound to a shader!\n", vulkan::getVkFormatString( candidateFormat ) );
                continue;
            }

            VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if( usedAsShaderInput )
            {
                usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            }

            VkImageFormatProperties imageFormatProperties;
            VulkanResult result = m_pVulkan->vkGetPhysicalDeviceImageFormatProperties( m_physicalDevice, candidateFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0u, &imageFormatProperties );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkGetPhysicalDeviceFormatProperties failed with error '%s'\n", result );
                continue;
            }

            KEEN_TRACE_INFO( "[graphics] Vulkan depth format %d has max extent of %dx%dx%d\n", vulkan::getVkFormatString( candidateFormat ), imageFormatProperties.maxExtent.width, imageFormatProperties.maxExtent.height, imageFormatProperties.maxExtent.depth );
            if( imageFormatProperties.maxExtent.width == 0u || imageFormatProperties.maxExtent.height == 0u )
            {
                // not usable as shader input..
                KEEN_TRACE_INFO( "[graphics] Skipping depth format %d because max extend is 0,0,0!\n", vulkan::getVkFormatString( candidateFormat ) );
                continue;
            }

            KEEN_TRACE_INFO( "[graphics] Found Matching depth stencil format %d\n", vulkan::getVkFormatString( candidateFormat ) );

            return candidateFormat;
        }

        KEEN_TRACE_INFO( "[graphics] No matching depth stencil format found!\n" );

        return VK_FORMAT_UNDEFINED;
    }

    Result<void> VulkanGraphicsObjects::fillVkBufferCreateInfo( VkBufferCreateInfo* pBufferCreateInfo, const GraphicsBufferParameters& parameters )
    {
        pBufferCreateInfo->sType    = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        pBufferCreateInfo->size     = parameters.sizeInBytes;
        pBufferCreateInfo->usage    = vulkan::getBufferUsageFlags( parameters.usage );

        return ErrorId_Ok;
    }

    Result<void> VulkanGraphicsObjects::fillVkImageCreateInfo( VkImageCreateInfo* pImageCreateInfo, const GraphicsTextureParameters& parameters )
    {
        const VkFormat vulkanFormat = vulkan::getVulkanFormat( parameters.format );
        if( vulkanFormat == VK_FORMAT_UNDEFINED )
        {
            KEEN_TRACE_ERROR( "[graphics] Unsupported texture format %s\n", image::getPixelFormatName( parameters.format ) );
            return ErrorId_NotSupported;
        }

        pImageCreateInfo->sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        pImageCreateInfo->imageType     = vulkan::getImageType( parameters.type );
        pImageCreateInfo->format        = vulkanFormat;
        pImageCreateInfo->extent        = vulkan::createExtent3d( parameters.width, parameters.height, parameters.depth );
        pImageCreateInfo->mipLevels     = parameters.levelCount;
        pImageCreateInfo->arrayLayers   = ( image::isCubeTextureType( parameters.type ) ? 6u : 1u ) * parameters.layerCount;
        pImageCreateInfo->samples       = vulkan::getSampleCountFlagBits( parameters.sampleCount );
        pImageCreateInfo->tiling        = VK_IMAGE_TILING_OPTIMAL;
        pImageCreateInfo->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if( image::isCubeTextureType( parameters.type ) )
        {
            pImageCreateInfo->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        pImageCreateInfo->usage = vulkan::getImageUsageMask( parameters.usageMask );

        return ErrorId_Ok;
    }

}
