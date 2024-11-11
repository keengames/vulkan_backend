#ifndef KEEN_VULKAN_GRAPHICS_DEVICE_WIN32_HPP_INCLUDED
#define KEEN_VULKAN_GRAPHICS_DEVICE_WIN32_HPP_INCLUDED

#include "vulkan_types.hpp"

#if defined( KEEN_PLATFORM_WIN32 )
namespace keen
{

#if KEEN_USING( KEEN_VULKAN_VALIDATION )
    void setupVulkanValidationSettingsWin32();
#endif

    void validateVulkanImplicitLayersWin32( bool disableUnknownVulkanLayers );
}
#endif

#endif
