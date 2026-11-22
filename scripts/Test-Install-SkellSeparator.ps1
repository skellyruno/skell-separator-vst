[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$PackageRoot,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$TestRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:assertions = 0
$script:passed = $false

function Assert([bool]$Condition, [string]$Message) {
    $script:assertions++
    if (-not $Condition) { throw $Message }
}

function Get-Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-PackageChecksums([string]$Root) {
    $rootFull = [System.IO.Path]::GetFullPath($Root)
    $sumPath = Join-Path $rootFull 'SHA256SUMS.txt'
    $files = @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force |
        Where-Object { -not [string]::Equals($_.FullName, $sumPath, [System.StringComparison]::OrdinalIgnoreCase) } |
        Sort-Object FullName)
    $lines = @()
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($rootFull.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $lines += ((Get-Hash $file.FullName) + '  ' + $relative)
    }
    [System.IO.File]::WriteAllLines($sumPath, [string[]]$lines, [System.Text.Encoding]::ASCII)
}

function Invoke-ExpectedFailure([scriptblock]$Action, [string]$Message) {
    $failed = $false
    try { & $Action }
    catch { $failed = $true }
    Assert $failed $Message
}

function Copy-Package([string]$Source, [string]$Destination) {
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -ErrorAction Stop
}

$package = [System.IO.Path]::GetFullPath($PackageRoot)
$installer = [System.IO.Path]::GetFullPath($InstallerPath)
$testRoot = [System.IO.Path]::GetFullPath($TestRoot)
if (-not (Test-Path -LiteralPath $package -PathType Container)) { throw "PackageRoot not found: $package" }
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw "Installer script not found: $installer" }
if (Test-Path -LiteralPath $testRoot) { throw "Refusing to use an existing test root: $testRoot" }
$pathRoot = [System.IO.Path]::GetPathRoot($testRoot)
if ([string]::Equals($testRoot.TrimEnd([char[]]@('\', '/')), $pathRoot.TrimEnd([char[]]@('\', '/')), [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing a filesystem root as the test root.'
}
$parent = [System.IO.Path]::GetDirectoryName($testRoot)
if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
New-Item -ItemType Directory -Path $testRoot -ErrorAction Stop | Out-Null
$token = [Guid]::NewGuid().ToString('N')
$marker = Join-Path $testRoot ('.skell-separator-test-root-' + $token)
[System.IO.File]::WriteAllText($marker, $token, [System.Text.UTF8Encoding]::new($false))

try {
    $packageV1 = Join-Path $testRoot 'package-v1'
    Copy-Package $package $packageV1

    $target = Join-Path $testRoot 'VST3'
    $backups = Join-Path $testRoot 'Backups'
    $whatIf = & $installer -PackageRoot $packageV1 -Vst3Directory $target -BackupRoot $backups -WhatIf
    Assert ($whatIf.status -eq 'what-if') 'WhatIf did not return the expected status.'
    Assert (-not (Test-Path -LiteralPath $target)) 'WhatIf created the installation target.'
    Assert (-not (Test-Path -LiteralPath $backups)) 'WhatIf created a backup directory.'

    $fresh = & $installer -PackageRoot $packageV1 -Vst3Directory $target -BackupRoot $backups -Confirm:$false
    Assert ($fresh.status -eq 'installed') 'Fresh install did not complete.'
    Assert (Test-Path -LiteralPath (Join-Path $target 'Skell Separator.vst3\Contents\x86_64-win\Skell Separator.vst3') -PathType Leaf) 'Installed VST3 module is missing.'
    Assert ([string]$fresh.version -eq '1.13.0') 'Fresh install has an unexpected version.'

    $again = & $installer -PackageRoot $packageV1 -Vst3Directory $target -BackupRoot $backups -Confirm:$false
    Assert ($again.status -eq 'already-installed') 'Repeated install was not idempotent.'
    Assert (-not (Test-Path -LiteralPath $backups)) 'Idempotent install unexpectedly created a backup.'

    $packageV2 = Join-Path $testRoot 'package-v2'
    Copy-Package $packageV1 $packageV2
    $moduleInfoPath = Join-Path $packageV2 'Skell Separator.vst3\Contents\Resources\moduleinfo.json'
    $moduleInfo = Get-Content -LiteralPath $moduleInfoPath -Raw | ConvertFrom-Json
    $moduleInfo.Version = '1.13.1'
    foreach ($class in $moduleInfo.Classes) { $class.Version = '1.13.1' }
    [System.IO.File]::WriteAllText($moduleInfoPath, (($moduleInfo | ConvertTo-Json -Depth 20) + [Environment]::NewLine), [System.Text.UTF8Encoding]::new($false))

    $verificationPath = Join-Path $packageV2 'vst3-verification.json'
    $verification = Get-Content -LiteralPath $verificationPath -Raw | ConvertFrom-Json
    $verification.version = '1.13.1'
    [System.IO.File]::WriteAllText($verificationPath, (($verification | ConvertTo-Json -Depth 20) + [Environment]::NewLine), [System.Text.UTF8Encoding]::new($false))

    $buildInfoPath = Join-Path $packageV2 'build-info.json'
    $buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
    $buildInfo.version = '1.13.1'
    [System.IO.File]::WriteAllText($buildInfoPath, (($buildInfo | ConvertTo-Json -Depth 20) + [Environment]::NewLine), [System.Text.UTF8Encoding]::new($false))
    Write-PackageChecksums $packageV2

    $updated = & $installer -PackageRoot $packageV2 -Vst3Directory $target -BackupRoot $backups -Confirm:$false
    Assert ($updated.status -eq 'installed') 'Update did not complete.'
    Assert ([string]$updated.version -eq '1.13.1') 'Updated version is incorrect.'
    Assert (-not [string]::IsNullOrWhiteSpace([string]$updated.backup_path)) 'Update did not create a persistent backup.'
    $originalBackup = [string]$updated.backup_path
    $backupManifest = Get-Content -LiteralPath (Join-Path $originalBackup 'backup-manifest.json') -Raw | ConvertFrom-Json
    Assert ($backupManifest.version -eq '1.13.0') 'Pre-update backup does not contain the original version.'
    Assert (Test-Path -LiteralPath (Join-Path $originalBackup 'Skell Separator.vst3') -PathType Container) 'Pre-update VST3 backup is missing.'

    $rolledBack = & $installer -RollbackFrom $originalBackup -Vst3Directory $target -BackupRoot $backups -Confirm:$false
    Assert ($rolledBack.status -eq 'rolled-back') 'Rollback did not complete.'
    Assert ([string]$rolledBack.version -eq '1.13.0') 'Rollback did not restore the original version.'
    Assert (-not [string]::IsNullOrWhiteSpace([string]$rolledBack.backup_path)) 'Rollback did not preserve the version it replaced.'

    $badBinaryPackage = Join-Path $testRoot 'bad-binary-package'
    Copy-Package $packageV1 $badBinaryPackage
    $moduleBinary = Join-Path $badBinaryPackage 'Skell Separator.vst3\Contents\x86_64-win\Skell Separator.vst3'
    [System.IO.File]::AppendAllText($moduleBinary, 'tamper')
    $badBinaryTarget = Join-Path $testRoot 'BadBinaryTarget'
    Invoke-ExpectedFailure {
        & $installer -PackageRoot $badBinaryPackage -Vst3Directory $badBinaryTarget -BackupRoot (Join-Path $testRoot 'BadBinaryBackups') -Confirm:$false
    } 'Installer accepted a package with a tampered module.'
    Assert (-not (Test-Path -LiteralPath $badBinaryTarget)) 'Tampered package created an installation target.'

    $badPathPackage = Join-Path $testRoot 'bad-path-package'
    Copy-Package $packageV1 $badPathPackage
    Add-Content -LiteralPath (Join-Path $badPathPackage 'SHA256SUMS.txt') -Value ((('0' * 64) + '  ../escape.txt'))
    $badPathTarget = Join-Path $testRoot 'BadPathTarget'
    Invoke-ExpectedFailure {
        & $installer -PackageRoot $badPathPackage -Vst3Directory $badPathTarget -BackupRoot (Join-Path $testRoot 'BadPathBackups') -Confirm:$false
    } 'Installer accepted a checksum path traversal.'
    Assert (-not (Test-Path -LiteralPath $badPathTarget)) 'Path traversal package created an installation target.'

    $duplicateJsonPackage = Join-Path $testRoot 'duplicate-json-package'
    Copy-Package $packageV1 $duplicateJsonPackage
    [System.IO.File]::WriteAllText((Join-Path $duplicateJsonPackage 'build-info.json'),
        '{"product":"Skell Separator","product":"Other"}', [System.Text.UTF8Encoding]::new($false))
    Write-PackageChecksums $duplicateJsonPackage
    $duplicateJsonTarget = Join-Path $testRoot 'DuplicateJsonTarget'
    Invoke-ExpectedFailure {
        & $installer -PackageRoot $duplicateJsonPackage -Vst3Directory $duplicateJsonTarget -BackupRoot (Join-Path $testRoot 'DuplicateJsonBackups') -Confirm:$false
    } 'Installer accepted duplicate JSON properties.'
    Assert (-not (Test-Path -LiteralPath $duplicateJsonTarget)) 'Duplicate JSON package created an installation target.'

    $staleReleaseStatusPackage = Join-Path $testRoot 'stale-release-status-package'
    Copy-Package $packageV1 $staleReleaseStatusPackage
    $staleStatusPath = Join-Path $staleReleaseStatusPackage 'vst3-verification.json'
    $staleStatus = Get-Content -LiteralPath $staleStatusPath -Raw | ConvertFrom-Json
    $staleStatus.release_status = 'private preview only; host acceptance not implied'
    [System.IO.File]::WriteAllText($staleStatusPath, (($staleStatus | ConvertTo-Json -Depth 20) + [Environment]::NewLine), [System.Text.UTF8Encoding]::new($false))
    Write-PackageChecksums $staleReleaseStatusPackage
    $staleStatusTarget = Join-Path $testRoot 'StaleReleaseStatusTarget'
    Invoke-ExpectedFailure {
        & $installer -PackageRoot $staleReleaseStatusPackage -Vst3Directory $staleStatusTarget -BackupRoot (Join-Path $testRoot 'StaleReleaseStatusBackups') -Confirm:$false
    } 'Installer accepted an outdated release_status value.'
    Assert (-not (Test-Path -LiteralPath $staleStatusTarget)) 'Outdated release_status created an installation target.'

    $badBackup = Join-Path $testRoot 'bad-backup'
    Copy-Package $originalBackup $badBackup
    $badBackupModule = Join-Path $badBackup 'Skell Separator.vst3\Contents\x86_64-win\Skell Separator.vst3'
    [System.IO.File]::AppendAllText($badBackupModule, 'tamper')
    $badRollbackTarget = Join-Path $testRoot 'BadRollbackTarget'
    Invoke-ExpectedFailure {
        & $installer -RollbackFrom $badBackup -Vst3Directory $badRollbackTarget -BackupRoot (Join-Path $testRoot 'BadRollbackBackups') -Confirm:$false
    } 'Rollback accepted a tampered backup.'
    Assert (-not (Test-Path -LiteralPath $badRollbackTarget)) 'Tampered backup created a rollback target.'

    $invalidTargetRoot = Join-Path $testRoot 'InvalidExistingTarget'
    New-Item -ItemType Directory -Path $invalidTargetRoot | Out-Null
    $invalidTarget = Join-Path $invalidTargetRoot 'Skell Separator.vst3'
    [System.IO.File]::WriteAllText($invalidTarget, 'preserve existing data')
    Invoke-ExpectedFailure {
        & $installer -PackageRoot $packageV1 -Vst3Directory $invalidTargetRoot -BackupRoot (Join-Path $testRoot 'InvalidTargetBackups') -Confirm:$false
    } 'Installer overwrote an invalid pre-existing target.'
    Assert ((Get-Content -LiteralPath $invalidTarget -Raw) -eq 'preserve existing data') 'Installer modified an invalid pre-existing target.'

    $script:passed = $true
    Write-Host "Skell Separator install/update/rollback tests: $script:assertions assertions passed."
}
finally {
    if ($script:passed -and (Test-Path -LiteralPath $testRoot -PathType Container)) {
        $rootItem = Get-Item -LiteralPath $testRoot -Force
        $markerItem = if (Test-Path -LiteralPath $marker -PathType Leaf) { Get-Item -LiteralPath $marker -Force } else { $null }
        $nested = @(Get-ChildItem -LiteralPath $testRoot -Recurse -Force -ErrorAction Stop |
            Where-Object { ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0 })
        if (($rootItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0 -and
            $null -ne $markerItem -and
            ($markerItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0 -and
            ([System.IO.File]::ReadAllText($marker) -ceq $token) -and
            $nested.Count -eq 0) {
            Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction Stop
        }
        else {
            Write-Warning "Test root ownership could not be verified; preserving it at $testRoot"
        }
    }
    elseif (-not $script:passed) {
        Write-Warning "Tests failed; preserving the owned test directory for diagnosis: $testRoot"
    }
}
