[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FormalRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$resolvedRoot = (Resolve-Path -LiteralPath $FormalRoot).Path.TrimEnd('\')
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

$manifestPath = Join-Path $resolvedOutput 'AILOD_Phase8_Formal_Archive_SHA256_v1.0.csv'
$summaryPath = Join-Path $resolvedOutput 'AILOD_Phase8_Formal_Archive_SHA256_Summary_v1.0.json'
$rootPrefix = $resolvedRoot + '\'
$files = @(Get-ChildItem -LiteralPath $resolvedRoot -File -Recurse | Sort-Object FullName)
$rows = [System.Collections.Generic.List[object]]::new()

for ($index = 0; $index -lt $files.Count; $index++) {
    $file = $files[$index]
    $relativePath = $file.FullName.Substring($rootPrefix.Length).Replace('\', '/')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
    $rows.Add([pscustomobject]@{
        RelativePath = $relativePath
        Bytes = $file.Length
        LastWriteTimeUtc = $file.LastWriteTimeUtc.ToString('o')
        SHA256 = $hash
    })
    if ((($index + 1) % 100) -eq 0 -or ($index + 1) -eq $files.Count) {
        Write-Progress -Activity 'Hashing Phase 8 formal archive' -Status "$($index + 1) / $($files.Count) files" -PercentComplete ((($index + 1) * 100.0) / $files.Count)
    }
}

$rows | Export-Csv -LiteralPath $manifestPath -NoTypeInformation -Encoding UTF8

$treeLines = $rows | ForEach-Object { "$($_.SHA256) $($_.Bytes) $($_.RelativePath)" }
$treeBytes = [System.Text.Encoding]::UTF8.GetBytes(($treeLines -join "`n"))
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $treeDigest = ([System.BitConverter]::ToString($sha256.ComputeHash($treeBytes))).Replace('-', '')
}
finally {
    $sha256.Dispose()
}

$summary = [ordered]@{
    SchemaVersion = '1.0'
    FormalRoot = $resolvedRoot
    FileCount = $files.Count
    TotalBytes = ($files | Measure-Object -Property Length -Sum).Sum
    TreeDigestAlgorithm = 'SHA256 of UTF-8 lines: <file sha256> <bytes> <relative path>, sorted by full path'
    TreeDigest = $treeDigest
    ManifestPath = $manifestPath
    ManifestSHA256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash
    GeneratedAtUtc = [DateTime]::UtcNow.ToString('o')
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

$summary | ConvertTo-Json -Depth 4
