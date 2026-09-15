param(
    [ValidateSet("AZERTY", "QWERTY")]
    [string]$Layout = "AZERTY"
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path "$PSScriptRoot\..\.."
$emu  = Join-Path $root "_Build\phase0-windows\kyty_emulator.exe"
$game = "C:\Dev\ps5\Bendy\PPSA27624-app0"

if (-not (Test-Path $emu)) {
    throw "Emulator not found: $emu"
}

$common = @(
    "Up=Up",
    "Down=Down",
    "Left=Left",
    "Right=Right",

    "RightStickUp=T",
    "RightStickDown=G",
    "RightStickLeft=F",
    "RightStickRight=H",

    "Triangle=I",
    "Circle=L",
    "Cross=Return",
    "Square=K",

    "R1=E",

    "L3=Left Shift",
    "R3=Left Ctrl",

    "Options=P",

    "TouchPad=Backspace",
    "TouchPadRight=Tab"
)

if ($Layout -eq "AZERTY") {
    $layoutBindings = @(
        "LeftStickUp=Z",
        "LeftStickDown=S",
        "LeftStickLeft=Q",
        "LeftStickRight=D",
        "L1=A"
    )
}
else {
    $layoutBindings = @(
        "LeftStickUp=W",
        "LeftStickDown=S",
        "LeftStickLeft=A",
        "LeftStickRight=D",
        "L1=Q"
    )
}

$bindings = $common + $layoutBindings

$args = @(
    "--game", $game
)

foreach ($binding in $bindings) {
    $args += "--keymap"
    $args += $binding
}

Remove-Item Env:SDL_AUDIODRIVER -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "ProsperoX keyboard preset: $Layout"
Write-Host "Movement: $(if ($Layout -eq 'AZERTY') {'ZQSD'} else {'WASD'})"
Write-Host "ENTER = Cross / Confirm"
Write-Host "L     = Circle / Back"
Write-Host "P     = Options"
Write-Host "TFGH  = Right stick"
Write-Host "Arrows = D-pad"
Write-Host ""

& $emu @args
