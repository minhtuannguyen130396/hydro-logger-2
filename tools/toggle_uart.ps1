param(
    [ValidateSet("disable", "enable")]
    [string]$Action = "disable"
)

$ErrorActionPreference = "Stop"
$rootDir = Split-Path -Parent $PSScriptRoot
Set-Location $rootDir

if ($Action -eq "disable") {
    Write-Host "[+] Disabling UART console and application logging (Release mode)..." -ForegroundColor Yellow

    # 1. Restore sdkconfig.release
    if (Test-Path "sdkconfig.release.disabled") {
        Move-Item "sdkconfig.release.disabled" "sdkconfig.release" -Force
    }

    # 2. Update sdkconfig.defaults
    if (Test-Path "sdkconfig.defaults") {
        $def = Get-Content "sdkconfig.defaults"
        $def = $def | Where-Object { 
            $_ -notmatch "CONFIG_ESP_CONSOLE_UART" -and 
            $_ -notmatch "CONFIG_LOG_DEFAULT_LEVEL" -and 
            $_ -notmatch "CONFIG_BOOTLOADER_LOG_LEVEL" 
        }
        $def | Set-Content "sdkconfig.defaults"
    }

    # 3. Update sdkconfig
    if (Test-Path "sdkconfig") {
        $c = Get-Content "sdkconfig"
        $c = $c -replace 'CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y', '# CONFIG_BOOTLOADER_LOG_LEVEL_INFO is not set'
        $c = $c -replace 'CONFIG_BOOTLOADER_LOG_LEVEL=3', 'CONFIG_BOOTLOADER_LOG_LEVEL=0'
        $c = $c -replace '# CONFIG_BOOTLOADER_LOG_LEVEL_NONE is not set', 'CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y'
        $c = $c -replace 'CONFIG_LOG_BOOTLOADER_LEVEL_INFO=y', '# CONFIG_LOG_BOOTLOADER_LEVEL_INFO is not set'
        $c = $c -replace 'CONFIG_LOG_BOOTLOADER_LEVEL=3', 'CONFIG_LOG_BOOTLOADER_LEVEL=0'
        $c = $c -replace '# CONFIG_LOG_BOOTLOADER_LEVEL_NONE is not set', 'CONFIG_LOG_BOOTLOADER_LEVEL_NONE=y'
        $c = $c -replace 'CONFIG_LOG_DEFAULT_LEVEL_INFO=y', '# CONFIG_LOG_DEFAULT_LEVEL_INFO is not set'
        $c = $c -replace 'CONFIG_LOG_DEFAULT_LEVEL=3', 'CONFIG_LOG_DEFAULT_LEVEL=0'
        $c = $c -replace '# CONFIG_LOG_DEFAULT_LEVEL_NONE is not set', 'CONFIG_LOG_DEFAULT_LEVEL_NONE=y'
        $c = $c -replace 'CONFIG_LOG_MAXIMUM_LEVEL=3', 'CONFIG_LOG_MAXIMUM_LEVEL=0'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y', '# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set'
        $c = $c -replace '# CONFIG_ESP_CONSOLE_NONE is not set', 'CONFIG_ESP_CONSOLE_NONE=y'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_UART_NUM=0', 'CONFIG_ESP_CONSOLE_UART_NUM=-1'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=0', 'CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1'
        $c = $c -replace '# CONFIG_CONSOLE_UART_NONE is not set', 'CONFIG_CONSOLE_UART_NONE=y'
        $c = $c -replace '# CONFIG_ESP_CONSOLE_UART_NONE is not set', 'CONFIG_ESP_CONSOLE_UART_NONE=y'
        $c = $c -replace 'CONFIG_CONSOLE_UART_DEFAULT=y', '# CONFIG_CONSOLE_UART_DEFAULT is not set'
        $c = $c -replace 'CONFIG_CONSOLE_UART_NUM=0', 'CONFIG_CONSOLE_UART_NUM=-1'
        $c | Set-Content "sdkconfig"
    }

    # 4. Update CMakeCache if exists
    if (Test-Path "build\CMakeCache.txt") {
        (Get-Content "build\CMakeCache.txt") -replace 'SDKCONFIG_DEFAULTS:UNINITIALIZED=sdkconfig.defaults', 'SDKCONFIG_DEFAULTS:UNINITIALIZED=sdkconfig.defaults;sdkconfig.release' | Set-Content "build\CMakeCache.txt"
    }

    # 5. Restore original main CMakeLists.txt if in component_test
    if (Test-Path "main\CMakeLists.txt.bak") {
        Copy-Item "main\CMakeLists.txt.bak" "main\CMakeLists.txt" -Force
    }

    Write-Host "[+] Reconfiguring and building..." -ForegroundColor Green
    powershell -ExecutionPolicy Bypass -File ".\.idfrun.ps1" reconfigure
    powershell -ExecutionPolicy Bypass -File ".\.idfrun.ps1" build
    Write-Host "[OK] UART disabled, Release build complete!" -ForegroundColor Cyan
}
elseif ($Action -eq "enable") {
    Write-Host "[+] Enabling UART console and application logging (Debug mode)..." -ForegroundColor Yellow

    if (Test-Path "sdkconfig.release") {
        Move-Item "sdkconfig.release" "sdkconfig.release.disabled" -Force
    }

    # Append console config if missing
    $defaults = Get-Content "sdkconfig.defaults" -Raw
    if ($defaults -notmatch "CONFIG_ESP_CONSOLE_UART_DEFAULT=y") {
        Add-Content "sdkconfig.defaults" "`n# Console & Logging`nCONFIG_ESP_CONSOLE_UART_DEFAULT=y`nCONFIG_ESP_CONSOLE_UART_BAUDRATE=115200`nCONFIG_LOG_DEFAULT_LEVEL_INFO=y`nCONFIG_LOG_DEFAULT_LEVEL=3`nCONFIG_BOOTLOADER_LOG_LEVEL_INFO=y`nCONFIG_BOOTLOADER_LOG_LEVEL=3`n"
    }

    if (Test-Path "build\CMakeCache.txt") {
        (Get-Content "build\CMakeCache.txt") -replace 'sdkconfig.defaults;sdkconfig.release', 'sdkconfig.defaults' | Set-Content "build\CMakeCache.txt"
    }

    if (Test-Path "sdkconfig") {
        $c = Get-Content "sdkconfig"
        $c = $c -replace 'CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y', '# CONFIG_BOOTLOADER_LOG_LEVEL_NONE is not set'
        $c = $c -replace '# CONFIG_BOOTLOADER_LOG_LEVEL_INFO is not set', 'CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y'
        $c = $c -replace 'CONFIG_BOOTLOADER_LOG_LEVEL=0', 'CONFIG_BOOTLOADER_LOG_LEVEL=3'
        $c = $c -replace 'CONFIG_LOG_BOOTLOADER_LEVEL_NONE=y', '# CONFIG_LOG_BOOTLOADER_LEVEL_NONE is not set'
        $c = $c -replace '# CONFIG_LOG_BOOTLOADER_LEVEL_INFO is not set', 'CONFIG_LOG_BOOTLOADER_LEVEL_INFO=y'
        $c = $c -replace 'CONFIG_LOG_BOOTLOADER_LEVEL=0', 'CONFIG_LOG_BOOTLOADER_LEVEL=3'
        $c = $c -replace 'CONFIG_LOG_DEFAULT_LEVEL_NONE=y', '# CONFIG_LOG_DEFAULT_LEVEL_NONE is not set'
        $c = $c -replace '# CONFIG_LOG_DEFAULT_LEVEL_INFO is not set', 'CONFIG_LOG_DEFAULT_LEVEL_INFO=y'
        $c = $c -replace 'CONFIG_LOG_DEFAULT_LEVEL=0', 'CONFIG_LOG_DEFAULT_LEVEL=3'
        $c = $c -replace 'CONFIG_LOG_MAXIMUM_LEVEL=0', 'CONFIG_LOG_MAXIMUM_LEVEL=3'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_NONE=y', '# CONFIG_ESP_CONSOLE_NONE is not set'
        $c = $c -replace '# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set', 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_UART_NUM=-1', 'CONFIG_ESP_CONSOLE_UART_NUM=0'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1', 'CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=0'
        $c = $c -replace 'CONFIG_CONSOLE_UART_NONE=y', '# CONFIG_CONSOLE_UART_NONE is not set'
        $c = $c -replace 'CONFIG_ESP_CONSOLE_UART_NONE=y', '# CONFIG_ESP_CONSOLE_UART_NONE is not set'
        $c = $c -replace '# CONFIG_CONSOLE_UART_DEFAULT is not set', 'CONFIG_CONSOLE_UART_DEFAULT=y'
        $c = $c -replace 'CONFIG_CONSOLE_UART_NUM=-1', 'CONFIG_CONSOLE_UART_NUM=0'
        $c | Set-Content "sdkconfig"
    }

    Write-Host "[+] Reconfiguring and building..." -ForegroundColor Green
    powershell -ExecutionPolicy Bypass -File ".\.idfrun.ps1" reconfigure
    powershell -ExecutionPolicy Bypass -File ".\.idfrun.ps1" build
    Write-Host "[OK] UART enabled, Debug build complete!" -ForegroundColor Cyan
}
