#ifndef KEEN_INCLUDED_VULKAN_DESCRIPTOR_SET_WRITER_HPP
#define KEEN_INCLUDED_VULKAN_DESCRIPTOR_SET_WRITER_HPP

#include "keen/graphics/graphics_types.hpp"
#include "vulkan_api.hpp"

namespace keen
{
    class VulkanDescriptorSetWriter
    {
    public:
        explicit VulkanDescriptorSetWriter( VulkanApi* pVulkan );
        ~VulkanDescriptorSetWriter();

        void startWriteDescriptors( VkDescriptorSet descriptorSet, const VulkanDescriptorSetLayout* pDescriptorSetLayout, uint32 bindingIndex, GraphicsDescriptorType descriptorType, uint32 startDescriptorIndex = 0u, uint32 descriptorCount = 1u );
        void writeDescriptor( const GraphicsDescriptorData& descriptorData );

        void flush();

    private:
        VulkanApi*                                  pVulkan;

        DynamicArray<VkWriteDescriptorSet, 64u>     writes;
        DynamicArray<VkDescriptorBufferInfo, 64u>   bufferInfos;
        DynamicArray<VkDescriptorImageInfo, 64u>    imageInfos;

        uint32                                      writeDescriptorCount;

        VkWriteDescriptorSet*   pushWrite();
        VkDescriptorBufferInfo* pushBufferInfo();
        VkDescriptorImageInfo*  pushImageInfo();
    };
}

#include "vulkan_descriptor_set_writer.inl"

#endif
