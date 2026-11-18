[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium', DefaultParameterSetName = 'Install')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')]
    [ValidateNotNullOrEmpty()]
    [string]$PackageRoot,

    [Parameter(Mandatory = $true, ParameterSetName = 'Rollback')]
    [ValidateNotNullOrEmpty()]
    [string]$RollbackFrom,

    [string]$Vst3Directory,
    [string]$BackupRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Product = 'Skell Separator'
$script:Vendor = 'Skell Audio'
$script:BundleName = 'Skell Separator.vst3'
$script:ModuleRelative = 'Contents\x86_64-win\Skell Separator.vst3'
$script:ModuleInfoRelative = 'Contents\Resources\moduleinfo.json'

if ([string]::IsNullOrWhiteSpace($Vst3Directory)) {
    if (-not [string]::IsNullOrWhiteSpace($env:CommonProgramFiles)) {
        $Vst3Directory = Join-Path $env:CommonProgramFiles 'VST3'
    }
    else {
        $Vst3Directory = 'C:\Program Files\Common Files\VST3'
    }
}
if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $BackupRoot = Join-Path $env:LOCALAPPDATA 'Skell\Backups\Separator'
    }
    else {
        $BackupRoot = Join-Path $env:TEMP 'Skell\Backups\Separator'
    }
}

function Get-FullPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'A required path is empty.' }
    [System.IO.Path]::GetFullPath($Path)
}

function Assert-NoReparsePathChain([string]$Path) {
    $cursor = Get-FullPath $Path
    while (-not [string]::IsNullOrWhiteSpace($cursor)) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse point in protected path: $cursor"
            }
        }
        $parent = [System.IO.Path]::GetDirectoryName($cursor)
        if ([string]::IsNullOrWhiteSpace($parent) -or
            [string]::Equals($parent, $cursor, [System.StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        $cursor = $parent
    }
}

function Assert-NoReparseTree([string]$Root) {
    $full = Get-FullPath $Root
    Assert-NoReparsePathChain $full
    if (-not (Test-Path -LiteralPath $full -PathType Container)) {
        throw "Directory not found: $full"
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $full -Recurse -Force -ErrorAction Stop)) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse point is not allowed: $($item.FullName)"
        }
    }
}

function Assert-NoDuplicateJsonProperties([string]$Source) {
    $stack = New-Object System.Collections.ArrayList
    $index = 0
    while ($index -lt $Source.Length) {
        $current = $Source[$index]
        if ($current -eq '"') {
            $start = $index
            $index++
            $escaped = $false
            $closed = $false
            while ($index -lt $Source.Length) {
                $character = $Source[$index]
                if ($escaped) {
                    $escaped = $false
                }
                elseif ($character -eq '\') {
                    $escaped = $true
                }
                elseif ($character -eq '"') {
                    $closed = $true
                    break
                }
                $index++
            }
            if (-not $closed) { throw 'JSON contains an unterminated string.' }
            $raw = $Source.Substring($start, $index - $start + 1)
            $after = $index + 1
            while ($after -lt $Source.Length -and [char]::IsWhiteSpace($Source[$after])) { $after++ }
            if ($after -lt $Source.Length -and $Source[$after] -eq ':' -and $stack.Count -gt 0) {
                $scope = $stack[$stack.Count - 1]
                if ($null -ne $scope) {
                    $key = [string](ConvertFrom-Json -InputObject $raw -ErrorAction Stop)
                    if (-not $scope.Add($key)) {
                        throw "JSON contains a duplicate or case-colliding property: $key"
                    }
                }
            }
            $index++
            continue
        }
        if ($current -eq '{') {
            $keys = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
            [void]$stack.Add($keys)
        }
        elseif ($current -eq '[') {
            [void]$stack.Add($null)
        }
        elseif ($current -eq '}' -or $current -eq ']') {
            if ($stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1) }
        }
        $index++
    }
}

function Assert-FiniteJsonNumbers([object]$Value) {
    if ($null -eq $Value) { return }
    if ($Value -is [double]) {
        if ([double]::IsNaN($Value) -or [double]::IsInfinity($Value)) {
            throw 'JSON contains a non-finite or overflowing number.'
        }
        return
    }
    if ($Value -is [single]) {
        if ([single]::IsNaN($Value) -or [single]::IsInfinity($Value)) {
            throw 'JSON contains a non-finite or overflowing number.'
        }
        return
    }
    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($key in $Value.Keys) { Assert-FiniteJsonNumbers $Value[$key] }
        return
    }
    if ($Value -is [System.Array]) {
        foreach ($entry in $Value) { Assert-FiniteJsonNumbers $entry }
        return
    }
    if ($Value -is [pscustomobject]) {
        foreach ($property in $Value.PSObject.Properties) {
            Assert-FiniteJsonNumbers $property.Value
        }
    }
}

function ConvertFrom-StrictJson([string]$Raw, [string]$Context) {
    if ([string]::IsNullOrWhiteSpace($Raw)) { throw "$Context is empty." }
    Assert-NoDuplicateJsonProperties $Raw
    $value = ConvertFrom-Json -InputObject $Raw -ErrorAction Stop
    Assert-FiniteJsonNumbers $value
    $value
}

function Get-FileSha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}

function Get-TreeSha256([string]$Root) {
    $full = Get-FullPath $Root
    Assert-NoReparseTree $full
    $files = @(Get-ChildItem -LiteralPath $full -Recurse -File -Force -ErrorAction Stop | Sort-Object FullName)
    if ($files.Count -eq 0) { throw "Cannot hash an empty bundle: $full" }
    $records = New-Object System.Collections.Generic.List[string]
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($full.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $records.Add($relative + '|' + (Get-FileSha256 $file.FullName))
    }
    $payload = [string]::Join([Environment]::NewLine, $records.ToArray())
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        [BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($payload))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Get-BundleEvidence([string]$BundlePath) {
    $bundle = Get-FullPath $BundlePath
    if ((Split-Path -Leaf $bundle) -ne $script:BundleName) {
        throw "Expected $($script:BundleName): $bundle"
    }
    Assert-NoReparseTree $bundle
    if (@(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.exe').Count -gt 0) {
        throw 'Standalone EXE files are not permitted in the VST3 bundle.'
    }
    if (@(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.zip').Count -gt 0) {
        throw 'Nested ZIP files are not permitted in the VST3 bundle.'
    }

    $infoPath = Join-Path $bundle $script:ModuleInfoRelative
    $binaryPath = Join-Path $bundle $script:ModuleRelative
    if (-not (Test-Path -LiteralPath $infoPath -PathType Leaf)) { throw 'VST3 moduleinfo.json is missing.' }
    if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) { throw 'Windows x64 VST3 module is missing.' }

    $modules = @(Get-ChildItem -LiteralPath $bundle -Recurse -File -Filter '*.vst3')
    if ($modules.Count -ne 1 -or
        -not [string]::Equals($modules[0].FullName, $binaryPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Expected exactly one module at $($script:ModuleRelative)."
    }

    $metadata = ConvertFrom-StrictJson (Get-Content -LiteralPath $infoPath -Raw -ErrorAction Stop) 'moduleinfo.json'
    if ($metadata.Name -ne $script:Product -or
        [string]$metadata.Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
        throw 'VST3 product name or semantic version is invalid.'
    }
    if ($metadata.'Factory Info'.Vendor -ne $script:Vendor) {
        throw 'VST3 vendor identity is not Skell Audio.'
    }
    $classes = @($metadata.Classes)
    if ($classes.Count -ne 2) { throw 'Expected exactly two VST3 classes.' }
    $processor = @($classes | Where-Object { $_.Category -eq 'Audio Module Class' })
    $controller = @($classes | Where-Object { $_.Category -eq 'Component Controller Class' })
    if ($processor.Count -ne 1 -or $controller.Count -ne 1) {
        throw 'Expected one audio processor class and one controller class.'
    }
    $classIds = @()
    foreach ($class in $classes) {
        if ($class.Name -ne $script:Product -or $class.Vendor -ne $script:Vendor -or
            [string]$class.Version -ne [string]$metadata.Version) {
            throw 'VST3 class identity does not match module metadata.'
        }
        $cid = [string]$class.CID
        if ($cid -notmatch '^[0-9A-Fa-f]{32}$') { throw "Invalid VST3 class ID: $cid" }
        $classIds += $cid.ToUpperInvariant()
    }
    if (@($classIds | Select-Object -Unique).Count -ne 2) { throw 'VST3 class IDs are not unique.' }

    $bytes = [System.IO.File]::ReadAllBytes($binaryPath)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw 'VST3 module is missing a valid DOS MZ header.'
    }
    [long]$peOffset = [BitConverter]::ToUInt32($bytes, 0x3C)
    if ($peOffset -lt 0x40 -or $peOffset + 26 -gt $bytes.Length) { throw 'Invalid PE header offset.' }
    if ([BitConverter]::ToUInt32($bytes, [int]$peOffset) -ne 0x00004550) { throw 'PE signature is missing.' }
    $machine = [BitConverter]::ToUInt16($bytes, [int]$peOffset + 4)
    $magic = [BitConverter]::ToUInt16($bytes, [int]$peOffset + 24)
    if ($machine -ne 0x8664 -or $magic -ne 0x020B) { throw 'VST3 module is not AMD64 PE32+.' }

    [pscustomobject]@{
        version = [string]$metadata.Version
        class_ids = @($classIds | Sort-Object)
        class_categories = @($classes | ForEach-Object { [string]$_.Category } | Sort-Object)
        binary_sha256 = Get-FileSha256 $binaryPath
        binary_size = [int64](Get-Item -LiteralPath $binaryPath).Length
        tree_sha256 = Get-TreeSha256 $bundle
    }
}

function Test-PackageChecksums([string]$Root) {
    $rootFull = Get-FullPath $Root
    $sumPath = Join-Path $rootFull 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $sumPath -PathType Leaf)) { throw 'Package is missing SHA256SUMS.txt.' }
    $rootPrefix = $rootFull.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
    $expected = @{}
    foreach ($line in @(Get-Content -LiteralPath $sumPath -ErrorAction Stop)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Malformed checksum entry: $line" }
        $relative = $Matches[2]
        if ($relative.Contains('\') -or $relative.Contains(':') -or
            [System.IO.Path]::IsPathRooted($relative) -or
            $relative -match '(^|/)\.\.(/|$)' -or $relative.StartsWith('/')) {
            throw "Unsafe checksum path: $relative"
        }
        $key = $relative.ToLowerInvariant()
        if ($expected.ContainsKey($key)) { throw "Duplicate checksum path: $relative" }
        $fullPath = [System.IO.Path]::GetFullPath((Join-Path $rootFull $relative.Replace('/', [System.IO.Path]::DirectorySeparatorChar)))
        if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Checksum path escapes package root: $relative"
        }
        $expected[$key] = $Matches[1].ToLowerInvariant()
    }

    $actualFiles = @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Force -ErrorAction Stop |
        Where-Object { -not [string]::Equals($_.FullName, $sumPath, [System.StringComparison]::OrdinalIgnoreCase) })
    $seen = @{}
    foreach ($file in $actualFiles) {
        $relative = $file.FullName.Substring($rootFull.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $key = $relative.ToLowerInvariant()
        if (-not $expected.ContainsKey($key)) { throw "File is missing from SHA256SUMS.txt: $relative" }
        if ((Get-FileSha256 $file.FullName) -ne $expected[$key]) { throw "Checksum mismatch: $relative" }
        $seen[$key] = $true
    }
    foreach ($key in $expected.Keys) {
        if (-not $seen.ContainsKey($key)) { throw "Checksum references a missing file: $key" }
    }
}

function Get-ValidatedPackage([string]$Root) {
    $rootFull = Get-FullPath $Root
    Assert-NoReparseTree $rootFull
    $bundles = @(Get-ChildItem -LiteralPath $rootFull -Directory -Filter '*.vst3')
    if ($bundles.Count -ne 1 -or $bundles[0].Name -ne $script:BundleName) {
        throw "Expected exactly one top-level $($script:BundleName) bundle."
    }
    if (@(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Filter '*.exe').Count -gt 0) {
        throw 'Standalone EXE files are not permitted in this VST3 preview package.'
    }
    if (@(Get-ChildItem -LiteralPath $rootFull -Recurse -File -Filter '*.zip').Count -gt 0) {
        throw 'Nested ZIP files are not permitted in this VST3 preview package.'
    }
    Test-PackageChecksums $rootFull

    $verificationPath = Join-Path $rootFull 'vst3-verification.json'
    $buildInfoPath = Join-Path $rootFull 'build-info.json'
    $previewPath = Join-Path $rootFull 'PREVIEW-ONLY.txt'
    foreach ($required in @($verificationPath, $buildInfoPath, $previewPath)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing release sidecar: $required" }
    }
    $verification = ConvertFrom-StrictJson (Get-Content -LiteralPath $verificationPath -Raw) 'vst3-verification.json'
    $buildInfo = ConvertFrom-StrictJson (Get-Content -LiteralPath $buildInfoPath -Raw) 'build-info.json'
    $previewText = Get-Content -LiteralPath $previewPath -Raw
    if (-not $previewText.Contains('Not an approved public or commercial release.')) {
        throw 'Package is missing the private-preview warning.'
    }

    $bundle = Join-Path $rootFull $script:BundleName
    $evidence = Get-BundleEvidence $bundle
    if ($verification.product -ne $script:Product -or $verification.vendor -ne $script:Vendor -or
        $verification.format -ne 'VST3' -or $verification.platform -ne 'Windows x86_64' -or
        $verification.bundle -ne $script:BundleName -or
        $verification.module -ne ($script:BundleName + '/' + $script:ModuleRelative.Replace('\', '/')) -or
        $verification.version -ne $evidence.version -or
        $verification.module_sha256 -ne $evidence.binary_sha256 -or
        [int64]$verification.module_bytes -ne $evidence.binary_size -or
        $verification.moduleinfo_normalized -ne $true -or
        $verification.release_status -ne 'private preview only; host acceptance not implied') {
        throw 'VST3 verification sidecar does not match the package contents.'
    }
    $manifestIds = @($verification.class_ids | ForEach-Object { ([string]$_).ToUpperInvariant() } | Sort-Object)
    $manifestCategories = @($verification.class_categories | ForEach-Object { [string]$_ } | Sort-Object)
    if ((Compare-Object -ReferenceObject @($evidence.class_ids) -DifferenceObject $manifestIds).Count -gt 0 -or
        (Compare-Object -ReferenceObject @($evidence.class_categories) -DifferenceObject $manifestCategories).Count -gt 0) {
        throw 'VST3 verification class identity does not match moduleinfo.json.'
    }
    if ([string]$verification.source_head_sha -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$verification.tested_commit -notmatch '^[0-9a-fA-F]{40}$' -or
        [string]$verification.workflow_run_id -notmatch '^\d+$') {
        throw 'VST3 verification provenance is incomplete.'
    }
    if ($buildInfo.product -ne $script:Product -or $buildInfo.vendor -ne $script:Vendor -or
        $buildInfo.version -ne $evidence.version -or $buildInfo.format -ne 'VST3' -or
        $buildInfo.platform -ne 'Windows x86_64' -or
        $buildInfo.source_head_sha -ne $verification.source_head_sha -or
        $buildInfo.tested_commit -ne $verification.tested_commit -or
        [string]$buildInfo.workflow_run_id -ne [string]$verification.workflow_run_id) {
        throw 'Build provenance does not match the VST3 verification sidecar.'
    }

    [pscustomobject]@{ root = $rootFull; bundle = $bundle; evidence = $evidence }
}

function New-VerifiedBackup([string]$BundlePath, [string]$Reason, [string]$Target) {
    Assert-NoReparsePathChain $BackupRoot
    New-Item -ItemType Directory -Path $BackupRoot -Force | Out-Null
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $token = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $backupPath = Join-Path $BackupRoot ('SkellSeparator-' + $stamp + '-' + $token)
    New-Item -ItemType Directory -Path $backupPath -ErrorAction Stop | Out-Null
    $backupBundle = Join-Path $backupPath $script:BundleName
    $before = Get-BundleEvidence $BundlePath
    Copy-Item -LiteralPath $BundlePath -Destination $backupBundle -Recurse -ErrorAction Stop
    $after = Get-BundleEvidence $backupBundle
    if ($after.tree_sha256 -ne $before.tree_sha256) {
        throw "Backup verification failed; original installation was not changed. Backup retained at $backupPath"
    }
    $manifest = [ordered]@{
        schema_version = 1
        product = $script:Product
        created_utc = [DateTime]::UtcNow.ToString('o')
        version = $after.version
        binary_sha256 = $after.binary_sha256
        bundle_tree_sha256 = $after.tree_sha256
        original_target = $Target
        reason = $Reason
    }
    $json = $manifest | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $backupPath 'backup-manifest.json'),
        $json + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
    $backupPath
}

function Get-ValidatedBackup([string]$BackupPath) {
    $root = Get-FullPath $BackupPath
    Assert-NoReparseTree $root
    $manifestPath = Join-Path $root 'backup-manifest.json'
    $bundlePath = Join-Path $root $script:BundleName
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Backup manifest is missing.' }
    $bundles = @(Get-ChildItem -LiteralPath $root -Directory -Filter '*.vst3')
    if ($bundles.Count -ne 1 -or $bundles[0].Name -ne $script:BundleName) { throw 'Backup must contain exactly one Separator VST3 bundle.' }
    $manifest = ConvertFrom-StrictJson (Get-Content -LiteralPath $manifestPath -Raw) 'backup-manifest.json'
    $evidence = Get-BundleEvidence $bundlePath
    if ($manifest.schema_version -ne 1 -or $manifest.product -ne $script:Product -or
        $manifest.version -ne $evidence.version -or
        $manifest.binary_sha256 -ne $evidence.binary_sha256 -or
        $manifest.bundle_tree_sha256 -ne $evidence.tree_sha256) {
        throw 'Backup manifest does not match the backup bundle.'
    }
    [pscustomobject]@{ root = $root; bundle = $bundlePath; evidence = $evidence }
}

function Invoke-Install([System.Management.Automation.PSCmdlet]$Cmdlet) {
    $package = Get-ValidatedPackage $PackageRoot
    $targetRoot = Get-FullPath $Vst3Directory
    Assert-NoReparsePathChain $targetRoot
    $target = Join-Path $targetRoot $script:BundleName
    $current = $null
    if (Test-Path -LiteralPath $target) {
        $targetItem = Get-Item -LiteralPath $target -Force
        if (-not $targetItem.PSIsContainer) { throw "Refusing to overwrite a non-directory target: $target" }
        $current = Get-BundleEvidence $target
        if ($current.tree_sha256 -eq $package.evidence.tree_sha256) {
            return [pscustomobject]@{ status = 'already-installed'; version = $current.version; target = $target; backup_path = $null; binary_sha256 = $current.binary_sha256 }
        }
    }
    if (-not $Cmdlet.ShouldProcess($target, ('Install private Skell Separator VST3 ' + $package.evidence.version))) {
        return [pscustomobject]@{ status = 'what-if'; version = $package.evidence.version; target = $target; backup_path = $null; binary_sha256 = $package.evidence.binary_sha256 }
    }

    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
    $backupPath = $null
    if ($null -ne $current) {
        $backupPath = New-VerifiedBackup $target 'pre-install-or-update' $target
    }
    $token = [Guid]::NewGuid().ToString('N')
    $stageRoot = Join-Path $targetRoot ('.SkellSeparator-stage-' + $token)
    $displaced = Join-Path $targetRoot ('.SkellSeparator-previous-' + $token)
    $targetMoved = $false
    $newTargetMoved = $false
    try {
        New-Item -ItemType Directory -Path $stageRoot -ErrorAction Stop | Out-Null
        Copy-Item -LiteralPath $package.bundle -Destination $stageRoot -Recurse -ErrorAction Stop
        $stagedBundle = Join-Path $stageRoot $script:BundleName
        $staged = Get-BundleEvidence $stagedBundle
        if ($staged.tree_sha256 -ne $package.evidence.tree_sha256) { throw 'Staged VST3 differs from the verified package.' }
        if ($null -ne $current) {
            Move-Item -LiteralPath $target -Destination $displaced -ErrorAction Stop
            $targetMoved = $true
        }
        Move-Item -LiteralPath $stagedBundle -Destination $target -ErrorAction Stop
        $newTargetMoved = $true
        $installed = Get-BundleEvidence $target
        if ($installed.tree_sha256 -ne $package.evidence.tree_sha256) { throw 'Installed VST3 failed post-install verification.' }
        if ($targetMoved -and (Test-Path -LiteralPath $displaced)) {
            try { Remove-Item -LiteralPath $displaced -Recurse -Force -ErrorAction Stop }
            catch { Write-Warning "Verified persistent backup exists; temporary displaced bundle was retained at $displaced" }
        }
    }
    catch {
        if ($newTargetMoved -and (Test-Path -LiteralPath $target)) {
            $failed = Join-Path $targetRoot ('.SkellSeparator-install-failed-' + $token)
            try { Move-Item -LiteralPath $target -Destination $failed -ErrorAction Stop }
            catch { Write-Warning "Could not move failed candidate aside: $target" }
        }
        if ($targetMoved -and (Test-Path -LiteralPath $displaced) -and -not (Test-Path -LiteralPath $target)) {
            try { Move-Item -LiteralPath $displaced -Destination $target -ErrorAction Stop }
            catch { Write-Warning "CRITICAL: automatic restoration failed; previous bundle remains at $displaced" }
        }
        throw
    }
    finally {
        if (Test-Path -LiteralPath $stageRoot) {
            try {
                $stageItem = Get-Item -LiteralPath $stageRoot -Force
                if (($stageItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
                    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction Stop
                }
            }
            catch { Write-Warning "Temporary staging directory was retained: $stageRoot" }
        }
    }
    [pscustomobject]@{ status = 'installed'; version = $installed.version; target = $target; backup_path = $backupPath; binary_sha256 = $installed.binary_sha256 }
}

function Invoke-Rollback([System.Management.Automation.PSCmdlet]$Cmdlet) {
    $backup = Get-ValidatedBackup $RollbackFrom
    $targetRoot = Get-FullPath $Vst3Directory
    Assert-NoReparsePathChain $targetRoot
    $target = Join-Path $targetRoot $script:BundleName
    $current = $null
    if (Test-Path -LiteralPath $target) {
        $targetItem = Get-Item -LiteralPath $target -Force
        if (-not $targetItem.PSIsContainer) { throw "Refusing to overwrite a non-directory target: $target" }
        $current = Get-BundleEvidence $target
        if ($current.tree_sha256 -eq $backup.evidence.tree_sha256) {
            return [pscustomobject]@{ status = 'already-restored'; version = $current.version; target = $target; backup_path = $null; binary_sha256 = $current.binary_sha256 }
        }
    }
    if (-not $Cmdlet.ShouldProcess($target, ('Restore private Skell Separator VST3 ' + $backup.evidence.version))) {
        return [pscustomobject]@{ status = 'what-if'; version = $backup.evidence.version; target = $target; backup_path = $null; binary_sha256 = $backup.evidence.binary_sha256 }
    }

    New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
    $currentBackup = $null
    if ($null -ne $current) {
        $currentBackup = New-VerifiedBackup $target 'pre-rollback' $target
    }
    $token = [Guid]::NewGuid().ToString('N')
    $stageRoot = Join-Path $targetRoot ('.SkellSeparator-rollback-stage-' + $token)
    $displaced = Join-Path $targetRoot ('.SkellSeparator-rollback-previous-' + $token)
    $targetMoved = $false
    $newTargetMoved = $false
    try {
        New-Item -ItemType Directory -Path $stageRoot -ErrorAction Stop | Out-Null
        Copy-Item -LiteralPath $backup.bundle -Destination $stageRoot -Recurse -ErrorAction Stop
        $stagedBundle = Join-Path $stageRoot $script:BundleName
        $staged = Get-BundleEvidence $stagedBundle
        if ($staged.tree_sha256 -ne $backup.evidence.tree_sha256) { throw 'Staged rollback bundle differs from verified backup.' }
        if ($null -ne $current) {
            Move-Item -LiteralPath $target -Destination $displaced -ErrorAction Stop
            $targetMoved = $true
        }
        Move-Item -LiteralPath $stagedBundle -Destination $target -ErrorAction Stop
        $newTargetMoved = $true
        $restored = Get-BundleEvidence $target
        if ($restored.tree_sha256 -ne $backup.evidence.tree_sha256) { throw 'Restored VST3 failed post-rollback verification.' }
        if ($targetMoved -and (Test-Path -LiteralPath $displaced)) {
            try { Remove-Item -LiteralPath $displaced -Recurse -Force -ErrorAction Stop }
            catch { Write-Warning "Verified pre-rollback backup exists; temporary displaced bundle was retained at $displaced" }
        }
    }
    catch {
        if ($newTargetMoved -and (Test-Path -LiteralPath $target)) {
            $failed = Join-Path $targetRoot ('.SkellSeparator-rollback-failed-' + $token)
            try { Move-Item -LiteralPath $target -Destination $failed -ErrorAction Stop }
            catch { Write-Warning "Could not move failed rollback candidate aside: $target" }
        }
        if ($targetMoved -and (Test-Path -LiteralPath $displaced) -and -not (Test-Path -LiteralPath $target)) {
            try { Move-Item -LiteralPath $displaced -Destination $target -ErrorAction Stop }
            catch { Write-Warning "CRITICAL: automatic restoration failed; previous bundle remains at $displaced" }
        }
        throw
    }
    finally {
        if (Test-Path -LiteralPath $stageRoot) {
            try {
                $stageItem = Get-Item -LiteralPath $stageRoot -Force
                if (($stageItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
                    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction Stop
                }
            }
            catch { Write-Warning "Temporary rollback staging directory was retained: $stageRoot" }
        }
    }
    [pscustomobject]@{ status = 'rolled-back'; version = $restored.version; target = $target; backup_path = $currentBackup; binary_sha256 = $restored.binary_sha256 }
}

if ($env:OS -ne 'Windows_NT') { throw 'Skell Separator VST3 installation is supported only on Windows.' }
if ($PSCmdlet.ParameterSetName -eq 'Install') {
    Invoke-Install $PSCmdlet
}
else {
    Invoke-Rollback $PSCmdlet
}
