#include "vulkan_graphics_device.hpp"

#include "keen/os/os_file_read_stream.hpp"
#include "keen/os/calendar_time.hpp"
#include "keen/json/json.hpp"

#include "global/graphics_device.hpp"

#if defined( KEEN_PLATFORM_WIN32 )

#define ERROR_SUCCESS           0L
#define KEY_QUERY_VALUE         ( 0x0001 )
#define KEY_SET_VALUE           ( 0x0002 )
#define KEY_ENUMERATE_SUB_KEYS  ( 0x0008 )
#define HKEY_CURRENT_USER       ( (HKEY)(ULONG_PTR)( (LONG)0x80000001 ) )
#define HKEY_LOCAL_MACHINE      ( (HKEY)(ULONG_PTR)( (LONG)0x80000002 ) )
#define REG_DWORD               ( 4ul ) // 32-bit number

typedef unsigned char       BYTE;
typedef long                LONG;
typedef long                BOOL;
typedef wchar_t             WCHAR;    // wc,   16-bit UNICODE character
typedef const char*         LPCSTR;
typedef char*               LPSTR;
typedef WCHAR*              LPWSTR;
typedef DWORD*              LPDWORD;
typedef unsigned __int64    ULONG_PTR;
typedef DWORD               ACCESS_MASK;
typedef ACCESS_MASK         REGSAM;
typedef void*               HKEY;
typedef void**              PHKEY;
typedef LONG                LSTATUS;

extern "C"
{
    __declspec( dllimport ) LSTATUS RegQueryInfoKeyW( HKEY hKey, LPWSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, void *lpftLastWriteTime );
    __declspec( dllimport ) LSTATUS RegEnumValueW( HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, void* lpData, LPDWORD lpcbData );
    __declspec( dllimport ) LSTATUS __stdcall RegOpenKeyExA( HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult );
    __declspec( dllimport ) LSTATUS __stdcall RegOpenKeyExW( HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult );
    __declspec( dllimport ) BOOL __stdcall SetEnvironmentVariableA( LPCSTR lpName, LPCSTR lpValue );
    __declspec( dllimport ) DWORD __stdcall GetEnvironmentVariableA( LPCSTR lpName, LPSTR lpBuffer, DWORD nSize );
    __declspec( dllimport ) LSTATUS __stdcall RegSetValueExA( HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, const BYTE* lpData, DWORD cbData );
    __declspec( dllimport ) LSTATUS __stdcall RegCloseKey( HKEY hKey );

}

namespace keen
{
    static void safeSetEnvironmentVariable( const StringView& variableName, const StringView& variableValue )
    {
        char buffer[ 1024 ];
        if( !::GetEnvironmentVariableA( variableName.getStart(), buffer, KEEN_COUNTOF( buffer ) ) )
        {
            ::SetEnvironmentVariableA( variableName.getStart(), variableValue.getStart() );
        }
    }

#if KEEN_USING( KEEN_VULKAN_VALIDATION )
    static bool setRegistryKey( const StringView& keyFolder, const StringView& keyName, uint32 keyValue )
    {
        TlsDynamicString registryPath;
        replaceString( &registryPath, keyName, "/", "\\" );
        registryPath.pushBack( '\0' );
        DWORD   data = keyValue;
        HKEY    hKey;
        LSTATUS result = RegOpenKeyExA( HKEY_CURRENT_USER, keyFolder.getStart(), 0, KEY_SET_VALUE, &hKey );
        if( result != ERROR_SUCCESS )
        {
            KEEN_TRACE_ERROR( "[graphics] Failed to access registry keys for validation" );
            return false;
        }

        result = RegSetValueExA( hKey, registryPath.getStart(), 0, REG_DWORD, (const BYTE*)&data, sizeof( data ) );
        if( result != ERROR_SUCCESS )
        {
            KEEN_TRACE_ERROR( "[graphics] Failed to set required registry keys for validation" );
            return false;
        }

        RegCloseKey( hKey );

        return true;
    }

    void setupVulkanValidationSettingsWin32()
    {
        BOOL success = ::SetEnvironmentVariableA( "ENABLE_KEEN_KHRONOS_SYNCHRONIZATION2_LAYER", "1" );
        success = ::SetEnvironmentVariableA( "ENABLE_KEEN_KHRONOS_VALIDATION_LAYER", "1" );

        if( !success )
        {
            KEEN_TRACE_ERROR( "[graphics] Failed to set required environment variables for validation" );
            return;
        }

        TlsPathName    exePath;
        Result< void > result = os::getCurrentExePath( &exePath );
        if( result.hasError() )
        {
            KEEN_TRACE_ERROR( "[graphics] Failed to retrieve path to current file!" );
            return;
        }
        exePath.setFileName( "VkLayer_keen_khronos_synchronization2.json" );
        if( !setRegistryKey( "SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", exePath, 0 ) )
        {
            return;
        }

        exePath.setFileName( "VkLayer_keen_khronos_validation.json" );
        setRegistryKey( "SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", exePath, 0 );
    }
#endif

    static void validateVulkanImplicitLayers( HKEY baseKey )
    {
        HKEY    hKey;
        LSTATUS result = ::RegOpenKeyExW( baseKey, L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hKey );
        if( result != ERROR_SUCCESS )
        {
            return;
        }

        DWORD   cValues = 0;    // number of values for key
        LSTATUS status = ::RegQueryInfoKeyW( hKey, NULL, NULL, NULL, NULL, NULL, NULL, &cValues, NULL, NULL, NULL, NULL );

        for( DWORD index = 0; index < cValues; ++index )
        {
            StaticArray< wchar_t, 4096 > keyName;
            DWORD                        keyNameLen = (DWORD)keyName.getCapacity();
            status = ::RegEnumValueW( hKey, index, keyName.getStart(), &keyNameLen, NULL, NULL, NULL, NULL );

            TlsDynamicArray< char, 4096 > utf8KeyName;
            Result< void >                convertResult = os::convertUtf16ToUtf8( &utf8KeyName, keyName.getStart() );
            if( convertResult.isOk() )
            {
                TlsPathName jsonConfigPath( utf8KeyName, false );
                if( jsonConfigPath.getFileName() == "obs-vulkan64.json" || jsonConfigPath.getFileName() == "obs-vulkan32.json" )
                {
                    OsFileReadStream   readStream( utf8KeyName );
                    JsonDocumentReader jsonReader( tls::getAllocator(), &readStream );

                    const JsonValue    layerData = jsonReader[ "layer" ];
                    StringView         localLibraryPath = layerData[ "library_path" ].getOptionalString();
                    if( !localLibraryPath.isEmpty() )
                    {
                        TlsPathName                 libraryPath( jsonConfigPath.getDirectoryPath(), localLibraryPath );
                        Result< CalendarTimeStamp > timestampResult = os::getFileTime( libraryPath );
                        if( timestampResult.isOk() )
                        {
                            CalendarTime calendarTime;
                            os::fillLocalCalendarTime( &calendarTime, timestampResult.value );
                            if( calendarTime.year < 2020 || ( calendarTime.year == 2020 && ( calendarTime.month < 8 || ( calendarTime.month == 8 && calendarTime.dayOfMonth <= 19 ) ) ) )
                            {
                                KEEN_TRACE_INFO( "[graphics] Found an outdated version of OBS vulkan hook at %k. Trying to disable it from hooking into Enshrouded. Please delete the folder manually *AND* reinstall OBS.", libraryPath );
                                if( !::SetEnvironmentVariableA( "DISABLE_VULKAN_OBS_CAPTURE", "1" ) ) 
                                {
                                    KEEN_TRACE_ERROR( "[graphics] Failed to disable OBS vulkan hook even though the version is outdated.\n" );
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    void validateVulkanImplicitLayersWin32( bool disableUnknownVulkanLayers )
    {
        if( disableUnknownVulkanLayers )
        {
            safeSetEnvironmentVariable( "DISABLE_MANGOHUD"_s, "1"_s );
            safeSetEnvironmentVariable( "DISABLE_RTSS_LAYER"_s, "1"_s );
            safeSetEnvironmentVariable( "DISABLE_VKBASALT"_s, "1"_s );
            safeSetEnvironmentVariable( "DISABLE_VK_LAYER_reshade_1"_s, "1"_s );

            safeSetEnvironmentVariable( "VK_LAYER_bandicam_helper_DEBUG_1"_s, "1"_s );
            safeSetEnvironmentVariable( "NODEVICE_SELECT"_s, "1"_s );

            // :FK: By recommendation from NVidia and other resources we disable this vulkan layer - we want to run on the dedicated graphics device anyways!
            safeSetEnvironmentVariable( "DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1"_s, "1"_s );

            safeSetEnvironmentVariable( "DISABLE_LAYER"_s, "1"_s );

            safeSetEnvironmentVariable( "EOS_OVERLAY_DISABLE_VULKAN_WIN64"_s, "1"_s );

            safeSetEnvironmentVariable( "DISABLE_VULKAN_OW_OBS_CAPTURE"_s, "1"_s );
            safeSetEnvironmentVariable( "DISABLE_VULKAN_OW_OVERLAY_LAYER"_s, "1"_s );
        }

        validateVulkanImplicitLayers( HKEY_CURRENT_USER );
        validateVulkanImplicitLayers( HKEY_LOCAL_MACHINE );
    }
}

#endif