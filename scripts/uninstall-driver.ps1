$ErrorActionPreference = 'Stop'
$driver = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'rokidmax'))
$steamPath = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
$roots = @()
if ($steamPath) {
    $roots += $steamPath
    $vdf = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $vdf) {
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw), '"path"\s+"([^"]+)"')) { $roots += ($match.Groups[1].Value -replace '\\\\', '\') }
    }
}
$candidates = @($roots | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object { Join-Path $_ 'steamapps\common\SteamVR\bin\win64\vrpathreg.exe' })
$steamVrInstall = (Get-ItemProperty -LiteralPath 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820' -ErrorAction SilentlyContinue).InstallLocation
if ($steamVrInstall) { $candidates += (Join-Path $steamVrInstall 'bin\win64\vrpathreg.exe') }
$candidates += 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'
$vrpathreg = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vrpathreg) {
    Write-Warning 'SteamVR/vrpathreg.exe was not found. The application files will still be removed.'
    exit 0
}
& $vrpathreg removedriver $driver
if ($LASTEXITCODE -ne 0) { throw "vrpathreg removedriver failed with exit code $LASTEXITCODE" }
& $vrpathreg show
Write-Host "Rokid Max external driver unregistered. No SteamVR files were modified." -ForegroundColor Green
