param([string]$ToolsRoot = (Join-Path $PSScriptRoot '../../_Build/tools'))
$ErrorActionPreference = 'Stop'
$ToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
$lock = Get-Content (Join-Path $PSScriptRoot 'toolchain-lock.json') -Raw | ConvertFrom-Json
$downloads = Join-Path $ToolsRoot 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
$sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
if (-not (Test-Path -LiteralPath $sevenZip)) { throw '7-Zip is required to extract the portable LLVM tools.' }
foreach ($name in 'llvm','glslang') {
    $asset = $lock.windows.$name
    $filename = if ($name -eq 'llvm') { "LLVM-$($asset.version)-win64.exe" } else { "glslang-$($asset.version).zip" }
    $archive = Join-Path $downloads $filename
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $asset.url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.sha256) {
        throw "Checksum mismatch: $archive (preserved for inspection)"
    }
    $destination = Join-Path $ToolsRoot "$name-$($asset.version)"
    if (-not (Test-Path -LiteralPath (Join-Path $destination '.phase0-verified'))) {
        & $sevenZip x $archive "-o$destination" -y
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $name" }
        Set-Content -LiteralPath (Join-Path $destination '.phase0-verified') -Value $asset.sha256
    }
}
# glslang 16 renamed the CLI. CMake's existing lookup still uses the legacy name.
Copy-Item -LiteralPath (Join-Path $ToolsRoot 'glslang-16.5.0/bin/glslang.exe') -Destination (Join-Path $ToolsRoot 'glslang-16.5.0/bin/glslangValidator.exe') -Force
Write-Output 'Portable tool archives verified. Dot-source windows-env.ps1 before configuring.'
