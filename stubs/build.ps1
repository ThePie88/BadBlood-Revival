$ErrorActionPreference = "Stop"

$mingwBin = Get-ChildItem "C:\Users\filip\AppData\Local\Microsoft\WinGet\Packages" -Recurse -Filter "gcc.exe" |
    Select-Object -First 1 -ExpandProperty DirectoryName
$gcc64 = Join-Path $mingwBin "gcc.exe"

$srcDir = "$PSScriptRoot\src"
$outDir = "$PSScriptRoot\out"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host "Using GCC: $gcc64"
Write-Host ""

# BEClient_x64.dll
Write-Host "Building BEClient_x64.dll..."
& $gcc64 -shared -o "$outDir\BEClient_x64.dll" "$srcDir\beclient.c" -Wl,--kill-at
Write-Host "  OK"

# BEServer_x64.dll
Write-Host "Building BEServer_x64.dll..."
& $gcc64 -shared -o "$outDir\BEServer_x64.dll" "$srcDir\beserver.c" -Wl,--kill-at
Write-Host "  OK"

# EasyAntiCheat_x64.dll
Write-Host "Building EasyAntiCheat_x64.dll..."
& $gcc64 -shared -o "$outDir\EasyAntiCheat_x64.dll" "$srcDir\eac_x64.c" -Wl,--kill-at
Write-Host "  OK"

# EasyAntiCheat_x86.dll — need 32-bit compiler
# Check if i686 gcc exists in the same package
$gcc32path = $mingwBin -replace "mingw64", "mingw32"
$gcc32 = Join-Path $gcc32path "gcc.exe"
if (Test-Path $gcc32) {
    Write-Host "Building EasyAntiCheat_x86.dll (32-bit)..."
    & $gcc32 -shared -o "$outDir\EasyAntiCheat_x86.dll" "$srcDir\eac_x86.c" -Wl,--kill-at
    Write-Host "  OK"
} else {
    Write-Host "WARN: No 32-bit GCC found at $gcc32 — building x86 stub as x64 (may need fix later)"
    & $gcc64 -shared -o "$outDir\EasyAntiCheat_x86.dll" "$srcDir\eac_x86.c" -Wl,--kill-at
    Write-Host "  OK (built as x64 — verify if game loads it)"
}

Write-Host ""
Write-Host "=== Build complete ==="
Get-ChildItem $outDir -Filter "*.dll" | ForEach-Object {
    Write-Host ("  {0} ({1:N0} bytes)" -f $_.Name, $_.Length)
}
