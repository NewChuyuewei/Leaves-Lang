<#
.SYNOPSIS
    Push to all configured Git remotes in one command.
.EXAMPLE
    .\scripts\push_all.ps1
    .\scripts\push_all.ps1 -DryRun
    .\scripts\push_all.ps1 -Tags
    .\scripts\push_all.ps1 -Remote github
#>

param(
    [switch]$All,
    [switch]$Tags,
    [switch]$Force,
    [switch]$DryRun,
    [string]$Remote,
    [switch]$SetUpstream
)

$ErrorActionPreference = "Continue"

function Write-Banner($text) {
    $line = "=" * 60
    Write-Host ""
    Write-Host $line -ForegroundColor DarkGray
    Write-Host "  $text" -ForegroundColor Cyan
    Write-Host $line -ForegroundColor DarkGray
}

function Write-Result($remoteName, $success, $detail) {
    if ($success) {
        Write-Host "  [OK]   $remoteName" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $remoteName" -ForegroundColor Red
        if ($detail) {
            Write-Host "         $detail" -ForegroundColor DarkRed
        }
    }
}

Write-Banner "Leaves - Push to All Remotes"

$gitDir = git rev-parse --git-dir 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "  ERROR: Not inside a Git repository." -ForegroundColor Red
    exit 1
}

$currentBranch = git branch --show-current 2>&1
if ([string]::IsNullOrWhiteSpace($currentBranch)) {
    Write-Host "  WARNING: Detached HEAD state." -ForegroundColor Yellow
    $currentBranch = "HEAD"
}

$statusOutput = git status --porcelain 2>&1
if ($statusOutput -and -not $DryRun) {
    $changedCount = ($statusOutput | Measure-Object -Line).Lines
    Write-Host "  WARNING: $changedCount uncommitted change(s) detected." -ForegroundColor Yellow
    Write-Host "  These will NOT be included in the push." -ForegroundColor Yellow
    Write-Host ""
}

$allRemotes = git remote 2>&1
if ($LASTEXITCODE -ne 0 -or -not $allRemotes) {
    Write-Host "  ERROR: No remotes configured." -ForegroundColor Red
    exit 1
}

if ($Remote) {
    if ($allRemotes -notcontains $Remote) {
        Write-Host "  ERROR: Remote '$Remote' not found." -ForegroundColor Red
        Write-Host "  Available remotes: $($allRemotes -join ', ')" -ForegroundColor Yellow
        exit 1
    }
    $targets = @($Remote)
} else {
    $targets = @($allRemotes)
}

Write-Host ""
Write-Host "  Branch : $currentBranch"
Write-Host "  Remotes: $($targets.Count) target(s)"
foreach ($r in $targets) {
    $url = git remote get-url $r 2>&1
    Write-Host "    - $r -> $url"
}
Write-Host ""

if ($DryRun) {
    Write-Host "  [DRY RUN] No actual push will be performed." -ForegroundColor Yellow
    Write-Host ""
}

$pushArgs = @("push")

if ($Force) {
    $pushArgs += "--force-with-lease"
    Write-Host "  WARNING: Force push enabled (--force-with-lease)." -ForegroundColor Red
    Write-Host ""
}

if ($Tags) {
    $pushArgs += "--tags"
}

if ($SetUpstream) {
    $pushArgs += "--set-upstream"
}

$successCount = 0
$failCount = 0
$results = @()

foreach ($r in $targets) {
    Write-Host "  Pushing to '$r'..." -ForegroundColor DarkGray

    if ($All) {
        $cmdArgs = $pushArgs + @($r, "--all")
    } else {
        $cmdArgs = $pushArgs + @($r, $currentBranch)
    }

    if ($DryRun) {
        $cmdLine = "git " + ($cmdArgs -join " ")
        Write-Host "    [DRY RUN] $cmdLine" -ForegroundColor Yellow
        $successCount++
        $results += @{ Name = $r; OK = $true; Detail = "dry run" }
        continue
    }

    $output = & git @cmdArgs 2>&1
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        $successCount++
        $results += @{ Name = $r; OK = $true; Detail = "" }
        Write-Result $r $true
    } else {
        $failCount++
        $errMsg = ($output | Out-String).Trim()
        $errLines = $errMsg -split "`n" | Where-Object { $_.Trim() -ne "" }
        $shortErr = if ($errLines.Count -gt 0) { $errLines[-1].Trim() } else { "unknown error" }
        $results += @{ Name = $r; OK = $false; Detail = $shortErr }
        Write-Result $r $false $shortErr
    }
}

Write-Banner "Summary"

foreach ($res in $results) {
    Write-Result $res.Name $res.OK $res.Detail
}

Write-Host ""
$total = $successCount + $failCount
if ($failCount -eq 0) {
    Write-Host "  All $total remote(s) pushed successfully." -ForegroundColor Green
} else {
    Write-Host "  $successCount succeeded, $failCount failed out of $total remote(s)." -ForegroundColor Red
}

if ($DryRun) {
    Write-Host "  (dry run - nothing was actually pushed)" -ForegroundColor Yellow
}

Write-Host ""
exit $failCount