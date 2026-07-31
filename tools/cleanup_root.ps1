# =============================================================================
# cleanup_root.ps1 — Rangement de la racine du depot VoxelForge-Engine.
#
# Deplace les fichiers historiques (CHANGELOG_*, INSTALL_*, CHECKLIST_*,
# MANIFEST versionnes) vers docs\history\, met les .obj parasites de la
# racine dans _to_delete\, et supprime NUL.obj (nom reserve Windows).
#
# SANS PARAMETRE : simulation seule, rien n'est modifie.
# Avec -Apply    : execute reellement les actions listees.
#
# Utilise "git mv" pour les fichiers suivis par git (l'historique est
# conserve et le deplacement apparait dans "git status" — a toi de committer).
# Compatible Windows PowerShell 5.1. A lancer depuis n'importe ou :
#     powershell -ExecutionPolicy Bypass -File tools\cleanup_root.ps1
#     powershell -ExecutionPolicy Bypass -File tools\cleanup_root.ps1 -Apply
# =============================================================================
param(
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'

# Racine du depot = dossier parent de tools\
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$HistoryDir = Join-Path $Root 'docs\history'
$TrashDir   = Join-Path $Root '_to_delete'

if (-not $Apply) {
    Write-Host '=== MODE SIMULATION (rien ne sera modifie) ===' -ForegroundColor Yellow
    Write-Host 'Relance avec -Apply pour executer.' -ForegroundColor Yellow
    Write-Host ''
}

# --- 1. Fichiers historiques a deplacer vers docs\history -------------------
$historyPatterns = @(
    'CHANGELOG_*.md',
    'CHECKLIST_*.md',
    'INSTALL_*.md',
    'INSTALL.txt',
    'MANIFEST_*.json',
    'VF-*_MANIFEST.txt'
)
# Restent volontairement a la racine : README.md, LICENSE.md, CONTRIBUTING.md,
# AGENTS.md, CHANGELOG.md (courant), MANIFEST.json (courant).

$historyFiles = @()
foreach ($pattern in $historyPatterns) {
    $historyFiles += Get-ChildItem -Path $Root -File -Filter $pattern |
        Where-Object { $_.DirectoryName -eq $Root }
}
$historyFiles = $historyFiles | Sort-Object Name -Unique

# --- 2. Objets compiles parasites a la racine (ignores par git) -------------
$junkObj = Get-ChildItem -Path $Root -File -Filter '*.obj' |
    Where-Object { ($_.DirectoryName -eq $Root) -and ($_.Name -ne 'NUL.obj') }

# NUL est un nom reserve Windows : le fichier doit etre traite via \\?\
$nulPath   = "\\?\$Root\NUL.obj"
$nulExists = [System.IO.File]::Exists($nulPath)

# --- Bilan -------------------------------------------------------------------
Write-Host ("Fichiers historiques vers docs\history : {0}" -f $historyFiles.Count)
$historyFiles | ForEach-Object { Write-Host ("  -> {0}" -f $_.Name) }
Write-Host ("Objets .obj parasites vers _to_delete  : {0}" -f $junkObj.Count)
$junkObj | ForEach-Object { Write-Host ("  -> {0}" -f $_.Name) }
Write-Host ("NUL.obj present : {0}" -f $nulExists)
Write-Host ''

if (-not $Apply) {
    Write-Host 'Simulation terminee. Aucune modification effectuee.'
    exit 0
}

# --- Execution ---------------------------------------------------------------
if (-not (Test-Path $HistoryDir)) {
    New-Item -ItemType Directory -Path $HistoryDir | Out-Null
}

$gitAvailable = $null -ne (Get-Command git -ErrorAction SilentlyContinue)

foreach ($file in $historyFiles) {
    $tracked = $false
    if ($gitAvailable) {
        git ls-files --error-unmatch -- $file.Name *> $null
        $tracked = ($LASTEXITCODE -eq 0)
    }
    if ($tracked) {
        git mv -- $file.Name ("docs/history/" + $file.Name)
        Write-Host ("git mv  {0} -> docs\history\" -f $file.Name)
    }
    else {
        Move-Item -LiteralPath $file.FullName -Destination $HistoryDir
        Write-Host ("move    {0} -> docs\history\" -f $file.Name)
    }
}

if ($junkObj.Count -gt 0) {
    if (-not (Test-Path $TrashDir)) {
        New-Item -ItemType Directory -Path $TrashDir | Out-Null
    }
    foreach ($file in $junkObj) {
        Move-Item -LiteralPath $file.FullName -Destination $TrashDir
        Write-Host ("move    {0} -> _to_delete\" -f $file.Name)
    }
    Write-Host 'Verifie le contenu de _to_delete\ puis supprime le dossier.'
}

if ($nulExists) {
    [System.IO.File]::Delete($nulPath)
    Write-Host 'suppr   NUL.obj (nom reserve Windows, fichier compilateur parasite)'
}

Write-Host ''
Write-Host 'Termine. Controle avec "git status", puis committe si tout est bon.'
