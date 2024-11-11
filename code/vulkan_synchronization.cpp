#include "vulkan_synchronization.hpp"

namespace keen
{
    struct VulkanAccessInfo
    {
        VkPipelineStageFlags    stageMask;
        VkAccessFlags           accessMask;
    };

    static VulkanAccessInfo getVulkanAccessInfo( GraphicsAccessFlag accessType, GraphicsOptionalShaderStageMask optionalShaderStages )
    {
        VkPipelineStageFlags anyShaderFlags =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        if( optionalShaderStages.isSet( GraphicsOptionalShaderStageFlag::GeometryShader ) )
        {
            anyShaderFlags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        }
        if( optionalShaderStages.isSet( GraphicsOptionalShaderStageFlag::TessellationShaders ) )
        {
            anyShaderFlags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
        }

        switch( accessType )
        {
        case GraphicsAccessFlag::IndirectBuffer:                        return { VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT };
        case GraphicsAccessFlag::IndexBuffer:                           return { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_INDEX_READ_BIT };
        case GraphicsAccessFlag::VertexBuffer:                          return { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT };
        case GraphicsAccessFlag::VS_Read_UniformBuffer:                 return { VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT };
        case GraphicsAccessFlag::VS_Read_SampledImage:                  return { VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::VS_Read_Other:                         return { VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::FS_Read_UniformBuffer:                 return { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT };
        case GraphicsAccessFlag::FS_Read_SampledImage:                  return { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::FS_Read_ColorInputAttachment:          return { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT };
        case GraphicsAccessFlag::FS_Read_DepthStencilInputAttachment:   return { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT };
        case GraphicsAccessFlag::FS_Read_Other:                         return { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::ColorAttachment_Read:                  return { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT };
        case GraphicsAccessFlag::DepthStencilAttachment_Read:           return { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT };
        case GraphicsAccessFlag::CS_Read_UniformBuffer:                 return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_UNIFORM_READ_BIT };
        case GraphicsAccessFlag::CS_Read_SampledImage:                  return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::CS_Read_Other:                         return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::AnyShader_Read_UniformBuffer:          return { anyShaderFlags, VK_ACCESS_UNIFORM_READ_BIT };
        case GraphicsAccessFlag::AnyShader_Read_UniformOrVertexBuffer:  return { anyShaderFlags, VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT };
        case GraphicsAccessFlag::AnyShader_Read_SampledImage:           return { anyShaderFlags, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::AnyShader_Read_Other:                  return { anyShaderFlags, VK_ACCESS_SHADER_READ_BIT };
        case GraphicsAccessFlag::Transfer_Read:                         return { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT };
        case GraphicsAccessFlag::Host_Read:                             return { VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT };
        case GraphicsAccessFlag::Present:                               return { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0 };
        case GraphicsAccessFlag::VS_Write:                              return { VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT };
        case GraphicsAccessFlag::FS_Write:                              return { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT };
        case GraphicsAccessFlag::ColorAttachment_Write:                 return { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
        case GraphicsAccessFlag::DepthStencilAttachment_Write:          return { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
        case GraphicsAccessFlag::CS_Write:                              return { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT };
        case GraphicsAccessFlag::AnyShader_Write:                       return { anyShaderFlags, VK_ACCESS_SHADER_WRITE_BIT };
        case GraphicsAccessFlag::Transfer_Write:                        return { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT };
        case GraphicsAccessFlag::Host_Write:                            return { VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT };
        case GraphicsAccessFlag::General:                               return { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT };
        }

        KEEN_BREAK( "invalid access mode" );
        return {};
    }

    template<typename VulkanBarrier, typename GraphicsBarrier>
        static void writeVulkanBarrier( VulkanBarrier* pVulkanBarrier, const GraphicsBarrier& barrier, bool forceVisibility, GraphicsOptionalShaderStageMask optionalShaderStages )
    {
        for( size_t i = barrier.oldAccessMask.findFirstSet(); i < barrier.oldAccessMask.getIndexCount(); i = barrier.oldAccessMask.findNextSet( i ) )
        {
            KEEN_ASSERT( barrier.oldAccessMask.isIndexSet( i ) );

            const GraphicsAccessFlag oldAccess = barrier.oldAccessMask.getFlag( i );
            const VulkanAccessInfo oldAccessInfo = getVulkanAccessInfo( oldAccess, optionalShaderStages );

            pVulkanBarrier->srcStageMask |= oldAccessInfo.stageMask;

            // Add appropriate availability operations - for writes only.
            if( GraphicsWriteAccessMask.isSet( oldAccess ) )
            {
                pVulkanBarrier->barrier.srcAccessMask |= oldAccessInfo.accessMask;
            }
        }

        for( size_t i = barrier.newAccessMask.findFirstSet(); i < barrier.newAccessMask.getIndexCount(); i = barrier.newAccessMask.findNextSet( i ) )
        {
            KEEN_ASSERT( barrier.newAccessMask.isIndexSet( i ) );

            const GraphicsAccessFlag newAccess = barrier.newAccessMask.getFlag( i );
            const VulkanAccessInfo newAccessInfo = getVulkanAccessInfo( newAccess, optionalShaderStages );

            pVulkanBarrier->dstStageMask |= newAccessInfo.stageMask;

            // Add visibility operations as necessary.
            // If the src access mask is zero, this is a WAR hazard (or for some reason a "RAR"),
            // so the dst access mask can be safely zeroed as these don't need visibility.
            if( pVulkanBarrier->barrier.srcAccessMask != 0u || forceVisibility )
            {
                pVulkanBarrier->barrier.dstAccessMask |= newAccessInfo.accessMask;
            }
        }

        // Ensure that the stage masks are valid if no stages were determined
        if( pVulkanBarrier->srcStageMask == 0 )
        {
            pVulkanBarrier->srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }

        if( pVulkanBarrier->dstStageMask == 0 )
        {
            pVulkanBarrier->dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
    }

    VulkanMemoryBarrier vulkan::getVulkanMemoryBarrier( const GraphicsMemoryBarrier& barrier, GraphicsOptionalShaderStageMask optionalShaderStages )
    {
        VulkanMemoryBarrier result{ 0u, 0u, { VK_STRUCTURE_TYPE_MEMORY_BARRIER } };

        writeVulkanBarrier( &result, barrier, false, optionalShaderStages );

        return result;
    }

    VulkanImageMemoryBarrier vulkan::getVulkanImageMemoryBarrier( const GraphicsTextureBarrier& barrier, GraphicsOptionalShaderStageMask optionalShaderStages )
    {
        VulkanImageMemoryBarrier result{ 0u, 0u, { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER } };

        const GraphicsTexture* pTexture                         = barrier.pTexture;
        GraphicsTextureSubresourceRange viewedSubresourceRange  = barrier.subresourceRange;
        graphics::resolveViewedTextureSubresourceRange( &pTexture, &viewedSubresourceRange );

        const VulkanTexture* pVulkanTexture = (const VulkanTexture*)pTexture;
        result.barrier.image                = pVulkanTexture->image;
        result.barrier.subresourceRange     = vulkan::getImageSubresourceRange( viewedSubresourceRange );
        result.barrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
        result.barrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;

        result.barrier.oldLayout = getImageLayout( barrier.oldLayout );
        result.barrier.newLayout = getImageLayout( barrier.newLayout );

        writeVulkanBarrier( &result, barrier, result.barrier.oldLayout != result.barrier.newLayout, optionalShaderStages );

        return result;
    }

}
