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
# >>> RUN ON WINDOWS (local NTFS) via pwsh. <<<  Keil's .__i response files
# cannot be created on the 9p filesystem, so the BUILD must be native Windows.
# (Re)build the source subtree first from WSL:  bash scripts/prepare_corpus.sh
#
# Prereqs (REGENERATE.md): Keil MDK + AC6 (armclang) + STM32F1xx_DFP 2.4.1 +
# CMSIS 6.x pack; UV4 at D:\MDK\UV4\UV4.exe. Source tree at D:\mf\STM32CubeF1
# with Drivers/ and Projects/ at the top (6-level include depth).
#
# Usage:
#   pwsh build_corpus_opt.ps1                       # all examples x {O0,O2,Oz}
#   pwsh build_corpus_opt.ps1 -OptLevels O0,O2      # subset
#   pwsh build_corpus_opt.ps1 -DryRun               # patch+print, no UV4 build
#
# Output: $OutRoot\nucleo_f103rb_<example>.ac6-<opt>.axf  (then copy into
# test/firmware/armcc\ from WSL: cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/)
#
# NOTE — UNTESTED on Windows from the WSL side. Verify in Keil GUI that the
# -O<opt> flag lands in C/C++ (AC6) -> Optimization / Controls; if Keil's
# structured Optimization field overrides MiscControls, set it there too.
# ============================================================================

[CmdletBinding()]
param(
    [string]   $Root      = "D:\mf\STM32CubeF1",
    [string]   $OutRoot   = "D:\mf\out",
    [string]   $Uv4       = "D:\MDK\UV4\UV4.exe",
    # Project file is Project.uvprojx in CubeF1; the TARGET/OUTPUT name inside
    # is STM32F103RB_Nucleo (-> output ...\STM32F103RB_Nucleo\STM32F103RB_Nucleo.axf).
    # CubeF1 Examples under Projects\STM32F103RB-Nucleo\Examples\<$Example>\MDK-ARM
    [string[]] $Examples  = @("GPIO/GPIO_IOToggle", "TIM/TIM_TimeBase", "UART/UART_Printf"),
    # AC6 (armclang) opt flags. AC6 has NO -Os; its min-size flag is -Oz
    # (the AC5/old-armcc -Os maps to AC6 -Oz). "Os" in the request -> "Oz" here.
    [string[]] $OptLevels = @("O0", "O2", "Oz"),
    [switch]   $DryRun
)
$ErrorActionPreference = "Stop"

$ExampleMap = @{
    "GPIO/GPIO_IOToggle" = "gpio_iotoggle"
    "TIM/TIM_TimeBase"   = "tim_timebase"
    "UART/UART_Printf"   = "uart_printf"
}

# AC5->AC6 patches + opt injection, XML-aware so the -O flag targets ONLY the
# C compiler (Cads), not the assembler. Writes to a build copy per opt level so
# each level's object/axf output is fully isolated (no clobber).
function Set-UvprojxAc6Opt {
    param([string]$Template, [string]$Dest, [string]$OptLevel)

    $xml = [xml](Get-Content -Raw -LiteralPath $Template)
    foreach ($t in @($xml.Project.Targets.Target)) {
        # Patch 1: select AC6 — <uAC6>1</uAC6> as a direct child of <Target>,
        # right after <ToolsetName> (NOT inside <TargetCommonOption>).
        if (-not $t.uAC6) {
            $u = $xml.CreateElement("uAC6"); $u.InnerText = "1"
            [void]$t.InsertAfter($u, $t.ToolsetName)
        }
        # Patch 3: drop AC5 language/LTO fields (else Keil forces C90 on AC6).
        $tco = $t.TargetOption.TargetCommonOption
        if ($tco) {
            foreach ($field in @("v6Lang", "v6LangP", "v6Lto")) {
                $node = $tco.SelectSingleNode($field)
                if ($node) { [void]$tco.RemoveChild($node) }
            }
        }
        # Patch 2 + 4 (C compiler only): drop AC5 --C99 and stale -O, set -O<opt>.
        $vc = $t.TargetOption.Cads.VariousControls
        if ($vc) {
            $mc = $vc.MiscControls
            $mc = $mc -replace "--C99\s*", "" -replace "(^|\s)-O[0-3z]+\s*", " "
            $vc.MiscControls = ($mc.Trim() + " -O$OptLevel").Trim()
        }
    }
    $writer = [System.Xml.XmlTextWriter]::new($Dest, (New-Object System.Text.UTF8Encoding($false)))
    $writer.Formatting = [System.Xml.Formatting]::Indented
    $xml.Save($writer); $writer.Close()
}

$projRel = "Projects\STM32F103RB-Nucleo\Examples"
$summary = @()

foreach ($ex in $Examples) {
    $stem   = $ExampleMap[$ex]
    $mdkArm = Join-Path $Root "$projRel\$ex\MDK-ARM"
    $tmpl   = Join-Path $mdkArm "Project.uvprojx"
    if (-not (Test-Path -LiteralPath $tmpl)) {
        Write-Warning "SKIP $ex : Project.uvprojx not found ($tmpl). Run scripts/prepare_corpus.sh first."
        continue
    }

    foreach ($opt in $OptLevels) {
        # Copy the whole MDK-ARM dir per opt so outputs/objects are isolated.
        $workDir = "$mdkArm-$opt"
        Copy-Item -Path "$mdkArm\*" -Destination $workDir -Recurse -Force
        $log     = Join-Path $workDir "build.$opt.log"
        $fixture = "$OutRoot\nucleo_f103rb_$stem.ac6-$opt.axf"
        Write-Host "`n=== $stem  -O$opt ===" -ForegroundColor Cyan

        Set-UvprojxAc6Opt -Template $tmpl -Dest (Join-Path $workDir "Project.uvprojx") -OptLevel $opt
        if ($DryRun) {
            $chk = [xml](Get-Content -Raw -LiteralPath (Join-Path $workDir "Project.uvprojx"))
            Write-Host "  MiscControls(C) = '$($chk.Project.Targets.Target.TargetOption.Cads.VariousControls.MiscControls)'"
            $summary += "$stem.$opt (dry-run)"; continue
        }

        # UV4 exit: 0 clean, 1 warnings-but-.axf-built, 2+ error. 0/1 = success.
        Start-Process -FilePath $Uv4 -ArgumentList "-b", (Join-Path $workDir "Project.uvprojx"), "-o", $log -NoNewWindow -Wait
        $axf = Join-Path $workDir "STM32F103RB_Nucleo\STM32F103RB_Nucleo.axf"
        if (($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq 1) -and (Test-Path -LiteralPath $axf)) {
            New-Item -ItemType Directory -Force -Path $OutRoot | Out-Null
            Copy-Item -LiteralPath $axf -Destination $fixture -Force
            Write-Host "OK (UV4=$LASTEXITCODE) -> $fixture" -ForegroundColor Green
            $summary += "$stem.$opt OK"
        } else {
            Write-Host "FAIL (UV4=$LASTEXITCODE); see $log" -ForegroundColor Red
            $summary += "$stem.$opt FAIL(UV4=$LASTEXITCODE)"
        }
    }
}

Write-Host "`n==== summary ===="; $summary | ForEach-Object { Write-Host "  $_" }
Write-Host "`nNext (from WSL):  cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/"
Write-Host "Then add BootsClean cases per opt variant in test/test_firmware_armcc.cpp (notes 018)."
