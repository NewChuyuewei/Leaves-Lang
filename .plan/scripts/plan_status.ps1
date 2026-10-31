<#
.SYNOPSIS
    Leaves project plan status reporter
.USAGE
    cd .plan
    .\scripts\plan_status.ps1
#>

$ErrorActionPreference = "Stop"
$PlanRoot = Split-Path -Parent $PSScriptRoot

function Read-Utf8($path) {
    return [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
}

function Read-Utf8Lines($path) {
    return [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
}

function Write-Section($title) {
    Write-Host ""
    Write-Host "=== $title ===" -ForegroundColor Cyan
}

function Get-FrontmatterField($content, $field) {
    $pattern = "(?m)^$field\s*:\s*(.+)$"
    $match = [regex]::Match($content, $pattern)
    if ($match.Success) {
        return $match.Groups[1].Value.Trim()
    }
    return "?"
}

Write-Host "Leaves Project Plan Status" -ForegroundColor Green
Write-Host "Scanning: $PlanRoot"
Write-Host "Generated at: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

# ---------------------------------------------------------
# 1. Decisions summary
# ---------------------------------------------------------
Write-Section "Decisions (decisions/)"

$decisionsPath = Join-Path $PlanRoot "decisions"
$ddFiles = Get-ChildItem -Path $decisionsPath -Filter "DD-*.md" -ErrorAction SilentlyContinue | Sort-Object Name

if (-not $ddFiles) {
    Write-Host "  (no decision files found)" -ForegroundColor Yellow
} else {
    $statusCount = @{ Locked = 0; Provisional = 0; Superseded = 0; Rejected = 0 }

    foreach ($f in $ddFiles) {
        $content = Read-Utf8 $f.FullName
        $id = Get-FrontmatterField $content "id"
        $title = Get-FrontmatterField $content "title"
        $status = Get-FrontmatterField $content "status"

        $color = switch ($status) {
            "Locked"      { "Green" }
            "Provisional" { "Yellow" }
            "Superseded"  { "DarkGray" }
            "Rejected"    { "Red" }
            default       { "White" }
        }

        if ($statusCount.ContainsKey($status)) {
            $statusCount[$status]++
        }

        Write-Host ("  {0,-8} [{1,-11}] {2}" -f $id, $status, $title) -ForegroundColor $color
    }

    Write-Host ""
    Write-Host ("  Summary: Locked={0}  Provisional={1}  Superseded={2}  Rejected={3}" -f `
        $statusCount.Locked, $statusCount.Provisional, $statusCount.Superseded, $statusCount.Rejected)
}

# ---------------------------------------------------------
# 2. Open questions summary
# ---------------------------------------------------------
Write-Section "Open Questions (02-open-questions.md)"

$oqFile = Join-Path $PlanRoot "02-open-questions.md"
if (Test-Path $oqFile) {
    $content = Read-Utf8 $oqFile
    $oqMatches = [regex]::Matches($content, "(?m)^## (OQ-\d+)")
    $openCount = ([regex]::Matches($content, "\[OPEN\]")).Count
    $progressCount = ([regex]::Matches($content, "\[IN PROGRESS\]")).Count
    $resolvedCount = ([regex]::Matches($content, "\[RESOLVED\]")).Count
    $deferredCount = ([regex]::Matches($content, "\[DEFERRED\]")).Count

    Write-Host ("  Total: {0}" -f $oqMatches.Count)
    Write-Host ("  OPEN: {0}   IN PROGRESS: {1}   RESOLVED: {2}   DEFERRED: {3}" -f `
        $openCount, $progressCount, $resolvedCount, $deferredCount)
} else {
    Write-Host "  (02-open-questions.md not found)" -ForegroundColor Yellow
}

# ---------------------------------------------------------
# 3. Roadmap progress
# ---------------------------------------------------------
Write-Section "Roadmap Progress (03-roadmap.md)"

$roadmapFile = Join-Path $PlanRoot "03-roadmap.md"
if (Test-Path $roadmapFile) {
    $content = Read-Utf8Lines $roadmapFile

    # \uFF1A = fullwidth colon, written as pure ASCII escape to avoid file encoding issues
    $phasePattern = "^## Phase (\d+)[:\uFF1A]\s*(.+)$"
    $currentPhase = ""
    $phaseStats = [ordered]@{}

    foreach ($line in $content) {
        if ($line -match $phasePattern) {
            $currentPhase = "Phase $($Matches[1]): $($Matches[2])"
            if (-not $phaseStats.Contains($currentPhase)) {
                $phaseStats[$currentPhase] = @{ Total = 0; Done = 0 }
            }
        }
        if ($currentPhase -ne "" -and $line -match "^\s*-\s\[([ x])\]") {
            $phaseStats[$currentPhase].Total++
            if ($Matches[1] -eq "x") {
                $phaseStats[$currentPhase].Done++
            }
        }
    }

    $totalAll = 0
    $doneAll = 0
    foreach ($phase in $phaseStats.Keys) {
        $t = $phaseStats[$phase].Total
        $d = $phaseStats[$phase].Done
        $totalAll += $t
        $doneAll += $d
        $pct = if ($t -gt 0) { [math]::Round(($d / $t) * 100) } else { 0 }

        $barLength = 20
        $filled = [math]::Round($barLength * $pct / 100)
        $bar = ("#" * $filled) + ("-" * ($barLength - $filled))

        Write-Host ("  {0,-45} [{1}] {2,3}% ({3}/{4})" -f $phase, $bar, $pct, $d, $t)
    }

    Write-Host ""
    $overallPct = if ($totalAll -gt 0) { [math]::Round(($doneAll / $totalAll) * 100) } else { 0 }
    Write-Host ("  Overall: {0}/{1} ({2}%)" -f $doneAll, $totalAll, $overallPct) -ForegroundColor Green
} else {
    Write-Host "  (03-roadmap.md not found)" -ForegroundColor Yellow
}

# ---------------------------------------------------------
# 4. Risks summary
# ---------------------------------------------------------
Write-Section "Risks (04-risks.md)"

$risksFile = Join-Path $PlanRoot "04-risks.md"
if (Test-Path $risksFile) {
    $content = Read-Utf8 $risksFile
    $riskMatches = [regex]::Matches($content, "(?m)^## (R-\d+)[:\uFF1A](.+)$")

    foreach ($m in $riskMatches) {
        $riskId = $m.Groups[1].Value
        $riskTitle = $m.Groups[2].Value.Trim()
        Write-Host ("  {0}: {1}" -f $riskId, $riskTitle)
    }
} else {
    Write-Host "  (04-risks.md not found)" -ForegroundColor Yellow
}

# ---------------------------------------------------------
# 5. RFC status
# ---------------------------------------------------------
Write-Section "RFCs (rfcs/)"

$rfcsPath = Join-Path $PlanRoot "rfcs"
$rfcFiles = Get-ChildItem -Path $rfcsPath -Filter "[0-9]*.md" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne "0000-template.md" }

if (-not $rfcFiles) {
    Write-Host "  (no RFCs submitted yet, template only)" -ForegroundColor DarkGray
} else {
    foreach ($f in $rfcFiles) {
        $content = Read-Utf8 $f.FullName
        $title = Get-FrontmatterField $content "title"
        $status = Get-FrontmatterField $content "status"
        Write-Host ("  [{0}] {1}" -f $status, $title)
    }
}

Write-Host ""
Write-Host "=== Report End ===" -ForegroundColor Cyan
Write-Host ""