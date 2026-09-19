<#
.SYNOPSIS
    Configure and build ProsperoX on Windows with the repository's pinned toolchain.

.DESCRIPTION
    The phase0-windows preset names the compiler as `clang-cl`, which resolves
    only when the pinned LLVM in _Build/tools is on PATH. A shell prepared with
    VsDevCmd.bat alone supplies MSVC but not that LLVM, so the next time CMake
    regenerates -- which a branch switch triggers on its own, because the source
    globs change -- configuration fails with:

        The CMAKE_C_COMPILER: clang-cl is not a full path and was not found in
        the PATH.

    That is an environment problem rather than a defect in the preset. This
    script prepares the environment the same way the Phase 0 gate does and then
    builds, so a build cannot pick up a half-prepared shell.

.PARAMETER Targets
    Build targets. Defaults to the emulator and the test targets.

.PARAMETER Bootstrap
    Download and verify the pinned LLVM and glslang first. Needed once per
    clone; harmless afterwards.

.PARAMETER Test
    Run the CTest suite after a successful build.

.EXAMPLE
    ./scripts/phase0/build-windows.ps1 -Bootstrap
    ./scripts/phase0/build-windows.ps1 -Targets kyty_emulator
    ./scripts/phase0/build-windows.ps1 -Test
#>
[CmdletBinding()]
param(
    [string[]]$Targets = @('kyty_emulator', 'kyty_tests'),
    [switch]$Bootstrap,
    [switch]$Test,
    [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $repoRoot
try {
    if ($Bootstrap) {
        & (Join-Path $PSScriptRoot 'bootstrap-windows.ps1')
    }

    # Dot-sourced: windows-env.ps1 sets process environment and verifies that
    # every pinned tool resolves, so a missing one fails here rather than
    # halfway through a configure.
    . (Join-Path $PSScriptRoot 'windows-env.ps1')

    # A dirty working tree stamps the build as dirty, and the Vulkan driver
    # pipeline cache refuses to persist for a dirty build. Measuring shader or
    # pipeline warm-up against such a build measures a cold cache every run.
    $dirty = (& git status --porcelain) | Where-Object { $_ }
    if ($dirty) {
        Write-Warning ("Working tree has {0} modified path(s). The build will be stamped dirty and the " +
                       "Vulkan pipeline cache will stay disabled. Commit or stash before taking " +
                       "performance measurements." -f $dirty.Count)
    }

    cmake --preset phase0-windows
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

    foreach ($target in $Targets) {
        cmake --build --preset phase0-windows --parallel $Jobs --target $target
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $target" }
    }

    if ($Test) {
        ctest --test-dir _Build/phase0-windows --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'CTest reported failures' }
    }
}
finally {
    Pop-Location
}
