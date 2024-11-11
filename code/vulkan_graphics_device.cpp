#include "vulkan_graphics_device.hpp"

#include "keen/base/array.hpp"
#include "keen/base/hash_map.hpp"
#include "keen/base/inivariables.hpp"
#include "keen/base/issue_tracker.hpp"
#include "keen/base/memory_read_stream.hpp"
#include "keen/base/memory_tracker.hpp"
#include "keen/base/text_reader.hpp"
#include "keen/base/zone_allocator.hpp"
#include "keen/base/tls_allocator_scope.hpp"
#include "keen/base/scan_string.hpp"
#include "keen/memory/memory_system.hpp"
#include "keen/task/task_system.hpp"
#include "keen/os_gui/window_system.hpp"
#include "keen/os/os_crash.hpp"
#include "keen/graphics/graphics_system.hpp"

#include "global/graphics_device.hpp"

#if defined( KEEN_PLATFORM_WIN32 )
#   include "vulkan_graphics_device_win32.hpp"
#endif

#define KEEN_VULKAN_ROBUST_BUFFER_ACCESS        KEEN_OFF
#define KEEN_VULKAN_CLEAR_ALL_EMPTY_TEXTURES    KEEN_OFF

namespace keen
{

    namespace vulkan
    {
        KEEN_DEFINE_BOOL_VARIABLE( s_traceGpuAllocations,           "vulkan/TraceGpuAllocations", false, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_useAllocatorCallback,          "vulkan/UseAllocatorCallback", false, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_enableCompiledShaderInfo,      "vulkan/EnableCompiledShaderInfo", false, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_testWorstCaseOffsetAlignments, "vulkan/TestWorstCaseOffsetAlignments", KEEN_TRUE_IN_DEBUG, "" );
        KEEN_DEFINE_BOOL_VARIABLE( s_robustBufferAccess,            "vulkan/RobustBufferAccess", true, "" );

        static constexpr uint32 VendorId_Nvidia = 0x10DEu;
        static constexpr uint32 VendorId_Amd = 0x1002u;
        static constexpr uint32 VendorId_Intel = 0x8086u;

        static constexpr PixelFormat s_baseLineTextureFormats[] =
        {
            PixelFormat::R8_unorm,
            PixelFormat::R8_snorm,
            PixelFormat::R8_uint,
            PixelFormat::R8_sint,
            PixelFormat::R32_sfloat,
            PixelFormat::R32_uint,
            PixelFormat::R8G8B8A8_unorm,
            PixelFormat::R8G8B8A8_srgb,
            PixelFormat::R8G8B8A8_snorm,
            PixelFormat::R8G8B8A8_unorm,
            PixelFormat::R8G8B8A8_uint,
            PixelFormat::A8B8G8R8_unorm_pack32, 
            PixelFormat::A8B8G8R8_srgb_pack32,  
            PixelFormat::D16_unorm,
            PixelFormat::D24_unorm_S8_uint,
            PixelFormat::R16G16B16A16_sfloat,
            PixelFormat::R16G16_unorm,
            PixelFormat::R32G32B32A32_sfloat,
            PixelFormat::R8G8_unorm,
            PixelFormat::R8G8_uint,
            PixelFormat::R16_uint,
            PixelFormat::R16G16_sfloat,
        };
        
    }

    VulkanGraphicsDevice::VulkanGraphicsDevice()
    {
        m_pAllocator = nullptr;
    }

    VulkanGraphicsDevice::~VulkanGraphicsDevice()
    {
    }

    GraphicsSystemCreateError VulkanGraphicsDevice::create( MemoryAllocator* pAllocator, const GraphicsDeviceParameters& parameters )
    {
        KEEN_TRACE_INFO( "[graphics] Creating Vulkan Device...\n" );
        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        Result<VulkanApi*> apiResult = vulkan::createVulkanApi( pAllocator );
        if( apiResult.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create Vulkan API.\n" );
            return GraphicsSystemCreateError::Generic;
        }

        m_pVulkan       = apiResult.value;
        m_pAllocator    = pAllocator;

        CreateInstanceFlags createInstanceFlags;
        createInstanceFlags.setIf( CreateInstanceFlag::UseMemoryCallbacks, parameters.useMemoryCallbacks );
#ifndef KEEN_BUILD_MASTER
        createInstanceFlags.setIf( CreateInstanceFlag::EnableValidation, parameters.enableDebugChecks );
        createInstanceFlags.setIf( CreateInstanceFlag::EnableSynchronizationValidation, parameters.enableSynchronizationValidation );
        createInstanceFlags.setIf( CreateInstanceFlag::EnableGpuAssistedValidation, parameters.enableGpuAssistedValidation );
#endif
        ErrorId error = createInstance( parameters.applicationName, parameters.applicationVersion, createInstanceFlags );
        if( error != ErrorId_Ok )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create vulkan instance! error=%s\n", getErrorString( error ) );
            destroy();
            return error == ErrorId_NotSupported ? GraphicsSystemCreateError::DriverTooOld : GraphicsSystemCreateError::Generic;
        }

        error = createDevice( parameters.pWindowSystem, parameters.forcePhysicalDeviceIndex );
        if( error != ErrorId_Ok )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create vulkan device! error=%s\n", getErrorString( error ) );
            destroy();
            return error == ErrorId_NotFound ? GraphicsSystemCreateError::NoCompatibleDeviceFound : GraphicsSystemCreateError::Generic;
        }

        const uint32 frameCount = 2u;

        VulkanGraphicsObjectsParameters objectsParameters;
        objectsParameters.pEventNotifier                = parameters.pEventNotifier;
        objectsParameters.pAllocator                    = m_pAllocator;
        objectsParameters.pVulkan                       = m_pVulkan;
        objectsParameters.instance                      = m_instance;
        objectsParameters.physicalDevice                = m_physicalDevice;
        objectsParameters.device                        = m_device;
        objectsParameters.pSharedData                   = &m_sharedData;
        objectsParameters.pTaskSystem                   = parameters.pTaskSystem;
        objectsParameters.pWindowSystem                 = parameters.pWindowSystem;
        objectsParameters.allocationBlockSizeInBytes    = parameters.allocationBlockSizeInBytes;
        objectsParameters.frameCount                    = frameCount;
        objectsParameters.staticDescriptorPoolSizes     = vulkan::fillDefaultVulkanDescriptorPoolSizes( parameters.staticDescriptorSetPoolSize );
        objectsParameters.dynamicDescriptorPoolSizes    = vulkan::fillDefaultVulkanDescriptorPoolSizes( parameters.dynamicDescriptorSetPoolSize );
        objectsParameters.pipelineCacheDirectory        = parameters.pipelineCacheDirectory;
        objectsParameters.enableBindlessDescriptors     = parameters.enableBindlessDescriptors;
        objectsParameters.bindlessTextureCount          = parameters.bindlessTextureCount;
        objectsParameters.bindlessSamplerCount          = parameters.bindlessSamplerCount;      

        error = m_objects.create( objectsParameters );
        if( error != ErrorId_Ok )
        {
            KEEN_TRACE_ERROR( "[graphics] Could not create vulkan graphics objects! error=%s\n", getErrorString( error ) );
            destroy();
            return GraphicsSystemCreateError::Generic;
        }

        VulkanRenderContextParameters renderContextParameters;
        renderContextParameters.pAllocator                  = m_pAllocator;
        renderContextParameters.pVulkan                     = m_pVulkan;
        renderContextParameters.device                      = m_device;
        renderContextParameters.physicalDevice              = m_physicalDevice;
        renderContextParameters.pObjects                    = &m_objects;
        renderContextParameters.pSharedData                 = &m_sharedData;
        renderContextParameters.frameCount                  = frameCount;
        renderContextParameters.isNonInteractiveApplication = parameters.isNonInteractiveApplication;
        renderContextParameters.enableBreadcrumbs           = parameters.enableBreadcrumbs;
        renderContextParameters.enableBindlessDescriptors   = parameters.enableBindlessDescriptors;
        renderContextParameters.bindlessTextureCount        = parameters.bindlessTextureCount;
        renderContextParameters.bindlessSamplerCount        = parameters.bindlessSamplerCount;

        if( !m_renderContext.tryCreate( renderContextParameters ) )
        {
            destroy();
            return GraphicsSystemCreateError::Generic;
        }

        m_sharedData.info.api = GraphicsApi::Vulkan;

        if( m_sharedData.deviceProperties.vendorID == vulkan::VendorId_Nvidia )
        {
            m_sharedData.info.gpuVendor = GraphicsGpuVendor::Nvidia;
        }
        else if( m_sharedData.deviceProperties.vendorID == vulkan::VendorId_Amd )
        {
            m_sharedData.info.gpuVendor = GraphicsGpuVendor::Amd;
        }
        else if( m_sharedData.deviceProperties.vendorID == vulkan::VendorId_Intel )
        {
            m_sharedData.info.gpuVendor = GraphicsGpuVendor::Intel;
        }
        else
        {
            m_sharedData.info.gpuVendor = GraphicsGpuVendor::Unknown;
        }

        m_sharedData.info.enableTextureFormats( vulkan::s_baseLineTextureFormats );

        for( size_t i = 0u; i < KEEN_COUNTOF( vulkan::s_baseLineTextureFormats ); ++i )
        {
            VkFormat vkFormat = vulkan::getVulkanFormat( vulkan::s_baseLineTextureFormats[ i ] );

            VkFormatProperties formatProperties;
            m_pVulkan->vkGetPhysicalDeviceFormatProperties( m_physicalDevice, vkFormat, &formatProperties );

            if( ( formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ) == 0u )
            {
                // not compatible.
                continue;
            }

            m_sharedData.info.enableRenderTargetFormat( vulkan::s_baseLineTextureFormats[ i ] );
        }

        // get list of render target formats:
        static constexpr PixelFormat s_renderTargetFormats[] =
        {
            PixelFormat::R8G8B8A8_unorm,
            PixelFormat::R8G8B8A8_srgb,
            PixelFormat::A8B8G8R8_unorm_pack32,
            PixelFormat::A8B8G8R8_srgb_pack32,
            PixelFormat::R16G16B16A16_sfloat,
            PixelFormat::R8G8B8_unorm,
            PixelFormat::R8G8B8_srgb,
            PixelFormat::B8G8R8_unorm,
            PixelFormat::B8G8R8_srgb,
            PixelFormat::R8G8B8A8_unorm,
            PixelFormat::B8G8R8A8_unorm,
            PixelFormat::B8G8R8A8_srgb,
            PixelFormat::A8B8G8R8_unorm_pack32,
            PixelFormat::A8B8G8R8_srgb_pack32,
            PixelFormat::A2R10G10B10_unorm_pack32,
            PixelFormat::A2B10G10R10_unorm_pack32,
            PixelFormat::B10G11R11_ufloat_pack32
        };

        PixelFormat defaultFormat32bpp = PixelFormat::None;
        PixelFormat defaultFormat32bpp_sRGB = PixelFormat::None;
        for( size_t i = 0u; i < KEEN_COUNTOF( s_renderTargetFormats ); ++i )
        {
            const PixelFormat format = s_renderTargetFormats[ i ];

            VkFormat vkFormat = vulkan::getVulkanFormat( format );
            KEEN_ASSERT( vkFormat != VK_FORMAT_UNDEFINED );

            VkFormatProperties formatProperties;
            m_pVulkan->vkGetPhysicalDeviceFormatProperties( m_physicalDevice, vkFormat, &formatProperties );

            if( ( formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ) &&
                ( formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT  ) )
            {
                m_sharedData.info.enableRenderTargetFormat( format );

                if( image::isRGBA32Format( format ) )
                {
                    if( image::isGammaPixelFormat( format ) )
                    {
                        if( defaultFormat32bpp_sRGB == PixelFormat::None )
                        {
                            defaultFormat32bpp_sRGB = format;
                        }
                    }
                    else
                    {
                        if( defaultFormat32bpp == PixelFormat::None )
                        {
                            defaultFormat32bpp = format;
                        }
                    }
                }
            }
        }

        if( defaultFormat32bpp != PixelFormat::None )
        {
            KEEN_TRACE_INFO( "[vulkan] Selected format %k as default 32bpp unorm format!\n", defaultFormat32bpp );
            m_sharedData.info.defaultFormat32bpp = defaultFormat32bpp;
        }
        else
        {
            KEEN_TRACE_ERROR( "[vulkan] Could not find default 32bpp unorm format!\n" );
        }
        if( defaultFormat32bpp_sRGB != PixelFormat::None )
        {
            KEEN_TRACE_INFO( "[vulkan] Selected format %k as default 32bpp sRGB format!\n", defaultFormat32bpp_sRGB );
            m_sharedData.info.defaultFormat32bpp_sRGB = defaultFormat32bpp_sRGB;
        }
        else
        {
            KEEN_TRACE_ERROR( "[vulkan] Could not find default 32bpp srgb format!\n" );
        }

        if( m_sharedData.deviceFeatures.textureCompressionBC )
        {
            KEEN_TRACE_INFO( "[graphics] Device supports BC texture compression!\n" );
            // list from https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceFeatures.html
            static constexpr PixelFormat s_bcTextureFormats[] =
            {
                PixelFormat::BC1_RGB_unorm_block,
                PixelFormat::BC1_RGB_srgb_block,
                PixelFormat::BC1_RGBA_unorm_block,
                PixelFormat::BC1_RGBA_srgb_block,
                PixelFormat::BC2_unorm_block,
                PixelFormat::BC2_srgb_block,
                PixelFormat::BC3_unorm_block,
                PixelFormat::BC3_srgb_block,
                PixelFormat::BC4_unorm_block,
                PixelFormat::BC4_snorm_block,
                PixelFormat::BC5_unorm_block,
                PixelFormat::BC5_snorm_block,
                PixelFormat::BC6H_ufloat_block,
                PixelFormat::BC6H_sfloat_block,
                PixelFormat::BC7_unorm_block,
                PixelFormat::BC7_srgb_block,
            };
            m_sharedData.info.enableTextureFormats( s_bcTextureFormats );
        }
        /*
        if( m_sharedData.deviceFeatures.textureCompressionETC2 )
        {
            KEEN_TRACE_INFO( "[graphics] Device supports ETC2 texture compression!\n" );
            static constexpr PixelFormat s_etc2TextureFormats[] = { PixelFormat::ETC1_R8G8B8, PixelFormat::ETC1_R8G8B8_sRGB, PixelFormat::ETC2_R8G8B8, PixelFormat::ETC2_R8G8B8_sRGB, PixelFormat::ETC2_R8G8B8A1, PixelFormat::ETC2_R8G8B8A1_sRGB, PixelFormat::ETC2_R8G8B8A8, PixelFormat::ETC2_R8G8B8A8_sRGB };
            m_sharedData.info.enableTextureFormats( s_etc2TextureFormats );
        }
        */

        m_sharedData.info.supportedFeatures.set( GraphicsFeature::Mrt );
        m_sharedData.info.supportedFeatures.set( GraphicsFeature::ShadowSampler );
        m_sharedData.info.supportedFeatures.set( GraphicsFeature::VolumeTexture );
        m_sharedData.info.supportedFeatures.setIf( GraphicsFeature::TessellationShader, m_sharedData.deviceFeatures.tessellationShader == VK_TRUE );

        m_sharedData.info.maxAnisotropyLevel = m_sharedData.deviceProperties.limits.maxSamplerAnisotropy;

        m_sharedData.info.supportsMultithreadedPipelineCreation = true;

        return GraphicsSystemCreateError::Ok;
    }

    void VulkanGraphicsDevice::destroy()
    {
        // wait for the device to turn idle before anything else:
        if( m_device != VK_NULL_HANDLE )
        {
            m_renderContext.waitForAllFramesFinished();
        }

        KEEN_DISABLE_FLOATINGPOINT_EXCEPTIONS_SCOPE;

        m_renderContext.destroy();
        m_objects.destroy();
        destroyDevice();

        destroyInstance();
        vulkan::destroyVulkanApi( m_pAllocator, m_pVulkan );
    }

    const GraphicsDeviceInfo& VulkanGraphicsDevice::updateInfo()
    {
#if defined( VK_EXT_memory_budget )
        if( m_pVulkan->EXT_memory_budget )
        {
            VkPhysicalDeviceMemoryProperties2 deviceMemoryProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
            KEEN_ASSERT( m_memoryBudgets.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT );
            KEEN_ASSERT( m_memoryBudgets.pNext == nullptr );
            deviceMemoryProperties.pNext = &m_memoryBudgets;
            m_pVulkan->vkGetPhysicalDeviceMemoryProperties2( m_physicalDevice, &deviceMemoryProperties );

            for( size_t i = 0u; i < m_sharedData.deviceMemoryProperties.memoryTypeCount; ++i )
            {
                const uint32 heapIndex = m_sharedData.deviceMemoryProperties.memoryTypes[ i ].heapIndex;

                GraphicsDeviceMemoryTypeBudgetInfo* pBudgetInfo = m_sharedData.memoryTypeInfos[ i ].budget.set();
                pBudgetInfo->usedSizeInBytes    = m_memoryBudgets.heapUsage[ heapIndex ];
                pBudgetInfo->budgetSizeInBytes  = m_memoryBudgets.heapBudget[ heapIndex ];
            }
        }
#endif

        return m_sharedData.info;
    }

    GraphicsDeviceMemory* VulkanGraphicsDevice::createDeviceMemory( const GraphicsDeviceMemoryParameters& parameters )
    {
        return m_objects.createDeviceMemory( parameters );
    }

    GraphicsSwapChain* VulkanGraphicsDevice::createSwapChain( const GraphicsSwapChainParameters& parameters )
    {
        KEEN_ASSERT( parameters.colorFormat != PixelFormat::None );

        if( !m_sharedData.info.supportedRenderTargetFormats.isSet( (size_t)parameters.colorFormat ) )
        {
            KEEN_TRACE_ERROR( "PixelFormat %k is not supported as render target / swap chain format!\n", parameters.colorFormat );
            return nullptr;
        }
        return m_objects.createSwapChain( parameters );
    }

    GraphicsPipelineLayout* VulkanGraphicsDevice::createPipelineLayout( const GraphicsPipelineLayoutParameters& parameters )
    {
        return m_objects.createPipelineLayout( parameters );
    }

    GraphicsRenderPipeline* VulkanGraphicsDevice::createRenderPipeline( const GraphicsRenderPipelineParameters& parameters )
    {
        return m_objects.createRenderPipeline( parameters );
    }

    GraphicsBuffer* VulkanGraphicsDevice::createBuffer( const GraphicsBufferParameters& parameters )
    {
        return m_objects.createBuffer( parameters );
    }

    void VulkanGraphicsDevice::flushCpuCache( const ArrayView<const GraphicsBufferRange>& bufferRanges )
    {
        constexpr size_t BatchSize = 16u;
        DynamicArray<VulkanGpuAllocation*, BatchSize>   allocations;
        DynamicArray<uint64, BatchSize>                 offsets;
        DynamicArray<uint64, BatchSize>                 sizes;
        DynamicArray<VkMappedMemoryRange, BatchSize>    mappedMemoryRanges;

        for( size_t i = 0u; i < bufferRanges.getCount(); ++i )
        {
            const GraphicsBufferRange& bufferRange = bufferRanges[ i ];
            const VulkanBuffer* pVulkanBuffer = (VulkanBuffer*)bufferRange.pBuffer;

            if( pVulkanBuffer->allocation.pAllocation != nullptr )
            {
                if( allocations.getRemainingCapacity() == 0u )
                {
                    m_objects.flushCpuMemoryCache( allocations, offsets, sizes );
                    allocations.clear();
                    offsets.clear();
                    sizes.clear();
                }

                allocations.pushBack( pVulkanBuffer->allocation.pAllocation );
                offsets.pushBack( bufferRange.offset );
                sizes.pushBack( bufferRange.size );
            }

            if( pVulkanBuffer->pBoundDeviceMemory != nullptr && !pVulkanBuffer->pBoundDeviceMemory->isCoherent )
            {
                if( mappedMemoryRanges.getRemainingCapacity() == 0u )
                {
                    m_pVulkan->vkFlushMappedMemoryRanges( m_pVulkan->device, rangecheck_cast<uint32>( mappedMemoryRanges.getCount() ), mappedMemoryRanges.getStart() );
                    mappedMemoryRanges.clear();
                }

                VkMappedMemoryRange memoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
                memoryRange.memory  = pVulkanBuffer->pBoundDeviceMemory->allocation.memory;
                memoryRange.offset  = pVulkanBuffer->boundMemoryOffset;
                memoryRange.size    = pVulkanBuffer->sizeInBytes;
                mappedMemoryRanges.pushBack( memoryRange );
            }
        }

        if( allocations.hasElements() )
        {
            m_objects.flushCpuMemoryCache( allocations, offsets, sizes );
        }
        if( mappedMemoryRanges.hasElements() )
        {
            m_pVulkan->vkFlushMappedMemoryRanges( m_pVulkan->device, rangecheck_cast<uint32>( mappedMemoryRanges.getCount() ), mappedMemoryRanges.getStart() );
        }
    }

    void VulkanGraphicsDevice::invalidateCpuCache( const ArrayView<const GraphicsBufferRange>& bufferRanges )
    {
        constexpr size_t BatchSize = 16u;
        DynamicArray<VulkanGpuAllocation*, BatchSize>   allocations;
        DynamicArray<uint64, BatchSize>                 offsets;
        DynamicArray<uint64, BatchSize>                 sizes;
        DynamicArray<VkMappedMemoryRange, BatchSize>    mappedMemoryRanges;

        for( size_t i = 0u; i < bufferRanges.getCount(); ++i )
        {
            const GraphicsBufferRange& bufferRange = bufferRanges[ i ];
            const VulkanBuffer* pVulkanBuffer = (VulkanBuffer*)bufferRange.pBuffer;

            if( pVulkanBuffer->allocation.pAllocation != nullptr )
            {
                if( allocations.getRemainingCapacity() == 0u )
                {
                    m_objects.invalidateCpuMemoryCache( allocations, offsets, sizes );
                    allocations.clear();
                    offsets.clear();
                    sizes.clear();
                }

                allocations.pushBack( pVulkanBuffer->allocation.pAllocation );
                offsets.pushBack( bufferRange.offset );
                sizes.pushBack( bufferRange.size );
            }

            if( pVulkanBuffer->pBoundDeviceMemory != nullptr && !pVulkanBuffer->pBoundDeviceMemory->isCoherent )
            {
                if( mappedMemoryRanges.getRemainingCapacity() == 0u )
                {
                    m_pVulkan->vkInvalidateMappedMemoryRanges( m_pVulkan->device, rangecheck_cast<uint32>( mappedMemoryRanges.getCount() ), mappedMemoryRanges.getStart() );
                    mappedMemoryRanges.clear();
                }

                VkMappedMemoryRange memoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
                memoryRange.memory  = pVulkanBuffer->pBoundDeviceMemory->allocation.memory;
                memoryRange.offset  = pVulkanBuffer->boundMemoryOffset;
                memoryRange.size    = pVulkanBuffer->sizeInBytes;
                mappedMemoryRanges.pushBack( memoryRange );
            }
        }

        if( allocations.hasElements() )
        {
            m_objects.invalidateCpuMemoryCache( allocations, offsets, sizes );
        }
        if( mappedMemoryRanges.hasElements() )
        {
            m_pVulkan->vkInvalidateMappedMemoryRanges( m_pVulkan->device, rangecheck_cast<uint32>( mappedMemoryRanges.getCount() ), mappedMemoryRanges.getStart() );
        }
    }

    void VulkanGraphicsDevice::resetQueryPool( GraphicsQueryPool* pQueryPool, uint32 firstQuery, uint32 queryCount )
    {
        VulkanQueryPool* pVulkanQueryPool = (VulkanQueryPool*)pQueryPool;
        m_pVulkan->vkResetQueryPool( m_device, pVulkanQueryPool->queryPool, firstQuery, queryCount );
    }

    Result<void> VulkanGraphicsDevice::copyQueryPoolTimeValues( ArrayView<Time> target, GraphicsQueryPool* pQueryPool, uint32 firstQuery )
    {
        const uint32 queryCount = (uint32)target.getCount();

        VulkanQueryPool* pVulkanQueryPool = (VulkanQueryPool*)pQueryPool;

        StaticArray<uint64,1024u> queryResults;

        VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
        if( !m_sharedData.isVkGetQueryPoolResultsBrokenOnAmdDeviceLost )
        {
            flags |= VK_QUERY_RESULT_WAIT_BIT;
        }

        uint32 queryOffset = 0u;
        uint32 remainingQueryCount = queryCount;
        while( remainingQueryCount > 0u )
        {
            KEEN_PROFILE_CPU( vkGetQueryPoolResults );

            const uint32 batchQueryCount = min<uint32>( (uint32)queryResults.getCount(), remainingQueryCount );

            VulkanResult result = m_pVulkan->vkGetQueryPoolResults( m_device, pVulkanQueryPool->queryPool, firstQuery + queryOffset, batchQueryCount, batchQueryCount * sizeof( uint64 ), queryResults.getStart(), sizeof( uint64 ), flags );
            if( m_renderContext.handleDeviceLost( result ) )
            {
                return result.getErrorId();
            }

            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkGetQueryPoolResults failed with error '%s'\n", result );
                return result.getErrorId();
            }

            for( uint32 i = 0u; i < batchQueryCount; ++i )
            {
                const uint64 queryValue = queryResults[ i ];

                const Time time = Time::fromNanoseconds( (sint64)( (double)m_sharedData.info.timestampPeriod * queryValue ) );

                target[ queryOffset + i ] = time;
            }

            remainingQueryCount -= batchQueryCount;
            queryOffset += batchQueryCount;
        }

        return ErrorId_Ok;
    }

    GraphicsTexture* VulkanGraphicsDevice::createTexture( const GraphicsTextureParameters& parameters )
    {
        return m_objects.createTexture( parameters );
    }

    GraphicsTexture* VulkanGraphicsDevice::createTextureView( const GraphicsTextureViewParameters& parameters )
    {
        return m_objects.createTextureView( parameters );
    }

    GraphicsSampler* VulkanGraphicsDevice::createSampler( const GraphicsSamplerParameters& parameters, DebugName debugName )
    {
        return m_objects.createSampler( parameters, debugName );
    }

    GraphicsDescriptorSetLayout* VulkanGraphicsDevice::createDescriptorSetLayout( const GraphicsDescriptorSetLayoutParameters& parameters )
    {
        return m_objects.createDescriptorSetLayout( parameters );
    }

    GraphicsDescriptorSet* VulkanGraphicsDevice::createStaticDescriptorSet( const GraphicsDescriptorSetParameters& parameters )
    {
        return m_objects.createStaticDescriptorSet( parameters );
    }

    GraphicsDescriptorSet* VulkanGraphicsDevice::createDynamicDescriptorSet( GraphicsFrame* pFrame, const GraphicsDescriptorSetParameters& parameters )
    {
        return m_objects.createDynamicDescriptorSet( (VulkanFrame*)pFrame, parameters );
    }

    GraphicsQueryPool* VulkanGraphicsDevice::createQueryPool( const GraphicsQueryPoolParameters& parameters )
    {
        return m_objects.createQueryPool( parameters );
    }

    GraphicsMemoryRequirements VulkanGraphicsDevice::queryTextureMemoryRequirements( const GraphicsTexture* pTexture )
    {
        return m_objects.queryTextureMemoryRequirements( (const VulkanTexture*)pTexture );
    }

    GraphicsMemoryRequirements VulkanGraphicsDevice::queryBufferMemoryRequirements( const GraphicsBuffer* pBuffer )
    {
        return m_objects.queryBufferMemoryRequirements( (const VulkanBuffer*)pBuffer );
    }

    void VulkanGraphicsDevice::bindMemory( const ArrayView<const GraphicsBufferMemoryBinding>& buffers, const ArrayView<const GraphicsTextureMemoryBinding>& textures )
    {
        m_objects.bindMemory( buffers, textures );
    }

    void VulkanGraphicsDevice::resizeSwapChain( GraphicsSwapChain* pSwapChain, uint2 size )
    {
        VulkanSwapChainWrapper* pSwapChainWrapper = (VulkanSwapChainWrapper*)pSwapChain;
        VulkanSwapChain* pVulkanSwapChain = pSwapChainWrapper->pSwapChain;
        pVulkanSwapChain->resize( size );

        pSwapChainWrapper->info.size = pSwapChainWrapper->pSwapChain->getSize();
    }

    void VulkanGraphicsDevice::setSwapChainPresentationInterval( GraphicsSwapChain* pSwapChain, uint32 presentationInterval )
    {
        VulkanSwapChainWrapper* pSwapChainWrapper = (VulkanSwapChainWrapper*)pSwapChain;
        VulkanSwapChain* pVulkanSwapChain = pSwapChainWrapper->pSwapChain;
        pVulkanSwapChain->setPresentationInterval( presentationInterval );

        pSwapChainWrapper->info.presentationInterval = presentationInterval;
    }

    GraphicsComputePipeline* VulkanGraphicsDevice::createComputePipeline( const GraphicsComputePipelineParameters& parameters )
    {
        return m_objects.createComputePipeline( parameters );
    }

#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )
    static void getCompiledPipelineStageInfoAMD( GraphicsCompiledPipelineStageInfo* pPipelineStageInfo, VulkanApi* pVulkan, VkPipeline pipeline, VkShaderStageFlagBits shaderStage )
    {
        if( !pVulkan->AMD_shader_info )
        {
            return;
        }

        VkShaderStatisticsInfoAMD statistics;
        size_t dataSize = sizeof( statistics );
        if( pVulkan->vkGetShaderInfoAMD( pVulkan->device, pipeline, shaderStage, VK_SHADER_INFO_TYPE_STATISTICS_AMD, &dataSize, &statistics ) != VK_SUCCESS )
        {
            return;
        }

        GraphicsCompiledPipelineStageInfoAMD* pAmd = pPipelineStageInfo->amd.set();

        pAmd->vectorRegisters           = statistics.resourceUsage.numUsedVgprs;
        pAmd->scalarRegisters           = statistics.resourceUsage.numUsedSgprs;
        pAmd->physicalVectorRegisters   = statistics.numPhysicalVgprs;
        pAmd->physicalScalarRegisters   = statistics.numPhysicalSgprs;
        pAmd->availableVectorRegisters  = statistics.numAvailableVgprs;
        pAmd->availableScalarRegisters  = statistics.numAvailableSgprs;

        pAmd->ldsUsageInBytes           = statistics.resourceUsage.ldsUsageSizeInBytes;
        pAmd->scratchMemoryUsageInBytes = statistics.resourceUsage.scratchMemUsageInBytes;

        pAmd->computeWorkGroupSize      = u3_load( statistics.computeWorkGroupSize[ 0 ], statistics.computeWorkGroupSize[ 1 ], statistics.computeWorkGroupSize[ 2 ] );
    }

    static bool getCompiledPipelineStageInfoNVIDIA( GraphicsCompiledPipelineStageInfoNVIDIA* pPipelineStageInfo, const ArrayView<const VkPipelineExecutableStatisticKHR> statistics )
    {
        zeroValue( pPipelineStageInfo );

        uint32 foundNvidiaStatisticsMask = 0u;

        for( size_t statisticIndex = 0u; statisticIndex < statistics.getSize(); ++statisticIndex )
        {
            const VkPipelineExecutableStatisticKHR& statistic = statistics[ statisticIndex ];

            const StringView statisticName = createStringView( statistic.name );

            if( statisticName == "Register Count"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->registerCount = statistic.value.u64;
                foundNvidiaStatisticsMask |= 1u << 0u;
            }
            if( statisticName == "Binary Size"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->binarySize = statistic.value.u64;
                foundNvidiaStatisticsMask |= 1u << 1u;
            }
            if( statisticName == "Stack Size"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->stackSize = statistic.value.u64;
                foundNvidiaStatisticsMask |= 1u << 2u;
            }
            if( statisticName == "Local Memory Size"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->localMemorySize = statistic.value.u64;
                foundNvidiaStatisticsMask |= 1 << 3u;
            }
            if( statisticName == "Input Count"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->inputCount = statistic.value.u64;
            }
            if( statisticName == "Output Count"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->outputCount = statistic.value.u64;
            }
            if( statisticName == "Shared Memory Size"_s && statistic.format == VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR )
            {
                pPipelineStageInfo->sharedMemorySize = statistic.value.u64;
            }
        }

        return foundNvidiaStatisticsMask == 0xfu;
    }

    static void getCompiledRenderPipelineInfoKHR( GraphicsCompiledRenderPipelineInfo* pPipelineInfo, VulkanApi* pVulkan, VkPipeline pipeline )
    {
        if( !pVulkan->KHR_pipeline_executable_properties )
        {
            return;
        }

        TlsStackAllocatorScope stackAllocator;

        VkPipelineInfoKHR pipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR };
        pipelineInfo.pipeline = pipeline;

        uint32 executableCount = 0u;
        Array<VkPipelineExecutablePropertiesKHR, 8u> executableProperties;
        if( pVulkan->vkGetPipelineExecutablePropertiesKHR( pVulkan->device, &pipelineInfo, &executableCount, NULL ) != VK_SUCCESS ||
            !executableProperties.tryCreateWithValue( &stackAllocator, executableCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR } ) ||
            pVulkan->vkGetPipelineExecutablePropertiesKHR( pVulkan->device, &pipelineInfo, &executableCount, executableProperties.getStart() ) != VK_SUCCESS )
        {
            return;
        }

        // there might be multiple executables that map to a single abstract pipeline stage, we don't support that for now
        VkShaderStageFlags seenStages = 0u;
        for( uint32 executableIndex = 0u; executableIndex < executableCount; ++executableIndex )
        {
            const uint32 executableGraphicsStages = executableProperties[ executableIndex ].stages & VK_SHADER_STAGE_ALL_GRAPHICS;

            if( isAnyBitSet( seenStages, executableGraphicsStages ) )
            {
                return;
            }

            seenStages |= executableGraphicsStages;
        }

        for( uint32 executableIndex = 0u; executableIndex < executableCount; ++executableIndex )
        {
            VkPipelineExecutableInfoKHR executableInfo = { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR };
            executableInfo.pipeline         = pipeline;
            executableInfo.executableIndex  = executableIndex;

            uint32 statisticsCount = 0u;
            Array<VkPipelineExecutableStatisticKHR, 16u> statistics;
            if( pVulkan->vkGetPipelineExecutableStatisticsKHR( pVulkan->device, &executableInfo, &statisticsCount, NULL ) != VK_SUCCESS ||
                !statistics.tryCreateWithValue( &stackAllocator, statisticsCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR } ) ||
                pVulkan->vkGetPipelineExecutableStatisticsKHR( pVulkan->device, &executableInfo, &statisticsCount, statistics.getStart() ) != VK_SUCCESS )
            {
                continue;
            }

            GraphicsCompiledPipelineStageInfoNVIDIA nvidia;
            if( getCompiledPipelineStageInfoNVIDIA( &nvidia, statistics ) )
            {
                if( isBitmaskSet( executableProperties[ executableIndex ].stages, VK_SHADER_STAGE_VERTEX_BIT ) )
                {
                    pPipelineInfo->vertex.nvidia.set( nvidia );
                }
                if( isBitmaskSet( executableProperties[ executableIndex ].stages, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ) )
                {
                    pPipelineInfo->tessellationControl.nvidia.set( nvidia );
                }
                if( isBitmaskSet( executableProperties[ executableIndex ].stages, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT ) )
                {
                    pPipelineInfo->tessellationEvaluation.nvidia.set( nvidia );
                }
                if( isBitmaskSet( executableProperties[ executableIndex ].stages, VK_SHADER_STAGE_FRAGMENT_BIT ) )
                {
                    pPipelineInfo->fragment.nvidia.set( nvidia );
                }
            }
        }
    }

    static void getCompiledComputePipelineInfoKHR( GraphicsCompiledComputePipelineInfo* pPipelineInfo, VulkanApi* pVulkan, VkPipeline pipeline )
    {
        if( !pVulkan->KHR_pipeline_executable_properties )
        {
            return;
        }

        TlsStackAllocatorScope stackAllocator;

        VkPipelineInfoKHR pipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR };
        pipelineInfo.pipeline = pipeline;

        uint32 executableCount = 0u;
        Array<VkPipelineExecutablePropertiesKHR, 8u> executableProperties;
        if( pVulkan->vkGetPipelineExecutablePropertiesKHR( pVulkan->device, &pipelineInfo, &executableCount, NULL ) != VK_SUCCESS ||
            !executableProperties.tryCreateWithValue( &stackAllocator, executableCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR } ) ||
            pVulkan->vkGetPipelineExecutablePropertiesKHR( pVulkan->device, &pipelineInfo, &executableCount, executableProperties.getStart() ) != VK_SUCCESS )
        {
            return;
        }

        // there might be multiple executables that map to a single abstract pipeline stage, we don't support that for now
        if( executableProperties.getSize() != 1u || !isBitmaskSet( executableProperties[ 0u ].stages, VK_SHADER_STAGE_COMPUTE_BIT ) )
        {
            return;
        }

        for( uint32 executableIndex = 0u; executableIndex < executableCount; ++executableIndex )
        {
            VkPipelineExecutableInfoKHR executableInfo = { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR };
            executableInfo.pipeline         = pipeline;
            executableInfo.executableIndex  = executableIndex;

            uint32 statisticsCount = 0u;
            Array<VkPipelineExecutableStatisticKHR, 16u> statistics;
            if( pVulkan->vkGetPipelineExecutableStatisticsKHR( pVulkan->device, &executableInfo, &statisticsCount, NULL ) != VK_SUCCESS ||
                !statistics.tryCreateWithValue( &stackAllocator, statisticsCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR } ) ||
                pVulkan->vkGetPipelineExecutableStatisticsKHR( pVulkan->device, &executableInfo, &statisticsCount, statistics.getStart() ) != VK_SUCCESS )
            {
                continue;
            }

            GraphicsCompiledPipelineStageInfoNVIDIA nvidia;
            if( getCompiledPipelineStageInfoNVIDIA( &nvidia, statistics ) )
            {
                pPipelineInfo->nvidia.set( nvidia );
            }
        }
    }

    void VulkanGraphicsDevice::getCompiledRenderPipelineInfo( GraphicsCompiledRenderPipelineInfo* pCompiledPipelineInfo, const GraphicsRenderPipeline* pRenderPipeline )
    {
        KEEN_ASSERT( pRenderPipeline != nullptr );
        const VulkanRenderPipeline* pVulkanRenderPipeline = static_cast<const VulkanRenderPipeline*>( pRenderPipeline );

        getCompiledPipelineStageInfoAMD( &pCompiledPipelineInfo->vertex, m_pVulkan, pVulkanRenderPipeline->pipeline, VK_SHADER_STAGE_VERTEX_BIT );
        getCompiledPipelineStageInfoAMD( &pCompiledPipelineInfo->tessellationControl, m_pVulkan, pVulkanRenderPipeline->pipeline, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT );
        getCompiledPipelineStageInfoAMD( &pCompiledPipelineInfo->tessellationEvaluation, m_pVulkan, pVulkanRenderPipeline->pipeline, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT );
        getCompiledPipelineStageInfoAMD( &pCompiledPipelineInfo->fragment, m_pVulkan, pVulkanRenderPipeline->pipeline, VK_SHADER_STAGE_FRAGMENT_BIT );

        getCompiledRenderPipelineInfoKHR( pCompiledPipelineInfo, m_pVulkan, pVulkanRenderPipeline->pipeline );
    }

    void VulkanGraphicsDevice::getCompiledComputePipelineInfo( GraphicsCompiledComputePipelineInfo* pCompiledPipelineInfo, const GraphicsComputePipeline* pComputePipeline )
    {
        const VulkanComputePipeline* pVulkanComputePipeline = static_cast<const VulkanComputePipeline*>( pComputePipeline );

        getCompiledPipelineStageInfoAMD( pCompiledPipelineInfo, m_pVulkan, pVulkanComputePipeline->pipeline, VK_SHADER_STAGE_COMPUTE_BIT );

        getCompiledComputePipelineInfoKHR( pCompiledPipelineInfo, m_pVulkan, pVulkanComputePipeline->pipeline );
    }
#endif

    GraphicsFrame* VulkanGraphicsDevice::beginFrame( ArrayView<GraphicsSwapChain*> swapChains )
    {
        MutexLock lock( &m_vulkanRenderContextMutex );

#if KEEN_USING( KEEN_USE_INI_VARS )
        if( vulkan::s_traceGpuAllocations )
        {
            vulkan::s_traceGpuAllocations.reset();
            m_objects.traceGpuAllocations();
        }
#endif
        return m_renderContext.beginFrame( swapChains );
    }

    void VulkanGraphicsDevice::submitFrame( GraphicsFrame* pInFrame, const GraphicsBindlessDescriptorSet& bindlessDescriptorSet )
    {
        MutexLock lock( &m_vulkanRenderContextMutex );

        KEEN_PROFILE_CPU( Vk_EndFrame );

        VulkanFrame* pFrame = (VulkanFrame*)pInFrame;
        m_renderContext.submitFrame( pFrame, bindlessDescriptorSet );
    }

    void VulkanGraphicsDevice::submitVulkanTransferBatch( VulkanTransferBatch* pBatch )
    {
        VulkanResult result = m_pVulkan->vkResetCommandPool( m_device, pBatch->commandPool, 0u /*VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT*/ );
        if( result.hasError() )
        {
            KEEN_BREAK( "[graphics] vkResetCommandPool failed with error '%s'\n", result );
            return;
        }

        VkCommandBufferBeginInfo commandBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = m_pVulkan->vkBeginCommandBuffer( pBatch->commandBuffer, &commandBufferBeginInfo );
        if( result.hasError() )
        {
            KEEN_BREAK( "[graphics] vkBeginCommandBuffer failed with error '%s'\n", result );
            return;
        }

        {
            VulkanRecordCommandBufferParameters recordParameters{};
            recordParameters.queueInfos = m_sharedData.queueInfos;

            GraphicsCommandBuffer* pCommandBuffer = pBatch->pFirstCommandBuffer;
            while( pCommandBuffer != nullptr )
            {
                vulkan::recordCommandBuffer( m_pVulkan, pBatch->commandBuffer, pCommandBuffer, recordParameters );
                pCommandBuffer = pCommandBuffer->pNextCommandBuffer;
            }
        }

        result = m_pVulkan->vkEndCommandBuffer( pBatch->commandBuffer );
        if( result.hasError() )
        {
            KEEN_BREAK( "[graphics] vkEndCommandBuffer failed with error '%s'\n", result );
            return;
        }

        result = m_pVulkan->vkResetFences( m_device, 1u, &pBatch->fence );
        if( result.hasError() )
        {
            KEEN_BREAK( "[graphics] vkResetFences failed with error '%s'\n", result );
            return;
        }

        VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount   = 0u;
        submitInfo.pWaitSemaphores      = nullptr;
        submitInfo.pWaitDstStageMask    = nullptr;
        submitInfo.commandBufferCount   = 1u;
        submitInfo.pCommandBuffers      = &pBatch->commandBuffer;

        result = m_pVulkan->vkQueueSubmit( m_sharedData.transferQueue, 1u, &submitInfo, pBatch->fence );

        if( m_renderContext.handleDeviceLost( result ) )
        {
            return;
        }

        if( result.hasError() )
        {
            KEEN_BREAK( "[graphics] vkQueueSubmit failed with error '%s'\n", result );
            return;
        }

#if KEEN_USING( KEEN_GPU_PROFILER )
        pBatch->submissionTime = profiler::getCurrentCpuTime();
#endif
    }

    GraphicsTransferBatch* VulkanGraphicsDevice::beginTransferBatch( DebugName debugName )
    {
        SynchronizedDataWriteLock<VulkanTransferQueue> sharedDataLock( &m_transferQueue );
        VulkanTransferQueue* pTransferQueue = sharedDataLock.getData();

        uint32 batchIndex;
        if( !pTransferQueue->freeBatchIndices.popBack( &batchIndex ) )
        {
            return nullptr;
        }

        const GraphicsTransferBatchId id = pTransferQueue->nextId;

        VulkanTransferBatch* pBatch = &pTransferQueue->batches[ batchIndex ];
        pBatch->id                  = id;
        pBatch->debugName           = debugName;
        pBatch->pFirstCommandBuffer = nullptr;
        pBatch->pLastCommandBuffer  = nullptr;

        pTransferQueue->nextId.value = id.value + 1u;
        if( pTransferQueue->nextId.value == 0u )
        {
            // holla.. that's a lot of transfer batches!!
            pTransferQueue->nextId.value = 1u;
        }

        return pBatch;
    }

    void VulkanGraphicsDevice::submitTransferBatch( GraphicsTransferBatch* pTransferBatch )
    {
        KEEN_ASSERT( pTransferBatch->id.value != 0u );
        submitVulkanTransferBatch( (VulkanTransferBatch*)pTransferBatch );
    }

    Result<void> VulkanGraphicsDevice::waitForTransferBatch( GraphicsTransferBatch* pTransferBatch, Time timeOut )
    {
        // find the submit:
        VulkanTransferBatch* pBatch = (VulkanTransferBatch*)pTransferBatch;
        KEEN_ASSERT( pBatch != nullptr );

        VulkanResult result;

        if( timeOut > 0_s )
        {
            result = m_pVulkan->vkWaitForFences( m_device, 1u, &pBatch->fence, VK_TRUE, (uint64)timeOut.toNanoseconds() );
            if( result.vkResult == VK_TIMEOUT )
            {
                return ErrorId_Temporary_TimeOut;
            }
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkWaitForFences failed with error '%s'\n", result );
            }
        }
        else
        {
            result = m_pVulkan->vkGetFenceStatus( m_device, pBatch->fence );
            if( result.vkResult == VK_NOT_READY )
            {
                return ErrorId_Temporary_TimeOut;
            }
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkGetFenceStatus failed with error '%s'\n", result );
            }
        }

        // free the batch:
        {
            SynchronizedDataWriteLock<VulkanTransferQueue> sharedDataLock( &m_transferQueue );
            VulkanTransferQueue* pTransferQueue = sharedDataLock.getData();

            const uint32 batchIndex = rangecheck_cast<uint32>( pTransferQueue->batches.getElementIndex( pBatch ) );

            pBatch->id.value = 0u;
            pBatch->debugName.clear();
            pTransferQueue->freeBatchIndices.pushBack( batchIndex );
        }

        return result.getErrorId();
    }

    void VulkanGraphicsDevice::waitForGpuIdle( const ArrayView<GraphicsDeviceObject*> destroyObjects )
    {
        m_renderContext.waitForAllFramesFinished();
        m_objects.destroyFrameObjects( destroyObjects );
    }

    ErrorId VulkanGraphicsDevice::createInstance( StringView applicationName, uint32 applicationVersion, CreateInstanceFlags flags )
    {
        TlsStackAllocatorScope stackAllocator;

        uint32_t instanceVersion = VK_VERSION_1_0; // when vkEnumerateInstanceVersion is nullptr, it's Vulkan 1.0
        if( m_pVulkan->vkEnumerateInstanceVersion != nullptr )
        {
            VulkanResult result = m_pVulkan->vkEnumerateInstanceVersion( &instanceVersion );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkEnumerateInstanceVersion failed with error '%k'\n", result );
                return result.getErrorId();
            }
        }

        KEEN_TRACE_INFO( "[graphics] Vulkan instance version %d.%d.%d (variant %d)\n", VK_API_VERSION_MAJOR( instanceVersion ), VK_API_VERSION_MINOR( instanceVersion ), VK_API_VERSION_PATCH( instanceVersion ), VK_API_VERSION_VARIANT( instanceVersion ) );

        if( VK_API_VERSION_VARIANT( instanceVersion ) != 0u )
        {
            KEEN_TRACE_ERROR( "[graphics] unsupported Vulkan variant\n" );
            return ErrorId_NotSupported;
        }
        if( VK_API_VERSION_MAJOR( instanceVersion ) != 1u )
        {
            KEEN_TRACE_ERROR( "[graphics] unsupported Vulkan major version\n" );
            return ErrorId_NotSupported;
        }
        if( VK_API_VERSION_MINOR( instanceVersion ) < 2u )
        {
            KEEN_TRACE_ERROR( "[graphics] At least Vulkan 1.2 is required\n" );
            return ErrorId_NotSupported;
        }

        VulkanLayerExtensionInfo instanceInfo;
        ErrorId error = vulkan::fillInstanceInfo( &instanceInfo, &stackAllocator, m_pVulkan );
        if( error != ErrorId_Ok )
        {
            return error;
        }

        DynamicArray<const char*, 16u> activeInstanceLayerNames( &stackAllocator );
        DynamicArray<const char*, 16u> activeInstanceExtensionNames( &stackAllocator );

#ifndef KEEN_BUILD_MASTER
        bool hasValidationFeaturesExtension = false;
        if( flags.isAnySet( { CreateInstanceFlag::EnableValidation, CreateInstanceFlag::EnableSynchronizationValidation, CreateInstanceFlag::EnableGpuAssistedValidation } ) )
        {
            // activate validation layers:
            if( instanceInfo.hasLayer( "VK_LAYER_KHRONOS_validation" ) )
            {
                KEEN_TRACE_INFO( "[vulkan] validation layer is enabled\n" );

                activeInstanceLayerNames.pushBack( "VK_LAYER_KHRONOS_validation" );
                vulkan::fillInstanceLayerExtensionInfo( &instanceInfo, &stackAllocator, m_pVulkan, "VK_LAYER_KHRONOS_validation" );

                if( instanceInfo.hasExtension( VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME ) )
                {
                    activeInstanceExtensionNames.pushBack( VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME );
                    hasValidationFeaturesExtension = true;
                }
            }
            else if( instanceInfo.hasLayer( "VK_LAYER_LUNARG_standard_validation" ) )
            {
                KEEN_TRACE_INFO( "[vulkan] old validation layer is enabled\n" );

                activeInstanceLayerNames.pushBack( "VK_LAYER_LUNARG_standard_validation" );
            }
            else
            {
                KEEN_TRACE_WARNING( "[vulkan] No Validation layer found!\n" );
            }
        }
#endif

#if KEEN_USING( KEEN_VULKAN_OBJECT_NAMES )
        if( instanceInfo.hasExtension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
        {
            activeInstanceExtensionNames.pushBack( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        }
#endif

#if defined( KEEN_PLATFORM_WIN32 )
        const char* pSurfaceExtensionName = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#elif defined( KEEN_PLATFORM_LINUX )
#   if KEEN_USING( KEEN_OS_USE_WAYLAND )
        const char* pSurfaceExtensionName = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#   else
        const char* pSurfaceExtensionName = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#   endif
#else
#   error Unsupported platform
#endif

        if( !instanceInfo.hasExtension( pSurfaceExtensionName ) ||
            !instanceInfo.hasExtension( VK_KHR_SURFACE_EXTENSION_NAME ) )
        {
            KEEN_TRACE_ERROR( "[graphics] No Surface extension found!\n" );
            return ErrorId_NotSupported;
        }
        activeInstanceExtensionNames.pushBack( pSurfaceExtensionName );
        activeInstanceExtensionNames.pushBack( VK_KHR_SURFACE_EXTENSION_NAME );

        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanAllocationCount, 0u, "Vk_AllocCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanAllocationSize, 0u, "Vk_AllocSize", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanInternalAllocationCount, 0u, "Vk_InternalAllocCount", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanInternalAllocationSize, 0u, "Vk_InternalAllocSize", false );
        KEEN_PROFILE_COUNTER_REGISTER( m_vulkanInternalAllocationSize, 0u, "Vk_InternalAllocSize", false );

        m_allocationCallbacks.pUserData             = this;
        m_allocationCallbacks.pfnAllocation         = vulkanAlloc;
        m_allocationCallbacks.pfnReallocation       = vulkanRealloc;
        m_allocationCallbacks.pfnFree               = vulkanFree;
        m_allocationCallbacks.pfnInternalAllocation = vulkanInternalAlloc;
        m_allocationCallbacks.pfnInternalFree       = vulkanInternalFree;

        if( flags.isSet( CreateInstanceFlag::UseMemoryCallbacks ) || vulkan::s_useAllocatorCallback )
        {
            if( !m_vulkanMemoryAllocator.tryCreate( m_pAllocator, 32u * 1024u * 1024u, "Vulkan"_debug, MemoryAllocatorFlag::DontTrack, 16u * 1024u * 1024u ) )
            {
                return ErrorId_OutOfMemory;
            }

            m_sharedData.pVulkanAllocationCallbacks = &m_allocationCallbacks;
        }
        else
        {
            m_sharedData.pVulkanAllocationCallbacks = nullptr;
        }

        m_vulkanRenderContextMutex.create( "VulkanRenderContext"_debug );

        KEEN_TRACE_INFO( "[vulkan] Vulkan Application Info: '%s' (version:%d)\n", applicationName, applicationVersion );
        DynamicArray<char, 64u> zeroTerminatedApplicationName;
        if( !zeroTerminatedApplicationName.tryCreate( &stackAllocator, applicationName.getCount() + 1u, false ) )
        {
            return ErrorId_OutOfMemory;
        }
        zeroTerminatedApplicationName.assign( applicationName );
        zeroTerminatedApplicationName.pushBack( '\0' );

        VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        applicationInfo.pApplicationName    = zeroTerminatedApplicationName.getStart();
        applicationInfo.applicationVersion  = applicationVersion;
        applicationInfo.pEngineName         = "holistic";
        applicationInfo.engineVersion       = 1u;
        applicationInfo.apiVersion          = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instanceCreateInfo.pApplicationInfo         = &applicationInfo;
        instanceCreateInfo.enabledLayerCount        = (uint32)activeInstanceLayerNames.getSize();
        instanceCreateInfo.ppEnabledLayerNames      = activeInstanceLayerNames.getStart();
        instanceCreateInfo.enabledExtensionCount    = (uint32)activeInstanceExtensionNames.getSize();
        instanceCreateInfo.ppEnabledExtensionNames  = activeInstanceExtensionNames.getStart();

#ifndef KEEN_BUILD_MASTER
        VkValidationFeaturesEXT validationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
        DynamicArray<VkValidationFeatureEnableEXT, 16u> enabledValidationFeatures( &stackAllocator );
        DynamicArray<VkValidationFeatureDisableEXT, 16u> disabledValidationFeatures( &stackAllocator );
        if( hasValidationFeaturesExtension )
        {
            instanceCreateInfo.pNext = &validationFeatures;

            if( flags.isSet( CreateInstanceFlag::EnableSynchronizationValidation ) )
            {
                KEEN_TRACE_INFO( "[vulkan] synchronization validation is enabled\n" );

                enabledValidationFeatures.pushBack( VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT );
            }
            if( flags.isSet( CreateInstanceFlag::EnableGpuAssistedValidation ) )
            {
                KEEN_TRACE_INFO( "[vulkan] gpu assisted validation is enabled\n" );

                enabledValidationFeatures.pushBack( VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT );
                enabledValidationFeatures.pushBack( VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT );
            }
            validationFeatures.enabledValidationFeatureCount    = rangecheck_cast<uint32>( enabledValidationFeatures.getCount() );
            validationFeatures.pEnabledValidationFeatures       = enabledValidationFeatures.getStart();
            validationFeatures.disabledValidationFeatureCount   = rangecheck_cast<uint32>( disabledValidationFeatures.getCount() );
            validationFeatures.pDisabledValidationFeatures      = disabledValidationFeatures.getStart();
        }
#endif

        VulkanResult result = m_pVulkan->vkCreateInstance( &instanceCreateInfo, m_sharedData.pVulkanAllocationCallbacks, &m_instance );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateInstance failed with error '%s'\n", result );
            return result.getErrorId();
        }
        KEEN_ASSERT( m_instance != VK_NULL_HANDLE );

        // now load the instance functions:
        error = vulkan::loadInstanceFunctions( m_pVulkan, m_instance, activeInstanceExtensionNames );
        if( error != ErrorId_Ok )
        {
            return error;
        }

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
        if( m_pVulkan->EXT_debug_utils )
        {
            KEEN_TRACE_INFO( "[graphics] Enable Vulkan Debug Report Callback!\n" );

            VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            messengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
            messengerCreateInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            messengerCreateInfo.pfnUserCallback = vulkanDebugUtilsMessengerCallback;
            messengerCreateInfo.pUserData       = this;

            result = m_pVulkan->vkCreateDebugUtilsMessengerEXT( m_instance, &messengerCreateInfo, m_sharedData.pVulkanAllocationCallbacks, &m_debugMessenger );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkCreateDebugUtilsMessengerEXT failed with error '%s'\n", result );
                return result.getErrorId();
            }
        }
        else
        {
            KEEN_TRACE_INFO( "[graphics] No Vulkan Debug Report Callback!\n" );
        }
#endif

        return ErrorId_Ok;
    }

    void VulkanGraphicsDevice::destroyInstance()
    {
#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
        if( m_debugMessenger != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyDebugUtilsMessengerEXT( m_instance, m_debugMessenger, m_sharedData.pVulkanAllocationCallbacks );
            m_debugMessenger = VK_NULL_HANDLE;
        }
#endif
        if( m_instance != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyInstance( m_instance, m_sharedData.pVulkanAllocationCallbacks );
            m_instance = VK_NULL_HANDLE;
        }

        KEEN_PROFILE_COUNTER_UNREGISTER( m_vulkanAllocationCount );
        KEEN_PROFILE_COUNTER_UNREGISTER( m_vulkanAllocationSize );
        KEEN_PROFILE_COUNTER_UNREGISTER( m_vulkanInternalAllocationCount );
        KEEN_PROFILE_COUNTER_UNREGISTER( m_vulkanInternalAllocationSize );

        m_vulkanRenderContextMutex.destroy();

        if( m_sharedData.pVulkanAllocationCallbacks != nullptr )
        {
#if KEEN_USING( KEEN_PROFILER )
            if( atomic::load_uint32_relaxed( &m_vulkanAllocationCount ) != 0u )
            {
                KEEN_TRACE_ERROR( "Vulkan memory leaks detected:\n" );
#   if KEEN_USING( KEEN_MEMORY_TRACKER )
                if( m_vulkanMemoryAllocator.getHandle().isValid() )
                {
                    debug::traceMemoryReport( 0u, m_vulkanMemoryAllocator.getHandle() );
                }
#   endif
            }
#endif
            m_sharedData.pVulkanAllocationCallbacks = nullptr;

            m_vulkanMemoryAllocator.destroy( m_pAllocator, true );
        }
    }

    static Result<uint32> encodeAdrenalinDriverVersion( uint32 major, uint32 minor, uint32 patch )
    {
        if( major >= 256u || minor >= 256u || patch >= 256u )
        {
            return ErrorId_OutOfRange;
        }
        return ( major << 16u ) | ( minor << 8u ) | ( patch << 0u );
    }

    static Result<uint32> parseAdrenalinDriverVersionFromDriverInfo( const StringView& driverInfo )
    {
        uint32 major;
        uint32 minor;
        uint32 patch;
        const Result<size_t> scanStringResult = scanString( driverInfo, "%d.%d.%d", &major, &minor, &patch );
        if( scanStringResult.hasError() )
        {
            return scanStringResult.getError();
        }
        if( scanStringResult.getValue() != 3u )
        {
            return ErrorId_InvalidValue;
        }

        return encodeAdrenalinDriverVersion( major, minor, patch );
    }

    static uint32 encodeNvidiaDriverVersion( uint32 major, uint32 minor )
    {
        // :JK: I couldn't find any official documentation about the encoding of nvidia driver versions - but i found
        // sascha willems code for vulkan gpuinfo: https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/1e6ca6e3c0763daabd6a101b860ab4354a07f5d3/functions.php#L294
        // according to that the nvidia driver version is encoded as 10.8.8.6 number
        KEEN_ASSERT( major < ( 1u << 10u ) );
        KEEN_ASSERT( minor < ( 1u << 8u ) );
        return ( major << 22u ) | ( minor << 14u );
    }

    static uint32 encodeIntelDriverVersion( uint32 major, uint32 minor )
    {
        // :JK: I couldn't find any official documentation about the encoding of intel driver versions - but i found
        // sascha willems code for vulkan gpuinfo: https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/1e6ca6e3c0763daabd6a101b860ab4354a07f5d3/functions.php#L294
        // according to that the intel driver version is encoded as 11.14 number
        KEEN_ASSERT( major < ( 1u << 11u ) );
        KEEN_ASSERT( minor < ( 1u << 14u ) );
        return ( major << 14u ) | ( minor );
    }

    ErrorId VulkanGraphicsDevice::createDevice( OsWindowSystem* pWindowSystem, Optional<uint32> forcePhysicalDeviceIndex )
    {
        KEEN_ASSERT( m_instance != VK_NULL_HANDLE );

        TlsStackAllocatorScope stackAllocator;

        Array<VkPhysicalDevice, 64u> physicalDevices;
        {
            uint32 physicalDeviceCount = 64u;
            VulkanResult result = m_pVulkan->vkEnumeratePhysicalDevices( m_instance, &physicalDeviceCount, nullptr );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkEnumeratePhysicalDevices#1 failed with error '%s'\n", result );
                return result.getErrorId();
            }
            if( !physicalDevices.tryCreate( &stackAllocator, physicalDeviceCount ) )
            {
                return ErrorId_OutOfMemory;
            }
            result = m_pVulkan->vkEnumeratePhysicalDevices( m_instance, &physicalDeviceCount, physicalDevices.getStart() );
            if( result.hasError() )
            {
                KEEN_TRACE_ERROR( "[graphics] vkEnumeratePhysicalDevices#2 failed with error '%s'\n", result );
                return result.getErrorId();
            }
        }

        struct PhysicalDeviceInfo
        {
            VkPhysicalDeviceProperties2                             deviceProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &devicePropertiesVulkan11 };
            VkPhysicalDeviceVulkan11Properties                      devicePropertiesVulkan11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES, &devicePropertiesVulkan12 };
            VkPhysicalDeviceVulkan12Properties                      devicePropertiesVulkan12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES };

            void **ppNextProperties                                 = &devicePropertiesVulkan12.pNext;
            VkPhysicalDeviceFeatures2                               deviceFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &deviceFeaturesVulkan11 };
            VkPhysicalDeviceVulkan11Features                        deviceFeaturesVulkan11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &deviceFeaturesVulkan12 };
            VkPhysicalDeviceVulkan12Features                        deviceFeaturesVulkan12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &deviceFeaturesShaderDemoteToHelperInvocation };
            VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures  deviceFeaturesShaderDemoteToHelperInvocation = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES, &deviceFeaturesDynamicRendering };
            VkPhysicalDeviceDynamicRenderingFeatures                deviceFeaturesDynamicRendering = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
            VkPhysicalDeviceMemoryPriorityFeaturesEXT               deviceFeaturesMemoryPriority = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT };
            VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT    deviceFeaturesPageableDeviceLocalMemory = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT };
#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )
            VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR deviceFeaturesPipelineExecutableProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR };
#endif
#if KEEN_USING( KEEN_VULKAN_ROBUST_BUFFER_ACCESS )
            VkPhysicalDeviceRobustness2FeaturesEXT                  deviceFeaturesRobustness = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
#endif
            void **ppNextFeatures                                   = &deviceFeaturesDynamicRendering.pNext;

            VulkanLayerExtensionInfo                                layerExtensionInfo;
            DynamicArray<const char*, 64u>                          activeDeviceExtensions;

            bool                                                    isMemoryPrioritySupported = false;
            bool                                                    isSupported = false;
        };
        Array<PhysicalDeviceInfo> physicalDeviceInfo;
        if( !physicalDeviceInfo.tryCreate( &stackAllocator, physicalDevices.getCount() ) )
        {
            return ErrorId_OutOfMemory;
        }

        for( uint32 physicalDeviceIndex = 0u; physicalDeviceIndex < physicalDevices.getCount(); ++physicalDeviceIndex )
        {
            const VkPhysicalDevice physicalDevice   = physicalDevices[ physicalDeviceIndex ];
            PhysicalDeviceInfo* pDeviceInfo         = &physicalDeviceInfo[ physicalDeviceIndex ];

            const VulkanLayerExtensionInfo&             layerExtensionInfo = pDeviceInfo->layerExtensionInfo;
            const VkPhysicalDeviceProperties&           deviceProperties = pDeviceInfo->deviceProperties.properties;
            const VkPhysicalDeviceVulkan11Properties&   devicePropertiesVulkan11 = pDeviceInfo->devicePropertiesVulkan11;
            const VkPhysicalDeviceVulkan12Properties&   devicePropertiesVulkan12 = pDeviceInfo->devicePropertiesVulkan12;

            VkPhysicalDeviceVulkan12Features            deviceFeaturesVulkan12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
            VkPhysicalDeviceVulkan11Features            deviceFeaturesVulkan11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &deviceFeaturesVulkan12 };
            VkPhysicalDeviceFeatures2                   deviceFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &deviceFeaturesVulkan11 };
            const VkPhysicalDeviceFeatures&             deviceFeatures = deviceFeatures2.features;

            m_pVulkan->vkGetPhysicalDeviceProperties( physicalDevices[ physicalDeviceIndex ], &pDeviceInfo->deviceProperties.properties );

            KEEN_TRACE_INFO( "[graphics] Vulkan device %d (%s):\n", physicalDeviceIndex, deviceProperties.deviceName );
            KEEN_TRACE_INFO( "[graphics] - api version   : %d.%d.%d\n", VK_API_VERSION_MAJOR( deviceProperties.apiVersion ), VK_API_VERSION_MINOR( deviceProperties.apiVersion ), VK_API_VERSION_PATCH( deviceProperties.apiVersion ) );
            KEEN_TRACE_INFO( "[graphics] - vendor id     : 0x%08x\n", deviceProperties.vendorID );
            KEEN_TRACE_INFO( "[graphics] - device id     : 0x%08x\n", deviceProperties.deviceID );
            KEEN_TRACE_INFO( "[graphics] - device type   : %s\n", vulkan::getDeviceTypeString( deviceProperties.deviceType ) );
            KEEN_TRACE_INFO( "[graphics] - driver version: 0x%08x\n", deviceProperties.driverVersion );

            if( VK_API_VERSION_MAJOR( deviceProperties.apiVersion ) != 1u || VK_API_VERSION_MINOR( deviceProperties.apiVersion ) < 2u )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because is does not support Vulkan 1.2!\n" );
                continue;
            }

            ErrorId error = vulkan::fillDeviceInfo( &pDeviceInfo->layerExtensionInfo, &stackAllocator, m_pVulkan, physicalDevice );
            if( error != ErrorId_Ok )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because querying extensions failed with error '%k'!\n", error );
                continue;
            }

            m_pVulkan->vkGetPhysicalDeviceProperties2( physicalDevices[ physicalDeviceIndex ], &pDeviceInfo->deviceProperties );
            m_pVulkan->vkGetPhysicalDeviceFeatures2( physicalDevices[ physicalDeviceIndex ], &deviceFeatures2 );

            KEEN_TRACE_INFO( "[graphics] - driver name: %s\n", devicePropertiesVulkan12.driverName );
            KEEN_TRACE_INFO( "[graphics] - driver info: %s\n", devicePropertiesVulkan12.driverInfo );
            KEEN_TRACE_INFO( "[graphics] - maxImageDimension1D: %,d\n", deviceProperties.limits.maxImageDimension1D );
            KEEN_TRACE_INFO( "[graphics] - maxImageDimension2D: %,d\n", deviceProperties.limits.maxImageDimension2D );
            KEEN_TRACE_INFO( "[graphics] - maxImageDimension3D: %,d\n", deviceProperties.limits.maxImageDimension3D );
            KEEN_TRACE_INFO( "[graphics] - maxImageDimensionCube: %,d\n", deviceProperties.limits.maxImageDimensionCube );
            KEEN_TRACE_INFO( "[graphics] - maxImageArrayLayers: %,d\n", deviceProperties.limits.maxImageArrayLayers );
            KEEN_TRACE_INFO( "[graphics] - maxTexelBufferElements: %,d\n", deviceProperties.limits.maxTexelBufferElements );
            KEEN_TRACE_INFO( "[graphics] - maxUniformBufferRange: %,d\n", deviceProperties.limits.maxUniformBufferRange );
            KEEN_TRACE_INFO( "[graphics] - maxStorageBufferRange: %,d\n", deviceProperties.limits.maxStorageBufferRange );
            KEEN_TRACE_INFO( "[graphics] - maxPushConstantsSize: %,d\n", deviceProperties.limits.maxPushConstantsSize );
            KEEN_TRACE_INFO( "[graphics] - maxMemoryAllocationCount: %,d\n", deviceProperties.limits.maxMemoryAllocationCount );
            KEEN_TRACE_INFO( "[graphics] - maxSamplerAllocationCount: %,d\n", deviceProperties.limits.maxSamplerAllocationCount );
            KEEN_TRACE_INFO( "[graphics] - bufferImageGranularity: %,zu\n", deviceProperties.limits.bufferImageGranularity );
            KEEN_TRACE_INFO( "[graphics] - sparseAddressSpaceSize: %,zu\n", deviceProperties.limits.sparseAddressSpaceSize );
            KEEN_TRACE_INFO( "[graphics] - maxBoundDescriptorSets: %,d\n", deviceProperties.limits.maxBoundDescriptorSets );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageDescriptorSamplers: %,d\n", deviceProperties.limits.maxPerStageDescriptorSamplers );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageDescriptorUniformBuffers: %,d\n", deviceProperties.limits.maxPerStageDescriptorUniformBuffers );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageDescriptorStorageBuffers: %,d\n", deviceProperties.limits.maxPerStageDescriptorStorageBuffers );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageDescriptorSampledImages: %,d\n", deviceProperties.limits.maxPerStageDescriptorSampledImages );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageDescriptorStorageImages: %,d\n", deviceProperties.limits.maxPerStageDescriptorStorageImages );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageDescriptorInputAttachments: %,d\n", deviceProperties.limits.maxPerStageDescriptorInputAttachments );
            KEEN_TRACE_INFO( "[graphics] - maxPerStageResources: %,d\n", deviceProperties.limits.maxPerStageResources );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetSamplers: %,d\n", deviceProperties.limits.maxDescriptorSetSamplers );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetUniformBuffers: %,d\n", deviceProperties.limits.maxDescriptorSetUniformBuffers );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetUniformBuffersDynamic: %,d\n", deviceProperties.limits.maxDescriptorSetUniformBuffersDynamic );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetStorageBuffers: %,d\n", deviceProperties.limits.maxDescriptorSetStorageBuffers );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetStorageBuffersDynamic: %,d\n", deviceProperties.limits.maxDescriptorSetStorageBuffersDynamic );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetSampledImages: %,d\n", deviceProperties.limits.maxDescriptorSetSampledImages );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetStorageImages: %,d\n", deviceProperties.limits.maxDescriptorSetStorageImages );
            KEEN_TRACE_INFO( "[graphics] - maxDescriptorSetInputAttachments: %,d\n", deviceProperties.limits.maxDescriptorSetInputAttachments );
            KEEN_TRACE_INFO( "[graphics] - maxVertexInputAttributes: %,d\n", deviceProperties.limits.maxVertexInputAttributes );
            KEEN_TRACE_INFO( "[graphics] - maxVertexInputBindings: %,d\n", deviceProperties.limits.maxVertexInputBindings );
            KEEN_TRACE_INFO( "[graphics] - maxVertexInputAttributeOffset: %,d\n", deviceProperties.limits.maxVertexInputAttributeOffset );
            KEEN_TRACE_INFO( "[graphics] - maxVertexInputBindingStride: %,d\n", deviceProperties.limits.maxVertexInputBindingStride );
            KEEN_TRACE_INFO( "[graphics] - maxVertexOutputComponents: %,d\n", deviceProperties.limits.maxVertexOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationGenerationLevel: %,d\n", deviceProperties.limits.maxTessellationGenerationLevel );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationPatchSize: %,d\n", deviceProperties.limits.maxTessellationPatchSize );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationControlPerVertexInputComponents: %,d\n", deviceProperties.limits.maxTessellationControlPerVertexInputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationControlPerVertexOutputComponents: %,d\n", deviceProperties.limits.maxTessellationControlPerVertexOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationControlPerPatchOutputComponents: %,d\n", deviceProperties.limits.maxTessellationControlPerPatchOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationControlTotalOutputComponents: %,d\n", deviceProperties.limits.maxTessellationControlTotalOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationEvaluationInputComponents: %,d\n", deviceProperties.limits.maxTessellationEvaluationInputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxTessellationEvaluationOutputComponents: %,d\n", deviceProperties.limits.maxTessellationEvaluationOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxGeometryShaderInvocations: %,d\n", deviceProperties.limits.maxGeometryShaderInvocations );
            KEEN_TRACE_INFO( "[graphics] - maxGeometryInputComponents: %,d\n", deviceProperties.limits.maxGeometryInputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxGeometryOutputComponents: %,d\n", deviceProperties.limits.maxGeometryOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxGeometryOutputVertices: %,d\n", deviceProperties.limits.maxGeometryOutputVertices );
            KEEN_TRACE_INFO( "[graphics] - maxGeometryTotalOutputComponents: %,d\n", deviceProperties.limits.maxGeometryTotalOutputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxFragmentInputComponents: %,d\n", deviceProperties.limits.maxFragmentInputComponents );
            KEEN_TRACE_INFO( "[graphics] - maxFragmentOutputAttachments: %,d\n", deviceProperties.limits.maxFragmentOutputAttachments );
            KEEN_TRACE_INFO( "[graphics] - maxFragmentDualSrcAttachments: %,d\n", deviceProperties.limits.maxFragmentDualSrcAttachments );
            KEEN_TRACE_INFO( "[graphics] - maxFragmentCombinedOutputResources: %,d\n", deviceProperties.limits.maxFragmentCombinedOutputResources );
            KEEN_TRACE_INFO( "[graphics] - maxComputeSharedMemorySize: %,d\n", deviceProperties.limits.maxComputeSharedMemorySize );
            KEEN_TRACE_INFO( "[graphics] - maxComputeWorkGroupCount[3]: %,d,%,d,%,d\n", deviceProperties.limits.maxComputeWorkGroupCount[0u], deviceProperties.limits.maxComputeWorkGroupCount[1u], deviceProperties.limits.maxComputeWorkGroupCount[2u] );
            KEEN_TRACE_INFO( "[graphics] - maxComputeWorkGroupInvocations: %,d\n", deviceProperties.limits.maxComputeWorkGroupInvocations );
            KEEN_TRACE_INFO( "[graphics] - maxComputeWorkGroupSize[3]: %,d,%,d,%,d\n", deviceProperties.limits.maxComputeWorkGroupSize[0u], deviceProperties.limits.maxComputeWorkGroupSize[1u], deviceProperties.limits.maxComputeWorkGroupSize[2u] );
            KEEN_TRACE_INFO( "[graphics] - subPixelPrecisionBits: %,d\n", deviceProperties.limits.subPixelPrecisionBits );
            KEEN_TRACE_INFO( "[graphics] - subTexelPrecisionBits: %,d\n", deviceProperties.limits.subTexelPrecisionBits );
            KEEN_TRACE_INFO( "[graphics] - mipmapPrecisionBits: %,d\n", deviceProperties.limits.mipmapPrecisionBits );
            KEEN_TRACE_INFO( "[graphics] - maxDrawIndexedIndexValue: %,d\n", deviceProperties.limits.maxDrawIndexedIndexValue );
            KEEN_TRACE_INFO( "[graphics] - maxDrawIndirectCount: %,d\n", deviceProperties.limits.maxDrawIndirectCount );
            KEEN_TRACE_INFO( "[graphics] - maxSamplerLodBias: %.2f\n", deviceProperties.limits.maxSamplerLodBias );
            KEEN_TRACE_INFO( "[graphics] - maxSamplerAnisotropy: %.2f\n", deviceProperties.limits.maxSamplerAnisotropy );
            KEEN_TRACE_INFO( "[graphics] - maxViewports: %,d\n", deviceProperties.limits.maxViewports );
            KEEN_TRACE_INFO( "[graphics] - maxViewportDimensions[2]: %d,%d\n", deviceProperties.limits.maxViewportDimensions[0u], deviceProperties.limits.maxViewportDimensions[1u] );
            KEEN_TRACE_INFO( "[graphics] - viewportBoundsRange[2]: %.2f,%.2f\n", deviceProperties.limits.viewportBoundsRange[0u], deviceProperties.limits.viewportBoundsRange[1u] );
            KEEN_TRACE_INFO( "[graphics] - viewportSubPixelBits: %,d\n", deviceProperties.limits.viewportSubPixelBits );
            KEEN_TRACE_INFO( "[graphics] - minMemoryMapAlignment: %zu\n", deviceProperties.limits.minMemoryMapAlignment );
            KEEN_TRACE_INFO( "[graphics] - minTexelBufferOffsetAlignment: %,zu\n", deviceProperties.limits.minTexelBufferOffsetAlignment );
            KEEN_TRACE_INFO( "[graphics] - minUniformBufferOffsetAlignment: %,zu\n", deviceProperties.limits.minUniformBufferOffsetAlignment );
            KEEN_TRACE_INFO( "[graphics] - minStorageBufferOffsetAlignment: %,zu\n", deviceProperties.limits.minStorageBufferOffsetAlignment );
            KEEN_TRACE_INFO( "[graphics] - minTexelOffset: %,d\n", deviceProperties.limits.minTexelOffset );
            KEEN_TRACE_INFO( "[graphics] - maxTexelOffset: %,d\n", deviceProperties.limits.maxTexelOffset );
            KEEN_TRACE_INFO( "[graphics] - minTexelGatherOffset: %,d\n", deviceProperties.limits.minTexelGatherOffset );
            KEEN_TRACE_INFO( "[graphics] - maxTexelGatherOffset: %,d\n", deviceProperties.limits.maxTexelGatherOffset );
            KEEN_TRACE_INFO( "[graphics] - minInterpolationOffset: %.2f\n", deviceProperties.limits.minInterpolationOffset );
            KEEN_TRACE_INFO( "[graphics] - maxInterpolationOffset: %.2f\n", deviceProperties.limits.maxInterpolationOffset );
            KEEN_TRACE_INFO( "[graphics] - subPixelInterpolationOffsetBits: %,d\n", deviceProperties.limits.subPixelInterpolationOffsetBits );
            KEEN_TRACE_INFO( "[graphics] - maxFramebufferWidth: %,d\n", deviceProperties.limits.maxFramebufferWidth );
            KEEN_TRACE_INFO( "[graphics] - maxFramebufferHeight: %,d\n", deviceProperties.limits.maxFramebufferHeight );
            KEEN_TRACE_INFO( "[graphics] - maxFramebufferLayers: %,d\n", deviceProperties.limits.maxFramebufferLayers );
            KEEN_TRACE_INFO( "[graphics] - framebufferColorSampleCounts: %0b\n", deviceProperties.limits.framebufferColorSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - framebufferDepthSampleCounts: %0b\n", deviceProperties.limits.framebufferDepthSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - framebufferStencilSampleCounts: %0b\n", deviceProperties.limits.framebufferStencilSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - framebufferNoAttachmentsSampleCounts: %0b\n", deviceProperties.limits.framebufferNoAttachmentsSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - maxColorAttachments: %,d\n", deviceProperties.limits.maxColorAttachments );
            KEEN_TRACE_INFO( "[graphics] - sampledImageColorSampleCounts: %0b\n", deviceProperties.limits.sampledImageColorSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - sampledImageIntegerSampleCounts: %0b\n", deviceProperties.limits.sampledImageIntegerSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - sampledImageDepthSampleCounts: %0b\n", deviceProperties.limits.sampledImageDepthSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - sampledImageStencilSampleCounts: %0b\n", deviceProperties.limits.sampledImageStencilSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - storageImageSampleCounts: %0b\n", deviceProperties.limits.storageImageSampleCounts );
            KEEN_TRACE_INFO( "[graphics] - maxSampleMaskWords: %,d\n", deviceProperties.limits.maxSampleMaskWords );
            KEEN_TRACE_INFO( "[graphics] - timestampComputeAndGraphics: %s\n", deviceProperties.limits.timestampComputeAndGraphics ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - timestampPeriod: %.2f\n", deviceProperties.limits.timestampPeriod );
            KEEN_TRACE_INFO( "[graphics] - maxClipDistances: %,d\n", deviceProperties.limits.maxClipDistances );
            KEEN_TRACE_INFO( "[graphics] - maxCullDistances: %,d\n", deviceProperties.limits.maxCullDistances );
            KEEN_TRACE_INFO( "[graphics] - maxCombinedClipAndCullDistances: %,d\n", deviceProperties.limits.maxCombinedClipAndCullDistances );
            KEEN_TRACE_INFO( "[graphics] - discreteQueuePriorities: %,d\n", deviceProperties.limits.discreteQueuePriorities );
            KEEN_TRACE_INFO( "[graphics] - pointSizeRange[2]: %f,%f\n", deviceProperties.limits.pointSizeRange[0u], deviceProperties.limits.pointSizeRange[1u] );
            KEEN_TRACE_INFO( "[graphics] - lineWidthRange[2]: %f,%f\n", deviceProperties.limits.lineWidthRange[0u], deviceProperties.limits.lineWidthRange[1u] );
            KEEN_TRACE_INFO( "[graphics] - pointSizeGranularity: %f\n", deviceProperties.limits.pointSizeGranularity );
            KEEN_TRACE_INFO( "[graphics] - lineWidthGranularity: %f\n", deviceProperties.limits.lineWidthGranularity );
            KEEN_TRACE_INFO( "[graphics] - strictLines: %s\n", deviceProperties.limits.strictLines ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - standardSampleLocations: %s\n", deviceProperties.limits.standardSampleLocations ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - optimalBufferCopyOffsetAlignment: %zu\n", deviceProperties.limits.optimalBufferCopyOffsetAlignment );
            KEEN_TRACE_INFO( "[graphics] - optimalBufferCopyRowPitchAlignment: %zu\n", deviceProperties.limits.optimalBufferCopyRowPitchAlignment );
            KEEN_TRACE_INFO( "[graphics] - nonCoherentAtomSize: %zu\n", deviceProperties.limits.nonCoherentAtomSize );
            KEEN_TRACE_INFO( "[graphics] - subgroupSize: %d\n", devicePropertiesVulkan11.subgroupSize );
            KEEN_TRACE_INFO( "[graphics] - subgroupSupportedStages: %b\n", devicePropertiesVulkan11.subgroupSupportedStages );
            KEEN_TRACE_INFO( "[graphics] - subgroupSupportedOperations: %b\n", devicePropertiesVulkan11.subgroupSupportedOperations );
            KEEN_TRACE_INFO( "[graphics] - shaderUniformBufferArrayNonUniformIndexingNative: %d\n", devicePropertiesVulkan12.shaderUniformBufferArrayNonUniformIndexingNative );
            KEEN_TRACE_INFO( "[graphics] - shaderSampledImageArrayNonUniformIndexingNative: %d\n", devicePropertiesVulkan12.shaderSampledImageArrayNonUniformIndexingNative );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageBufferArrayNonUniformIndexingNative: %d\n", devicePropertiesVulkan12.shaderStorageBufferArrayNonUniformIndexingNative );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageArrayNonUniformIndexingNative: %d\n", devicePropertiesVulkan12.shaderStorageImageArrayNonUniformIndexingNative );
            KEEN_TRACE_INFO( "[graphics] - shaderInputAttachmentArrayNonUniformIndexingNative: %d\n", devicePropertiesVulkan12.shaderInputAttachmentArrayNonUniformIndexingNative );
            KEEN_TRACE_INFO( "[graphics] - supportedDepthResolveModes: %b\n", devicePropertiesVulkan12.supportedDepthResolveModes );
            KEEN_TRACE_INFO( "[graphics] - independentResolveNone: %d\n", devicePropertiesVulkan12.independentResolveNone );
            KEEN_TRACE_INFO( "[graphics] - independentResolve: %d\n", devicePropertiesVulkan12.independentResolve );
            KEEN_TRACE_INFO( "[graphics] - shaderSignedZeroInfNanPreserveFloat32: %d\n", devicePropertiesVulkan12.shaderSignedZeroInfNanPreserveFloat32 );          
            KEEN_TRACE_INFO( "[graphics] Vulkan Device Features:\n" );
            KEEN_TRACE_INFO( "[graphics] - robustBufferAccess: %s\n", deviceFeatures.robustBufferAccess ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - fullDrawIndexUint32: %s\n", deviceFeatures.fullDrawIndexUint32 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - imageCubeArray: %s\n", deviceFeatures.imageCubeArray ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - independentBlend: %s\n", deviceFeatures.independentBlend ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - geometryShader: %s\n", deviceFeatures.geometryShader ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - tessellationShader: %s\n", deviceFeatures.tessellationShader ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sampleRateShading: %s\n", deviceFeatures.sampleRateShading ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - dualSrcBlend: %s\n", deviceFeatures.dualSrcBlend ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - logicOp: %s\n", deviceFeatures.logicOp ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - multiDrawIndirect: %s\n", deviceFeatures.multiDrawIndirect ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - drawIndirectFirstInstance: %s\n", deviceFeatures.drawIndirectFirstInstance ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - depthClamp: %s\n", deviceFeatures.depthClamp ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - depthBiasClamp: %s\n", deviceFeatures.depthBiasClamp ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - fillModeNonSolid: %s\n", deviceFeatures.fillModeNonSolid ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - depthBounds: %s\n", deviceFeatures.depthBounds ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - wideLines: %s\n", deviceFeatures.wideLines ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - largePoints: %s\n", deviceFeatures.largePoints ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - alphaToOne: %s\n", deviceFeatures.alphaToOne ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - multiViewport: %s\n", deviceFeatures.multiViewport ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - samplerAnisotropy: %s\n", deviceFeatures.samplerAnisotropy ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - textureCompressionETC2: %s\n", deviceFeatures.textureCompressionETC2 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - textureCompressionASTC_LDR: %s\n", deviceFeatures.textureCompressionASTC_LDR ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - textureCompressionBC: %s\n", deviceFeatures.textureCompressionBC ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - occlusionQueryPrecise: %s\n", deviceFeatures.occlusionQueryPrecise ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - pipelineStatisticsQuery: %s\n", deviceFeatures.pipelineStatisticsQuery ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - vertexPipelineStoresAndAtomics: %s\n", deviceFeatures.vertexPipelineStoresAndAtomics ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - fragmentStoresAndAtomics: %s\n", deviceFeatures.fragmentStoresAndAtomics ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderTessellationAndGeometryPointSize: %s\n", deviceFeatures.shaderTessellationAndGeometryPointSize ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderImageGatherExtended: %s\n", deviceFeatures.shaderImageGatherExtended ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageExtendedFormats: %s\n", deviceFeatures.shaderStorageImageExtendedFormats ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageMultisample: %s\n", deviceFeatures.shaderStorageImageMultisample ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageReadWithoutFormat: %s\n", deviceFeatures.shaderStorageImageReadWithoutFormat ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageWriteWithoutFormat: %s\n", deviceFeatures.shaderStorageImageWriteWithoutFormat ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderUniformBufferArrayDynamicIndexing: %s\n", deviceFeatures.shaderUniformBufferArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderSampledImageArrayDynamicIndexing: %s\n", deviceFeatures.shaderSampledImageArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageBufferArrayDynamicIndexing: %s\n", deviceFeatures.shaderStorageBufferArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageArrayDynamicIndexing: %s\n", deviceFeatures.shaderStorageImageArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderClipDistance: %s\n", deviceFeatures.shaderClipDistance ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderCullDistance: %s\n", deviceFeatures.shaderCullDistance ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderFloat64: %s\n", deviceFeatures.shaderFloat64 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderInt64: %s\n", deviceFeatures.shaderInt64 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderInt16: %s\n", deviceFeatures.shaderInt16 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderResourceResidency: %s\n", deviceFeatures.shaderResourceResidency ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderResourceMinLod: %s\n", deviceFeatures.shaderResourceMinLod ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseBinding: %s\n", deviceFeatures.sparseBinding ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidencyBuffer: %s\n", deviceFeatures.sparseResidencyBuffer ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidencyImage2D: %s\n", deviceFeatures.sparseResidencyImage2D ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidencyImage3D: %s\n", deviceFeatures.sparseResidencyImage3D ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidency2Samples: %s\n", deviceFeatures.sparseResidency2Samples ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidency4Samples: %s\n", deviceFeatures.sparseResidency4Samples ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidency8Samples: %s\n", deviceFeatures.sparseResidency8Samples ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidency16Samples: %s\n", deviceFeatures.sparseResidency16Samples ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - sparseResidencyAliased: %s\n", deviceFeatures.sparseResidencyAliased ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - variableMultisampleRate: %s\n", deviceFeatures.variableMultisampleRate ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - inheritedQueries: %s\n", deviceFeatures.inheritedQueries ? "VK_TRUE" : "VK_FALSE" );

            KEEN_TRACE_INFO( "[graphics] Vulkan 1.1 Device Features:\n" );
            KEEN_TRACE_INFO( "[graphics] - storageBuffer16BitAccess: %s\n", deviceFeaturesVulkan11.storageBuffer16BitAccess ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - uniformAndStorageBuffer16BitAccess: %s\n", deviceFeaturesVulkan11.uniformAndStorageBuffer16BitAccess ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - storagePushConstant16: %s\n", deviceFeaturesVulkan11.storagePushConstant16 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - storageInputOutput16: %s\n", deviceFeaturesVulkan11.storageInputOutput16 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - multiview: %s\n", deviceFeaturesVulkan11.multiview ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - multiviewGeometryShader: %s\n", deviceFeaturesVulkan11.multiviewGeometryShader ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - multiviewTessellationShader: %s\n", deviceFeaturesVulkan11.multiviewTessellationShader ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - variablePointersStorageBuffer: %s\n", deviceFeaturesVulkan11.variablePointersStorageBuffer ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - variablePointers: %s\n", deviceFeaturesVulkan11.variablePointers ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - protectedMemory: %s\n", deviceFeaturesVulkan11.protectedMemory ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - samplerYcbcrConversion: %s\n", deviceFeaturesVulkan11.samplerYcbcrConversion ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderDrawParameters: %s\n", deviceFeaturesVulkan11.shaderDrawParameters ? "VK_TRUE" : "VK_FALSE" );

            KEEN_TRACE_INFO( "[graphics] Vulkan 1.2 Device Features:\n" );
            KEEN_TRACE_INFO( "[graphics] - samplerMirrorClampToEdge: %s\n", deviceFeaturesVulkan12.samplerMirrorClampToEdge ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - drawIndirectCount: %s\n", deviceFeaturesVulkan12.drawIndirectCount ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - storageBuffer8BitAccess: %s\n", deviceFeaturesVulkan12.storageBuffer8BitAccess ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - uniformAndStorageBuffer8BitAccess: %s\n", deviceFeaturesVulkan12.uniformAndStorageBuffer8BitAccess ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - storagePushConstant8: %s\n", deviceFeaturesVulkan12.storagePushConstant8 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderBufferInt64Atomics: %s\n", deviceFeaturesVulkan12.shaderBufferInt64Atomics ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderSharedInt64Atomics: %s\n", deviceFeaturesVulkan12.shaderSharedInt64Atomics ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderFloat16: %s\n", deviceFeaturesVulkan12.shaderFloat16 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderInt8: %s\n", deviceFeaturesVulkan12.shaderInt8 ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorIndexing: %s\n", deviceFeaturesVulkan12.descriptorIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderInputAttachmentArrayDynamicIndexing: %s\n", deviceFeaturesVulkan12.shaderInputAttachmentArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderUniformTexelBufferArrayDynamicIndexing: %s\n", deviceFeaturesVulkan12.shaderUniformTexelBufferArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageTexelBufferArrayDynamicIndexing: %s\n", deviceFeaturesVulkan12.shaderStorageTexelBufferArrayDynamicIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderUniformBufferArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderUniformBufferArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderSampledImageArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderSampledImageArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageBufferArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderStorageBufferArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageImageArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderStorageImageArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderInputAttachmentArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderInputAttachmentArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderUniformTexelBufferArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderUniformTexelBufferArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderStorageTexelBufferArrayNonUniformIndexing: %s\n", deviceFeaturesVulkan12.shaderStorageTexelBufferArrayNonUniformIndexing ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingUniformBufferUpdateAfterBind: %s\n", deviceFeaturesVulkan12.descriptorBindingUniformBufferUpdateAfterBind ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingSampledImageUpdateAfterBind: %s\n", deviceFeaturesVulkan12.descriptorBindingSampledImageUpdateAfterBind ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingStorageImageUpdateAfterBind: %s\n", deviceFeaturesVulkan12.descriptorBindingStorageImageUpdateAfterBind ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingStorageBufferUpdateAfterBind: %s\n", deviceFeaturesVulkan12.descriptorBindingStorageBufferUpdateAfterBind ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingUniformTexelBufferUpdateAfterBind: %s\n", deviceFeaturesVulkan12.descriptorBindingUniformTexelBufferUpdateAfterBind ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingStorageTexelBufferUpdateAfterBind: %s\n", deviceFeaturesVulkan12.descriptorBindingStorageTexelBufferUpdateAfterBind ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingUpdateUnusedWhilePending: %s\n", deviceFeaturesVulkan12.descriptorBindingUpdateUnusedWhilePending ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingPartiallyBound: %s\n", deviceFeaturesVulkan12.descriptorBindingPartiallyBound ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - descriptorBindingVariableDescriptorCount: %s\n", deviceFeaturesVulkan12.descriptorBindingVariableDescriptorCount ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - runtimeDescriptorArray: %s\n", deviceFeaturesVulkan12.runtimeDescriptorArray ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - samplerFilterMinmax: %s\n", deviceFeaturesVulkan12.samplerFilterMinmax ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - scalarBlockLayout: %s\n", deviceFeaturesVulkan12.scalarBlockLayout ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - imagelessFramebuffer: %s\n", deviceFeaturesVulkan12.imagelessFramebuffer ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - uniformBufferStandardLayout: %s\n", deviceFeaturesVulkan12.uniformBufferStandardLayout ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderSubgroupExtendedTypes: %s\n", deviceFeaturesVulkan12.shaderSubgroupExtendedTypes ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - separateDepthStencilLayouts: %s\n", deviceFeaturesVulkan12.separateDepthStencilLayouts ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - hostQueryReset: %s\n", deviceFeaturesVulkan12.hostQueryReset ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - timelineSemaphore: %s\n", deviceFeaturesVulkan12.timelineSemaphore ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - bufferDeviceAddress: %s\n", deviceFeaturesVulkan12.bufferDeviceAddress ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - bufferDeviceAddressCaptureReplay: %s\n", deviceFeaturesVulkan12.bufferDeviceAddressCaptureReplay ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - bufferDeviceAddressMultiDevice: %s\n", deviceFeaturesVulkan12.bufferDeviceAddressMultiDevice ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - vulkanMemoryModel: %s\n", deviceFeaturesVulkan12.vulkanMemoryModel ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - vulkanMemoryModelDeviceScope: %s\n", deviceFeaturesVulkan12.vulkanMemoryModelDeviceScope ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - vulkanMemoryModelAvailabilityVisibilityChains: %s\n", deviceFeaturesVulkan12.vulkanMemoryModelAvailabilityVisibilityChains ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderOutputViewportIndex: %s\n", deviceFeaturesVulkan12.shaderOutputViewportIndex ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - shaderOutputLayer: %s\n", deviceFeaturesVulkan12.shaderOutputLayer ? "VK_TRUE" : "VK_FALSE" );
            KEEN_TRACE_INFO( "[graphics] - subgroupBroadcastDynamicId: %s\n", deviceFeaturesVulkan12.subgroupBroadcastDynamicId ? "VK_TRUE" : "VK_FALSE" );

            if( !devicePropertiesVulkan12.shaderSignedZeroInfNanPreserveFloat32 )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because it does not support property 'shaderSignedZeroInfNanPreserveFloat32'!\n" );
                continue;
            }

            if( !layerExtensionInfo.hasExtension( VK_KHR_SWAPCHAIN_EXTENSION_NAME ) )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because it does not support extension '%s'!\n", VK_KHR_SWAPCHAIN_EXTENSION_NAME );
                continue;
            }
            pDeviceInfo->activeDeviceExtensions.pushBack( VK_KHR_SWAPCHAIN_EXTENSION_NAME );

            if( !deviceFeaturesVulkan12.scalarBlockLayout )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because it does not support scalar Block layout!\n" );
                continue;
            }
            pDeviceInfo->deviceFeaturesVulkan12.scalarBlockLayout = VK_TRUE;

            // required for SPIR-V generated by dxc
            // both of those are promoted to core with Vulkan 1.3, so remove this once we made the switch
            if( !layerExtensionInfo.hasExtension( VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME ) )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because it does not support extension '%s'!\n", VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME );
                continue;
            }
            if( !layerExtensionInfo.hasExtension( VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME ) )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because it does not support extension '%s'!\n", VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME );
                continue;
            }
            pDeviceInfo->activeDeviceExtensions.pushBack( VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME );
            pDeviceInfo->activeDeviceExtensions.pushBack( VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME );
            pDeviceInfo->deviceFeaturesShaderDemoteToHelperInvocation.shaderDemoteToHelperInvocation = VK_TRUE; // required to be supported when the extension is supported

            if( !layerExtensionInfo.hasExtension( VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME ) )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because it does not support extension '%s'!\n", VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME );
                continue;
            }
            pDeviceInfo->activeDeviceExtensions.pushBack( VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME );
            pDeviceInfo->deviceFeaturesDynamicRendering.dynamicRendering = VK_TRUE; // required to be supported when the extension is supported

            if( layerExtensionInfo.hasExtension( VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME ) )
            {
                pDeviceInfo->activeDeviceExtensions.pushBack( VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME );
                pDeviceInfo->deviceFeaturesMemoryPriority.memoryPriority = VK_TRUE; // required to be supported when the extension is supported
                vulkan::appendToStructChain( &pDeviceInfo->ppNextFeatures, &pDeviceInfo->deviceFeaturesMemoryPriority );

                pDeviceInfo->isMemoryPrioritySupported = true;
            }

            if( layerExtensionInfo.hasExtension( VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME ) )
            {
                pDeviceInfo->activeDeviceExtensions.pushBack( VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME );
                pDeviceInfo->deviceFeaturesPageableDeviceLocalMemory.pageableDeviceLocalMemory  = VK_TRUE;
                vulkan::appendToStructChain( &pDeviceInfo->ppNextFeatures, &pDeviceInfo->deviceFeaturesPageableDeviceLocalMemory );
            }

#if KEEN_USING( KEEN_GPU_BREADCRUMB_SUPPORT )
#if KEEN_USING( KEEN_TRACE_FEATURE )
            if( layerExtensionInfo.hasExtension( VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME ) )
            {
                pDeviceInfo->activeDeviceExtensions.pushBack( VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME );
            }
            if( layerExtensionInfo.hasExtension( VK_AMD_BUFFER_MARKER_EXTENSION_NAME ) )
            {
                pDeviceInfo->activeDeviceExtensions.pushBack( VK_AMD_BUFFER_MARKER_EXTENSION_NAME );
            }
            if( layerExtensionInfo.hasExtension( VK_EXT_DEVICE_FAULT_EXTENSION_NAME ) )
            {
                pDeviceInfo->activeDeviceExtensions.pushBack( VK_EXT_DEVICE_FAULT_EXTENSION_NAME );
            }
#endif
#endif

#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )
            if( vulkan::s_enableCompiledShaderInfo )
            {
                if( layerExtensionInfo.hasExtension( VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME ) )
                {
                    pDeviceInfo->activeDeviceExtensions.pushBack( VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME );

                    vulkan::appendToStructChain( &pDeviceInfo->ppNextFeatures, &pDeviceInfo->deviceFeaturesPipelineExecutableProperties );

                    pDeviceInfo->deviceFeaturesPipelineExecutableProperties.pipelineExecutableInfo = VK_TRUE;
                }
                if( layerExtensionInfo.hasExtension( VK_AMD_SHADER_INFO_EXTENSION_NAME ) )
                {
                    pDeviceInfo->activeDeviceExtensions.pushBack( VK_AMD_SHADER_INFO_EXTENSION_NAME );
                }
            }
#endif

#if KEEN_USING( KEEN_VULKAN_ROBUST_BUFFER_ACCESS )
            if( layerExtensionInfo.hasExtension( VK_EXT_ROBUSTNESS_2_EXTENSION_NAME ) )
            {
                pDeviceInfo->deviceFeaturesRobustness.robustBufferAccess2   = VK_TRUE;
                pDeviceInfo->deviceFeaturesRobustness.robustImageAccess2    = VK_TRUE;

                vulkan::appendToStructChain( &pDeviceInfo->ppNextFeatures, &pDeviceInfo->deviceFeaturesRobustness );
            }
#endif

#if defined( VK_EXT_memory_budget )
            if( layerExtensionInfo.hasExtension( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME ) )
            {
                pDeviceInfo->activeDeviceExtensions.pushBack( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
            }
#endif

            if( !deviceFeatures.samplerAnisotropy )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'samplerAnisotropy' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.samplerAnisotropy = VK_TRUE;

            pDeviceInfo->deviceFeatures.features.geometryShader     = deviceFeatures.geometryShader;
            pDeviceInfo->deviceFeatures.features.tessellationShader = deviceFeatures.tessellationShader;
            pDeviceInfo->deviceFeatures.features.fillModeNonSolid   = deviceFeatures.fillModeNonSolid;  // :LF: I guess we only need that for debugging, so put a #ifndef KEEN_BUILD_MASTER around this and make sure that we never set VK_POLYGON_MODE_LINE ?

            if( vulkan::s_robustBufferAccess && deviceFeatures.robustBufferAccess )
            {
                pDeviceInfo->deviceFeatures.features.robustBufferAccess = VK_TRUE;
            }

#if defined( KEEN_PLATFORM_WIN32 ) || defined( KEEN_PLATFORM_NX ) || defined( KEEN_PLATFORM_LINUX )
            if( !deviceFeatures.textureCompressionBC )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'textureCompressionBC' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.textureCompressionBC = VK_TRUE;
#endif

            if( !deviceFeatures.fragmentStoresAndAtomics )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'fragmentStoresAndAtomics' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.fragmentStoresAndAtomics = VK_TRUE;

            // :JW: required by amd fidelityfx cacao
            if( !deviceFeatures.shaderImageGatherExtended )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'shaderImageGatherExtended' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.shaderImageGatherExtended = VK_TRUE;

#if defined( KEEN_PLATFORM_WIN32 )
            // :JK: required by renderdoc
            if( !deviceFeatures.depthClamp )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'depthClamp' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.depthClamp = VK_TRUE;
#endif

            if( !deviceFeatures.multiDrawIndirect )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'multiDrawIndirect' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.multiDrawIndirect = VK_TRUE;

            if( !deviceFeatures.drawIndirectFirstInstance )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'drawIndirectFirstInstance' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.drawIndirectFirstInstance = VK_TRUE;

            if( !deviceFeatures.imageCubeArray )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'imageCubeArray' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.imageCubeArray = VK_TRUE;

            if( !deviceFeatures.shaderStorageImageExtendedFormats )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'shaderStorageImageExtendedFormats' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.shaderStorageImageExtendedFormats = VK_TRUE;

            if( !deviceFeatures.shaderStorageImageReadWithoutFormat )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'shaderStorageImageReadWithoutFormat' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.shaderStorageImageReadWithoutFormat = VK_TRUE;

            if( !deviceFeatures.shaderStorageImageWriteWithoutFormat )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'shaderStorageImageWriteWithoutFormat' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeatures.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;

            pDeviceInfo->deviceFeatures.features.shaderInt16    = deviceFeatures.shaderInt16; // We need this for FSR support
            pDeviceInfo->deviceFeaturesVulkan12.shaderFloat16   = deviceFeaturesVulkan12.shaderFloat16; // We need this for FSR support

#if KEEN_USING( KEEN_VULKAN_VALIDATION )
            // :FK: We need this for Gpu assisted validation for validating robust buffer access
            pDeviceInfo->deviceFeaturesVulkan12.uniformAndStorageBuffer8BitAccess = deviceFeaturesVulkan12.uniformAndStorageBuffer8BitAccess;
#endif

            if( !deviceFeaturesVulkan11.shaderDrawParameters )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'shaderDrawParameters' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeaturesVulkan11.shaderDrawParameters = VK_TRUE;

            if( !deviceFeaturesVulkan12.drawIndirectCount )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'drawIndirectCount' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeaturesVulkan12.drawIndirectCount = VK_TRUE;

            if( !deviceFeaturesVulkan12.descriptorIndexing )
            {
                KEEN_TRACE_INFO( "[graphics] skipping device because 'descriptorIndexing' is not supported!\n" );
                continue;
            }
            pDeviceInfo->deviceFeaturesVulkan12.descriptorIndexing = VK_TRUE;

            // required to be supported when descriptorIndexing is supported
            pDeviceInfo->deviceFeaturesVulkan12.descriptorBindingPartiallyBound             = VK_TRUE;
            pDeviceInfo->deviceFeaturesVulkan12.descriptorBindingUpdateUnusedWhilePending   = VK_TRUE;
            pDeviceInfo->deviceFeaturesVulkan12.shaderSampledImageArrayNonUniformIndexing   = VK_TRUE;
            pDeviceInfo->deviceFeaturesVulkan12.shaderStorageBufferArrayNonUniformIndexing  = VK_TRUE;
            pDeviceInfo->deviceFeaturesVulkan12.shaderStorageImageArrayNonUniformIndexing   = VK_TRUE;
            pDeviceInfo->deviceFeaturesVulkan12.samplerFilterMinmax                         = deviceFeaturesVulkan12.samplerFilterMinmax;

            // required to be supported for Vulkan version >= 1.2
            pDeviceInfo->deviceFeaturesVulkan12.hostQueryReset      = VK_TRUE;
            pDeviceInfo->deviceFeaturesVulkan12.timelineSemaphore   = VK_TRUE;

            pDeviceInfo->deviceFeatures.features.shaderInt64    = deviceFeatures.shaderInt64;

            pDeviceInfo->isSupported = true;
        }

        Optional<size_t> selectedPhysicalDeviceIndex;
        if( forcePhysicalDeviceIndex.isSet() )
        {
            if( forcePhysicalDeviceIndex.get() < physicalDevices.getCount() )
            {
                const PhysicalDeviceInfo* pDeviceInfo = &physicalDeviceInfo[ forcePhysicalDeviceIndex.get() ];

                if( !pDeviceInfo->isSupported )
                {
                    KEEN_TRACE_WARNING( "[graphics] force selected device index %d is not supported, but using it anyways\n", forcePhysicalDeviceIndex.get() );
                }

                selectedPhysicalDeviceIndex = forcePhysicalDeviceIndex.get();
            }
            else
            {
                KEEN_TRACE_ERROR( "[graphics] force selected device index is not valid\n" );
            }
        }
        else
        {
            for( size_t i = 0u; i < physicalDevices.getCount(); ++i )
            {
                const PhysicalDeviceInfo* pDeviceInfo = &physicalDeviceInfo[ i ];

                if( !pDeviceInfo->isSupported )
                {
                    continue;
                }

                bool selectDevice = false;

                // 1. use the first supported device
                if( selectedPhysicalDeviceIndex.isClear() )
                {
                    selectDevice = true;
                }

                // 2. prefer a discrete gpu
                if( selectedPhysicalDeviceIndex.isSet() )
                {
                    const PhysicalDeviceInfo* pSelectedDeviceInfo = &physicalDeviceInfo[ selectedPhysicalDeviceIndex.get() ];

                    if( pDeviceInfo->deviceProperties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU )
                    {
                        selectDevice = true;
                    }
                }

                if( selectDevice )
                {
                    selectedPhysicalDeviceIndex = i;
                }
            }
        }
        if( selectedPhysicalDeviceIndex.isClear() )
        {
            KEEN_TRACE_ERROR( "[graphics] No usable Vulkan device found!\n" );
            return ErrorId_NotFound;
        }

        m_physicalDevice = physicalDevices[ selectedPhysicalDeviceIndex.get() ];

        const PhysicalDeviceInfo* pSelectedDeviceInfo = &physicalDeviceInfo[ selectedPhysicalDeviceIndex.get() ];
        m_sharedData.deviceProperties               = pSelectedDeviceInfo->deviceProperties.properties;
        m_sharedData.deviceProperties_1_1           = pSelectedDeviceInfo->devicePropertiesVulkan11;
        m_sharedData.deviceProperties_1_1.pNext     = nullptr;  // this should never be used, so reset it to be safe
        m_sharedData.deviceProperties_1_2           = pSelectedDeviceInfo->devicePropertiesVulkan12;
        m_sharedData.deviceProperties_1_2.pNext     = nullptr;  // this should never be used, so reset it to be safe
        m_sharedData.info.isMemoryPrioritySupported = pSelectedDeviceInfo->isMemoryPrioritySupported;

        // check for old drivers:
        if( pSelectedDeviceInfo->devicePropertiesVulkan12.driverID == VK_DRIVER_ID_AMD_PROPRIETARY )
        {
            const Result<uint32> oldestAcceptableAdrenalinVersion = encodeAdrenalinDriverVersion( 24u, 7u, 1u );    // 24.7.1
            KEEN_ASSERT( oldestAcceptableAdrenalinVersion.isOk() );

            const StringView driverInfo = pSelectedDeviceInfo->devicePropertiesVulkan12.driverInfo;
            const Result<uint32> adrenalinVersionResult = parseAdrenalinDriverVersionFromDriverInfo( driverInfo );
            if( adrenalinVersionResult.hasError() )
            {
                KEEN_TRACE_INFO( "[vulkan] could not parse AMD proprietary driver info\n" );
            }
            else if( adrenalinVersionResult.getValue() < oldestAcceptableAdrenalinVersion.getValue() )
            {
                KEEN_TRACE_INFO( "[vulkan] Old Adrenalin version detected with version 0x%08x\n", adrenalinVersionResult.getValue() );
                m_sharedData.info.hasOldDriver = true;
            }
            else
            {
                KEEN_TRACE_INFO( "[vulkan] Up to date Adrenalin version detected with version 0x%08x\n", adrenalinVersionResult.getValue() );
            }
        }
        else if( pSelectedDeviceInfo->devicePropertiesVulkan12.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY )
        {
            const uint32 driverVersion = pSelectedDeviceInfo->deviceProperties.properties.driverVersion;

            const uint32 oldestAcceptableDriverVersion = encodeNvidiaDriverVersion( 551u, 86u );

            if( driverVersion < oldestAcceptableDriverVersion )
            {
                KEEN_TRACE_INFO( "[vulkan] Old Nvidia proprietary driver detected with driver version 0x%08x\n", driverVersion );
                m_sharedData.info.hasOldDriver = true;
            }
            else
            {
                KEEN_TRACE_INFO( "[vulkan] Up to date Nvidia proprietary driver detected with driver version 0x%08x\n", driverVersion );
            }
        }
        else if( pSelectedDeviceInfo->devicePropertiesVulkan12.driverID == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS )
        {
            const uint32 driverVersion = pSelectedDeviceInfo->deviceProperties.properties.driverVersion;

            const uint32 oldestAcceptableDriverVersion = encodeIntelDriverVersion( 101u, 5768u );

            if( driverVersion < oldestAcceptableDriverVersion )
            {
                KEEN_TRACE_INFO( "[vulkan] Old Intel proprietary driver detected with driver version 0x%08x\n", driverVersion );
                m_sharedData.info.hasOldDriver = true;
            }
            else
            {
                KEEN_TRACE_INFO( "[vulkan] Up to date Intel proprietary driver detected with driver version 0x%08x\n", driverVersion );
            }
        }

        if( pSelectedDeviceInfo->deviceFeatures.features.geometryShader )
        {
            m_sharedData.info.optionalShaderStages |= GraphicsOptionalShaderStageFlag::GeometryShader;
        }
        if( pSelectedDeviceInfo->deviceFeatures.features.tessellationShader )
        {
            m_sharedData.info.optionalShaderStages |= GraphicsOptionalShaderStageFlag::TessellationShaders;
        }

        if( pSelectedDeviceInfo->deviceFeatures.features.shaderInt16 != 0u )
        {
            m_sharedData.info.isFsr3Supported = true;
        }

        m_sharedData.deviceFeatures                         = pSelectedDeviceInfo->deviceFeatures.features;
        m_sharedData.deviceFeatures_1_1                     = pSelectedDeviceInfo->deviceFeaturesVulkan11;
        m_sharedData.deviceFeatures_1_1.pNext               = nullptr;  // this should never be used, so reset it to be safe
        m_sharedData.deviceFeatures_1_2                     = pSelectedDeviceInfo->deviceFeaturesVulkan12;
        m_sharedData.deviceFeatures_1_2.pNext               = nullptr;  // this should never be used, so reset it to be safe

        m_sharedData.info.timestampPeriod                   = m_sharedData.deviceProperties.limits.timestampPeriod;

        {
            VkPhysicalDeviceMemoryProperties2 deviceMemoryProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
            m_pVulkan->vkGetPhysicalDeviceMemoryProperties2( m_physicalDevice, &deviceMemoryProperties );
            m_sharedData.deviceMemoryProperties = deviceMemoryProperties.memoryProperties;
        }

        KEEN_ASSERT( m_sharedData.deviceProperties_1_1.maxMemoryAllocationSize >= GraphicsLimits_MaxDeviceMemorySizeInBytes );
        m_sharedData.info.maxDeviceMemorySizeInBytes = m_sharedData.deviceProperties_1_1.maxMemoryAllocationSize;

        // :NOTE: :FK: Check if resizeable BAR support is enabled!
        if( m_sharedData.deviceProperties.vendorID == vulkan::VendorId_Intel )
        {
            const VkPhysicalDeviceMemoryProperties *memoryProperties = &m_sharedData.deviceMemoryProperties;

            // find largest heap with DEVICE_LOCAL_BIT memory type
            uint32_t     largestDeviceLocalHeapIndex = UINT32_MAX;
            VkDeviceSize largestDeviceLocalHeapSize = 0;
            for( uint32_t i = 0; i < memoryProperties->memoryTypeCount; ++i )
            {
                if( memoryProperties->memoryTypes[ i ].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT && memoryProperties->memoryHeaps[ memoryProperties->memoryTypes[ i ].heapIndex ].size > largestDeviceLocalHeapSize )
                {
                    largestDeviceLocalHeapIndex = memoryProperties->memoryTypes[ i ].heapIndex;
                    largestDeviceLocalHeapSize = memoryProperties->memoryHeaps[ memoryProperties->memoryTypes[ i ].heapIndex ].size;
                }
            }

            bool resizableBAREnabled = false;
            // check if that heap is also HOST_VISIBLE. I am not sure if HOST_COHERENT or HOST_CACHED are strictly necessary for resizable BAR
            for( uint32_t i = 0; i < memoryProperties->memoryTypeCount; ++i )
            {
                if( memoryProperties->memoryTypes[ i ].heapIndex == largestDeviceLocalHeapIndex && memoryProperties->memoryTypes[ i ].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
                {
                    resizableBAREnabled = true;
                }
            }

            KEEN_TRACE_INFO( "[vulkan] Intel GPU detected. Resizable BAR support %s.\n", resizableBAREnabled ? "enabled" : "disabled. This is likely wrong as it is a basic requirement for the vulkan driver" );
        }

        KEEN_TRACE_INFO( "[graphics] selected device %d (%s) for rendering!\n", selectedPhysicalDeviceIndex.get(), m_sharedData.deviceProperties.deviceName );

#if KEEN_USING( KEEN_OS_CRASH )
        KEEN_VERIFY( os::addCrashDumpUserInfo( "gpu_driver", createStringView( m_sharedData.deviceProperties_1_2.driverInfo ) ) );
#endif

        m_sharedData.info.maxTextureDimension1d     = m_sharedData.deviceProperties.limits.maxImageDimension1D;
        m_sharedData.info.maxTextureDimension2d     = m_sharedData.deviceProperties.limits.maxImageDimension2D;
        m_sharedData.info.maxTextureDimension3d     = m_sharedData.deviceProperties.limits.maxImageDimension3D;
        m_sharedData.info.maxTextureDimensionCube   = m_sharedData.deviceProperties.limits.maxImageDimensionCube;
        m_sharedData.info.maxFramebufferDimension   = min( m_sharedData.deviceProperties.limits.maxFramebufferWidth, m_sharedData.deviceProperties.limits.maxFramebufferHeight );
        m_sharedData.info.maxDispatchGroupCount     = uint3{ m_sharedData.deviceProperties.limits.maxComputeWorkGroupCount[ 0u ], m_sharedData.deviceProperties.limits.maxComputeWorkGroupCount[ 1u ], m_sharedData.deviceProperties.limits.maxComputeWorkGroupCount[ 2u ] };

        m_sharedData.info.minUniformBufferOffsetAlignment           = m_sharedData.deviceProperties.limits.minUniformBufferOffsetAlignment;
        m_sharedData.info.minStorageBufferOffsetAlignment           = m_sharedData.deviceProperties.limits.minStorageBufferOffsetAlignment;
        m_sharedData.info.minVertexBufferOffsetAlignment            = 4u; //DataCacheLineSize;
        m_sharedData.info.minIndexBufferOffsetAlignment             = 4u; //DataCacheLineSize;
        m_sharedData.info.minArgumentBufferOffsetAlignment          = 4u; //DataCacheLineSize;
        m_sharedData.info.minBufferTextureCopyBufferOffsetAlignment = m_sharedData.deviceProperties.limits.optimalBufferCopyOffsetAlignment;

#if !defined( KEEN_BUILD_MASTER )
        if( vulkan::s_testWorstCaseOffsetAlignments )
        {
            // according to vulkan spec https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap48.html#limits-minmax
            m_sharedData.info.minUniformBufferOffsetAlignment       = GraphicsLimits_MaxUniformBufferOffsetAlignment;
            m_sharedData.info.minStorageBufferOffsetAlignment       = GraphicsLimits_MaxStorageBufferOffsetAlignment;
        }
#endif

        m_sharedData.info.supportedFeatures.setIf( GraphicsFeature::SamplerReduction, m_sharedData.deviceFeatures_1_2.samplerFilterMinmax == VK_TRUE );
        m_sharedData.info.supportedFeatures.setIf( GraphicsFeature::DepthResolveModeMinMax, isBitmaskSet( m_sharedData.deviceProperties_1_2.supportedDepthResolveModes, VK_RESOLVE_MODE_MIN_BIT | VK_RESOLVE_MODE_MAX_BIT ) );
        m_sharedData.info.supportedFeatures.setIf( GraphicsFeature::DepthResolveModeAverage, isBitmaskSet( m_sharedData.deviceProperties_1_2.supportedDepthResolveModes, VK_RESOLVE_MODE_AVERAGE_BIT ) );
        m_sharedData.info.supportedFeatures.setIf( GraphicsFeature::ShaderFloat16, m_sharedData.deviceFeatures_1_2.shaderFloat16 == VK_TRUE );

        m_sharedData.info.subgroupSize = m_sharedData.deviceProperties_1_1.subgroupSize;

        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_1_BIT == 1u );
        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_2_BIT == 2u );
        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_4_BIT == 4u );
        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_8_BIT == 8u );
        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_16_BIT == 16u );
        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_32_BIT == 32u );
        KEEN_STATIC_ASSERT( VK_SAMPLE_COUNT_64_BIT == 64u );
        for( uint32 i = 1u; i <= 64u; i *= 2u )
        {
            if( isBitmaskSet( m_sharedData.deviceProperties.limits.framebufferColorSampleCounts, i ) &&
                isBitmaskSet( m_sharedData.deviceProperties.limits.framebufferDepthSampleCounts, i ) )
            {
                m_sharedData.info.supportedSampleCountMask |= i;
            }
        }

        if( m_sharedData.deviceProperties_1_1.deviceLUIDValid )
        {
            uint64* luid = m_sharedData.info.luid.set();
            KEEN_STATIC_ASSERT( sizeof( *luid ) == sizeof( m_sharedData.deviceProperties_1_1.deviceLUID ) );
            copyMemoryNonOverlapping( luid, m_sharedData.deviceProperties_1_1.deviceLUID, sizeof( *luid ) );
        }
        m_sharedData.info.gpuIdentifier.assign( createStringView( m_sharedData.deviceProperties.deviceName ) );

        KEEN_ASSERT( m_sharedData.info.totalMemorySize == 0u );
        KEEN_ASSERT( m_sharedData.info.dedicatedMemorySize == 0u );
        for( size_t i = 0u; i < m_sharedData.deviceMemoryProperties.memoryHeapCount; ++i )
        {
            m_sharedData.info.totalMemorySize += m_sharedData.deviceMemoryProperties.memoryHeaps[ i ].size;
            if( m_sharedData.deviceMemoryProperties.memoryHeaps[ i ].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT )
            {
                m_sharedData.info.dedicatedMemorySize += m_sharedData.deviceMemoryProperties.memoryHeaps[ i ].size;
            }
        }
        for( size_t i = 0u; i < m_sharedData.deviceMemoryProperties.memoryTypeCount; ++i )
        {
            GraphicsDeviceMemoryTypeInfo* pInfo = &m_sharedData.memoryTypeInfos[ i ];

            const VkMemoryType& vulkanType = m_sharedData.deviceMemoryProperties.memoryTypes[ i ];
            const VkMemoryHeap& vulkanHeap = m_sharedData.deviceMemoryProperties.memoryHeaps[ vulkanType.heapIndex ];

            pInfo->totalSizeInBytes = vulkanHeap.size;

            pInfo->flags.setIf( GraphicsDeviceMemoryTypeFlag::GpuLocal, isBitmaskSet( vulkanType.propertyFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) );
            pInfo->flags.setIf( GraphicsDeviceMemoryTypeFlag::CpuAccessible, isBitmaskSet( vulkanType.propertyFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) );
            pInfo->flags.setIf( GraphicsDeviceMemoryTypeFlag::CpuCached, isBitmaskSet( vulkanType.propertyFlags, VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) );
            pInfo->flags.setIf( GraphicsDeviceMemoryTypeFlag::CpuCoherent, isBitmaskSet( vulkanType.propertyFlags, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) );
        }
        m_sharedData.info.memoryTypes = m_sharedData.memoryTypeInfos.getPartTo( m_sharedData.deviceMemoryProperties.memoryTypeCount );

        if( m_sharedData.deviceProperties_1_2.driverID == VK_DRIVER_ID_AMD_PROPRIETARY )
        {
#if KEEN_USING( KEEN_GPU_PROFILER )
            KEEN_TRACE_WARNING( "[graphics] bugged amd driver detected, gpu profiler might return incomplete values\n" );
#endif
            m_sharedData.isVkGetQueryPoolResultsBrokenOnAmdDeviceLost = true; // vkGetQueryPoolResults might freeze forever with AMD driver version 23.7.2 on device lost (as reported by vulkan devicePropertiesVulkan12.driverInfo)
        }

        if( !prepareQueueCreationInfo( pWindowSystem ) )
        {
            return ErrorId_InvalidState;
        }

        KEEN_ASSERT( m_queueCreateInfos.hasElements() );

        VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceCreateInfo.pNext                      = &pSelectedDeviceInfo->deviceFeatures;
        deviceCreateInfo.queueCreateInfoCount       = rangecheck_cast<uint32>( m_queueCreateInfos.getCount() );
        deviceCreateInfo.pQueueCreateInfos          = m_queueCreateInfos.getStart();
        deviceCreateInfo.enabledLayerCount          = 0u;
        deviceCreateInfo.ppEnabledLayerNames        = nullptr;
        deviceCreateInfo.enabledExtensionCount      = rangecheck_cast<uint32>( pSelectedDeviceInfo->activeDeviceExtensions.getCount() );
        deviceCreateInfo.ppEnabledExtensionNames    = pSelectedDeviceInfo->activeDeviceExtensions.getStart();
        deviceCreateInfo.pEnabledFeatures           = nullptr;

        VulkanResult result = m_pVulkan->vkCreateDevice( m_physicalDevice, &deviceCreateInfo, m_sharedData.pVulkanAllocationCallbacks, &m_device );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] vkCreateDevice failed with error '%s'\n", result );
            return result.getErrorId();
        }
        KEEN_ASSERT( m_device != VK_NULL_HANDLE );

        KEEN_TRACE_INFO( "[graphics] Created Vulkan device!\n" );

        ErrorId error = vulkan::loadDeviceFunctions( m_pVulkan, m_physicalDevice, m_device, pSelectedDeviceInfo->activeDeviceExtensions.getConstView() );
        if( error != ErrorId_Ok )
        {
            return error;
        }

        m_pVulkan->vkGetDeviceQueue( m_device, m_sharedData.graphicsQueueFamilyIndex, (uint32)m_graphicsQueueIndex, &m_sharedData.graphicsQueue );
        if( m_sharedData.graphicsQueue == VK_NULL_HANDLE )
        {
            KEEN_TRACE_ERROR( "Could not get graphics queue from vulkan device (family:%d queue:%d)\n", m_sharedData.graphicsQueueFamilyIndex, m_graphicsQueueIndex );
            return ErrorId_InvalidState;
        }
        vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)m_sharedData.graphicsQueue, VK_OBJECT_TYPE_QUEUE, "GraphicsQueue"_debug );

        KEEN_ASSERT( m_sharedData.graphicsQueueFamilyIndex == m_sharedData.presentQueueFamilyIndex );
        m_sharedData.presentQueue = m_sharedData.graphicsQueue;

        if( m_sharedData.computeQueueFamilyIndex == m_sharedData.graphicsQueueFamilyIndex )
        {
            m_sharedData.computeQueue = m_sharedData.graphicsQueue;
        }
        else
        {
            m_pVulkan->vkGetDeviceQueue( m_device, m_sharedData.computeQueueFamilyIndex, (uint32)m_computeQueueIndex, &m_sharedData.computeQueue );

            if( m_sharedData.computeQueue == VK_NULL_HANDLE )
            {
                KEEN_TRACE_ERROR( "Could not get compute queue from vulkan device (family:%d queue:%d)\n", m_sharedData.computeQueueFamilyIndex, m_computeQueueIndex );
                return ErrorId_InvalidState;
            }
            vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)m_sharedData.graphicsQueue, VK_OBJECT_TYPE_QUEUE, "ComputeQueue"_debug );
        }

        if( m_sharedData.transferQueueFamilyIndex != m_sharedData.queueFamilyProperties.getCount() )
        {
            m_pVulkan->vkGetDeviceQueue( m_device, m_sharedData.transferQueueFamilyIndex, (uint32)m_transferQueueIndex, &m_sharedData.transferQueue );
            if( m_sharedData.transferQueue == VK_NULL_HANDLE )
            {
                KEEN_TRACE_ERROR( "Could not get transfer queue from vulkan device (family:%d queue:%d)\n", m_sharedData.transferQueueFamilyIndex, m_transferQueueIndex );
                return ErrorId_InvalidState;
            }
            vulkan::setObjectName( m_pVulkan, m_device, (VkObjectHandle)m_sharedData.graphicsQueue, VK_OBJECT_TYPE_QUEUE, "TransferQueue"_debug );

            // create transfer queue data:
            SynchronizedDataWriteLock<VulkanTransferQueue> sharedDataLock( &m_transferQueue );
            VulkanTransferQueue* pTransferQueue = sharedDataLock.getData();

            pTransferQueue->nextId  = { 1u };
            pTransferQueue->batches.create( m_pAllocator, GraphicsLimits_MaxTransferSubmitCount );
            pTransferQueue->freeBatchIndices.create( m_pAllocator, GraphicsLimits_MaxTransferSubmitCount );

            for( uint32 batchIndex = 0u; batchIndex < GraphicsLimits_MaxTransferSubmitCount; ++batchIndex )
            {
                VulkanTransferBatch* pBatch = &pTransferQueue->batches[ batchIndex ];

                VkCommandPoolCreateInfo commandPoolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
                commandPoolCreateInfo.queueFamilyIndex  = m_sharedData.transferQueueFamilyIndex;
                commandPoolCreateInfo.flags             = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                result = m_pVulkan->vkCreateCommandPool( m_device, &commandPoolCreateInfo, m_sharedData.pVulkanAllocationCallbacks, &pBatch->commandPool );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkCreateCommandPool failed with error '%s'\n", result );
                    return ErrorId_InvalidState;
                }

                VkCommandBufferAllocateInfo commandBufferAllocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
                commandBufferAllocateInfo.commandPool           = pBatch->commandPool;
                commandBufferAllocateInfo.level                 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                commandBufferAllocateInfo.commandBufferCount    = 1u;

                result = m_pVulkan->vkAllocateCommandBuffers( m_device, &commandBufferAllocateInfo, &pBatch->commandBuffer );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkAllocateCommandBuffers failed with error '%s'\n", result );
                    return ErrorId_InvalidState;
                }

                VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                result = m_pVulkan->vkCreateFence( m_device, &fenceCreateInfo, m_sharedData.pVulkanAllocationCallbacks, &pBatch->fence );
                if( result.hasError() )
                {
                    KEEN_TRACE_ERROR( "[graphics] vkCreateFence failed with error '%s'\n", result );
                    return ErrorId_InvalidState;
                }
            }

            for( uint32 batchIndex = 0u; batchIndex < GraphicsLimits_MaxTransferSubmitCount; ++batchIndex )
            {
                pTransferQueue->freeBatchIndices.pushBack( GraphicsLimits_MaxTransferSubmitCount - batchIndex - 1u );
            }
        }

        return ErrorId_Ok;
    }

    void VulkanGraphicsDevice::destroyDevice()
    {
        {
            SynchronizedDataWriteLock<VulkanTransferQueue> sharedDataLock( &m_transferQueue );
            VulkanTransferQueue* pTransferQueue = sharedDataLock.getData();

            for( uint32 batchIndex = 0u; batchIndex < pTransferQueue->batches.getCount(); ++batchIndex )
            {
                VulkanTransferBatch* pBatch = &pTransferQueue->batches[ batchIndex ];

                if( pBatch->fence != VK_NULL_HANDLE )
                {
                    m_pVulkan->vkDestroyFence( m_device, pBatch->fence, m_sharedData.pVulkanAllocationCallbacks );
                    pBatch->fence = VK_NULL_HANDLE;
                }

                if( pBatch->commandBuffer != VK_NULL_HANDLE )
                {
                    m_pVulkan->vkFreeCommandBuffers( m_device, pBatch->commandPool, 1u, &pBatch->commandBuffer );
                    pBatch->commandBuffer = VK_NULL_HANDLE;
                }

                if( pBatch->commandPool != VK_NULL_HANDLE )
                {
                    m_pVulkan->vkDestroyCommandPool( m_device, pBatch->commandPool, m_sharedData.pVulkanAllocationCallbacks );
                    pBatch->commandPool = VK_NULL_HANDLE;
                }
            }
            pTransferQueue->freeBatchIndices.destroy();
            pTransferQueue->batches.destroy();
        }

        m_sharedData.presentQueue = VK_NULL_HANDLE;
        m_sharedData.graphicsQueue = VK_NULL_HANDLE;
        m_sharedData.computeQueue = VK_NULL_HANDLE;
        m_sharedData.queueFamilyProperties.destroy();
        if( m_device != VK_NULL_HANDLE )
        {
            m_pVulkan->vkDestroyDevice( m_device, m_sharedData.pVulkanAllocationCallbacks );
            m_device = VK_NULL_HANDLE;
        }
    }

    bool VulkanGraphicsDevice::prepareQueueCreationInfo( OsWindowSystem* pWindowSystem )
    {
        uint32 queueFamilyCount = 0u;
        m_pVulkan->vkGetPhysicalDeviceQueueFamilyProperties( m_physicalDevice, &queueFamilyCount, nullptr );
        if( !m_sharedData.queueFamilyProperties.tryCreate( m_pAllocator, queueFamilyCount ) )
        {
            return false;
        }

        m_pVulkan->vkGetPhysicalDeviceQueueFamilyProperties( m_physicalDevice, &queueFamilyCount, m_sharedData.queueFamilyProperties.getStart() );
        KEEN_ASSERT( queueFamilyCount >= 1 );

        KEEN_TRACE_INFO( "[graphics] Found %d Queue Families!\n", queueFamilyCount );

        // search for a queue that supports graphics and presenting to our surface:
        size_t combinedQueueFamilyIndex = m_sharedData.queueFamilyProperties.getSize();

        size_t computeQueueFamilyIndex  = m_sharedData.queueFamilyProperties.getSize();
        size_t dedicatedComputeQueueFamilyIndex = m_sharedData.queueFamilyProperties.getSize();

        size_t dedicatedTransferQueueFamilyIndex = m_sharedData.queueFamilyProperties.getSize();
        size_t transferQueueFamilyIndex = m_sharedData.queueFamilyProperties.getSize();

        for( size_t queueFamilyIndex = 0u; queueFamilyIndex < m_sharedData.queueFamilyProperties.getSize(); ++queueFamilyIndex )
        {
            VkBool32 supportsPresent;
#if defined( KEEN_PLATFORM_WIN32 )
            supportsPresent = m_pVulkan->vkGetPhysicalDeviceWin32PresentationSupportKHR( m_physicalDevice, (uint32)queueFamilyIndex );
#elif defined( KEEN_PLATFORM_LINUX )
            if( pWindowSystem != nullptr )
            {
                OsWindowSystemInfo windowSystemInfo;
                KEEN_VERIFY( os::getWindowSystemInfo( &windowSystemInfo, pWindowSystem ) );
#   if KEEN_USING( KEEN_OS_USE_WAYLAND )
                supportsPresent = m_pVulkan->vkGetPhysicalDeviceWaylandPresentationSupportKHR( m_physicalDevice, (uint32)queueFamilyIndex,
                    (wl_display*)windowSystemInfo.pDisplay );
#   else
                supportsPresent = m_pVulkan->vkGetPhysicalDeviceXlibPresentationSupportKHR( m_physicalDevice, (uint32)queueFamilyIndex,
                    (Display*)windowSystemInfo.pDisplay, windowSystemInfo.visualID );
#   endif
            }
            else
            {
                supportsPresent = false;
            }
#else
#   error Not Implemented
#endif
            const bool supportsGraphics = m_sharedData.queueFamilyProperties[ queueFamilyIndex ].queueFlags & VK_QUEUE_GRAPHICS_BIT;
            const bool supportsCompute = m_sharedData.queueFamilyProperties[ queueFamilyIndex ].queueFlags & VK_QUEUE_COMPUTE_BIT;
            const bool supportsTransfer = m_sharedData.queueFamilyProperties[ queueFamilyIndex ].queueFlags & VK_QUEUE_TRANSFER_BIT;

            KEEN_TRACE_INFO( "[graphics] Vulkan Queue Family #%zu: flags:%#b%s%s%s count:%d timeStampValidBits:%d\n",
                queueFamilyIndex,
                m_sharedData.queueFamilyProperties[ queueFamilyIndex ].queueFlags,
                supportsGraphics ? " GRAPHICS" : "", supportsCompute ? " COMPUTE" : "", supportsTransfer ? " TRANSFER" : "",
                m_sharedData.queueFamilyProperties[ queueFamilyIndex ].queueCount,
                m_sharedData.queueFamilyProperties[ queueFamilyIndex ].timestampValidBits );

            if( supportsGraphics && supportsCompute && ( supportsPresent || pWindowSystem == nullptr ) && combinedQueueFamilyIndex == m_sharedData.queueFamilyProperties.getSize() )
            {
                combinedQueueFamilyIndex = queueFamilyIndex;
            }

            if( supportsCompute && !supportsGraphics )
            {
                dedicatedComputeQueueFamilyIndex = queueFamilyIndex;
            }

            if( supportsTransfer && !supportsGraphics )
            {
                dedicatedTransferQueueFamilyIndex = queueFamilyIndex;
            }
        }

        if( combinedQueueFamilyIndex == m_sharedData.queueFamilyProperties.getSize() )
        {
            KEEN_TRACE_ERROR( "[graphics] No vulkan queue family found which supports graphics, compute and presentation into our surface!\n" );
            return false;
        }
        if( dedicatedTransferQueueFamilyIndex != m_sharedData.queueFamilyProperties.getSize() )
        {
            // use the dedicated transfer queue:
            transferQueueFamilyIndex = dedicatedTransferQueueFamilyIndex;
        }
        else
        {
            // use the combined family queue as fall back for batches:
            transferQueueFamilyIndex = combinedQueueFamilyIndex;
        }

        if( dedicatedComputeQueueFamilyIndex != m_sharedData.queueFamilyProperties.getSize() )
        {
            computeQueueFamilyIndex = dedicatedComputeQueueFamilyIndex;
        }
        else
        {
            // use the combined family queue as fall back for compute:
            computeQueueFamilyIndex = combinedQueueFamilyIndex;
        }

        if( computeQueueFamilyIndex == m_sharedData.queueFamilyProperties.getSize() )
        {
            KEEN_TRACE_ERROR( "[graphics] No vulkan queue familiy found which supports compute!\n" );
            return false;
        }

        m_sharedData.computeQueueFamilyIndex = (uint32)computeQueueFamilyIndex;
        KEEN_TRACE_INFO( "[graphics] Selected Queue Family #%zu for compute\n", m_sharedData.computeQueueFamilyIndex );

        m_sharedData.graphicsQueueFamilyIndex   = (uint32)combinedQueueFamilyIndex;
        m_sharedData.presentQueueFamilyIndex    = (uint32)combinedQueueFamilyIndex;
        m_sharedData.transferQueueFamilyIndex   = (uint32)transferQueueFamilyIndex;

        m_sharedData.queueInfos.mainQueueFamilyIndex        = (uint32)combinedQueueFamilyIndex;
        m_sharedData.queueInfos.transferQueueFamilyIndex    = (uint32)transferQueueFamilyIndex;

        KEEN_TRACE_INFO( "[graphics] Selected Queue Family #%zu for graphics\n", m_sharedData.graphicsQueueFamilyIndex );
        KEEN_TRACE_INFO( "[graphics] Selected Queue Family #%zu for presentation\n", m_sharedData.presentQueueFamilyIndex );
        KEEN_TRACE_INFO( "[graphics] Selected Queue Family #%zu for transfer\n", m_sharedData.transferQueueFamilyIndex );

        m_queuePriorities.createZero( m_pAllocator, m_sharedData.queueFamilyProperties.getSize() );

        constexpr float transferQueuePriority = 0.0f;
        constexpr float graphicsQueuePriority = 1.0f;
        constexpr float computeQueuePriority = 1.0f;

        {
            // add main graphics queue:
            m_graphicsQueueIndex = m_queuePriorities[ m_sharedData.graphicsQueueFamilyIndex ].getSize();
            m_queuePriorities[ m_sharedData.graphicsQueueFamilyIndex ].pushBack( graphicsQueuePriority );
        }

        if( m_sharedData.computeQueueFamilyIndex != m_sharedData.graphicsQueueFamilyIndex )
        {
            m_computeQueueIndex = m_queuePriorities[ m_sharedData.computeQueueFamilyIndex ].getSize();
            m_queuePriorities[ m_sharedData.computeQueueFamilyIndex ].pushBack( computeQueuePriority );
        }

        if( m_sharedData.transferQueueFamilyIndex != m_sharedData.graphicsQueueFamilyIndex )
        {
            m_transferQueueIndex = m_queuePriorities[ m_sharedData.transferQueueFamilyIndex ].getSize();
            m_queuePriorities[ m_sharedData.transferQueueFamilyIndex ].pushBack( transferQueuePriority );
        }

        for( size_t i = 0; i < m_queuePriorities.getSize(); ++i )
        {
            if( m_queuePriorities[ i ].isEmpty() )
            {
                continue;
            }
            VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueCreateInfo.queueFamilyIndex    = (uint32)i;
            queueCreateInfo.pQueuePriorities    = m_queuePriorities[ i ].getStart();
            queueCreateInfo.queueCount          = (uint32)m_queuePriorities[ i ].getSize();
            m_queueCreateInfos.pushBack( queueCreateInfo );
        }

        return true;
    }

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
    VkBool32 VulkanGraphicsDevice::vulkanDebugUtilsMessengerCallback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData )
    {
        KEEN_UNUSED1( pUserData );
        debug::setDebugTraceLevel( DebugTraceLevel_Info );

        const StringView message = createStringView( pCallbackData->pMessage );

        // :LF: SPIR-V generated by dxc declares this extension, and while it's not supported everywhere it doesn't seem to break, so just ignore it
        if( containsSubString( message, "The SPIR-V Extension (SPV_GOOGLE_user_type) was declared, but none of the requirements were met to use it."_s ) ||
            containsSubString( message, "The SPIR-V Extension (SPV_GOOGLE_hlsl_functionality1) was declared, but none of the requirements were met to use it."_s ) )
        {
            return VK_FALSE;
        }

        // :FK: This is a warning that VK_AMD_marker_buffer extension is supposed to be used only in dev tools
        if( pCallbackData->messageIdNumber == 0x1563642e )
        {
            return VK_FALSE;
        }

        // :FK: This is a complaint about VkGraphicsPipelineCreateInfo.pMultiSampleState being NULL when in fact it is never NULL in our codebase
        if( pCallbackData->messageIdNumber == (sint32)0x92d66fc1 )
        {
            return VK_FALSE;
        }

        // There are ongoing discussions about how that one should be solved, see
        // https://github.com/KhronosGroup/Vulkan-Docs/issues/1345
        // https://github.com/KhronosGroup/Vulkan-Docs/issues/1700
        if( containsSubString( message, "Hazard WRITE_AFTER_READ in subpass 0 for attachment 1 depth aspect during store with storeOp VK_ATTACHMENT_STORE_OP_STORE."_s ) )
        {
            return VK_FALSE;
        }

        // :FK: this is a vkCmdDraw-viewType and vkCmdDraw-format validation that is happening on a dispatch.....
        // https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/8263
        if( pCallbackData->messageIdNumber == ( sint32 )0x73ed4ff5 ||
            pCallbackData->messageIdNumber == ( sint32 )0x7be8f3b5 )
        {
            return VK_FALSE;
        }

        KEEN_TRACE_INFO( "[graphics] Vulkan: %s\n", message );
        if( pCallbackData->cmdBufLabelCount )
        {
            KEEN_TRACE_INFO( "\tdebug labels:\n" );
            for( size_t i = 0u; i < pCallbackData->cmdBufLabelCount; ++i )
            {
                const size_t reverseIndex = pCallbackData->cmdBufLabelCount - 1u - i;
                KEEN_TRACE_INFO( "\t\t- %s\n", pCallbackData->pCmdBufLabels[ reverseIndex ].pLabelName );
            }
        }

        if( ( messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) ||
            ( messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && !isAnyBitSet( messageTypes, VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT ) ) )
        {
            KEEN_BREAKPOINT;
        }

        // spec: The application should always return VK_FALSE. The VK_TRUE value is reserved for use in layer development.
        return VK_FALSE;
    }
#endif

    void* VKAPI_PTR VulkanGraphicsDevice::vulkanAlloc( void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope )
    {
        KEEN_USE_ARGUMENT( allocationScope );

        VulkanGraphicsDevice* pDevice = (VulkanGraphicsDevice*)pUserData;

        WriteMutexLock lock( &pDevice->m_vulkanAllocatorMutex );
        void* pMemory = pDevice->m_vulkanMemoryAllocator.allocate( size, alignment, {}, "Vulkan"_debug );

#if KEEN_USING( KEEN_PROFILER )
        if( pMemory != nullptr )
        {
            atomic::inc_uint32_ordered( &pDevice->m_vulkanAllocationCount );
            atomic::add_uint32_ordered( &pDevice->m_vulkanAllocationSize, (uint32)pDevice->m_vulkanMemoryAllocator.getAllocationSize( pMemory ) );
        }
#endif
        return pMemory;
    }

    void* VKAPI_PTR VulkanGraphicsDevice::vulkanRealloc( void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope )
    {
        KEEN_USE_ARGUMENT( allocationScope );

        KEEN_ASSERTE( alignment <= sizeof( void* ) );

        VulkanGraphicsDevice* pDevice = (VulkanGraphicsDevice*)pUserData;

        WriteMutexLock lock( &pDevice->m_vulkanAllocatorMutex );
#if KEEN_USING( KEEN_PROFILER )
        if( pOriginal != nullptr )
        {
            atomic::dec_uint32_ordered( &pDevice->m_vulkanAllocationCount );
            atomic::sub_uint32_ordered( &pDevice->m_vulkanAllocationSize, (uint32)pDevice->m_vulkanMemoryAllocator.getAllocationSize( pOriginal ) );
        }
#endif

        void* pMemory = pDevice->m_vulkanMemoryAllocator.reallocate( pOriginal, size, alignment );

#if KEEN_USING( KEEN_PROFILER )
        if( pMemory != nullptr )
        {
            atomic::inc_uint32_ordered( &pDevice->m_vulkanAllocationCount );
            atomic::add_uint32_ordered( &pDevice->m_vulkanAllocationSize, (uint32)pDevice->m_vulkanMemoryAllocator.getAllocationSize( pMemory ) );
        }
#endif
        return pMemory;
    }

    void VKAPI_PTR VulkanGraphicsDevice::vulkanFree( void* pUserData, void* pMemory )
    {
        if( pMemory == nullptr )
        {
            return;
        }
        VulkanGraphicsDevice* pDevice = (VulkanGraphicsDevice*)pUserData;

        WriteMutexLock lock( &pDevice->m_vulkanAllocatorMutex );
#if KEEN_USING( KEEN_PROFILER )
        if( pMemory != nullptr )
        {
            atomic::dec_uint32_ordered( &pDevice->m_vulkanAllocationCount );
            atomic::sub_uint32_ordered( &pDevice->m_vulkanAllocationSize, (uint32)pDevice->m_vulkanMemoryAllocator.getAllocationSize( pMemory ) );
        }
#endif

        pDevice->m_vulkanMemoryAllocator.free( pMemory );
    }

    void VKAPI_PTR VulkanGraphicsDevice::vulkanInternalAlloc( void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope )
    {
        KEEN_USE_ARGUMENT( pUserData );
        KEEN_USE_ARGUMENT( size );
        KEEN_USE_ARGUMENT( allocationType );
        KEEN_USE_ARGUMENT( allocationScope );

#if KEEN_USING( KEEN_PROFILER )
        VulkanGraphicsDevice* pDevice = (VulkanGraphicsDevice*)pUserData;

        atomic::inc_uint32_ordered( &pDevice->m_vulkanInternalAllocationCount );
        atomic::add_uint32_ordered( &pDevice->m_vulkanInternalAllocationSize, (uint32)size );
#endif
    }

    void VKAPI_PTR VulkanGraphicsDevice::vulkanInternalFree( void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope )
    {
        KEEN_USE_ARGUMENT( pUserData );
        KEEN_USE_ARGUMENT( size );
        KEEN_USE_ARGUMENT( allocationType );
        KEEN_USE_ARGUMENT( allocationScope );

#if KEEN_USING( KEEN_PROFILER )
        VulkanGraphicsDevice* pDevice = (VulkanGraphicsDevice*)pUserData;

        atomic::dec_uint32_ordered( &pDevice->m_vulkanInternalAllocationCount );
        atomic::sub_uint32_ordered( &pDevice->m_vulkanInternalAllocationSize, (uint32)size );
#endif
    }

    struct VulkanApiInstance : GraphicsApiInstance
    {
    };

    namespace graphics
    {
        static GraphicsDeviceCreateResult   createVulkanDevice( MemoryAllocator* pAllocator, GraphicsApiInstance* pApi, const GraphicsDeviceParameters& parameters );
        static void                         destroyVulkanApi( MemoryAllocator* pAllocator, GraphicsApiInstance* pApi );
    }

    GraphicsApiInstance* graphics::createVulkanApi( MemoryAllocator* pAllocator, const GraphicsApiParameters& parameters )
    {
        KEEN_ASSERT( parameters.api == GraphicsApi::Vulkan );
        KEEN_UNUSED1( parameters );

        VulkanApiInstance* pApi = newObjectZero<VulkanApiInstance>( pAllocator, "VulkanApiInstance"_debug );
        if( pApi == nullptr )
        {
            return nullptr;
        }

        pApi->apiId         = GraphicsApi::Vulkan;
        pApi->createDevice  = createVulkanDevice;
        pApi->destroyApi    = destroyVulkanApi;

        return pApi;
    }

    static GraphicsDeviceCreateResult graphics::createVulkanDevice( MemoryAllocator* pAllocator, GraphicsApiInstance* pApi, const GraphicsDeviceParameters& parameters )
    {
        VulkanApiInstance* pVulkanApi = (VulkanApiInstance*)pApi;
        KEEN_UNUSED1( pVulkanApi );

#if defined( KEEN_PLATFORM_WIN32 )
        validateVulkanImplicitLayersWin32( parameters.disableUnknownVulkanLayers );
#endif

#if KEEN_USING( KEEN_VULKAN_VALIDATION )
        if( parameters.enableDebugChecks ||
            parameters.enableSynchronizationValidation ||
            parameters.enableGpuAssistedValidation )
        {
#if defined( KEEN_PLATFORM_WIN32 )
            setupVulkanValidationSettingsWin32();
#endif
        }
#endif

        VulkanGraphicsDevice* pDevice = newObjectZero<VulkanGraphicsDevice>( pAllocator, "VulkanGraphicsDevice"_debug );

        const GraphicsSystemCreateError error = pDevice->create( pAllocator, parameters );
        if( error != GraphicsSystemCreateError::Ok )
        {
            return error;
        }

        return GraphicsDeviceCreateResult{ pDevice };
    }

    static void graphics::destroyVulkanApi( MemoryAllocator* pAllocator, GraphicsApiInstance* pApi )
    {
        VulkanApiInstance* pVulkanApi = (VulkanApiInstance*)pApi;
        KEEN_UNUSED1( pVulkanApi );
        deleteObject( pAllocator, pApi );
    }

}
