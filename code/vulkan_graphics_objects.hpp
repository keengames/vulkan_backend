#ifndef KEEN_VULKAN_GRAPHICS_OBJECTS_HPP_INCLUDED
#define KEEN_VULKAN_GRAPHICS_OBJECTS_HPP_INCLUDED

#include "vulkan_types.hpp"
#include "keen/task/task_types.hpp"
#include "keen/base/map.hpp"
#include "keen/base/mutex.hpp"
#include "keen/base/hash_map.hpp"

namespace keen
{
    struct OsWindowSystem;

    struct VulkanDescriptorPoolSizes
    {
        static constexpr uint32 DefaultDescriptorSetCount = 4096u;
        uint32          descriptorSetCount = DefaultDescriptorSetCount;
        uint32          uniformBufferDescriptorCount = DefaultDescriptorSetCount * 2u;
        uint32          storageBufferDescriptorCount = DefaultDescriptorSetCount * 2u;
        uint32          samplerDescriptorCount = DefaultDescriptorSetCount * 4u;
        uint32          sampledImageDescriptorCount = DefaultDescriptorSetCount * 16u;
        uint32          storageImageDescriptorCount = DefaultDescriptorSetCount * 16u;
        uint32          accelerationStructureDescriptorCount = 0u;
    };

    struct VulkanGraphicsObjectsParameters
    {
        MemoryAllocator*            pAllocator = nullptr;
        EventNotifier*              pEventNotifier = nullptr;

        TaskSystem*                 pTaskSystem = nullptr;
        VulkanApi*                  pVulkan = nullptr;
        VkInstance                  instance;
        VkPhysicalDevice            physicalDevice;
        VkDevice                    device;

        StringView                  pipelineCacheDirectory;

        VulkanSharedData*           pSharedData = nullptr;
        OsWindowSystem*             pWindowSystem = nullptr;

        size_t                      allocationBlockSizeInBytes;
        size_t                      frameCount;

        VulkanDescriptorPoolSizes   staticDescriptorPoolSizes;
        VulkanDescriptorPoolSizes   dynamicDescriptorPoolSizes;

        bool                        enableBindlessDescriptors = false;
        uint32                      bindlessTextureCount = 0u;
        uint32                      bindlessSamplerCount = 0u;
    };

    enum VulkanDepthFormat
    {
        VulkanDepthFormat_Depth16,
        VulkanDepthFormat_Depth16_ShaderInput,
        VulkanDepthFormat_Depth24S8,
        VulkanDepthFormat_Depth24S8_ShaderInput,
        VulkanDepthFormat_Count
    };

    namespace vulkan
    {
        VkImageSubresourceRange     getImageSubresourceRange( const VulkanTexture* pTexture );
        VulkanDescriptorPoolSizes   fillDefaultVulkanDescriptorPoolSizes( uint32 descriptorSetCount );
    }

    class VulkanGraphicsObjects
    {
    public:

        ErrorId                             create( const VulkanGraphicsObjectsParameters& parameters );
        void                                destroy();

        VulkanSwapChainWrapper*             createSwapChain( const GraphicsSwapChainParameters& parameters );
        VulkanPipelineLayout*               createPipelineLayout( const GraphicsPipelineLayoutParameters& parameters );
        GraphicsRenderPipeline*             createRenderPipeline( const GraphicsRenderPipelineParameters& parameters );         
        VulkanComputePipeline*              createComputePipeline( const GraphicsComputePipelineParameters& parameters );
        VulkanDeviceMemory*                 createDeviceMemory( const GraphicsDeviceMemoryParameters& parameters );
        VulkanTexture*                      createTexture( const GraphicsTextureParameters& parameters );
        VulkanTexture*                      createTextureView( const GraphicsTextureViewParameters& parameters );
        VulkanBuffer*                       createBuffer( const GraphicsBufferParameters& parameters );
        VulkanSampler*                      createSampler( const GraphicsSamplerParameters& parameters, DebugName debugName );
        VulkanDescriptorSetLayout*          createDescriptorSetLayout( const GraphicsDescriptorSetLayoutParameters& parameters );
        VulkanDescriptorSet*                createStaticDescriptorSet( const GraphicsDescriptorSetParameters& parameters );
        VulkanDescriptorSet*                createDynamicDescriptorSet( VulkanFrame* pFrame, const GraphicsDescriptorSetParameters& parameters );
        VulkanQueryPool*                    createQueryPool( const GraphicsQueryPoolParameters& parameters );

        GraphicsMemoryRequirements          queryTextureMemoryRequirements( const VulkanTexture* pTexture );
        GraphicsMemoryRequirements          queryBufferMemoryRequirements( const VulkanBuffer* pBuffer );
        void                                bindMemory( const ArrayView<const GraphicsBufferMemoryBinding>& buffers, const ArrayView<const GraphicsTextureMemoryBinding>& textures );

        VkDescriptorSetLayout               getBindlessDescriptorSetLayout() const { return m_bindlessDescriptorSetLayout; }
        VkDescriptorSetLayout               getEmptyDescriptorSetLayout() const { return m_emptyDescriptorSetLayout; }

        void                                destroyFrameObjects( ArrayView<GraphicsDeviceObject*> objects );
        void                                destroyDeviceObject( GraphicsDeviceObject* pObject );

        void                                destroyTexture( VulkanTexture* pTexture );

        void                                flushCpuMemoryCache( ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes );
        void                                invalidateCpuMemoryCache( ArrayView<VulkanGpuAllocation*> allocations, ArrayView<uint64> offsets, ArrayView<uint64> sizes );

        void                                traceGpuAllocations();

        VulkanDescriptorPool*               createDescriptorPool( VulkanDescriptorPoolType type );
        void                                freeDescriptorPool( VulkanDescriptorPool* pDescriptorPool );
        void                                destroyDescriptorPool( VulkanDescriptorPool* pDescriptorPool );

    private:
        using CachedDescriptorSetLayoutMap  = HashMap< uint64, VkDescriptorSetLayout >;
        using GraphicsDeviceObjectArray     = DynamicArray< GraphicsDeviceObject* >;
        using ObjectAllocatorPoolArray      = StaticArray<BaseChunkedPoolAllocator, (size_t)GraphicsDeviceObjectType::Count>;
        using MemoryRequirementsMap         = Map< HashKey64, VkMemoryRequirements >;
        using StaticSamplerMap              = Map< HashKey32, VulkanSampler* >;

        VulkanSampler*                      createStaticSampler( const GraphicsSamplerParameters& parameters );

        MemoryAllocator*                m_pAllocator;

        size_t                          m_frameCount;

        Mutex                           m_allocatorMutex;
        ObjectAllocatorPoolArray        m_objectAllocators;

        VulkanDescriptorPoolSizes       m_staticDescriptorPoolSizes;
        VulkanDescriptorPoolSizes       m_dynamicDescriptorPoolSizes;

        Mutex                           m_descriptorPoolMutex;
        VulkanDescriptorPool*           m_pFirstFreeDynamicDescriptorPool;

        Mutex                           m_staticDescriptorPoolMutex;
        VulkanDescriptorPool*           m_pFirstStaticDescriptorPool;

        VulkanApi*                      m_pVulkan;
        VkInstance                      m_instance;
        VkPhysicalDevice                m_physicalDevice;
        VkDevice                        m_device;

        StaticSamplerMap                m_staticSamplerMap;

        PathName                        m_pipelineCachePath;
        VkPipelineCache                 m_pipelineCache;

        VkDescriptorSetLayout           m_emptyDescriptorSetLayout;
        VkDescriptorSetLayout           m_bindlessDescriptorSetLayout;

        VulkanSharedData*               m_pSharedData;

        OsWindowSystem*                 m_pWindowSystem;

        MemoryRequirementsMap           m_cachedMemoryRequirements;

        VulkanGpuAllocator*             m_pGpuAllocator;

        Mutex                           m_freeObjectListMutex;

        VkFormat                        m_depthFormats[ VulkanDepthFormat_Count ];

        uint32                          m_nextBufferId;
        uint32                          m_nextTextureId;

#if KEEN_USING( KEEN_PROFILER )
        uint32_atomic                   m_vulkanDeviceMemoryCount;
        uint32_atomic                   m_vulkanRenderPipelineCount;
        uint32_atomic                   m_vulkanTextureCount;
        uint32_atomic                   m_vulkanBufferCount;
        uint32_atomic                   m_vulkanDescriptorSetLayoutCount;
        uint32_atomic                   m_vulkanSamplerCount;
        uint32_atomic                   m_vulkanPipelineLayoutCount;
        uint32_atomic                   m_vulkanSwapChainCount;
        uint32_atomic                   m_vulkanCompilationContextCount;
        uint32_atomic                   m_vulkanQueryPoolCount;
        uint32_atomic                   m_vulkanComputePipelineCount;
        uint32_atomic                   m_vulkanDescriptorPoolCount;
#endif

        MemoryBlock                     allocateDeviceObjectBase( GraphicsDeviceObjectType type );
        void                            freeDeviceObjectBase( GraphicsDeviceObjectType type, void* pObject );

        template<typename T>
            void                        createObjectPoolAllocator( size_t chunkSize, size_t alignment, const DebugName& debugName );

        template<typename T>
            inline T*                   allocateDeviceObject() 
            { 
                MemoryBlock objectMemory = allocateDeviceObjectBase( (GraphicsDeviceObjectType)T::ObjectType ); 
                if( objectMemory.pStart == nullptr )
                {
                    return nullptr;
                }
                KEEN_ASSERT_SLOW( objectMemory.size == sizeof( T ) );
                return callDefaultConstructor( (T*)objectMemory.pStart );
            }

        template<typename T>
            inline void                 freeDeviceObject( T* pObject ) 
            { 
                callDestructor( pObject );
                freeDeviceObjectBase( (GraphicsDeviceObjectType)T::ObjectType, pObject ); 
            }

        struct RenderPipelineShaderModules
        {
            VkShaderModule              vertexShader;
            VkShaderModule              tcShader;
            VkShaderModule              teShader;
            VkShaderModule              fragmentShader;
        };

        ErrorId                             createShaderModule( VkShaderModule* pShaderModule, ConstMemoryBlock shaderCode, const DebugName& debugName );
        Result<void>                        prepareRenderPipelineCompileParameters( RenderPipelineShaderModules* pShaderModules, const GraphicsRenderPipelineParameters& parameters );
        void                                destroyShaderModules( const RenderPipelineShaderModules& shaderModules );

        void                                destroySwapChain( VulkanSwapChainWrapper* pSwapChain );
        void                                destroyRenderPipeline( VulkanRenderPipeline* pRenderPipeline );
        void                                destroyComputePipeline( VulkanComputePipeline* pComputePipeline );
        void                                destroyPipelineLayout( VulkanPipelineLayout* pPipelineLayout );
        void                                destroyDeviceMemory( VulkanDeviceMemory* pDeviceMemory );
        void                                destroyBuffer( VulkanBuffer* pBuffer );
        void                                destroySampler( VulkanSampler* pSampler );
        void                                destroyDescriptorSetLayout( VulkanDescriptorSetLayout* pLayout );
        void                                destroyDescriptorSet( StaticVulkanDescriptorSet* pDesciptorSet );
        void                                destroyQueryPool( VulkanQueryPool* pQueryPool );

        bool                                createDefaultTextureView( VulkanTexture* pTexture );

        static GraphicsDescriptorSet*       allocateDescriptorSet( VulkanFrame* pFrame, const GraphicsDescriptorSetParameters& parameters );

        ErrorId                             compileRenderPipeline( VulkanRenderPipeline* pPipeline, const GraphicsRenderPipelineParameters& parameters, const RenderPipelineShaderModules& shaderModules );
        ErrorId                             compileComputePipeline( VulkanComputePipeline* pPipeline, const GraphicsComputePipelineParameters& parameters, VkShaderModule computeShader );

        VkFormat                            findFirstMatchingDepthStencilFormat( const ArrayView<const VkFormat>& candidates, bool usedAsShaderInput ) const;

        Result<void>                        fillVkBufferCreateInfo( VkBufferCreateInfo* pBufferCreateInfo, const GraphicsBufferParameters& parameters );
        Result<void>                        fillVkImageCreateInfo( VkImageCreateInfo* pImageCreateInfo, const GraphicsTextureParameters& parameters );
    };
}

#endif
