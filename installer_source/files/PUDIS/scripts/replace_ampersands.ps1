# ---------------------------------------------------------------------------
# PUDIS Utility: Ampersand Renamer (v1.2)
# Usage: .\replace_ampersands.ps1 <TargetDriveOrPath>
# ---------------------------------------------------------------------------

param (
    [Parameter(Mandatory=$false, Position=0)]
    [string]$TargetPath
)

# 1. Check if the user provided a parameter
if ([string]::IsNullOrWhiteSpace($TargetPath)) {
    Write-Host ""
    Write-Host "ERROR: Missing Target Path." -ForegroundColor Red
    Write-Host "--------------------------------------------------------------" -ForegroundColor Gray
    Write-Host "USAGE:" -ForegroundColor White
    Write-Host "  .\replace_ampersands.ps1 <DriveOrFolderPath>"
    Write-Host ""
    Write-Host "EXAMPLES:" -ForegroundColor White
    Write-Host "  .\replace_ampersands.ps1 J:\"
    Write-Host "  .\replace_ampersands.ps1 C:\MyWork\ProjectA"
    Write-Host "--------------------------------------------------------------" -ForegroundColor Gray
    exit
}

# 2. Check if the provided path actually exists
if (-not (Test-Path $TargetPath)) {
    Write-Error "The path '$TargetPath' does not exist."
    exit
}

Write-Host "Searching for items containing '&' in: $TargetPath" -ForegroundColor Cyan

# Get all files and folders containing '&'
# We sort by FullName Descending to ensure we fix files inside folders 
# BEFORE we rename the parent folder itself.
$items = Get-ChildItem -Path $TargetPath -Recurse | 
         Where-Object { $_.Name -like "*&*" } | 
         Sort-Object FullName -Descending

if ($null -eq $items -or $items.Count -eq 0) {
    Write-Host "No files or folders with '&' found." -ForegroundColor Green
    exit
}

Write-Host "Found $($items.Count) items to rename." -ForegroundColor Yellow
Write-Host "--------------------------------------------------------------"

foreach ($item in $items) {
    $newName = $item.Name -replace '&', 'amp'
    
    try {
        # Using -LiteralPath handles other special characters (like brackets) safely
        Write-Host "Renaming: $($item.Name) -> $newName"
        Rename-Item -LiteralPath $item.FullName -NewName $newName -ErrorAction Stop
    }
    catch {
        Write-Warning "Failed to rename $($item.FullName): $($_.Exception.Message)"
    }
}

Write-Host "--------------------------------------------------------------"
Write-Host "Renaming process complete!" -ForegroundColor Green