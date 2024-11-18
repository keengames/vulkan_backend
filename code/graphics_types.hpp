#ifndef KEEN_GRAPHICS_TYPES_HPP_INCLUDED
#define KEEN_GRAPHICS_TYPES_HPP_INCLUDED

#include "keen/base/bitmask.hpp"
#include "keen/base/bitmask.hpp"
#include "keen/base/optional.hpp"
#include "keen/base/static_array.hpp"
#include "keen/base/static_bit_array.hpp"
#include "keen/base/static_string.hpp"
#include "keen/base/intrinsics.hpp"
#include "keen/base/dynamic_array.hpp"
#include "keen/base/internallist.hpp"
#include "keen/base/pixel_format.hpp"
#include "keen/base/debug_name.hpp"
#include "keen/math/quaternion.hpp"
#if KEEN_USING( KEEN_OS_WINDOW_SUPPORT )
#	include "keen/os_gui/window_types.hpp"
#endif
#if KEEN_USING( KEEN_USE_DEBUG_RENDERER )
#	include "keen/debug_renderer/debug_renderer.hpp"
#endif

#define KEEN_GRAPHICS_DEBUG_NAMES						KEEN_OFF_IN_MASTER_BUILD
#define KEEN_GRAPHICS_DEBUG_CODE						KEEN_OFF_IN_MASTER_BUILD
#define KEEN_GRAPHICS_DEBUG_CALLBACK					KEEN_OFF_IN_MASTER_BUILD
#define KEEN_GRAPHICS_OLD_STORAGE_BUFFER_DESCRIPTORS	KEEN_ON

#if	defined( KEEN_PLATFORM_WIN32 ) || defined( KEEN_PLATFORM_LINUX )
#	define KEEN_GRAPHICS_LAME_STATIC_SAMPLERS			KEEN_ON
#else
	// on nice platforms this is encoded in the shader binary
#	define KEEN_GRAPHICS_LAME_STATIC_SAMPLERS			KEEN_OFF
#endif

#if defined( KEEN_PLATFORM_WIN32 ) || defined( KEEN_PLATFORM_LINUX )
#	define KEEN_GRAPHICS_BIN2C_SHADER_FILEPATH( path ) KEEN_CONCATENATE( path, _spv )
#elif defined( KEEN_PLATFORM_PS5 )
#	define KEEN_GRAPHICS_BIN2C_SHADER_FILEPATH( path ) KEEN_CONCATENATE( path, _ags )
#elif defined( KEEN_PLATFORM_XBS )
#	define KEEN_GRAPHICS_BIN2C_SHADER_FILEPATH( path ) KEEN_CONCATENATE( path, _xbs )
#endif

namespace keen
{

	class MemoryAllocator;
	struct TaskSystem;
	struct MemorySystem;
	struct EventNotifier;
	struct VertexFormat;

#if KEEN_USING( KEEN_DEBUG_UI )
	struct DebugUiDrawContext;
#endif

#if KEEN_USING( KEEN_USE_DEBUG_RENDERER )
	struct DebugRenderBuffer;
	class Camera;
#endif

	using GraphicsFrameId = uint64;
	struct GraphicsTransferBatchId { uint64 value; };

	// graphics types (most are opaque)
	struct GraphicsApiInstance;
	struct GraphicsSystemParameters;
	struct GraphicsSystem;
	struct GraphicsFrame;
	struct GraphicsTransferBatch;
	struct GraphicsCommandBuffer;
	struct GraphicsPipelineLayout;
	struct GraphicsPipelineLayoutParameters;
	struct GraphicsRenderPipelineParameters;
	struct GraphicsComputePipelineParameters;
	struct GraphicsRenderPipeline;
	struct GraphicsComputePipeline;
	struct GraphicsDeviceMemoryParameters;
	struct GraphicsDeviceMemory;
	struct GraphicsBufferParameters;
	struct GraphicsStagingHeap;
	struct GraphicsDynamicBuffer;
	struct GraphicsBuffer;
	struct GraphicsTextureParameters;
	struct GraphicsDownloadQueue;
	struct GraphicsTexture;
	struct GraphicsRenderTargetParameters;
	struct GraphicsSampler;
	struct GraphicsSamplerParameters;
	struct GraphicsSwapChain;
	struct GraphicsDeviceComputePipeline;
	struct GraphicsDescriptorSetLayout;
	struct GraphicsDescriptorSet;
	struct GraphicsQueryPool;
	struct GraphicsCopyPipeline;

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
	struct GraphicsRayTraceMesh;
	struct GraphicsRayTraceScene;
	struct GraphicsRayTracePipelineParameters;
	struct GraphicsRayTracePipeline;
#endif

	enum class GraphicsApi : uint8
	{
		Invalid = 0u,
		Stub,
		Direct3D9,
		Direct3D11,
		GL,
		GLes3,
		Metal,
		GCM,
		XGraphics,
		GNM,
		GX2,
		Mantle,
		Direct3D12,
		Vulkan,
		GLes2,
		Nvn,
		Direct3D11x,
		Agc,
		Direct3D12x,
		Count
	};

	enum class GraphicsSystemCreateError : uint32
	{
		Ok,
		Generic,
		DriverTooOld,
		NoCompatibleDeviceFound,
	};

	struct GraphicsSystemCreateResult
	{
		GraphicsSystemCreateError	error = GraphicsSystemCreateError::Generic;
		GraphicsSystem*				pGraphicsSystem = nullptr;

		GraphicsSystemCreateResult( GraphicsSystemCreateError error ) : error( error ) {}
		explicit GraphicsSystemCreateResult( GraphicsSystem* pGraphicsSystem ) : error( GraphicsSystemCreateError::Ok ), pGraphicsSystem( pGraphicsSystem ) {}
	};

	enum class GraphicsDeviceObjectType : uint8
	{
		SwapChain,
		PipelineLayout,
		RenderPipeline,
		ComputePipeline,
		DeviceMemory,
		Buffer,
		Texture,
		Sampler,
		DescriptorSetLayout,
		DescriptorSet,
		QueryPool,

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		RayTraceMesh,
		RayTraceScene,
		RayTracePipeline,
#endif
		Count
	};

	constexpr size_t GraphicsDeviceObjectType_Count = (size_t)GraphicsDeviceObjectType::Count;

	// :JK: these are the compile-time limits.. the actual device might have lower limits
	constexpr uint32 GraphicsLimits_MaxColorTargetCount = 7u;

	constexpr uint32 GraphicsLimits_MaxDescriptorSetSlotCount = 4u;
	constexpr uint32 GraphicsLimits_MaxPushConstantBufferSize = 16u;

	constexpr uint64 GraphicsLimits_MaxDeviceMemorySizeInBytes = 1_gib;
	constexpr uint64 GraphicsLimits_MaxUniformBufferOffsetAlignment = 256u;		// worst case uniform buffer offset alignment
	constexpr uint64 GraphicsLimits_MaxStorageBufferOffsetAlignment = 256u;		// worst case storage buffer offset alignment

	constexpr uint32 GraphicsLimits_MaxVertexFormatCount	= 128u;

	constexpr uint32 GraphicsLimits_MaxRenderTargetPixelFormatCount = 2u;	// how many pixel format views per render target?

	constexpr uint32 GraphicsLimits_MaxProfileEventCount = (uint)4_kib;		// per frame.

	constexpr uint32 GraphicsLimits_MaxImmediateGeometryVertexDataSize = 256u * 1024u;

	constexpr size_t GraphicsLimits_MaxTextureBarrierCount	= 64u;
	constexpr size_t GraphicsLimits_MaxBarrierBatchCount	= 64u;		// the maximum cache size for batching barriers

	constexpr size_t GraphicsLimits_MaxTransferSubmitCount = 16u;

	constexpr uint32 Graphics_ReservedBindlessTextureCount = 8u;
	constexpr uint32 Graphics_ReservedBindlessSamplerCount = 1u;

	constexpr uint32 GraphicsLimits_MaxBindlessTextureCount = 16384u;
	constexpr uint32 GraphicsLimits_MaxBindlessSamplerCount = 128u;

	struct GraphicsElementRange
	{
		uint32	offset;
		uint32	count;
	};

	struct GraphicsMemoryRange
	{
		uint64	offset;
		uint64	size;
	};

	struct GraphicsDeviceMemoryRange
	{
		const GraphicsDeviceMemory*	pDeviceMemory;
		uint64						offset;
		uint64						size;
	};


	// Shader matrix types are optimized for the use in gpu shader programs
	// you should use them exclusively in shader constant buffers

	struct GraphicsMatrix44
	{
		float4	row0;
		float4	row1;
		float4	row2;
		float4	row3;
	};

	struct GraphicsMatrix43
	{
		float4	row0;
		float4	row1;
		float4	row2;
	};

	// this is valid for vulkan, d3d12 and gnm, check future platforms please
	struct GraphicsDrawIndirectParameter
	{
		uint32 vertexCount;
		uint32 instanceCount;
		uint32 vertexOffset;
		uint32 instanceOffset;
	};

	struct GraphicsDrawIndexedIndirectParameter
	{
		uint32 indexCount;
		uint32 instanceCount;
		uint32 indexOffset;
		sint32 vertexOffset;
		uint32 instanceOffset;
	};

	struct GraphicsDispatchIndirectParameter
	{
		uint32 groupCountX;
		uint32 groupCountY;
		uint32 groupCountZ;
	};

	using GraphicsDeviceMemoryTypeIndex	= uint8;
	using GraphicsDeviceMemoryTypeMask	= uint32;

	enum class GraphicsDeviceMemoryPriority : uint8
	{
		Lowest,
		Low,
		Normal,
		High,
		Highest
	};

	struct GraphicsDeviceMemoryParameters
	{
		GraphicsDeviceMemoryTypeIndex		memoryTypeIndex = 0u;
		uint64								sizeInBytes = 0u;
		bool								mapMemory = false;
		Optional<const GraphicsBuffer*>		dedicatedBuffer = {};
		Optional<const GraphicsTexture*>	dedicatedTexture = {};
		GraphicsDeviceMemoryPriority		priority = GraphicsDeviceMemoryPriority::Normal;
		DebugName							debugName = {};
	};

	struct GraphicsDeviceMemoryInfo
	{
		GraphicsDeviceMemoryTypeIndex	memoryTypeIndex;
		uint64							sizeInBytes;
	};

	struct GraphicsMemoryRequirements
	{
		uint64							size;
		uint64							alignment;
		GraphicsDeviceMemoryTypeMask	supportedDeviceMemoryTypeIndices;
		bool							prefersDedicatedAllocation = false;
		bool							requiresDedicatedAllocation = false;
	};

	struct GraphicsBufferMemoryBinding
	{
		const GraphicsBuffer*		pBuffer;
		GraphicsDeviceMemoryRange	memoryRange;
	};

	struct GraphicsTextureMemoryBinding
	{
		const GraphicsTexture*		pTexture;
		GraphicsDeviceMemoryRange	memoryRange;
	};

	enum class GraphicsPipelineStage : uint8 
	{
		// reorder to match actual graphics pipeline?!
		Vertex, 
		Fragment, 
		
		Compute,

		TS_Control,
		TS_Evaluation,

		// mesh shader
		MS_Task,
		MS_Mesh,

		// ray tracing..
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		RT_Raygen,
		RT_AnyHit,
		RT_ClosestHit,
		RT_Miss,
		RT_Intersection,
		RT_Callable,
#endif
	};

	using GraphicsPipelineStageMask = Bitmask16<GraphicsPipelineStage>;
	static constexpr GraphicsPipelineStageMask GraphicsPipelineStageMask_AllRender		= { GraphicsPipelineStage::Vertex, GraphicsPipelineStage::TS_Control, GraphicsPipelineStage::TS_Evaluation, GraphicsPipelineStage::Fragment };
	static constexpr GraphicsPipelineStageMask GraphicsPipelineStageMask_AllMesh		= { GraphicsPipelineStage::MS_Task, GraphicsPipelineStage::MS_Mesh, GraphicsPipelineStage::Fragment };

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
	static constexpr GraphicsPipelineStageMask GraphicsPipelineStageMask_AllRaytrace	= { GraphicsPipelineStage::RT_Raygen, GraphicsPipelineStage::RT_AnyHit, GraphicsPipelineStage::RT_ClosestHit, GraphicsPipelineStage::RT_Miss, GraphicsPipelineStage::RT_Intersection, GraphicsPipelineStage::RT_Callable };
#endif

	struct GraphicsPipelineEvent
	{
		UserData						userData;

		// nullptr iff the compilation failed
		union
		{
			const GraphicsRenderPipeline*	pRenderPipeline;
			const GraphicsComputePipeline*	pComputePipeline;
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
			const GraphicsRayTracePipeline*	pRayTracePipeline;
#endif
		};
	};

	enum class GraphicsQueueId : uint8
	{
		Main,
		Transfer,
	};

	struct GraphicsRectangle
	{
		sint32			x;
		sint32			y;
		uint32			width;
		uint32			height;
	};

	enum class GraphicsPrimitiveType : uint8
	{
		TriangleList,
		TriangleStrip,
		LineList,
		PatchList,
		Count
	};

	enum class GraphicsTilingMode : uint8
	{
		None,
		TwoVertical,
		FourVertical,
		TwoHorizontal,
		FourHorizontal
	};

	enum class GraphicsFeature : uint8
	{
		Mrt,
		ShadowSampler,
		VolumeTexture,
		Compute,
		BackBufferSampling,
		TessellationShader,
		SamplerReduction,
		DepthResolveModeMinMax,
		DepthResolveModeAverage,
		ShaderFloat16,
	};

	using GraphicsFeatureFlags = Bitmask32<GraphicsFeature>;

	enum class GraphicsInternalVertexFormatId : uint8
	{
		Pos2f,				// float2 position
		Pos2f_Tex2f,		// float2 position, float2 texCoord0
		Immediate,			// float3 position, uint32 color, float2 texCoord
		Debug,				// float3 position, uint32 color (r8g8b8a8 unorm)
		Count
	};

	enum class GraphicsInternalTexture : uint8
	{
		WhiteRGBA8,
		CheckerboardRGBA8,
		BlackRGBA8,
		BlackTransparentRGBA8,	// RGBA 0,0,0,0
		Red_RGBA8,
		Green_RGBA8,
		Blue_RGBA8,
		Magenta_RGBA8,

		WhiteRGBA8_3d,
		BlackTransparentRGBA8_3d,

		Count,
	};

	enum class GraphicsTextureUsageFlag : uint8
	{
		TransferSource,
		TransferTarget,
		Render_ShaderResource,			// rename to Sampled
		Render_ColorTarget,
		Render_DepthTarget,
		Render_StencilTarget,			// :JK: rename to StencilAttachment
		Render_ShaderStorage,		
	};
	using GraphicsTextureUsageMask = Bitmask8<GraphicsTextureUsageFlag>;
	constexpr GraphicsTextureUsageMask GraphicsTextureUsageFlagMask_Render_DepthStencilTarget = { GraphicsTextureUsageFlag::Render_DepthTarget, GraphicsTextureUsageFlag::Render_StencilTarget };

	enum class GraphicsTextureFlag : uint8
	{
		PreferHostMemory,
	};
	using GraphicsTextureFlagMask = Bitmask8<GraphicsTextureFlag>;

	enum class GraphicsTextureLayout : uint8
	{
		Undefined,
		General,
		ColorAttachmentOptimal,
		DepthAttachmentOptimal,
		DepthStencilReadOnlyOptimal,
		StencilAttachmentOptimal,
		DepthStencilAttachmentOptimal,
		ShaderReadOnlyOptimal,
		TransferSourceOptimal,
		TransferTargetOptimal,
#if KEEN_USING( KEEN_GRAPHICS_VALIDATION )
		InvalidatedByAlias,
#endif
	};

	enum class GraphicsRenderTargetAspect : uint8
	{
		Color0,
		Color1,
		Color2,
		Color3,
		Depth,
		Stencil,
		Count
	};

	using GraphicsRenderTargetAspectMask = Bitmask8<GraphicsRenderTargetAspect>;

	// :JK: there is a limit of around 30 color values that are fast on nvidia gpus.. so we should keep the actual number we use as small as possible:
	enum class GraphicsColorClearValue : uint8
	{
		RGBA_Zero,					// all zero
		RGB_Zero_Alpha_One,			// solid black
		RGB_Half_Alpha_One,			// { 0.5, 0.5, 0.5, 1.0 }
		RGBA_One,					// { 1.0, 1.0, 1.0, 1.0 }

		// for testing:
		Red,						// { 1.0, 0.0, 0.0, 1.0 }
		Green,						// { 0.0, 1.0, 0.0, 1.0 }
		Blue,						// { 0.0, 0.0, 1.0, 1.0 }
		Magenta,					// { 1.0, 0.0, 1.0, 1.0 }
		Cyan,						// { 0.0, 1.0, 1.0, 1.0 }
		Yellow,						// { 1.0, 1.0, 0.0, 1.0 }

		Fsr3AutoExposure, 			// { -1.f, 1e8f, 0.f, 0.f }
	};

	enum class GraphicsDepthClearValue : uint8
	{
		Zero,
		One
	};

	using GraphicsStencilClearValue = uint8;

	union GraphicsClearValue
	{
		GraphicsColorClearValue		color;
		GraphicsDepthClearValue		depth;
		GraphicsStencilClearValue	stencil;
	};

	enum class GraphicsBufferUsageFlag : uint8
	{
		TransferSource,
		TransferTarget,
		UniformBuffer,
		StorageBuffer,		// :JK: either ByteAddressBuffer or StructuredBuffer
		IndexBuffer,
		VertexBuffer,
		ArgumentBuffer,		// indirect draw parameters

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		RayTraceScratchBuffer,
		RayTraceMeshBuffer,
		RayTraceInstanceBuffer,
		ShaderBindingTableBuffer,
#endif
		Count
	};
	constexpr size_t GraphicsBufferUsageFlagCount = (size_t)GraphicsBufferUsageFlag::Count;
	using GraphicsBufferUsageMask = Bitmask32<GraphicsBufferUsageFlag>;

	enum class GraphicsAccessModeFlag : uint8
	{
		Read,
		Write,
	};

	using GraphicsAccessMode = Bitmask8<GraphicsAccessModeFlag>;

	constexpr GraphicsAccessMode GraphicsAccessMode_None		= {};
	constexpr GraphicsAccessMode GraphicsAccessMode_ReadOnly	= { GraphicsAccessModeFlag::Read };
	constexpr GraphicsAccessMode GraphicsAccessMode_WriteOnly	= { GraphicsAccessModeFlag::Write };
	constexpr GraphicsAccessMode GraphicsAccessMode_ReadWrite	= { GraphicsAccessModeFlag::Read, GraphicsAccessModeFlag::Write };

	enum class GraphicsLoadAction : uint8
	{
		DontCare,
		Load,
		Clear,
	};

	enum class GraphicsStoreAction : uint8
	{
		DontCare,
		Store,
		None,
	};

	enum class GraphicsIndexFormat : uint8
	{
		Invalid,
		Uint16,
		Uint32,
	};

	enum class GraphicsPipelineEntryPointId : uint8
	{
		Internal_Stage_Main,		// "internal_{fs|vs|cs|...}_main"
		Stage_Main,					// "{fs|vs|cs|...}_main"
	};

	enum class GraphicsComparisonFunction : uint8
	{
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always,
	};

	struct GraphicsTextureParameters
	{
		DebugName								debugName = {};
		uint32									width = 0u;
		uint32									height = 0u;
		uint32									depth = 1u;
		uint16									layerCount = 1u;
		uint8									levelCount = 1u;
		uint8									sampleCount = 1u;
		TextureType								type = TextureType::Texture2D;
		PixelFormat								format = PixelFormat::None;
		GraphicsTextureUsageMask				usageMask = GraphicsTextureUsageFlag::Render_ShaderResource;
		GraphicsQueueId							ownerQueue = GraphicsQueueId::Main;
		bool									allocateMemory = true;
		GraphicsDeviceMemoryRange				deviceMemoryRange = {};
		GraphicsTextureFlagMask					flags = {};
		ArrayView<const PixelFormat>			viewFormats = {};
		GraphicsClearValue						clearValue = {}; // all-zero for all pixel formats
		Optional<GraphicsComparisonFunction>	preferredComparisonFunction = {};
	};

	struct GraphicsBufferRange
	{
		const GraphicsBuffer*	pBuffer;
		uint64					offset;
		uint64					size;

		constexpr bool isValid() const { return pBuffer != nullptr; }
		constexpr bool isInvalid() const { return pBuffer == nullptr; }
		constexpr void setInvalid() { pBuffer = nullptr; }
	};


	struct GraphicsStridedBufferRange
	{
		const GraphicsBuffer*	pBuffer;
		uint64					offset;
		uint64					stride;
		uint64					size;
	};

	enum class GraphicsTextureAspectFlag : uint8
	{
		Color,
		Depth,
		Stencil,
	};

	using GraphicsTextureAspectFlagMask = Bitmask8<GraphicsTextureAspectFlag>;

	struct GraphicsTextureSubresourceRange
	{
		GraphicsTextureAspectFlagMask	aspectMask = GraphicsTextureAspectFlag::Color;
		uint32							firstArrayLayer = 0u;
		uint32							arrayLayerCount = 1u;
		uint32							firstMipLevel = 0u;
		uint32							mipLevelCount = 1u;
	};

	struct GraphicsTextureViewParameters
	{
		const GraphicsTexture*			pTexture = nullptr;
		TextureType						type = TextureType::Texture2D;
		PixelFormat						format = PixelFormat::None;
		GraphicsTextureUsageMask		usageMask = GraphicsTextureUsageFlag::Render_ShaderResource;
		GraphicsTextureSubresourceRange	subresourceRange = {};
		DebugName						debugName = {};
	};

	struct GraphicsTextureInfo
	{
		uint32						width;
		uint32						height;
		uint32						depth;
		uint8						levelCount;
		uint8						sampleCount;
		uint16						layerCount;
		GraphicsTextureUsageMask	usageMask;
		TextureType					type;
		PixelFormat					format;
		const GraphicsTexture*		pViewedTexture;
	};

	struct GraphicsBufferInfo
	{
		uint64						sizeInBytes;
	};

	struct GraphicsUploadBufferRange
	{
		GraphicsBufferRange		bufferRange;
		MemoryBlock				cpuMemory;		// mapped memory of the buffer (at offset)
	};

	struct GraphicsUploadTextureInfo
	{
		MemoryBlock				cpuMemory;		// mapped memory of the buffer (at offset)
		ImageMemoryInfo			memoryInfo;		// only for the first layer+level
		uint3					size;
		TextureType				type;
		PixelFormat				format;
		uint16					layerCount;
		uint8					levelCount;
	};

	struct GraphicsStagingAllocationId
	{
		uint64 value;
	};

	struct GraphicsStagingAllocation
	{
		GraphicsStagingAllocationId	id;
		GraphicsBufferRange			bufferRange;
		MemoryBlock					cpuMemory;		// mapped memory of the buffer (at offset)
	};

	struct GraphicsBufferUploadInfo
	{
		GraphicsStagingAllocationId	allocationId;
		const GraphicsBuffer*		pTarget;		// maybe use a GraphicsBufferRange
		uint64						targetOffset;
		GraphicsBufferRange			uploadBufferRange;
		MemoryBlock					cpuMemory;		// mapped memory of the buffer (at offset)
	};

	struct GraphicsIndexBufferInfo
	{
		GraphicsBufferRange		bufferRange;
		GraphicsIndexFormat		format;
	};

	struct GraphicsRangeAllocationId
	{
		uint32 value;
	};

	struct GraphicsRangeAllocation
	{
		GraphicsRangeAllocationId	id;
		uint32						offset;
		uint32						size;
	};

	struct GraphicsRenderTargetFormat
	{
		PixelFormat			colorTargetFormats[ GraphicsLimits_MaxColorTargetCount ] = {};
		PixelFormat			depthStencilTargetFormat = PixelFormat::None;

		uint8 getColorTargetCount() const
		{
			uint8 counter = 0;

			for( uint8 i = 0; i < GraphicsLimits_MaxColorTargetCount; ++i )
			{
				if( colorTargetFormats[ i ] == PixelFormat::None )
				{
					break;
				}

				counter += 1;
			}

			return counter;
		}
	};

	inline bool operator == ( const GraphicsRenderTargetFormat& lhs, const GraphicsRenderTargetFormat& rhs );
	inline bool operator != ( const GraphicsRenderTargetFormat& lhs, const GraphicsRenderTargetFormat& rhs );

	struct GraphicsBufferParameters
	{
		uint64						sizeInBytes = 0u;
		GraphicsBufferUsageMask		usage = {};
		GraphicsAccessMode			cpuAccess = {};		// :JK: maybe model this as GraphicsBufferUsageFlag ? GraphicsBufferUsageFlag::CpuWriteConsecutive, GraphicsBufferUsageFlag::CpuReadCached ?
		bool						allocateMemory = true;
#ifndef KEEN_BUILD_MASTER	// this is probably only ever useful for debugging, so disable it in master builds...
		bool						allocateDedicatedDeviceMemory = false;
#endif
		GraphicsDeviceMemoryRange	deviceMemoryRange = {};
		Optional<uint64>			alignment;
		DebugName					debugName = {};
	};

	enum class GraphicsDescriptorType : uint8
	{
		Invalid,
		Sampler,
		SamplerArray,
		SampledImage,			// Texture
		SampledImageArray,		// Texture[]
		StorageImage,			// RWTexture
		StorageImageArray,		// RWTexture[]
		UniformBuffer,			// ConstantBuffer?
		ByteAddressBuffer,
		StructuredBuffer,
		RWByteAddressBuffer,
		RWStructuredBuffer,
#if KEEN_USING( KEEN_GRAPHICS_OLD_STORAGE_BUFFER_DESCRIPTORS )
		StorageBuffer,			// legacy storage buffers.. remove next
#endif
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		RayTraceScene,
#endif
	};

	enum class GraphicsDescriptorSetLayoutBindingFlag : uint8
	{
		AllowInvalidDescriptor,
	};
	using GraphicsDescriptorSetLayoutBindingFlags = Bitmask8<GraphicsDescriptorSetLayoutBindingFlag>;

	enum class GraphicsDescriptorDataType : uint8
	{
		Unknown,
		Texture1D,
		Texture2D,
		Texture3D,
		TextureCube,
		Texture2DMS,
	};

	struct GraphicsDescriptorSetLayoutBinding
	{
		// :JK: memory layout is awkward.. would be nice to clean this up a bit 
		GraphicsDescriptorType						type;			// :JK: maybe split data type from usage pattern?
		GraphicsPipelineStageMask					stageMask;
		GraphicsDescriptorSetLayoutBindingFlags		flags;		
		uint32										arraySizeOrBufferStride = 0u;	// array size for XXXImageArray type, buffer stride for StructuredBuffer
		GraphicsDescriptorDataType					dataType;						// more specific data type -> maybe possible/better to fold this into the type?
	};

	struct GraphicsDescriptorData_Buffer
	{
		const GraphicsBuffer*		pBuffer;
		uint64						offset;
		uint32						stride;		// 1 for byte address buffers, size in bytes for uniform buffers and structured buffers
		uint32						count;		// size in bytes for byte address buffers, 1 for uniform buffers and number of elements for structured buffers
	};

	struct GraphicsDescriptorData_Image
	{
		const GraphicsTexture*		pTexture;
		GraphicsTextureLayout		layout;		// only relevant for SampledImage
	};

	struct GraphicsDescriptorData_ImageArray
	{
		const GraphicsDescriptorData_Image*	pFirstImage;
		uint32								imageCount;
		uint32								targetOffset;	// start offset in the array descriptor where we want to update the descriptors
	};

	struct GraphicsDescriptorData_Sampler
	{
		const GraphicsSampler*		pSampler;
	};

	struct GraphicsDescriptorData_SamplerArray
	{
		const GraphicsDescriptorData_Sampler*	pFirstSampler;
		uint32									samplerCount;
		uint32									targetOffset;
	};

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
	struct GraphicsDescriptorData_RayTraceScene
	{
		const GraphicsRayTraceScene*	pScene;
	};
#endif

	struct GraphicsDescriptorData
	{
		GraphicsDescriptorType		type;
		union
		{
			GraphicsDescriptorData_Buffer			buffer;
			GraphicsDescriptorData_Image			image;
			GraphicsDescriptorData_ImageArray		imageArray;
			GraphicsDescriptorData_Sampler			sampler;
			GraphicsDescriptorData_SamplerArray		samplerArray;
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
			GraphicsDescriptorData_RayTraceScene	scene;
#endif
		};

		bool isValid() const { return type != GraphicsDescriptorType::Invalid; }
		bool isInvalid() const { return type == GraphicsDescriptorType::Invalid; }
	};

	constexpr GraphicsDescriptorData Graphics_InvalidDescriptor				 = { GraphicsDescriptorType::Invalid };
	constexpr uint32				 Graphics_InvalidBindlessDescriptorIndex = 0u;

	struct GraphicsDescriptorSetWrite
	{
		uint32										bindingIndex = 0u;
		uint32										arrayIndex = 0u;
		ArrayView<const GraphicsDescriptorData>		data;
	};

	struct GraphicsRenderingAttachmentInfo
	{
		const GraphicsTexture*			pTexture = nullptr;
		const GraphicsTexture*			pResolveTexture = nullptr;
		GraphicsTextureLayout			textureLayout{};
		GraphicsTextureLayout			resolveTextureLayout{};
		GraphicsClearValue				clearValue{};
		GraphicsLoadAction				loadAction = GraphicsLoadAction::DontCare;
		GraphicsStoreAction				storeAction = GraphicsStoreAction::Store;
	};

	struct GraphicsBeginRenderingParameters
	{
		DebugName						debugName = {};
		GraphicsRenderingAttachmentInfo	colorAttachments[ GraphicsLimits_MaxColorTargetCount ];
		uint32							colorAttachmentCount = 0u;
		GraphicsRenderingAttachmentInfo	depthAttachment;
		GraphicsRenderingAttachmentInfo	stencilAttachment;
	};

	enum class GraphicsBlendOperation : uint8
	{
		None,
		Add,
		Subtract,
		RevSubtract,
		Min,
		Max,
	};

	enum class GraphicsBlendFactor : uint8
	{
		Zero,
		One,
		SrcColor,
		InvSrcColor,
		SrcAlpha,
		InvSrcAlpha,
		DestAlpha,
		InvDestAlpha,
		DestColor,
		InvDestColor,
		SrcAlphaSat,
	};

	enum class GraphicsFillMode : uint8
	{
		Wireframe,
		Solid,
	};

	enum class GraphicsCullMode : uint8
	{
		None,
		Front,
		Back,
	};

	enum class GraphicsStencilFace : uint8
	{
		Front,
		Back,
	};

	enum class GraphicsWindingOrder : uint8
	{
		Ccw,
		Cw,
	};

	enum class GraphicsSamplerFilterMode : uint8
	{
		Nearest,
		Linear,
	};

	enum class GraphicsSamplerAddressMode : uint8
	{
		Wrap,
		Mirror,
		Clamp,
		ClampToZero,
	};

	enum class GraphicsStencilOperation : uint8
	{
		Keep,
		Zero,
		Replace,
		Increment,
		Decrement,
		IncrementWrap,
		DecrementWrap,
		Invert,
	};

	enum class GraphicsColorWriteFlag : uint8
	{
		Red,
		Green,
		Blue,
		Alpha,
	};

	using GraphicsColorWriteMask = Bitmask8<GraphicsColorWriteFlag>;
	using GraphicsStencilFaceMask = Bitmask8<GraphicsStencilFace>;
	constexpr GraphicsColorWriteMask GraphicsColorWriteMask_RGB = { GraphicsColorWriteFlag::Red, GraphicsColorWriteFlag::Green, GraphicsColorWriteFlag::Blue };
	constexpr GraphicsColorWriteMask GraphicsColorWriteMask_None = {};
	constexpr GraphicsColorWriteMask GraphicsColorWriteMask_All = { GraphicsColorWriteFlag::Red, GraphicsColorWriteFlag::Green, GraphicsColorWriteFlag::Blue, GraphicsColorWriteFlag::Alpha };

	enum class GraphicsSamplerReductionMode : uint8
	{
		Disabled,
		Min,
		Max,
	};

	enum class GraphicsQueryType : uint8
	{
		TimeStamp_PipelineTop,
		TimeStamp_PipelineBottom
	};

	// :JK: don't change the order of these - it's referenced in the gpc compiler output
	struct GraphicsSamplerParameters
	{
		float							mipLodBias = 0.0f;
		float							minLod = 0.0f;
		float							maxLod = KEEN_FLOAT_MAX;
		float							maxAnisotropyLevel = 1.0f;
		GraphicsSamplerFilterMode		minFilter = GraphicsSamplerFilterMode::Linear;
		GraphicsSamplerFilterMode		magFilter = GraphicsSamplerFilterMode::Linear;
		GraphicsSamplerFilterMode		mipFilter = GraphicsSamplerFilterMode::Nearest;
		GraphicsSamplerAddressMode		addressU = GraphicsSamplerAddressMode::Wrap;
		GraphicsSamplerAddressMode		addressV = GraphicsSamplerAddressMode::Wrap;
		GraphicsSamplerAddressMode		addressW = GraphicsSamplerAddressMode::Wrap;
		GraphicsComparisonFunction		comparisonFunction = GraphicsComparisonFunction::Never;
		GraphicsSamplerReductionMode    reductionMode = GraphicsSamplerReductionMode::Disabled;
	};

	struct GraphicsPipelineLayoutParameters
	{
		const GraphicsDescriptorSetLayout*	descriptorSetLayouts[ GraphicsLimits_MaxDescriptorSetSlotCount ] = {};
		uint32								descriptorSetLayoutCount = 0u;
		GraphicsPipelineStageMask			pushConstantsStageMask = {};
		uint16								pushConstantsSize = 0u;
		bool								useBindlessDescriptors = false;
		DebugName							debugName = {};
	};

	enum class GraphicsDynamicStateFlag : uint8
	{
		Scissor,
		Viewport,
		StencilReference,
		StencilWriteMask,
		StencilCompareMask,
	};

	struct GraphicsStencilParameters
	{
		void				set( bool inEnable, GraphicsComparisonFunction inTestFunc = GraphicsComparisonFunction::Always, uint8 inTestMask = 0xffu, uint8 inWriteMask = 0x00u, GraphicsStencilOperation inOpFail = GraphicsStencilOperation::Keep, GraphicsStencilOperation inOpDepthFail = GraphicsStencilOperation::Keep, GraphicsStencilOperation inOpDepthPass = GraphicsStencilOperation::Keep )
		{
			testEnabled			= inEnable;
			testFunc			= inTestFunc;
			testMask			= inTestMask;
			writeMask			= inWriteMask;
			opFail				= inOpFail;
			opDepthFail			= inOpDepthFail;
			opDepthPass			= inOpDepthPass;
		}

		uint8							testMask = 0xffu;
		uint8							writeMask = 0u;
		bool8							testEnabled = { false };
		GraphicsComparisonFunction		testFunc = GraphicsComparisonFunction::Always;
		GraphicsStencilOperation		opFail = GraphicsStencilOperation::Keep;
		GraphicsStencilOperation		opDepthFail = GraphicsStencilOperation::Keep;
		GraphicsStencilOperation		opDepthPass = GraphicsStencilOperation::Keep;
	};

	using GraphicsDynamicStateFlagMask = Bitmask8<GraphicsDynamicStateFlag>;

	// :JK: GraphicsShaderEntryPointName is always zero terminated (and the zero is *not* part of the size)
	struct GraphicsShaderEntryPointName
	{
		const char*	pStart = nullptr;
		size_t		size = 0u;
	};

	struct GraphicsRenderPipelineParameters
	{
		inline void setRasterizerState( GraphicsCullMode inCullMode, GraphicsFillMode inFillMode, GraphicsWindingOrder inWindingOrder )
		{
			cullMode		= inCullMode;
			fillMode		= inFillMode;
			windingOrder	= inWindingOrder;
		}

		inline void setVertexFormat( const VertexFormat* pFormat )
		{
			pVertexFormat = pFormat;
		}

		inline void setBlendState( GraphicsBlendOperation inBlendOp, GraphicsBlendFactor inSourceFactor, GraphicsBlendFactor inDestFactor, GraphicsColorWriteMask inColorWriteMask0 = GraphicsColorWriteMask_All, bool enableAlphaToCoverage = false )
		{
			blendOp					= inBlendOp;
			blendSourceFactor		= inSourceFactor;
			blendDestFactor			= inDestFactor;
			colorWriteMask[ 0u ]	= inColorWriteMask0;
			alphaToCoverage			= enableAlphaToCoverage;
		}

		// :JK: rename to depth state..
		inline void setDepthState( GraphicsComparisonFunction inDepthCompareFunction, bool inEnableDepthWrite )
		{
			depthWriteEnabled		= inEnableDepthWrite;
			depthComparisonFunction	= inDepthCompareFunction;
		}

		inline void setRenderTargetFormat( const GraphicsRenderTargetFormat& format )
		{
			renderTargetFormat = format;
		}

		inline void setRenderTargetFormat( PixelFormat colorTargetFormat, PixelFormat depthStencilFormat )
		{
			renderTargetFormat.colorTargetFormats[ 0u ] = colorTargetFormat;
			renderTargetFormat.depthStencilTargetFormat = depthStencilFormat;
		}

		inline void setPrimitiveType( GraphicsPrimitiveType type )
		{
			primitiveType = type;
		}

		ConstMemoryBlock					vertexShaderCode = InvalidConstMemoryBlock;
		ConstMemoryBlock					tcShaderCode = InvalidConstMemoryBlock;
		ConstMemoryBlock					teShaderCode = InvalidConstMemoryBlock;
		ConstMemoryBlock					fragmentShaderCode = InvalidConstMemoryBlock;
		
		const GraphicsPipelineLayout*		pPipelineLayout = nullptr;
		const VertexFormat*					pVertexFormat = nullptr;

		GraphicsRenderTargetFormat			renderTargetFormat;
		GraphicsStencilParameters			frontStencil;
		GraphicsStencilParameters			backStencil;

		GraphicsPrimitiveType				primitiveType = GraphicsPrimitiveType::TriangleList;
		uint8								patchSize = 0u;
		GraphicsCullMode					cullMode = GraphicsCullMode::Back;
		GraphicsFillMode					fillMode = GraphicsFillMode::Solid;
		GraphicsWindingOrder				windingOrder = GraphicsWindingOrder::Ccw;

		// :JK: to support non-dynamic/fixed viewport+scissor rects we would need to be able to specify them in the pipeline parameters.. for now we always assume they are dynamic
		GraphicsDynamicStateFlagMask		dynamicState = { GraphicsDynamicStateFlag::Scissor, GraphicsDynamicStateFlag::Viewport, GraphicsDynamicStateFlag::StencilReference, GraphicsDynamicStateFlag::StencilCompareMask, GraphicsDynamicStateFlag::StencilWriteMask };

		uint8								sampleCount = 1u;

		float32								constDepthBias = 0.0f;
		float32								slopeDepthBias = 0.0f;

		GraphicsColorWriteMask				colorWriteMask[ GraphicsLimits_MaxColorTargetCount ] = { GraphicsColorWriteMask_All, GraphicsColorWriteMask_All, GraphicsColorWriteMask_All, GraphicsColorWriteMask_All, GraphicsColorWriteMask_All, GraphicsColorWriteMask_All };	// 6
		GraphicsBlendOperation				blendOp = GraphicsBlendOperation::None;
		GraphicsBlendFactor					blendSourceFactor = GraphicsBlendFactor::Zero;
		GraphicsBlendFactor					blendDestFactor = GraphicsBlendFactor::Zero;
		bool8								sampleShading = { false };
		bool8								alphaToCoverage = { false };
		GraphicsComparisonFunction			depthComparisonFunction = GraphicsComparisonFunction::Always;
		bool8								depthWriteEnabled = { false };
		bool8								enableScissorTest = { false };
		GraphicsPipelineEntryPointId		entryPointId = { GraphicsPipelineEntryPointId::Stage_Main };

		GraphicsShaderEntryPointName		vsEntryPoint;
		GraphicsShaderEntryPointName		tcEntryPoint;
		GraphicsShaderEntryPointName		teEntryPoint;
		GraphicsShaderEntryPointName		fsEntryPoint;

		DebugName							debugName = {};
	};

	struct GraphicsSpecializationConstant
	{
		uint8								slotIndex = 0u;
		uint8								dataSize = 0u;
	};

	struct GraphicsComputePipelineParameters
	{
		ConstMemoryBlock					shaderCode = InvalidConstMemoryBlock;
		const GraphicsPipelineLayout*		pPipelineLayout = nullptr;
		GraphicsPipelineEntryPointId		entryPointId = { GraphicsPipelineEntryPointId::Stage_Main };
		DebugName							debugName = EmptyDebugName;
	};

	// high level, typed similar to ArrayView<T>
	template<typename T>
	class GraphicsBufferView
	{
	public:
		using SelfType = GraphicsBufferView<T>;

		constexpr bool					isValid() const;
		constexpr bool					isInvalid() const;

		constexpr uint32				getCount() const;
		constexpr uint64				getSizeInBytes() const;

		constexpr SelfType				getPart( uint32 begin, uint32 end ) const;

		constexpr GraphicsBufferRange	getFullRange() const;
		constexpr GraphicsBufferRange	getPartRange( uint32 begin, uint32 end ) const;

		GraphicsBufferRange		m_range;
	};

#if KEEN_USING( KEEN_COMPILED_SHADER_INFO )

	struct GraphicsCompiledPipelineStageInfoAMD
	{
		size_t	ldsUsageInBytes;
		size_t	scratchMemoryUsageInBytes;

		uint32	vectorRegisters;
		uint32	scalarRegisters;
		uint32	physicalVectorRegisters;
		uint32	physicalScalarRegisters;
		uint32	availableVectorRegisters;
		uint32	availableScalarRegisters;

		uint3	computeWorkGroupSize;
	};

	struct GraphicsCompiledPipelineStageInfoNVIDIA
	{
		uint64	registerCount;
		uint64	binarySize;
		uint64	stackSize;
		uint64	localMemorySize;

		// render only
		uint64	inputCount;
		uint64	outputCount;

		// compute only
		uint64	sharedMemorySize;
	};

	struct GraphicsCompiledPipelineStageInfo
	{
		Optional<GraphicsCompiledPipelineStageInfoAMD>		amd;
		Optional<GraphicsCompiledPipelineStageInfoNVIDIA>	nvidia;
	};

	struct GraphicsCompiledRenderPipelineInfo
	{
		GraphicsCompiledPipelineStageInfo	vertex;
		GraphicsCompiledPipelineStageInfo	tessellationControl;
		GraphicsCompiledPipelineStageInfo	tessellationEvaluation;
		GraphicsCompiledPipelineStageInfo	fragment;
	};

	struct GraphicsCompiledComputePipelineInfo : GraphicsCompiledPipelineStageInfo
	{
	};

#endif

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )

	struct GraphicsRayTracePipelineParameters
	{
		ConstMemoryBlock					shaderCode = InvalidConstMemoryBlock;
		uint32								missShaderCount = 0u;
		uint32								closestHitShaderCount = 0u;
		uint32								maxRecursionDepth = 1u;
		const GraphicsPipelineLayout*		pPipelineLayout = nullptr;
		DebugName							debugName = EmptyDebugName;
	};

	struct GraphicsBuildRayTraceMeshParameters
	{
		const GraphicsRayTraceMesh*	pMesh = nullptr;
		GraphicsBufferRange			scratchBuffer{};
		GraphicsBufferRange			vertexBuffer{};
		GraphicsBufferRange			indexBuffer{};
		uint32						vertexStride = 0u;
		uint32						triangleCount = 0u;
		uint32						vertexCount = 0u;
	};

	struct GraphicsBuildRayTraceSceneParameters
	{
		const GraphicsRayTraceScene*	pScene = nullptr;
		GraphicsBufferRange				scratchBuffer = {};
		GraphicsBufferRange				instanceBuffer = {};
		uint32							instanceCount = 0u;
	};

	// :JK: don't change this enum + struct !! it's defined by the DXR/Vulkan Ray Tracing API
	enum GraphicsRayTraceSceneInstanceFlag
	{
		DisableTriangleCull,
		TriangleFacingCcw,
		ForceOpaque,
		ForceNonOpaque
	};

	using GraphicsRayTraceSceneInstanceFlagMask = Bitmask8<GraphicsRayTraceSceneInstanceFlag>;

	struct GraphicsRayTraceMeshDeviceAddress
	{
		uint64		value;
	};

	// don't change this struct - it's a gpu / hardware format
	struct alignas( 16 ) GraphicsRayTraceSceneInstance
	{
		GraphicsMatrix43						transform;
		uint32									instanceCustomIndex:24;
		uint32									mask:8;
		uint32									instanceShaderBindingTableRecordOffset:24;
		uint32									flags:8;
		GraphicsRayTraceMeshDeviceAddress		meshAddress;
	};
	KEEN_STATIC_ASSERT( sizeof( GraphicsRayTraceSceneInstance ) == 64u );

	enum class GraphicsRayTraceVertexFormat : uint8
	{
		Float3,
		Uint16_xyz_snorm,
		Uint16_xyz_unorm
	};

	struct GraphicsRayTraceMeshParameters
	{
		DebugName						debugName = EmptyDebugName;
		uint32							maxVertexCount = 0u;
		uint32							maxTriangleCount = 0u;
		GraphicsRayTraceVertexFormat	vertexFormat = GraphicsRayTraceVertexFormat::Float3;
		GraphicsIndexFormat				indexFormat = GraphicsIndexFormat::Uint16;
	};

	struct GraphicsRayTraceSceneParameters
	{
		DebugName					debugName = EmptyDebugName; 
		uint32						maxInstanceCount = 0u;		
	};

	static constexpr GraphicsStridedBufferRange createGraphicsStridedBufferRegion( const GraphicsBuffer* pBuffer, uint64 offset, uint64 stride, uint64 size )
	{
		const GraphicsStridedBufferRange result{pBuffer, offset, stride, size};
		return result;
	}

	static constexpr GraphicsBufferRange createGraphicsBufferRangePart( const GraphicsBufferRange& range, uint64 offset, uint64 size )
	{
		KEEN_ASSERT( ( offset + size ) <= range.size );
		return GraphicsBufferRange{ range.pBuffer, range.offset + offset, size };
	}

	static constexpr GraphicsStridedBufferRange EmptyGraphicsStridedBufferRegion{ nullptr, 0u, 0u, 0u };

	struct GraphicsRayTraceShaderBindingTable
	{
		GraphicsStridedBufferRange		rayGenShaderEntry = EmptyGraphicsStridedBufferRegion;
		GraphicsStridedBufferRange		missShaderEntry = EmptyGraphicsStridedBufferRegion;
		GraphicsStridedBufferRange		hitShaderEntry = EmptyGraphicsStridedBufferRegion;
		GraphicsStridedBufferRange		callableShaderEntry = EmptyGraphicsStridedBufferRegion;
	};

	struct GraphicsRayTracePipelineInfo
	{
		ArrayView<const uint8>	shaderGroupHandleStorage;
		uint32					shaderGroupHandleSize;
		uint32					shaderGroupCount;
		uint32					raygenGroupIndex;
		uint32					missGroupIndex;
		uint32					closestHitGroupIndex;
	};

#endif

	struct GraphicsViewport
	{
	public:
		inline GraphicsViewport() {}
		explicit inline GraphicsViewport( uint32 width, uint32 height );
		explicit inline GraphicsViewport( uint2 size );
		explicit inline GraphicsViewport( uint32 x, uint32 y, uint32 width, uint32 height );

		inline void		set( uint32 inX, uint32 inY, uint32 inWidth, uint32 inHeight, float inMinDepth, float inMaxDepth );
		inline void		setOrigin( uint32 inX, uint32 inY );
		inline void		setOrigin( uint2 origin );
		inline void		setSize( uint32 inWidth, uint32 inHeight );
		inline void		setSize( uint2 size );
		inline uint2	getOrigin() const;
		inline uint2	getSize() const;
		inline void		setDepthRange( float inMinDepth, float inMaxDepth );
		inline bool		containsPoint( int px, int py ) const;

		inline bool		isEmpty() const { return width == 0u || height == 0u || minDepth >= maxDepth; }

		uint32			x			= 0u;
		uint32			y			= 0u;
		uint32			width		= 0u;
		uint32			height		= 0u;
		float			minDepth	= 0.0f;
		float			maxDepth	= 1.0f;
	};

	struct GraphicsDescriptorSetLayoutParameters
	{
		DebugName											debugName = EmptyDebugName;
		ArrayView<const GraphicsDescriptorSetLayoutBinding>	bindings;

#if KEEN_USING( KEEN_GRAPHICS_LAME_STATIC_SAMPLERS )
		// some platforms required this info at runtime...
		ArrayView<const GraphicsSamplerParameters>			staticSamplers;	// :JK: binding slots for static samplers have to come after the normal (dynamic) bindings
#endif
	};

	struct GraphicsDescriptorSetParameters
	{
		DebugName										debugName = EmptyDebugName;
		const GraphicsDescriptorSetLayout*				pDescriptorSetLayout = nullptr;
		ArrayView<const GraphicsDescriptorData>			descriptorData;
	};

	struct GraphicsTextureRegion
	{
		GraphicsTextureAspectFlagMask	aspectMask = GraphicsTextureAspectFlag::Color;
		uint32							level = 0u;
		uint32							baseLayer = 0u;
		uint32							layerCount = 1u;
		uint3							offset = u3_zero;
		uint3							size = u3_one;
	};

	struct GraphicsBufferTextureCopyRegion
	{
		uint64						bufferOffset = 0ull;
		GraphicsTextureRegion		textureRegion;		
	};

	struct GraphicsCopyTextureParameters
	{
		const GraphicsTexture*	pTarget = nullptr;
		const GraphicsTexture*	pSource = nullptr;
		GraphicsTextureRegion	targetRegion;
		GraphicsTextureRegion	sourceRegion;
	};

	enum class GraphicsOptionalShaderStageFlag : uint8
	{
		GeometryShader,
		TessellationShaders,
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		RaytracingShaders,
#endif
	};

	using GraphicsOptionalShaderStageMask = Bitmask8< GraphicsOptionalShaderStageFlag >;

	enum class GraphicsDeviceMemoryTypeFlag : uint32
	{
		GpuLocal,
		CpuAccessible,
		CpuCached,
		CpuCoherent,
	};

	using GraphicsDeviceMemoryTypeFlags = Bitmask32<GraphicsDeviceMemoryTypeFlag>;

	struct GraphicsDeviceMemoryTypeBudgetInfo
	{
		uint64											usedSizeInBytes;
		uint64											budgetSizeInBytes;
	};

	struct GraphicsDeviceMemoryTypeInfo
	{
		uint64											totalSizeInBytes;
		Optional<GraphicsDeviceMemoryTypeBudgetInfo>	budget;		// this info can change at any time!
		GraphicsDeviceMemoryTypeFlags					flags;
	};

	using GraphicsDeviceMemoryTypeInfos = ArrayView<const GraphicsDeviceMemoryTypeInfo>;

	enum class GraphicsGpuVendor
	{
		Nvidia,
		Amd,
		Intel,
		Unknown,
	};

	// GraphicsDeviceInfo only contains static device information (doesn't change over time)
	struct GraphicsDeviceInfo
	{
		GraphicsDeviceInfo();
		void							enableTextureFormats( const ArrayView<const PixelFormat>& formats );
		void							enableRenderTargetFormat( PixelFormat format );

		bool							isSampleCountSupported( size_t sampleCount ) const;
		uint8							getMaxSupportedSampleCount() const;

		GraphicsApi						api = GraphicsApi::Invalid;

		Optional<uint64>				luid; // local device identifier, e.g. use for mapping to NVAPI
		GraphicsGpuVendor				gpuVendor;
		StaticString<256u>				gpuIdentifier;

		using PixelFormatSupportArray = StaticBitArray<PixelFormatCount>;
		PixelFormatSupportArray			supportedTextureFormats;
		PixelFormatSupportArray			supportedRenderTargetFormats;
		GraphicsFeatureFlags			supportedFeatures;

		GraphicsOptionalShaderStageMask optionalShaderStages = {};

		PixelFormat						defaultFormat32bpp = PixelFormat::None;
		PixelFormat						defaultFormat32bpp_sRGB = PixelFormat::None;

		uint64							totalMemorySize = 0u;		// total usable memory size..
		uint64							dedicatedMemorySize = 0u;
		GraphicsDeviceMemoryTypeInfos	memoryTypes;
		uint64							maxDeviceMemorySizeInBytes = 0u;

		float							maxAnisotropyLevel = 1.0f;
		uint8							supportedSampleCountMask = 0x00000001;

		uint32							internalFrameCount = 0u;

		double							timestampPeriod;	// nanoseconds

		bool							hasOldDriver = false;

		bool							supportsMultithreadedPipelineCreation = false;	// if this is set to true createXXXPipeline functions can be called from multiple threads in parallel

#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		bool							isRayTracingSupported = false;
		uint32							shaderGroupHandleSize  = 0u;
		uint32							maxRayRecursionDepth = 0u;
		uint32							rayTraceShaderGroupBaseAlignment = 0u;
		uint32							minRayTraceScratchOffsetAlignment = 0u;
#endif

		bool							isMemoryPrioritySupported = false;

		bool							isFsr3Supported		= false;
		bool							isDlssSupported		= false;
#if KEEN_USING( KEEN_NVREFLEX_SUPPORT )
		bool							isNvReflexSupported = false;
#endif

		uint32							maxTextureDimension1d = 0u;
		uint32							maxTextureDimension2d = 0u;
		uint32							maxTextureDimension3d = 0u;
		uint32							maxTextureDimensionCube = 0u;
		uint32							maxFramebufferDimension = 0u;
		uint3							maxDispatchGroupCount = {};

		uint64							minUniformBufferOffsetAlignment	= 0u;
		uint64							minStorageBufferOffsetAlignment = 0u;
		uint64							minVertexBufferOffsetAlignment = 0u;
		uint64							minIndexBufferOffsetAlignment = 0u;
		uint64							minArgumentBufferOffsetAlignment = 0u;
		uint64							minBufferTextureCopyBufferOffsetAlignment = 0u;

		uint32							subgroupSize = 0u;

		bool							areBreadcrumbsEnabled = false;
	};

#if KEEN_USING( KEEN_NVN_SUPPORT )
	struct GraphicsMemoryBlocksNvn
	{
		MemoryBlock firmware = InvalidMemoryBlock;			// memory block used for the driver/firmware
		MemoryBlock graphicsMemory = InvalidMemoryBlock;	// memory block used for all graphics objects (textures,buffers,shaders,render targets)
	};
#elif KEEN_USING( KEEN_GNM_SUPPORT )
	struct GraphicsMemoryBlocksGnm
	{
		MemoryBlock onionMemory = InvalidMemoryBlock;
		MemoryBlock garlicMemory = InvalidMemoryBlock;
	};
#elif KEEN_USING( KEEN_D3D11X_SUPPORT )
	struct GraphicsMemoryBlocksD3D11X
	{
		MemoryBlock dynamicBufferMemory = InvalidMemoryBlock;
	};
#endif

	struct GraphicsSystemParameters
	{
		TaskSystem*					pTaskSystem = nullptr;
#if KEEN_USING( KEEN_OS_WINDOW_SUPPORT )
		OsWindowSystem*				pWindowSystem = nullptr;
#endif
		EventNotifier*				pEventNotifier = nullptr;
		GraphicsApiInstance*		pApi = nullptr;

		StringView					applicationName = "keen_game"_s;
		uint32						applicationVersion = 1u;

		StringView					pipelineCacheDirectory;

		bool						enableDebugChecks = false;
		bool						enableGpuCrashDumps = false;

		bool						enableRaytracingSupport = false;
		bool						enableRaytracingOnNvidiaOnly = false;
		bool						isNonInteractiveApplication = false;
		bool						enableBreadcrumbs = false;

		size_t						stagingHeapChunkSize = 64_mib;
		bool						growStagingHeapWhenFull = true;
		size_t						dynamicUniformBufferSizePerFrame = 1_mib;
		size_t						dynamicDataBufferSizePerFrame = 16_mib;
		size_t						dynamicUploadBufferSizePerFrame = 16_mib;
		bool						growDynamicBuffersWhenFull = true;	// :JK: should be disabled for games..

		bool						enableBindlessDescriptors = false;

#if KEEN_USING( KEEN_NVN_SUPPORT )
		GraphicsMemoryBlocksNvn		memoryBlocks;
#elif KEEN_USING( KEEN_GNM_SUPPORT )
		GraphicsMemoryBlocksGnm		memoryBlocks;
#elif KEEN_USING( KEEN_D3D11X_SUPPORT )
		GraphicsMemoryBlocksD3D11X	memoryBlocks;
#endif

#if KEEN_USING( KEEN_VULKAN_SUPPORT )
		bool						useMemoryCallbacks = false;
		bool						enableSynchronizationValidation = false;
		bool						enableGpuAssistedValidation = false;
		bool						disableUnknownVulkanLayers = true;
		Optional<uint32>			forcePhysicalDeviceIndex;
		size_t						allocationBlockSizeInBytes = 64_mib;
#endif

#if KEEN_USING( KEEN_METAL_SUPPORT )
		void*						pMetalDevice = nullptr;
		const char*					pMetalShaderLibraryName = nullptr;
#endif

#if KEEN_USING( KEEN_NVREFLEX_SUPPORT )
		bool						enableNvReflex = true;
		bool						enableNvReflexPcLatencyStats = false;
#endif

#ifndef KEEN_BUILD_MASTER
		char						captureTargetPath[ 260u ] = "";
#endif
	};

	struct GraphicsSwapChainParameters
	{
		DebugName					debugName;

		PixelFormat					colorFormat = PixelFormat::R8G8B8A8_unorm;
		PixelFormat					alternativeColorFormat = PixelFormat::None;
		uint2						size = u2_zero;
		uint32						presentationInterval = 1u;
		GraphicsTextureUsageMask	usageMask = GraphicsTextureUsageFlag::Render_ColorTarget;

#if KEEN_USING( KEEN_OS_WINDOW_SUPPORT )
		OsWindowHandle				windowHandle = InvalidOsWindowHandle;
#endif
	};

	struct GraphicsQueryPoolParameters
	{
		DebugName					debugName;
		uint32						queryCount = 0u;
	};

	struct GraphicsSwapChainInfo
	{
		uint2						size;
		PixelFormat					colorFormat;
		uint32						presentationInterval;

#if KEEN_USING( KEEN_OS_WINDOW_SUPPORT )
		OsWindowHandle				windowHandle;
#endif
	};

	// :JK: inspired by https://github.com/Tobski/simple_vulkan_synchronization
	enum class GraphicsAccessFlag : uint8
	{
		// Read access
		IndirectBuffer,										// Read as an indirect buffer for drawing or dispatch
		IndexBuffer,										// Read as an index buffer for drawing
		VertexBuffer,										// Read as a vertex buffer for drawing
		VS_Read_UniformBuffer,								// Read as a uniform buffer in a vertex shader
		VS_Read_SampledImage,								// Read as a sampled image/uniform texel buffer in a vertex shader
		VS_Read_Other,										// Read as any other resource in a vertex shader
		FS_Read_UniformBuffer,								// Read as a uniform buffer in a fragment shader
		FS_Read_SampledImage,								// Read as a sampled image/uniform texel buffer  in a fragment shader
		FS_Read_ColorInputAttachment,						// Read as an input attachment with a color format in a fragment shader
		FS_Read_DepthStencilInputAttachment,				// Read as an input attachment with a depth/stencil format in a fragment shader
		FS_Read_Other,										// Read as any other resource in a fragment shader
		ColorAttachment_Read,								// Read by standard blending/logic operations or subpass load operations
		DepthStencilAttachment_Read,						// Read by depth/stencil tests or subpass load operations
		CS_Read_UniformBuffer,								// Read as a uniform buffer in a compute shader
		CS_Read_SampledImage,								// Read as a sampled image/uniform texel buffer in a compute shader
		CS_Read_Other,										// Read as any other resource in a compute shader
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		AccelerationStructureBuild_Read,
		RT_Read_UniformBuffer,
		RT_Read_Other,
#endif
		AnyShader_Read_UniformBuffer,						// Read as a uniform buffer in any shader
		AnyShader_Read_UniformOrVertexBuffer,				// Read as a uniform buffer in any shader, or a vertex buffer
		AnyShader_Read_SampledImage,						// Read as a sampled image in any shader
		AnyShader_Read_Other,								// Read as any other resource (excluding attachments) in any shader
		Transfer_Read,										// Read as the source of a transfer operation
		Host_Read,											// Read on the host

		// Requires VK_KHR_swapchain to be enabled
		Present,											// Read by the presentation engine (i.e. vkQueuePresentKHR)

		// Write access
		FirstWriteAccess,
		VS_Write = FirstWriteAccess,						// Written as any resource in a vertex shader
		FS_Write,											// Written as any resource in a fragment shader
		ColorAttachment_Write,								// Written as a color attachment during rendering, or via a subpass store op
		DepthStencilAttachment_Write,						// Written as a depth/stencil attachment during rendering, or via a subpass store op
		CS_Write,											// Written as any resource in a compute shader
#if KEEN_USING( KEEN_GRAPHICS_RAY_TRACING )
		AccelerationStructureBuild_Write,
		RT_Write,
#endif
		AnyShader_Write,									// Written as any resource in any shader
		Transfer_Write,										// Written as the destination of a transfer operation
		Host_Write,											// Written on the host

		// General access
		General,											// Covers any access - useful for debug, generally avoid for performance reasons
		// update getEnumValueCount<GraphicsAccessFlag> just below when appending a new member here!!!
	};
	template<> constexpr size_t getEnumValueCount<GraphicsAccessFlag>() { return (size_t)GraphicsAccessFlag::General + 1u; }

	using GraphicsAccessMask = Bitmask64<GraphicsAccessFlag>;
	KEEN_STATIC_ASSERT( getEnumValueCount<GraphicsAccessFlag>() <= GraphicsAccessMask::getIndexCount() );

	constexpr GraphicsAccessMask GraphicsReadAccessMask		= GraphicsAccessMask::fromMaskValue( ( 1u << (uint32)GraphicsAccessFlag::FirstWriteAccess ) - 1u ) | GraphicsAccessFlag::General;
	constexpr GraphicsAccessMask GraphicsWriteAccessMask	= ~GraphicsReadAccessMask | GraphicsAccessFlag::General;

	struct GraphicsMemoryBarrier
	{
		GraphicsAccessMask	oldAccessMask;
		GraphicsAccessMask	newAccessMask;
	};

	// :JK: don't change the order of the fields please.. we are using initializer lists in lots of places
	struct GraphicsTextureBarrier
	{
		const GraphicsTexture*			pTexture = nullptr;
		GraphicsTextureSubresourceRange	subresourceRange;
		GraphicsTextureLayout			oldLayout = GraphicsTextureLayout::Undefined;
		GraphicsTextureLayout			newLayout = GraphicsTextureLayout::Undefined;
		GraphicsAccessMask				oldAccessMask;
		GraphicsAccessMask				newAccessMask;
	};

	struct GraphicsTextureRelease
	{
		const GraphicsTexture*			pTexture = nullptr;
		GraphicsTextureSubresourceRange	subresourceRange;
		GraphicsTextureLayout			oldLayout = GraphicsTextureLayout::Undefined;
		GraphicsTextureLayout			newLayout = GraphicsTextureLayout::Undefined;
		GraphicsAccessMask				oldAccessMask;
	};

	struct GraphicsTextureAcquire
	{
		const GraphicsTexture*			pTexture = nullptr;
		GraphicsTextureSubresourceRange	subresourceRange;
		GraphicsTextureLayout			oldLayout = GraphicsTextureLayout::Undefined;
		GraphicsTextureLayout			newLayout = GraphicsTextureLayout::Undefined;
		GraphicsAccessMask				newAccessMask;
	};

	struct GraphicsTrackedTexture
	{
		const GraphicsTexture*			pTexture = nullptr;
		GraphicsTextureLayout			currentLayout = GraphicsTextureLayout::Undefined;
		GraphicsAccessMask				lastAccessMask{};
#if KEEN_USING(KEEN_GRAPHICS_DEBUG)
		bool							traceBarriers = false;
		bool							breakOnBarriers = false;
#endif
	};

}

#include "../../../sources/global/graphics_types.inl"

#endif
