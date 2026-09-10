param([string]$BuildDir = '_Build/phase0-windows', [string]$EvidenceDir = '_Build/evidence/phase3',
      [string]$Tests = 'phase3_|phase0_eop_visibility|command_scheduler_timeline|stream_buffer_ring|gpu_command_lane|pm4_context_state|buffer_cache_dirty_gc')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../phase0/windows-env.ps1"
$sdk = [IO.Path]::GetFullPath("$PSScriptRoot/../../_Build/tools/vulkan-1.4.357.0/Bin")
if (!(Test-Path -LiteralPath "$sdk/VkLayer_khronos_validation.json")) {
    throw 'Missing pinned local Vulkan validation layer. See docs/phase3-gpu-completion.md.'
}
$env:VK_LAYER_PATH = $sdk
$env:PROSPEROX_VULKAN_DEVICE = 'RX 7800 XT'
$env:PROSPEROX_VULKAN_VALIDATION = '1'
$env:VK_LAYER_VALIDATE_SYNC = '1'
if (Test-Path -LiteralPath $EvidenceDir) { throw 'Choose a new evidence directory; previous runs are immutable.' }
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
New-Item -ItemType Directory -Path "$EvidenceDir/tmp" | Out-Null
$env:TEMP = [IO.Path]::GetFullPath("$EvidenceDir/tmp")
$env:TMP = $env:TEMP
git rev-parse HEAD > "$EvidenceDir/revision.txt"
git diff --binary > "$EvidenceDir/changes.patch"
Get-FileHash "$sdk/VkLayer_khronos_validation.dll" | Format-List > "$EvidenceDir/validation-layer.txt"
& "$sdk/vulkaninfoSDK.exe" --summary > "$EvidenceDir/device.txt" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Vulkan device inventory failed' }
ctest --test-dir $BuildDir -R $Tests -V --output-junit "$([IO.Path]::GetFullPath($EvidenceDir))/ctest.xml" > "$EvidenceDir/ctest.log" 2>&1
$result = $LASTEXITCODE
Get-Content "$EvidenceDir/ctest.log" -Tail 25
if ($result -ne 0) { throw "Validation suite failed; inspect $EvidenceDir/ctest.log" }
