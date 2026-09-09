param([string]$ToolsRoot = (Join-Path $PSScriptRoot '../../_Build/tools'))
$ErrorActionPreference = 'Stop'
$ToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Install the Visual Studio C++ build tools and Windows SDK.' }
$vcVars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
$vcCommand = '"' + $vcVars + '" >nul && set'
& $env:ComSpec /d /s /c $vcCommand | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
$ninjaDir = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:PATH = "$ToolsRoot/llvm-21.1.8/bin;$ToolsRoot/glslang-16.5.0/bin;$ninjaDir;C:/Program Files/Git/mingw64/bin;C:/Program Files/Git/usr/bin;" + $env:PATH
foreach ($tool in 'clang-cl','lld-link','ninja','cmake','glslangValidator') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing tool: $tool" }
}
# Test-local isolation only. Do not alter registry or installed overlays.
$env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
$env:SDL_AUDIODRIVER = 'dummy'
