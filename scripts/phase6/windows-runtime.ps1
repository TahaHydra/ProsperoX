param(
    [Parameter(Mandatory=$true)][Alias('GameDir')][string]$GamePath,
    [Parameter(Mandatory=$true)][string]$EvidenceDir,
    [ValidateRange(1,1800)][int]$Seconds = 90,
    [switch]$GraphicsDebugDump
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../phase0/windows-env.ps1"
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/../..")
$entry = Get-Item -LiteralPath $GamePath
$game = $entry.FullName
$gameRoot = if ($entry.PSIsContainer) { $game } else { $entry.DirectoryName }
$evidence = [IO.Path]::GetFullPath($EvidenceDir)
if (Test-Path -LiteralPath $evidence) { throw 'Choose a new evidence directory.' }
$exe = "$repo/_Build/phase0-windows/kyty_emulator.exe"
$eboot = if ($entry.PSIsContainer) { Join-Path $game 'eboot.bin' } else { $game }
if (!(Test-Path -LiteralPath $eboot)) { throw 'GamePath must be an executable or contain eboot.bin.' }
New-Item -ItemType Directory -Path $evidence | Out-Null
Get-FileHash -LiteralPath $exe,$eboot | Format-List > "$evidence/hashes.txt"
git -C $repo rev-parse HEAD > "$evidence/revision.txt"
git -C $repo diff --binary > "$evidence/changes.patch"
$metadataPath = Join-Path $gameRoot 'sce_sys/param.json'
if (Test-Path -LiteralPath $metadataPath) {
    Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json |
        Select-Object titleId,contentId,contentVersion,masterVersion,requiredSystemSoftwareVersion |
        ConvertTo-Json > "$evidence/title.json"
}
$env:VK_LAYER_PATH = "$repo/_Build/tools/vulkan-1.4.357.0/Bin"
$env:PROSPEROX_VULKAN_DEVICE = 'RX 7800 XT'
$env:PROSPEROX_VULKAN_VALIDATION = '1'
$env:VK_LAYER_VALIDATE_SYNC = '1'
$env:VK_VALIDATION_VALIDATE_SYNC = 'true'
$env:VK_VALIDATION_SYNCVAL_SUBMIT_TIME_VALIDATION = 'true'
# The synthetic build helper selects dummy audio. Runtime must use the real
# Windows output backend; changes apply to this helper process only.
Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue
Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public static class Phase6ErrorMode { [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode); }'
[Phase6ErrorMode]::SetErrorMode(0x8003) | Out-Null
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = $evidence
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($arg in @('--game',$game,'--vulkan-validation','true','--printf-direction','File',
                   '--printf-output-file',"$evidence/guest.log")) { $start.ArgumentList.Add($arg) }
if ($GraphicsDebugDump) {
    $start.ArgumentList.Add('--graphics-debug-dump')
    $start.ArgumentList.Add('true')
}
$start.ArgumentList | ConvertTo-Json > "$evidence/arguments.json"
$process = [Diagnostics.Process]::Start($start)
$outFile = [IO.File]::Create("$evidence/stdout.log")
$errFile = [IO.File]::Create("$evidence/stderr.log")
$outCopy = $process.StandardOutput.BaseStream.CopyToAsync($outFile)
$errCopy = $process.StandardError.BaseStream.CopyToAsync($errFile)
$begin = Get-Date
Write-Output "PHASE6_RUNTIME_START pid=$($process.Id) seconds=$Seconds evidence=$evidence"
$naturalExit = $process.WaitForExit($Seconds * 1000)
if (!$naturalExit) {
    $requested = $process.CloseMainWindow()
    if (!$requested -or !$process.WaitForExit(10000)) { $process.Kill($true); $process.WaitForExit() }
}
$null = $outCopy.GetAwaiter().GetResult(); $null = $errCopy.GetAwaiter().GetResult()
$outFile.Dispose(); $errFile.Dispose()
@{ start=$begin.ToString('o'); end=(Get-Date).ToString('o'); exit=$process.ExitCode;
   natural_exit=$naturalExit; timeout_seconds=$Seconds; pid=$process.Id } |
    ConvertTo-Json > "$evidence/result.json"
Get-Content "$evidence/result.json"
# A timeout is a bounded observation, not proof of successful gameplay or stop.
if (!$naturalExit) { exit 2 }
exit $process.ExitCode
