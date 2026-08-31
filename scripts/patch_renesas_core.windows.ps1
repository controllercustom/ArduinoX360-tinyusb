# patch_renesas_core.windows.ps1 - apply the ArduinoX360-tinyusb
# compatibility patch to the locally installed Arduino Renesas core
# (arduino:renesas_uno) using only stock Windows PowerShell 5.1 (no patch.exe,
# no Git required). Edits are exact-match replacements; LF and CRLF files both
# work. Idempotent; partially-patched cores are rejected.
#
# Run from cmd.exe or PowerShell:
#   powershell -NoProfile -ExecutionPolicy Bypass -File patch_renesas_core.ps1
# (or double-click patch_renesas_core.windows.bat)
#
# Restore by reinstalling the core:
#   arduino-cli core uninstall arduino:renesas_uno
#   arduino-cli core install arduino:renesas_uno
#
# Pinned core version: 1.6.0 (pass -AllowAnyVersion to override).

param([switch]$AllowAnyVersion)
$ErrorActionPreference = 'Stop'

$ScriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$PinnedVersion = '1.6.0'

# ---- Locate the installed core ---------------------------------------------
$DataDir = $null
try {
    $out = & arduino-cli config get directories.data 2>$null
    if ($LASTEXITCODE -eq 0 -and $out) { $DataDir = [string]$out }
} catch { }
if (-not $DataDir -or -not (Test-Path $DataDir)) {
    $DataDir = Join-Path $env:LOCALAPPDATA 'Arduino15'
}
$CoreRoot = Join-Path $DataDir 'packages\arduino\hardware\renesas_uno'
if (-not (Test-Path $CoreRoot)) {
    Write-Error "ERROR: no renesas_uno core installed. Run: arduino-cli core install arduino:renesas_uno"
}
$Versions = Get-ChildItem -LiteralPath $CoreRoot -Directory | Sort-Object -Property Name
if ($Versions.Count -eq 0) {
    Write-Error "ERROR: no version directory under $CoreRoot"
}
$CoreDir = $Versions[-1].FullName
$Ver     = Split-Path -Leaf $CoreDir

if ($Ver -ne $PinnedVersion -and -not $AllowAnyVersion) {
    Write-Error ("ERROR: core version {0} found, but this script is pinned to {1}. Re-run with -AllowAnyVersion to try anyway." -f $Ver, $PinnedVersion)
}

Write-Host "Core: $CoreDir"

# ---- Edit definitions (byte-exact segments of renesas_uno 1.6.0) ------------
$Edits = @(
    @{
        File   = 'variants\NANOR4\tusb_config.h'
        Label  = 'NANOR4_vendor'
        Old    = @'
#define CFG_TUD_VENDOR           0
'@
        New    = @'
#define CFG_TUD_VENDOR_RX_BUFSIZE  64
#define CFG_TUD_VENDOR_TX_BUFSIZE  64
#define CFG_TUD_VENDOR           1
'@
    },
    @{
        File   = 'variants\MINIMA\tusb_config.h'
        Label  = 'MINIMA_vendor'
        Old    = @'
#define CFG_TUD_VENDOR           0
'@
        New    = @'
#define CFG_TUD_VENDOR_RX_BUFSIZE  64
#define CFG_TUD_VENDOR_TX_BUFSIZE  64
#define CFG_TUD_VENDOR           1
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'usb_hooks_insert'
        Old    = @'
const uint8_t *tud_descriptor_device_cb(void) {
'@
        New    = @'
// XR4 (ArduinoX360-tinyusb): weak hooks that let a library contribute a
// custom vendor-class interface and/or override VID/PID. Implemented strongly
// by the ArduinoX360-tinyusb Renesas backend; absent otherwise.
extern "C" uint8_t const* __USBGetCustomInterfaceDescriptor(
    uint8_t itfnum, size_t* len, uint8_t* num_interfaces) __attribute__((weak));
extern "C" bool __USBGetVidPid(uint16_t* vid, uint16_t* pid) __attribute__((weak));

const uint8_t *tud_descriptor_device_cb(void) {
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'usb_vidpid_override'
        Old    = @'
    // Descriptors are always composite
    usbd_desc_device.bDeviceClass = 0;
    usbd_desc_device.bDeviceSubClass = 0;
    usbd_desc_device.bDeviceProtocol = 0;
    return (const uint8_t *)&usbd_desc_device;
}
'@
        New    = @'
    // Descriptors are always composite
    usbd_desc_device.bDeviceClass = 0;
    usbd_desc_device.bDeviceSubClass = 0;
    usbd_desc_device.bDeviceProtocol = 0;

    // XR4: lazy VID/PID override - this descriptor is rebuilt on every call,
    // so values set later from setup() are still honored at enumeration time.
    if (__USBGetVidPid) {
        uint16_t vid = 0;
        uint16_t pid = 0;
        if (__USBGetVidPid(&vid, &pid)) {
            usbd_desc_device.idVendor = vid;
            usbd_desc_device.idProduct = pid;
        }
    }
    return (const uint8_t *)&usbd_desc_device;
}
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'usb_custom_iface_block'
        Old    = @'
#else
        uint8_t msd_desc[0] = {};
#endif
        
        // Create configuration descriptor
'@
        New    = @'
#else
        uint8_t msd_desc[0] = {};
#endif

        // XR4: optional library-provided custom vendor interface(s)
        bool install_custom = false;
        size_t custom_len = 0;
        uint8_t custom_iface_count = 0;
        uint8_t const* custom_desc = nullptr;
        if (__USBGetCustomInterfaceDescriptor) {
            custom_desc = __USBGetCustomInterfaceDescriptor(
                interface_count, &custom_len, &custom_iface_count);
            install_custom = custom_desc && custom_len > 0;
            interface_count += custom_iface_count;
        }

        // Create configuration descriptor
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'usb_desc_len_line'
        Old    = @'
            + (install_MSD ? sizeof(msd_desc) : 0);
'@
        New    = @'
            + (install_MSD ? sizeof(msd_desc) : 0)
            + (install_custom ? custom_len : 0);
'@
    },
    @{
        File   = 'cores\arduino\usb\USB.cpp'
        Label  = 'usb_memcpy_append'
        Old    = @'
            if (install_MSD) {
                memcpy(ptr, msd_desc, sizeof(msd_desc));
                ptr += sizeof(msd_desc);
            }
        }
    }
}
'@
        New    = @'
            if (install_MSD) {
                memcpy(ptr, msd_desc, sizeof(msd_desc));
                ptr += sizeof(msd_desc);
            }
            if (install_custom && custom_desc) {
                memcpy(ptr, custom_desc, custom_len);
                ptr += custom_len;
            }
        }
    }
}
'@
    },
    @{
        File   = 'cores\arduino\tinyusb\class\vendor\vendor_device.c'
        Label  = 'vendord_xfer'
        Old    = @'
    // Open endpoint pair with usbd helper
    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, desc_itf->bNumEndpoints, TUSB_XFER_BULK, &p_vendor->ep_out, &p_vendor->ep_in), 0);
'@
        New    = @'
    // Open endpoint pair with usbd helper. XR4: use the transfer type the
    // endpoint descriptor actually declares. The stock code forces BULK here,
    // which rejects the INTERRUPT endpoints of Xbox 360 (XInput) interfaces
    // and makes SET_CONFIGURATION stall.
    uint8_t const ep_xfer_type =
        ((tusb_desc_endpoint_t const *) p_desc)->bmAttributes.xfer;
    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, desc_itf->bNumEndpoints, ep_xfer_type, &p_vendor->ep_out, &p_vendor->ep_in), 0);
'@
    }
)

# ---- Helpers ----------------------------------------------------------------
function Read-CoreFile([string]$Rel) {
    [System.IO.File]::ReadAllText((Join-Path $CoreDir $Rel))
}
function Write-CoreFile([string]$Rel, [string]$Text) {
    [System.IO.File]::WriteAllText((Join-Path $CoreDir $Rel), $Text, (New-Object System.Text.UTF8Encoding($false)))
}

# Exact replace that tolerates LF vs CRLF working trees.
function Apply-Replace([string]$Text, [string]$Old, [string]$New, [string]$Label) {
    foreach ($eol in @("`r`n", "`n")) {
        $o = $Old.Replace("`n", $eol)
        if ($Text.Contains($o)) {
            return @{ Text = $Text.Replace($o, $New.Replace("`n", $eol)); Ok = $true }
        }
    }
    throw "Marker not found in current core: $Label"
}

# ---- Idempotency / partial-state checks -------------------------------------
$Markers = @(
    @{ File='cores\arduino\usb\USB.cpp';                                   Pattern='__USBGetCustomInterfaceDescriptor'; Name='usb' },
    @{ File='variants\NANOR4\tusb_config.h';                               Pattern='#define CFG_TUD_VENDOR           1'; Name='nanor4' },
    @{ File='variants\MINIMA\tusb_config.h';                               Pattern='#define CFG_TUD_VENDOR           1'; Name='minima' },
    @{ File='cores\arduino\tinyusb\class\vendor\vendor_device.c';          Pattern='XR4: use the transfer type';        Name='vendord' }
)
$have = @{}
$total = 0
foreach ($m in $Markers) {
    $text = Read-CoreFile $m.File
    $present = ($text.IndexOf($m.Pattern, [System.StringComparison]::Ordinal) -ge 0)
    $have[$m.Name] = $present
    if ($present) { $total++ }
}
if ($total -eq $Markers.Count) {
    Write-Host 'Already patched - nothing to do.'
    exit 0
}
if ($total -ne 0) {
    $state = ($Markers | ForEach-Object { "{0}={1}" -f $_.Name, $(if ($have[$_.Name]) { 1 } else { 0 }) }) -join ' '
    Write-Error "ERROR: core appears PARTIALLY patched (markers: $state). Reinstall the core for a clean state: arduino-cli core uninstall arduino:renesas_uno && arduino-cli core install arduino:renesas_uno"
}

# ---- Apply ------------------------------------------------------------------
$files = @{}
foreach ($e in $Edits) {
    if (-not $files.ContainsKey($e.File)) { $files[$e.File] = Read-CoreFile $e.File }
}
foreach ($e in $Edits) {
    $r = Apply-Replace $files[$e.File] $e.Old $e.New $e.Label
    $files[$e.File] = $r.Text
    Write-Host ("  patched: {0} ({1})" -f $e.File, $e.Label)
}
foreach ($rel in $files.Keys) { Write-CoreFile $rel $files[$rel] }

# ---- Post-checks -------------------------------------------------------------
$fail = $false
foreach ($m in $Markers) {
    $text = Read-CoreFile $m.File
    if ($text.IndexOf($m.Pattern, [System.StringComparison]::Ordinal) -lt 0) {
        Write-Host ("POSTCHECK FAIL: {0}" -f $m.Name)
        $fail = $true
    }
}
if ($fail) { exit 1 }

Write-Host 'Post-checks passed.'
Write-Host ''
Write-Host 'Build XInput sketches for Nano R4 / UNO R4 Minima with:'
Write-Host '  arduino-cli compile --fqbn arduino:renesas_uno:nanor4 \'
Write-Host '    --build-property compiler.cpp.extra_flags=-DDISABLE_USB_SERIAL \'
Write-Host '    --library <path-to-ArduinoX360-tinyusb> <sketch>'
