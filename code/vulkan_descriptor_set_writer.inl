#include "vulkan_descriptor_set_writer.hpp"

namespace keen
{
    inline VulkanDescriptorSetWriter::VulkanDescriptorSetWriter( VulkanApi* pVulkan )
        : pVulkan( pVulkan )
    {
    }

    inline VulkanDescriptorSetWriter::~VulkanDescriptorSetWriter()
    {
        flush();
        KEEN_ASSERT( writes.isEmpty() );
    }

    inline GraphicsDescriptorType getBindingElementType( GraphicsDescriptorType descriptorType )
    {
        if( descriptorType == GraphicsDescriptorType::SamplerArray )
        {
            return GraphicsDescriptorType::Sampler;
        }
        else if( descriptorType == GraphicsDescriptorType::SampledImageArray )
        {
            return GraphicsDescriptorType::SampledImage;
        }
        else if( descriptorType == GraphicsDescriptorType::StorageImageArray )
        {
            return GraphicsDescriptorType::StorageImage;
        }
        else if( descriptorType == GraphicsDescriptorType::SamplerArray )
        {
            return GraphicsDescriptorType::Sampler;
        }
        else
        {
            return descriptorType;
        }
    }

    inline void VulkanDescriptorSetWriter::startWriteDescriptors( VkDescriptorSet descriptorSet, const VulkanDescriptorSetLayout* pDescriptorSetLayout, uint32 bindingIndex, GraphicsDescriptorType descriptorType, uint32 startArrayIndex, uint32 descriptorCount )
    {
        KEEN_ASSERT( descriptorSet != VK_NULL_HANDLE );
        KEEN_ASSERT( descriptorCount > 0u );
        KEEN_ASSERT( writes.isEmpty() || writes.getLast().descriptorCount == writeDescriptorCount ); // you did not write as many descriptors as you said you would!

        VkWriteDescriptorSet* pWrite = pushWrite();

        pWrite->sType               = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        pWrite->dstSet              = descriptorSet;
        pWrite->dstBinding          = bindingIndex;
        pWrite->dstArrayElement     = startArrayIndex;
        pWrite->descriptorType      = vulkan::getDescriptorType( descriptorType );

        writeDescriptorCount        = descriptorCount;

#if KEEN_USING( KEEN_GRAPHICS_VALIDATION ) && KEEN_USING( KEEN_ASSERT_FEATURE )
        const GraphicsDescriptorSetLayoutBinding* pBinding = &pDescriptorSetLayout->bindings[ bindingIndex ];

        if( graphics::isArrayDescriptor( descriptorType ) )
        {
            KEEN_ASSERT( graphics::isDescriptorTypeCompatible( descriptorType, pBinding->type ) );
            KEEN_ASSERT( startArrayIndex + descriptorCount <= pBinding->arraySizeOrBufferStride );
        }
        else
        {
            KEEN_ASSERT( graphics::isDescriptorTypeCompatible( descriptorType, getBindingElementType( pBinding->type ) ) );
        }
#else
        KEEN_UNUSED1( pDescriptorSetLayout );
#endif
    }

    inline void VulkanDescriptorSetWriter::writeDescriptor( const GraphicsDescriptorData& descriptorData )
    {
        KEEN_ASSERT( descriptorData.isValid() );
        KEEN_ASSERT( writes.hasElements() ); // call startWriteDescriptors first
        KEEN_ASSERT( writes.getLast().descriptorType == vulkan::getDescriptorType( descriptorData.type ) );
        KEEN_ASSERT( writes.getLast().descriptorCount < writeDescriptorCount );

        switch( descriptorData.type )
        {
        case GraphicsDescriptorType::Sampler:
            {
                const VulkanSampler* pSampler = (const VulkanSampler*)descriptorData.sampler.pSampler;

                VkDescriptorImageInfo* pImageInfo = pushImageInfo();
                pImageInfo->sampler = pSampler->sampler;

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::SamplerArray:
            for( uint32 i = 0u; i < descriptorData.samplerArray.samplerCount; ++i )
            {
                const GraphicsDescriptorData_Sampler& samplerElement = descriptorData.samplerArray.pFirstSampler[ i ];
                const VulkanSampler* pSampler = (const VulkanSampler*)samplerElement.pSampler;

                VkDescriptorImageInfo* pImageInfo = pushImageInfo();
                pImageInfo->sampler = pSampler->sampler;

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::SampledImage:
            {
                const VulkanTexture* pTexture = (const VulkanTexture*)descriptorData.image.pTexture;

                VkDescriptorImageInfo* pImageInfo = pushImageInfo();
                pImageInfo->imageView   = pTexture->imageView;
                pImageInfo->imageLayout = vulkan::getImageLayout( descriptorData.image.layout );

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::SampledImageArray:
            for( uint32 i = 0u; i < descriptorData.imageArray.imageCount; ++i )
            {
                const GraphicsDescriptorData_Image& imageElement = descriptorData.imageArray.pFirstImage[ i ];
                const VulkanTexture* pTexture = (const VulkanTexture*)imageElement.pTexture;

                VkDescriptorImageInfo* pImageInfo = pushImageInfo();
                pImageInfo->imageView   = pTexture->imageView;
                pImageInfo->imageLayout = vulkan::getImageLayout( imageElement.layout );

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::StorageImage:
            {
                const VulkanTexture* pTexture = (const VulkanTexture*)descriptorData.image.pTexture;

                VkDescriptorImageInfo* pImageInfo = pushImageInfo();
                pImageInfo->imageView   = pTexture->imageView;
                pImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::StorageImageArray:
            for( uint32 i = 0u; i < descriptorData.imageArray.imageCount; ++i )
            {
                const GraphicsDescriptorData_Image& imageElement = descriptorData.imageArray.pFirstImage[ i ];
                const VulkanTexture* pTexture = (const VulkanTexture*)imageElement.pTexture;

                VkDescriptorImageInfo* pImageInfo = pushImageInfo();
                pImageInfo->imageView   = pTexture->imageView;
                pImageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::UniformBuffer:
        case GraphicsDescriptorType::ByteAddressBuffer:
        case GraphicsDescriptorType::StructuredBuffer:
        case GraphicsDescriptorType::RWByteAddressBuffer:
        case GraphicsDescriptorType::RWStructuredBuffer:
#if KEEN_USING( KEEN_GRAPHICS_OLD_STORAGE_BUFFER_DESCRIPTORS )
        case GraphicsDescriptorType::StorageBuffer:
#endif
            {
                const VulkanBuffer* pBuffer = (const VulkanBuffer*)descriptorData.buffer.pBuffer;

                VkDescriptorBufferInfo* pBufferInfo = pushBufferInfo();
                pBufferInfo->buffer = pBuffer->buffer;
                pBufferInfo->offset = descriptorData.buffer.offset;
                pBufferInfo->range  = descriptorData.buffer.stride * descriptorData.buffer.count;

                writes.getLast().descriptorCount += 1u;
            }
            break;

        case GraphicsDescriptorType::Invalid:
            KEEN_BREAK( "invalid descriptor type" );
            break;
        }       
    }

    inline void VulkanDescriptorSetWriter::flush()
    {
        if( writes.isEmpty() )
        {
            KEEN_ASSERT( bufferInfos.isEmpty() );
            KEEN_ASSERT( imageInfos.isEmpty() );

            return;
        }

        const VkWriteDescriptorSet lastWrite = writes.getLast();

        if( lastWrite.descriptorCount == 0u )
        {
            // descriptorCount == 0 is invalid by the spec, this will be continued with after the flush
            writes.popBack();
        }

        pVulkan->vkUpdateDescriptorSets( pVulkan->device, (uint32)writes.getSize(), writes.getStart(), 0u, nullptr );

        writes.clear();
        bufferInfos.clear();
        imageInfos.clear();

        KEEN_ASSERT( lastWrite.descriptorCount <= writeDescriptorCount );
        if( lastWrite.descriptorCount != writeDescriptorCount )
        {
            // couldn't finish writing all descriptors of the last write, so continue where we left off

            VkWriteDescriptorSet* pWrite = writes.pushBackZero();
            KEEN_ASSERT( pWrite != nullptr );

            pWrite->sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            pWrite->dstSet          = lastWrite.dstSet;
            pWrite->dstBinding      = lastWrite.dstBinding;
            pWrite->dstArrayElement = lastWrite.dstArrayElement + lastWrite.descriptorCount;
            pWrite->descriptorType  = lastWrite.descriptorType;

            writeDescriptorCount    -= lastWrite.descriptorCount;
        }
    }

    inline VkWriteDescriptorSet* VulkanDescriptorSetWriter::pushWrite()
    {
        if( writes.getRemainingCapacity() == 0u )
        {
            flush();
        }

        VkWriteDescriptorSet* pVulkanWrite = writes.pushBackZero();
        KEEN_ASSERT( pVulkanWrite != nullptr );
        return pVulkanWrite;
    }

    inline VkDescriptorBufferInfo* VulkanDescriptorSetWriter::pushBufferInfo()
    {
        if( bufferInfos.getRemainingCapacity() == 0u )
        {
            flush();
        }

        VkDescriptorBufferInfo* pBufferInfo = bufferInfos.pushBackZero();
        KEEN_ASSERT( pBufferInfo != nullptr );

        VkWriteDescriptorSet* pLastWrite = &writes.getLast();
        if( pLastWrite->pBufferInfo == nullptr )
        {
            KEEN_ASSERT( pLastWrite->descriptorCount == 0u );
            pLastWrite->pBufferInfo = pBufferInfo;
        }

        return pBufferInfo;
    }

    inline VkDescriptorImageInfo* VulkanDescriptorSetWriter::pushImageInfo()
    {
        if( imageInfos.getRemainingCapacity() == 0u )
        {
            flush();
        }

        VkDescriptorImageInfo* pImageInfo = imageInfos.pushBackZero();
        KEEN_ASSERT( pImageInfo != nullptr );

        VkWriteDescriptorSet* pLastWrite = &writes.getLast();
        if( pLastWrite->pImageInfo == nullptr )
        {
            KEEN_ASSERT( pLastWrite->descriptorCount == 0u );
            pLastWrite->pImageInfo = pImageInfo;
        }

        return pImageInfo;
    }
}