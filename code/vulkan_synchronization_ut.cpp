#include "vulkan_synchronization.hpp"

#include "keen/base/unit_test.hpp"


namespace keen
{
    constexpr GraphicsOptionalShaderStageMask TestOptionalShaderStageMask = {};

    struct VulkanImageBarrierResult
    {
        VkPipelineStageFlags    srcStageMask;
        VkPipelineStageFlags    dstStageMask;
        VkAccessFlags           srcAccessMask;
        VkAccessFlags           dstAccessMask;
    };

    struct VulkanGlobalBarrierResult
    {
        VkPipelineStageFlags    srcStageMask;
        VkPipelineStageFlags    dstStageMask;
        VkAccessFlags           srcAccessMask;
        VkAccessFlags           dstAccessMask;
    };

    class VulkanSynchronizationTestFixture : public UnitTest
    {
    public:
        void testGlobalBarrier( const GraphicsAccessMask& oldAccessMask, const GraphicsAccessMask& newAccessMask, const VulkanGlobalBarrierResult& expectedResult )
        {
            GraphicsMemoryBarrier barrier{};
            barrier.oldAccessMask = oldAccessMask;
            barrier.newAccessMask = newAccessMask;

            const VulkanMemoryBarrier memoryBarrier = vulkan::getVulkanMemoryBarrier( barrier, TestOptionalShaderStageMask );

            KEEN_UT_COMPARE_UINT32( memoryBarrier.srcStageMask, expectedResult.srcStageMask );
            KEEN_UT_COMPARE_UINT32( memoryBarrier.dstStageMask, expectedResult.dstStageMask );
            KEEN_UT_COMPARE_UINT32( memoryBarrier.barrier.srcAccessMask, expectedResult.srcAccessMask );
            KEEN_UT_COMPARE_UINT32( memoryBarrier.barrier.dstAccessMask, expectedResult.dstAccessMask );
        }

        void testImageBarrier( const GraphicsAccessMask& oldAccessMask, const GraphicsAccessMask& newAccessMask, const VulkanImageBarrierResult& expectedResult )
        {
            // :JK: we have to supply a texture - but it's fine if that is completly empty
            VulkanTexture fakeTexture{};

            GraphicsTextureBarrier textureBarrier;
            textureBarrier.oldAccessMask    = oldAccessMask;
            textureBarrier.newAccessMask    = newAccessMask;
            textureBarrier.pTexture         = &fakeTexture; 

            const VulkanImageMemoryBarrier imageBarrier = vulkan::getVulkanImageMemoryBarrier( textureBarrier, TestOptionalShaderStageMask );

            KEEN_UT_COMPARE_UINT32( imageBarrier.srcStageMask, expectedResult.srcStageMask );
            KEEN_UT_COMPARE_UINT32( imageBarrier.dstStageMask, expectedResult.dstStageMask );
            KEEN_UT_COMPARE_UINT32( imageBarrier.barrier.srcAccessMask, expectedResult.srcAccessMask );
            KEEN_UT_COMPARE_UINT32( imageBarrier.barrier.dstAccessMask, expectedResult.dstAccessMask );
        }
    };

    KEEN_UNIT_TEST_F( VulkanSynchronizationTestFixture, testImageBarriers )
    {
        testImageBarrier( {}, GraphicsAccessFlag::Transfer_Write, 
            { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0 } );

        testImageBarrier( GraphicsAccessFlag::Transfer_Write, GraphicsAccessFlag::FS_Read_SampledImage, 
            { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testGlobalBarrier( GraphicsAccessFlag::CS_Write, GraphicsAccessFlag::CS_Read_Other,
            { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testGlobalBarrier( GraphicsAccessFlag::CS_Read_Other, GraphicsAccessFlag::CS_Write, { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0 } );

        testGlobalBarrier( GraphicsAccessFlag::CS_Write, GraphicsAccessFlag::IndexBuffer,
            { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDEX_READ_BIT } );

        {           
            const GraphicsAccessMask oldAccessMask = { GraphicsAccessFlag::CS_Write };          
            const GraphicsAccessMask newAccessMask = { GraphicsAccessFlag::IndexBuffer, GraphicsAccessFlag::CS_Read_UniformBuffer };
            testGlobalBarrier( oldAccessMask, newAccessMask,
                { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT } );
        }

        testGlobalBarrier( GraphicsAccessFlag::CS_Write, GraphicsAccessFlag::IndirectBuffer,
            { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::CS_Write, GraphicsAccessFlag::FS_Read_SampledImage, 
            { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        {
            const GraphicsAccessMask oldAccessMask = { GraphicsAccessFlag::CS_Write };
            const GraphicsAccessMask newAccessMask = { GraphicsAccessFlag::IndirectBuffer, GraphicsAccessFlag::FS_Read_UniformBuffer };
            testGlobalBarrier( oldAccessMask, newAccessMask, 
                { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT } );
        }

        testImageBarrier( GraphicsAccessFlag::ColorAttachment_Write, GraphicsAccessFlag::CS_Read_SampledImage,
            { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::DepthStencilAttachment_Write, GraphicsAccessFlag::CS_Read_SampledImage,
            { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::DepthStencilAttachment_Write, GraphicsAccessFlag::FS_Read_DepthStencilInputAttachment,
            { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::DepthStencilAttachment_Write, GraphicsAccessFlag::FS_Read_SampledImage,
            { VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::ColorAttachment_Write, GraphicsAccessFlag::FS_Read_ColorInputAttachment,
            { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_INPUT_ATTACHMENT_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::ColorAttachment_Write, GraphicsAccessFlag::FS_Read_SampledImage,
            { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::ColorAttachment_Write, GraphicsAccessFlag::VS_Read_SampledImage,
            { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::FS_Read_SampledImage, GraphicsAccessFlag::ColorAttachment_Write,
            { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0 } );

        testGlobalBarrier( {}, GraphicsAccessFlag::Transfer_Read, { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0 } );

        testGlobalBarrier( GraphicsAccessFlag::Transfer_Write, GraphicsAccessFlag::VertexBuffer,
            { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT } );

        testImageBarrier( GraphicsAccessFlag::Transfer_Write, GraphicsAccessFlag::FS_Read_SampledImage,
            { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT } );

        //testImageBarrier( GraphicsAccessFlag::ColorAttachment_Write, GraphicsAccessFlag::Present,
        //  { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 } );

        testGlobalBarrier( GraphicsAccessFlag::General, GraphicsAccessFlag::General,
            { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT } );
    }

}
