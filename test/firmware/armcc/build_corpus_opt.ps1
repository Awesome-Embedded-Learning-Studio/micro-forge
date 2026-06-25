# build_corpus_opt.ps1
# ============================================================================
# Build the STM32F103RB-Nucleo firmware corpus at multiple AC6 optimization
# levels for the micro-forge E2 Tier-2 conformance gate.
#
# WHY: different -O levels emit different Thumb-2 instruction mixes (e.g. -O2
# may emit adc.w/sbc.w/sdiv that -O0 avoids). Running each variant through
# micro-forge and asserting boots-clean/no-fault is the cheapest trust
# multiplier beyond the single-level corpus (notes 017/018).
#
# >>> RUN ON WINDOWS (local NTFS), NOT UNDER WSL. <<<  Keil's .__i response
# files cannot be created on the 9p filesystem (\\wsl.localhost / mapped drive
# both fail). From WSL you can still read /mnt/d/mf afterward to copy the .axf
# back into the repo. See test/firmware/armcc/REGENERATE.md for the full recipe.
#
# Prereqs (per REGENERATE.md):
#   * Keil MDK + Arm Compiler 6 (armclang) + STM32F1xx_DFP 2.4.1 + CMSIS 6.x
#   * Pack repo at D:\MDK-Pack ; UV4 at D:\MDK\UV4\UV4.exe
#   * Minimal CubeF1 subtree already at $Root (D:\mf\STM32CubeF1) with the
#     ..\..\..\..\..\Drivers relative depth preserved (REGENERATE.md step 1).
#
# Usage:
#   pwsh build_corpus_opt.ps1                       # all examples x {O0,O2,Oz}
#   pwsh build_corpus_opt.ps1 -OptLevels O0,O2      # subset
#   pwsh build_corpus_opt.ps1 -DryRun               # patch+print, no UV4 build
#
# Output: $OutRoot\<example>.ac6-<opt>.axf  (default D:\mf\out)
#   Copy them into test/firmware/armcc\ from WSL:
#     cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/
# ============================================================================

[CmdletBinding()]
param(
    [string]   $Root      = "D:\mf\STM32CubeF1",
    [string]   $OutRoot   = "D:\mf\out",
    [string]   $Uv4       = "D:\MDK\UV4\UV4.exe",
    # CubeF1 Examples under Projects\STM32F103RB-Nucleo\Examples\<$Example>\MDK-ARM
    [string[]] $Examples  = @("GPIO/GPIO_IOToggle", "TIM/TIM_TimeBase", "UART/UART_Printf"),
    # AC6 (armclang) opt flags. NOTE: AC6 has NO -Os; its min-size flag is -Oz
    # (the AC5/old-armcc -Os maps to AC6 -Oz). "Os" in the request -> "Oz" here.
    [string[]] $OptLevels = @("O0", "O2", "Oz"),
    [switch]   $DryRun
)

$ErrorActionPreference = "Stop"

# Friendly name -> on-disk Example subdir -> output fixture stem.
$ExampleMap = @{
    "GPIO/GPIO_IOToggle" = "gpio_iotoggle"
    "TIM/TIM_TimeBase"   = "tim_timebase"
    "UART/UART_Printf"   = "uart_printf"
}

function Invoke-Uv4 {
    param([string]$Project, [string]$Log)
    # UV4 exit codes: 0 clean, 1 warnings-but-.axf-built, 2+ error. 0 and 1 are success.
    Start-Process -FilePath $Uv4 -ArgumentList "-b", $Project, "-o", $Log `
        -NoNewWindow -Wait -PassThru | Out-Null
    $code = $LASTEXITCODE
    return $code
}

# Apply the AC5->AC6 3-patch (REGENERATE.md) + inject the AC6 opt flag, writing
# a per-opt build copy so the pristine template is never mutated.
function Patch-Uvprojx {
    param([string]$Template, [string]$Dest, [string]$OptLevel)

    $xml = Get-Content -Raw -LiteralPath $Template

    # Patch 1: select AC6 — insert <uAC6>1</uAC6> right after <ToolsetName>ARM-ADS</ToolsetName>
    # (MUST be a direct child of <Target>, NOT inside <TargetCommonOption>).
    if ($xml -notmatch '<uAC6>') {
        $xml = $xml -replace '(<ToolsetName>ARM-ADS</ToolsetName>)', '$1' + "`r`n        <uAC6>1</uAC6>"
    }

    # Patch 2: drop AC5 --C99 from MiscControls (AC6 doesn't accept it; gnu11 default).
    $xml = $xml -replace '--C99\s*', ''

    # Patch 3: remove AC5 language/LTO fields so Keil doesn't force C90 on AC6.
    $xml = $xml -replace '\s*<v6Lang>[^<]*</v6Lang>', ''
    $xml = $xml -replace '\s*<v6LangP>[^<]*</v6LangP>', ''
    $xml = $xml -replace '\s*<v6Lto>[^<]*</v6Lto>', ''

    # Patch 4 (opt level): strip any existing -O flag in MiscControls, append the
    # requested one. armclang honors MiscControls -O0/-O1/-O2/-O3/-Oz directly.
    # If Keil's structured Optimization field overrides this in your MDK build,
    # also set it in the GUI (Options -> C/C++ -> Optimization) and report back.
    $optFlag = "-O$OptLevel"
    $xml = [regex]::Replace($xml, '\s-O[0-3zspaceptime]*', '')  # remove stale -O tokens
    $xml = $xml -replace '(<MiscControls>)([^<]*)(</MiscControls>)', "`$1`$2 $optFlag`$3"

    Set-Content -LiteralPath $Dest -Value $xml -NoNewline -Encoding UTF8
}

# ----------------------------------------------------------------------------
$projRel = "Projects\STM32F103RB-Nucleo\Examples"
$summary = @()

foreach ($ex in $Examples) {
    $stem  = $ExampleMap[$ex]
    $tmpl  = Join-Path $Root "$projRel\$ex\MDK-ARM\STM32F103RB_Nucleo.uvprojx"
    $workDir = Join-Path $Root "$projRel\$ex\MDK-ARM"

    if (-not (Test-Path -LiteralPath $tmpl)) {
        Write-Warning "SKIP $ex : template not found ($tmpl). Copy the Example subtree first (REGENERATE.md step 1)."
        continue
    }

    foreach ($opt in $OptLevels) {
        $projCopy = Join-Path $workDir "STM32F103RB_Nucleo.$opt.uvprojx"
        $log      = Join-Path $workDir "build.$opt.log"
        $fixture  = "$OutRoot\nucleo_f103rb_$stem.ac6-$opt.axf"
        Write-Host "`n=== $stem  -O$opt ===" -ForegroundColor Cyan

        Patch-Uvprojx -Template $tmpl -Dest $projCopy -OptLevel $opt
        Write-Host "patched -> $projCopy"

        if ($DryRun) { $summary += "$stem.$opt (dry-run)"; continue }

        $code = Invoke-Uv4 -Project $projCopy -Log $log
        $axf  = Join-Path $workDir "STM32F103RB_Nucleo.axf"
        if (($code -eq 0 -or $code -eq 1) -and (Test-Path -LiteralPath $axf)) {
            New-Item -ItemType Directory -Force -Path $OutRoot | Out-Null
            Copy-Item -LiteralPath $axf -Destination $fixture -Force
            Write-Host "OK (UV4=$code) -> $fixture" -ForegroundColor Green
            $summary += "$stem.$opt OK"
        } else {
            Write-Host "FAIL (UV4=$code); see $log" -ForegroundColor Red
            $summary += "$stem.$opt FAIL(UV4=$code)"
        }
    }
}

Write-Host "`n==== summary ===="
$summary | ForEach-Object { Write-Host "  $_" }
Write-Host "`nNext (from WSL):  cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/"
Write-Host "Then extend test/test_firmware_armcc.cpp with BootsClean cases per opt variant (notes 018)."
