[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $GameDirectory,

    [Parameter(Mandatory = $true)]
    [string] $RunDirectory,

    [string] $GameExecutable,
    [string] $AssetPack,
    [switch] $PrepareOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-AbsolutePath([string] $Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Get-Sha256([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Initialize-Dozen([string] $Directory) {
    $version = '26.3.0'
    $archiveHash = 'c505fcc77e8f63a09d7ddac086591d2f542c0698d37fc9e3094685a34d0d9991'
    $files = [ordered]@{
        'vulkan_dzn.dll' = '2ee93175bb51f2d135b26296ad01b25e71fb006c95b0a24f14b4458d68d31a6d'
        'dxil.dll' = '64be2368211d257a038a7d4ea6bb3e24c878eb8d588c7b8414fccf759b69c2ac'
    }

    $bin = Join-Path $Directory 'x64'
    $download = $false
    foreach ($file in $files.Keys) {
        $path = Join-Path $bin $file
        if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Sha256 $path) -ne $files[$file]) {
            $download = $true
            break
        }
    }

    if ($download) {
        [void] [IO.Directory]::CreateDirectory($Directory)
        $archiveName = "mesa3d-$version-release-msvc.7z"
        $archive = Join-Path $Directory $archiveName
        if (!(Test-Path -LiteralPath $archive -PathType Leaf) -or (Get-Sha256 $archive) -ne $archiveHash) {
            $url = "https://github.com/pal1000/mesa-dist-win/releases/download/$version/$archiveName"
            Write-Host "Downloading Mesa Dozen $version for this profile..."
            & "$env:SystemRoot\System32\curl.exe" -fLsS --retry 2 --output $archive $url
            if ($LASTEXITCODE -ne 0) { throw 'The Dozen download failed.' }
            if ((Get-Sha256 $archive) -ne $archiveHash) { throw 'The Dozen archive checksum did not match.' }
        }
        & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $Directory -- x64/vulkan_dzn.dll x64/dxil.dll
        if ($LASTEXITCODE -ne 0) { throw 'The Dozen archive could not be extracted.' }
        foreach ($file in $files.Keys) {
            if ((Get-Sha256 (Join-Path $bin $file)) -ne $files[$file]) {
                throw "The Dozen checksum did not match for $file."
            }
        }
        Remove-Item -LiteralPath $archive
    }

    # An absolute ICD library path works even when the game is launched from a
    # different directory or the private profile contains spaces.
    $manifest = Join-Path $bin 'catherine-dozen.json'
    $record = [ordered]@{
        file_format_version = '1.0.1'
        ICD = [ordered]@{
            library_path = (Join-Path $bin 'vulkan_dzn.dll')
            api_version = '1.1.363'
            library_arch = '64'
        }
    }
    [IO.File]::WriteAllText($manifest, ($record | ConvertTo-Json -Depth 3))
    return $manifest
}

$source = (Resolve-Path -LiteralPath $GameDirectory).ProviderPath
$run = Get-AbsolutePath $RunDirectory
$sourcePrefix = $source.TrimEnd('\', '/') + '\'
$runPrefix = $run.TrimEnd('\', '/') + '\'
if ($runPrefix.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Place the private DirectX 12 profile outside the original game directory.'
}
if (!$GameExecutable) { $GameExecutable = Join-Path $source 'Catherine.exe' }
if (!$AssetPack) { $AssetPack = Join-Path $source 'assets.pak' }
$executable = (Resolve-Path -LiteralPath $GameExecutable).ProviderPath
$assets = (Resolve-Path -LiteralPath $AssetPack).ProviderPath
$exeHash = Get-Sha256 $executable

[void] [IO.Directory]::CreateDirectory($run)
$lockPath = Join-Path $run '.dx12-profile.lock'
$profilePath = Join-Path $run '.dx12-profile.json'
if (!(Test-Path -LiteralPath $profilePath -PathType Leaf) -and
    @(Get-ChildItem -LiteralPath $run -Force | Where-Object Name -NE '.dx12-profile.lock').Count) {
    throw 'Choose an empty folder or a profile already created by this launcher.'
}

try {
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch [IO.IOException] {
    throw 'This DirectX 12 profile is already in use.'
}

try {
    if (Test-Path -LiteralPath $profilePath -PathType Leaf) {
        $profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
        if ($profile.ExecutableSha256 -ne $exeHash -or $profile.Source -ne $source -or $profile.AssetPack -ne $assets) {
            throw 'This profile belongs to another build or asset pack. Choose a new run directory.'
        }
    } else {
        $profile = [ordered]@{ Source = $source; AssetPack = $assets; ExecutableSha256 = $exeHash }
        [IO.File]::WriteAllText($profilePath, ($profile | ConvertTo-Json))
    }

    $game = Join-Path $run 'Catherine.exe'
    if (!(Test-Path -LiteralPath $game -PathType Leaf) -or (Get-Sha256 $game) -ne $exeHash) {
        Copy-Item -LiteralPath $executable -Destination $game -Force
    }
    # Seed settings and saves once. Subsequent game writes stay in the profile.
    $dependencies = @(
        Get-ChildItem -LiteralPath $source -File | Where-Object {
            $_.Extension -eq '.dll' -or $_.Name -in @('save.dat', 'catherine-settings.ini', 'dev-checkpoints.pak')
        }
    )
    foreach ($file in $dependencies) {
        $target = Join-Path $run $file.Name
        if (!(Test-Path -LiteralPath $target -PathType Leaf)) {
            Copy-Item -LiteralPath $file.FullName -Destination $target
        }
    }

    $icd = Initialize-Dozen (Join-Path $run 'dozen-26.3.0')
    if ($PrepareOnly) {
        Write-Output $icd
        return
    }

    $launch = New-Object Diagnostics.ProcessStartInfo
    $launch.FileName = $game
    $launch.WorkingDirectory = $run
    $launch.UseShellExecute = $false
    $launch.RedirectStandardError = $true
    $launch.Arguments = '--assets-pak "' + $assets + '"'
    $launch.EnvironmentVariables['NX_DATA_ROOT'] = $run
    $launch.EnvironmentVariables['VK_DRIVER_FILES'] = $icd
    [void] $launch.EnvironmentVariables.Remove('VK_ICD_FILENAMES')
    [void] $launch.EnvironmentVariables.Remove('VK_ADD_DRIVER_FILES')
    $launch.EnvironmentVariables['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'
    # Dozen lacks depth-clip-control. The port's vertex transform preserves
    # NVN's clip range. Render-pass objects use full pass barriers and keep
    # the port from advertising the MSAA resolve path that faults on Dozen.
    $launch.EnvironmentVariables['NX_CLIP_Z_PATCH'] = '1'
    $launch.EnvironmentVariables['NX_RENDER_PASS_OBJECTS'] = '1'
    $launch.EnvironmentVariables['PATH'] = (Split-Path -Parent $icd) + ';' + $launch.EnvironmentVariables['PATH']
    $logPath = Join-Path $run 'catherine-dx12.log'
    $log = [IO.File]::Open($logPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $process = [Diagnostics.Process]::Start($launch)
        try {
            $capture = $process.StandardError.BaseStream.CopyToAsync($log)
            Write-Host "Catherine DirectX 12 (Mesa Dozen), PID $($process.Id)"
            $process.WaitForExit()
            $capture.GetAwaiter().GetResult()
            if ($process.ExitCode -ne 0) { throw "Catherine exited with code $($process.ExitCode)." }
        } finally {
            $process.Dispose()
        }
    } finally {
        $log.Dispose()
    }
} finally {
    $lock.Dispose()
}
