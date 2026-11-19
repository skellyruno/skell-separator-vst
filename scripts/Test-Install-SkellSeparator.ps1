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

function New-TestJunction([string]$Path, [string]$Target) {
    if (Test-Path -LiteralPath $Path) { throw "Test junction path already exists: $Path" }
    if (-not (Test-Path -LiteralPath $Target -PathType Container)) { throw "Test junction target is missing: $Target" }
    New-Item -ItemType Junction -Path $Path -Target $Target -ErrorAction Stop | Out-Null
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
        Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        throw "Test path was not created as a reparse point: $Path"
    }
}

function Remove-TestJunction([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    if ($null -eq $item) { return }
    if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
        throw "Refusing to remove a non-junction test path: $Path"
    }
    Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
}

function Invoke-ExpectedReparseFailure([scriptblock]$Action, [string]$Message) {
    $failure = $null
    try { & $Action | Out-Null }
    catch { $failure = $_.Exception.Message }
    Assert (-not [string]::IsNullOrWhiteSpace($failure)) $Message
    Assert ($failure -match '(?i)reparse point') "Failure did not identify the reparse-point guard: $failure"
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


    # Reparse-point regression coverage. All links and targets stay inside this
    # disposable test root; each expected rejection must name the guard itself.
    $junctionPackageTarget = Join-Path $testRoot 'junction-package-target'
    Copy-Package $packageV1 $junctionPackageTarget
    $junctionPackage = Join-Path $testRoot 'junction-package-root'
    New-TestJunction $junctionPackage $junctionPackageTarget
    $junctionPackageInstall = Join-Path $testRoot 'junction-package-install'
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $junctionPackage -Vst3Directory $junctionPackageInstall -BackupRoot (Join-Path $testRoot 'junction-package-backups') -Confirm:$false
    } 'Installer accepted a package-root junction.'
    Assert (-not (Test-Path -LiteralPath $junctionPackageInstall)) 'Package-root junction created an installation target.'
    Remove-TestJunction $junctionPackage

    $nestedPackage = Join-Path $testRoot 'nested-junction-package'
    Copy-Package $packageV1 $nestedPackage
    $nestedLinkTarget = Join-Path $testRoot 'nested-junction-target'
    New-Item -ItemType Directory -Path $nestedLinkTarget -ErrorAction Stop | Out-Null
    $nestedLink = Join-Path $nestedPackage 'unexpected-directory-link'
    New-TestJunction $nestedLink $nestedLinkTarget
    $nestedInstall = Join-Path $testRoot 'nested-junction-install'
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $nestedPackage -Vst3Directory $nestedInstall -BackupRoot (Join-Path $testRoot 'nested-junction-backups') -Confirm:$false
    } 'Installer accepted a nested package junction.'
    Assert (-not (Test-Path -LiteralPath $nestedInstall)) 'Nested package junction created an installation target.'
    Remove-TestJunction $nestedLink

    $destinationTarget = Join-Path $testRoot 'destination-junction-target'
    New-Item -ItemType Directory -Path $destinationTarget -ErrorAction Stop | Out-Null
    $destinationLink = Join-Path $testRoot 'destination-junction-root'
    New-TestJunction $destinationLink $destinationTarget
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $packageV1 -Vst3Directory $destinationLink -BackupRoot (Join-Path $testRoot 'destination-junction-backups') -Confirm:$false
    } 'Installer accepted a VST3 destination-root junction.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $destinationTarget 'Skell Separator.vst3'))) 'Destination-root junction wrote through to its target.'
    Remove-TestJunction $destinationLink

    $destinationParentTarget = Join-Path $testRoot 'destination-parent-target'
    New-Item -ItemType Directory -Path $destinationParentTarget -ErrorAction Stop | Out-Null
    $destinationParentLink = Join-Path $testRoot 'destination-parent-junction'
    New-TestJunction $destinationParentLink $destinationParentTarget
    $destinationChild = Join-Path $destinationParentLink 'VST3'
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $packageV1 -Vst3Directory $destinationChild -BackupRoot (Join-Path $testRoot 'destination-parent-backups') -Confirm:$false
    } 'Installer accepted a VST3 destination below a junction parent.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $destinationParentTarget 'VST3'))) 'Destination-parent junction created a child directory.'
    Remove-TestJunction $destinationParentLink

    $backupGuardTarget = Join-Path $testRoot 'backup-junction-install'
    $backupGuardNormalRoot = Join-Path $testRoot 'backup-junction-normal-backups'
    $backupGuardInstall = & $installer -PackageRoot $packageV1 -Vst3Directory $backupGuardTarget -BackupRoot $backupGuardNormalRoot -Confirm:$false
    Assert ($backupGuardInstall.status -eq 'installed') 'Could not prepare the backup-junction test installation.'
    $backupJunctionTarget = Join-Path $testRoot 'backup-junction-target'
    New-Item -ItemType Directory -Path $backupJunctionTarget -ErrorAction Stop | Out-Null
    $backupJunction = Join-Path $testRoot 'backup-junction-root'
    New-TestJunction $backupJunction $backupJunctionTarget
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $packageV2 -Vst3Directory $backupGuardTarget -BackupRoot $backupJunction -Confirm:$false
    } 'Installer accepted a backup-root junction during update.'
    $backupGuardInfo = Get-Content -LiteralPath (Join-Path $backupGuardTarget 'Skell Separator.vst3\Contents\Resources\moduleinfo.json') -Raw | ConvertFrom-Json
    Assert ([string]$backupGuardInfo.Version -eq '1.13.0') 'Backup-root junction rejection changed the installed version.'
    Assert (@(Get-ChildItem -LiteralPath $backupJunctionTarget -Force).Count -eq 0) 'Backup-root junction wrote through to its target.'
    Remove-TestJunction $backupJunction

    $backupParentTarget = Join-Path $testRoot 'backup-parent-target'
    New-Item -ItemType Directory -Path $backupParentTarget -ErrorAction Stop | Out-Null
    $backupParentLink = Join-Path $testRoot 'backup-parent-junction'
    New-TestJunction $backupParentLink $backupParentTarget
    $backupChild = Join-Path $backupParentLink 'Backups'
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $packageV2 -Vst3Directory $backupGuardTarget -BackupRoot $backupChild -Confirm:$false
    } 'Installer accepted a backup directory below a junction parent.'
    $backupGuardInfoAfterParent = Get-Content -LiteralPath (Join-Path $backupGuardTarget 'Skell Separator.vst3\Contents\Resources\moduleinfo.json') -Raw | ConvertFrom-Json
    Assert ([string]$backupGuardInfoAfterParent.Version -eq '1.13.0') 'Backup-parent junction rejection changed the installed version.'
    Assert (-not (Test-Path -LiteralPath $backupChild)) 'Backup-parent junction created a child directory.'
    Remove-TestJunction $backupParentLink

    $bundleJunctionTarget = Join-Path $testRoot 'existing-bundle-junction-target'
    Copy-Item -LiteralPath (Join-Path $packageV1 'Skell Separator.vst3') -Destination $bundleJunctionTarget -Recurse -ErrorAction Stop
    $bundleModule = Join-Path $bundleJunctionTarget 'Contents\x86_64-win\Skell Separator.vst3'
    $bundleHashBefore = Get-Hash $bundleModule
    $bundleDestination = Join-Path $testRoot 'existing-bundle-junction-destination'
    New-Item -ItemType Directory -Path $bundleDestination -ErrorAction Stop | Out-Null
    $bundleJunction = Join-Path $bundleDestination 'Skell Separator.vst3'
    New-TestJunction $bundleJunction $bundleJunctionTarget
    Invoke-ExpectedReparseFailure {
        & $installer -PackageRoot $packageV1 -Vst3Directory $bundleDestination -BackupRoot (Join-Path $testRoot 'existing-bundle-junction-backups') -Confirm:$false
    } 'Installer accepted an existing VST3 bundle-root junction.'
    Assert ((Get-Hash $bundleModule) -eq $bundleHashBefore) 'Existing bundle-root junction target was modified.'
    Remove-TestJunction $bundleJunction

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
