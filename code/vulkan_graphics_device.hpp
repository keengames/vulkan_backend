#ifndef KEEN_VULKAN_GRAPHICS_DEVICE_HPP_INCLUDED
#define KEEN_VULKAN_GRAPHICS_DEVICE_HPP_INCLUDED

#include "keen/base/dynamic_array.hpp"
#include "keen/base/hash_map.hpp"
#include "keen/base/synchronized_data.hpp"
#include "keen/memory/tlsf_allocator.hpp"

#include "global/graphics_device.hpp"

#include "vulkan_graphics_objects.hpp"
#include "vulkan_render_context.hpp"
#include "vulkan_swap_chain.hpp"
#include "vulkan_types.hpp"

namespace keen
{
    class GraphicsDevice;
    class MemoryAllocator;
    struct GraphicsDeviceParameters;
    struct TaskQueue;
    struct TaskSystem;

    class VulkanGraphicsDevice : public GraphicsDevice
    {
    public:
        VulkanGraphicsDevice();
        virtual                                     ~VulkanGraphicsDevice();

        GraphicsSystemCreateError                   create( MemoryAllocator* pAllocator, const GraphicsDeviceParameters& parameters );
        virtual void                                destroy() override final;

        virtual const GraphicsDeviceInfo&           updateInfo() override final;

        virtual GraphicsDeviceMemory*               createDeviceMemory( const GraphicsDeviceMemoryParameters& parameters ) override final;
        virtual GraphicsSwapChain*                  createSwapChain( const GraphicsSwapChainParameters& parameters ) override final;
        virtual GraphicsPipelineLayout*             createPipelineLayout( const GraphicsPipelineLayoutParameters& parameters ) override final;
        virtual GraphicsRenderPipeline*             createRenderPipeline( const GraphicsRenderPipelineParameters& parameters )  override final;
        virtual GraphicsComputePipeline*            createComputePipeline( const GraphicsComputePipelineParameters& parameters ) override final;
        virtual GraphicsTexture*                    createTexture( const GraphicsTextureParameters& parameters ) override final;
        virtual GraphicsTexture*                    createTextureView( const GraphicsTextureViewParameters& parameters ) override final;
        virtual GraphicsBuffer*                     createBuffer( const GraphicsBufferParameters& parameters ) override final;
        virtual GraphicsSampler*                    createSampler( const GraphicsSamplerParameters& parameters, DebugName debugName ) override final;
        virtual GraphicsDescriptorSetLayout*        createDescriptorSetLayout( const GraphicsDescriptorSetLayoutParameters& parameters ) override final;
        virtual GraphicsDescriptorSet*              createStaticDescriptorSet( const GraphicsDescriptorSetParameters& parameters ) override final;
        virtual GraphicsDescriptorSet*              createDynamicDescriptorSet( GraphicsFrame* pFrame, const GraphicsDescriptorSetParameters& parameters ) override final;
        virtual GraphicsQueryPool*                  createQueryPool( const GraphicsQueryPoolParameters& parameters ) override final;

        virtual GraphicsMemoryRequirements          queryTextureMemoryRequirements( const GraphicsTexture* pTexture ) override final;
        virtual GraphicsMemoryRequirements          queryBufferMemoryRequirements( const GraphicsBuffer* pBuffer ) override final;
        virtual void                                bindMemory( const ArrayView<const GraphicsBufferMemoryBinding>& buffers, const ArrayView<const GraphicsTextureMemoryBinding>& textures ) override final;

        virtual void                                flushCpuCache( const ArrayView<const GraphicsBufferRange>& bufferRanges ) override final;
        virtual void                                invalidateCpuCache( const ArrayView<const GraphicsBufferRange>& bufferRanges ) override final;
        virtual void                                resetQueryPool( GraphicsQueryPool* pQueryPool, uint32 firstQuery, uint32 queryCount ) override final;
        virtual Result<void>                        copyQueryPoolTimeValues( ArrayView<Time> target, GraphicsQueryPool* pQueryPool, uint32 firstQuery ) override final;

        virtual void                                resizeSwapChain( GraphicsSwapChain* pSwapChain, uint2 size ) override final;
        virtual void                                setSwapChainPresentationInterval( GraphicsSwapChain* pSwapChain, uint32 presentationInterval ) override final;

#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )
        virtual void                                getCompiledRenderPipelineInfo( GraphicsCompiledRenderPipelineInfo* pCompiledPipelineInfo, const GraphicsRenderPipeline* pRenderPipeline ) override final;
        virtual void                                getCompiledComputePipelineInfo( GraphicsCompiledComputePipelineInfo* pCompiledPipelineInfo, const GraphicsComputePipeline* pComputePipeline ) override final;
#endif

        virtual GraphicsFrame*                      beginFrame( ArrayView<GraphicsSwapChain*> swapChains ) override final;
        virtual void                                submitFrame( GraphicsFrame* pFrame, const GraphicsBindlessDescriptorSet& bindlessDescriptorSet ) override final;

        virtual GraphicsTransferBatch*              beginTransferBatch( DebugName debugName ) override final;
        virtual void                                submitTransferBatch( GraphicsTransferBatch* pTransferBatch ) override final;
        virtual Result<void>                        waitForTransferBatch( GraphicsTransferBatch* pTransferBatch, Time timeOut ) override final;

        virtual void                                waitForGpuIdle( const ArrayView<GraphicsDeviceObject*> destroyObjects ) override final;

    private:
        enum class CreateInstanceFlag
        {
            UseMemoryCallbacks,
#ifndef KEEN_BUILD_MASTER
            EnableValidation,
            EnableSynchronizationValidation,
            EnableGpuAssistedValidation,
#endif
        };
        using CreateInstanceFlags = Bitmask32<CreateInstanceFlag>;

        ErrorId                                     createInstance( StringView applicationName, uint32 applicationVersion, CreateInstanceFlags flags );
        void                                        destroyInstance();
        ErrorId                                     createDevice( OsWindowSystem* pWindowSystem, Optional<uint32> forcePhysicalDeviceIndex );
        void                                        destroyDevice();

        bool                                        prepareQueueCreationInfo( OsWindowSystem* pWindowSystem );

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
        static VkBool32 VKAPI_PTR                   vulkanDebugUtilsMessengerCallback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData );
#endif
        static void* VKAPI_PTR                      vulkanAlloc( void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope );
        static void* VKAPI_PTR                      vulkanRealloc( void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope );
        static void VKAPI_PTR                       vulkanFree( void* pUserData, void* pMemory );
        static void VKAPI_PTR                       vulkanInternalAlloc( void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope );
        static void VKAPI_PTR                       vulkanInternalFree( void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope );

        MemoryAllocator*                            m_pAllocator;

#if KEEN_USING( KEEN_PROFILER )
        uint32_atomic                               m_vulkanInternalAllocationSize;
        uint32_atomic                               m_vulkanInternalAllocationCount;
        uint32_atomic                               m_vulkanAllocationSize;
        uint32_atomic                               m_vulkanAllocationCount;
#endif

        ReadWriteMutex                              m_vulkanAllocatorMutex;
        Mutex                                       m_vulkanRenderContextMutex;
        TlsfAllocator                               m_vulkanMemoryAllocator;
        VkAllocationCallbacks                       m_allocationCallbacks;

        VulkanApi*                                  m_pVulkan;
        VkInstance                                  m_instance;
        VkPhysicalDevice                            m_physicalDevice;
        VkDevice                                    m_device;

        VulkanSharedData                            m_sharedData;
        VulkanRenderContext                         m_renderContext;
        VulkanGraphicsObjects                       m_objects;

#if defined( VK_EXT_memory_budget )
        VkPhysicalDeviceMemoryBudgetPropertiesEXT   m_memoryBudgets = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
#endif

#if KEEN_USING( KEEN_GRAPHICS_DEBUG_CALLBACK )
        VkDebugUtilsMessengerEXT                    m_debugMessenger;
#endif

        static constexpr uint32 MaxQueueCount = 3u;
        using QueueCreationInfoArray = DynamicArray<VkDeviceQueueCreateInfo,MaxQueueCount>;
        using QueuePriorityArray = Array<DynamicArray<float32,MaxQueueCount>>;

        QueueCreationInfoArray                      m_queueCreateInfos;
        QueuePriorityArray                          m_queuePriorities;
        size_t                                      m_graphicsQueueIndex;
        size_t                                      m_computeQueueIndex;
        size_t                                      m_transferQueueIndex;

        using VulkanTransferQueueSynchronized = SynchronizedData<VulkanTransferQueue>;
        VulkanTransferQueueSynchronized             m_transferQueue;

        void                                        submitVulkanTransferBatch( VulkanTransferBatch* pBatch );
    };
}

#endif
