param(
    [string]$RootDir = $PSScriptRoot,
    [string]$OutputCsv = "benchmark_results.csv",
    [switch]$FlushCache = $true,
    [int]$WarmupRuns = 0,
    [int]$BenchmarkRuns = 3
)

# --- Paths relative to this script ---
$DATA_FILE = Join-Path $RootDir "data\1brc.txt"
$VALIDATION_FILE = Join-Path $RootDir "data\validation.txt"
$OUTPUT_CSV = Join-Path $RootDir $OutputCsv

# --- List of programs to benchmark ---
$PROGRAMS = @(
    "solutions\markusaksli_fast_threaded\x64\Release\markusaksli_fast_threaded.exe",
    "solutions\markusaksli_default_threaded\x64\Release\markusaksli_default_threaded.exe",
    "solutions\markusaksli_fast\x64\Release\markusaksli_fast.exe",
    "solutions\markusaksli_default\x64\Release\markusaksli_default.exe",
    "solutions\naive_plus\x64\Release\naive_plus.exe",
    "solutions\naive\x64\Release\naive.exe"
)

function Test-Admin {
    return [Security.Principal.WindowsPrincipal]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# -------------------------
# Elevation block
# If user requested FlushCache but we're not elevated, re-launch with elevation
# -------------------------
if ($FlushCache -and -not (Test-Admin)) {
    Write-Host "FlushCache requested but script is not running as Administrator." -ForegroundColor Yellow
    Write-Host "Requesting elevation (UAC) to allow running sync.exe for cache flushing..." -ForegroundColor Yellow

    # Build argument string from the bound parameters so we re-run with same parameters
    $argParts = @()

    foreach ($key in $PSBoundParameters.Keys) {
        $val = $PSBoundParameters[$key]

        if ($val -is [System.Management.Automation.SwitchParameter]) {
            if ($val) {
                $argParts += "-$key"
            }
        }
        elseif ($val -is [System.Array]) {
            # join array values with commas and quote
            $joined = ($val -join ",")
            $escaped = $joined -replace '"','\"'
            $argParts += "-$key `"$escaped`""
        }
        else {
            if ($null -ne $val) {
                $escaped = $val.ToString() -replace '"','\"'
            } else {
                $escaped = ""
            }
            $argParts += "-$key `"$escaped`""
        }
    }

    $argString = $argParts -join " "

    # Use $PSCommandPath to get the current script path (works when script is run from file)
    $scriptPath = if ($PSCommandPath) { $PSCommandPath } else { $MyInvocation.MyCommand.Definition }

    try {
        Start-Process -FilePath "powershell.exe" -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`" $argString" -Verb RunAs -WindowStyle Normal
        # Exit the non-elevated process; the elevated one will continue
        exit
    } catch {
        Write-Warning "Elevation was not granted or failed. Continuing without full cache-flush capability."
        # Continue running non-elevated; the script will warn about limited flushing below
    }
}

# --- Validate required files exist ---
if (-not (Test-Path $DATA_FILE)) {
    Write-Error "Data file not found: $DATA_FILE"
    exit 1
}

if (-not (Test-Path $VALIDATION_FILE)) {
    Write-Error "Validation file not found: $VALIDATION_FILE"
    exit 1
}

# --- Check admin rights for cache clearing (informational, but elevation already attempted above) ---
if ($FlushCache -and -not (Test-Admin)) {
    Write-Warning "Not running as administrator. Cache flushing will be limited."
    Write-Warning "For best results, re-run PowerShell as Administrator or allow the UAC prompt when requested."
}

# --- Initialize results CSV ---
"Program,Average_s,Best_s,Worst_s,Cold_s,Runs" | Out-File -FilePath $OUTPUT_CSV -Encoding UTF8

$results = @()

foreach ($program in $PROGRAMS) {
    $EXE_PATH = Join-Path $RootDir $program
    $programName = Split-Path $program -Leaf
    
    Write-Host "`nBenchmarking $programName..." -ForegroundColor Cyan
    
    if (-not (Test-Path $EXE_PATH)) {
        Write-Warning "Program not found: $EXE_PATH"
        continue
    }
    
    # Warmup runs (optional)
    for ($i = 0; $i -lt $WarmupRuns; $i++) {
        Write-Host "  Warmup run $($i + 1)/$WarmupRuns..." -ForegroundColor Gray
        $tempFile = [System.IO.Path]::GetTempFileName()
        $process = Start-Process -FilePath $EXE_PATH -ArgumentList $DATA_FILE -PassThru -NoNewWindow -Wait -RedirectStandardOutput $tempFile
        Remove-Item $tempFile -ErrorAction SilentlyContinue
    }
    	
	if ($FlushCache) {
		& sync.exe
		Write-Host ""
	}
	
    # Benchmark runs
    $runTimes = @()
    
    for ($run = 1; $run -le $BenchmarkRuns; $run++) {
		Write-Host "  Wait 10 seconds..."
		Start-Sleep -Seconds 10
        Write-Host "  Benchmark run $run/$BenchmarkRuns..." -ForegroundColor White
        
        try {
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            
            # Use temporary file to capture output with proper encoding
            $tempFile = [System.IO.Path]::GetTempFileName()
            $process = Start-Process -FilePath $EXE_PATH -ArgumentList $DATA_FILE -PassThru -NoNewWindow -Wait -RedirectStandardOutput $tempFile
            $output = [System.IO.File]::ReadAllText($tempFile, [System.Text.Encoding]::UTF8)
            
            $sw.Stop()
            $time = $sw.ElapsedMilliseconds
            
            # Read validation file
            $validationContent = [System.IO.File]::ReadAllText($VALIDATION_FILE, [System.Text.Encoding]::UTF8)
            
            # Trim both and compare
            $validationNormalized = $validationContent.Trim()
            $outputNormalized = $output.Trim()
            
            $valid = $validationNormalized -eq $outputNormalized
            
            $runTimes += $time
            
            Write-Host "    Time: $time ms, Valid: $valid" -ForegroundColor $(if ($valid) { "Green" } else { "Red" })
            
            # Clean up
            Remove-Item $tempFile -ErrorAction SilentlyContinue
        }
        catch {
            Write-Error "Error running $programName on run $run : $($_.Exception.Message)"
        }
    }
    
    # Calculate statistics
    if ($runTimes.Count -gt 0) {
		$avgTimeMs = ($runTimes | Measure-Object -Average).Average
		$minTimeMs = ($runTimes | Measure-Object -Minimum).Minimum
		$maxTimeMs = ($runTimes | Measure-Object -Maximum).Maximum
		$coldMs = @($runTimes)[0]

		# Convert to seconds and round
		$avgSec = [math]::Round($avgTimeMs / 1000, 2)
		$minSec = [math]::Round($minTimeMs / 1000, 2)
		$maxSec = [math]::Round($maxTimeMs / 1000, 2)
		$coldSec = [math]::Round($coldMs / 1000, 2)

		Write-Host "  Results for $($programName):" -ForegroundColor Cyan
		Write-Host ("    Average: {0:F2} s" -f $avgSec) -ForegroundColor Green
		Write-Host ("    Best:    {0:F2} s" -f $minSec) -ForegroundColor Green
		Write-Host ("    Worst:   {0:F2} s" -f $maxSec) -ForegroundColor Yellow
		Write-Host ("    Cold:    {0:F2} s" -f $coldSec) -ForegroundColor Green
		Write-Host "    Runs:    $($runTimes.Count)" -ForegroundColor Gray
        
		$results += [PSCustomObject]@{
			Program = $programName
			Cold = $coldSec
			AverageTime = $avgSec
			BestTime = $minSec
			WorstTime = $maxSec
			Runs = $runTimes.Count
		}
		
        "$programName,$avgSec,$minSec,$maxSec,$coldSec,$($runTimes.Count)" | Add-Content -Path $OUTPUT_CSV -Encoding UTF8
    }
}

# --- Display summary ---
Write-Host "`nBenchmark Summary:" -ForegroundColor Yellow
$results | Format-Table -AutoSize

Write-Host "Detailed results saved in: $OUTPUT_CSV" -ForegroundColor Green

if ($Host.UI.RawUI) {
    Write-Host "`nPress any key to continue..." -ForegroundColor Gray
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}