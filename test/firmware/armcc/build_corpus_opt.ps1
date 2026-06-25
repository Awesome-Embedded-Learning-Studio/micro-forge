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
# >>> RUN ON WINDOWS (local NTFS). <<<  Keil's .__i response files cannot be
# created on the 9p filesystem, so the BUILD must be native Windows. Tip: pass
# -NoProfile to skip your $PROFILE (e.g. oh-my-posh) noise:
#   powershell -NoProfile -ExecutionPolicy Bypass -File build_corpus_opt.ps1
# (Re)build the source subtree first from WSL:  bash scripts/prepare_corpus.sh
#
# Prereqs (REGENERATE.md): Keil MDK + AC6 (armclang) + STM32F1xx_DFP 2.4.1 +
# CMSIS 6.x pack; UV4 at D:\MDK\UV4\UV4.exe. Source tree at D:\mf\STM32CubeF1
# with Drivers/ and Projects/ at the top (6-level include depth).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File build_corpus_opt.ps1
#   ... -OptLevels O0,O2      # subset
#   ... -DryRun               # patch+print MiscControls, no UV4 build
#
# Output: $OutRoot\nucleo_f103rb_<example>.ac6-<opt>.axf  (then from WSL:
#   cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/)
#
# NOTE — not testable from the WSL side. Verify in Keil GUI that -O<opt> lands
# in C/C++ (AC6) -> Optimization / Controls; if Keil's structured field
# overrides MiscControls, set it there too.
# ============================================================================

[CmdletBinding()]
param(
    [string]   $Root      = "D:\mf\STM32CubeF1",
    [string]   $OutRoot   = "D:\mf\out",
    [string]   $Uv4       = "D:\MDK\UV4\UV4.exe",
    # Project file is Project.uvprojx in CubeF1; the TARGET/OUTPUT name inside
    # is STM32F103RB_Nucleo (-> output ...\STM32F103RB_Nucleo\STM32F103RB_Nucleo.axf).
    [string[]] $Examples  = @("GPIO/GPIO_IOToggle", "TIM/TIM_TimeBase", "UART/UART_Printf"),
    # Opt level STRINGS already include the O (O0/O2/Oz); the armclang flag is
    # "-"+level ("-O0"/"-O2"/"-Oz"). AC6 has NO -Os; min-size is -Oz.
    [string[]] $OptLevels = @("O0", "O2", "Oz"),
    [switch]   $DryRun
)
$ErrorActionPreference = "Stop"

$ExampleMap = @{
    "GPIO/GPIO_IOToggle" = "gpio_iotoggle"
    "TIM/TIM_TimeBase"   = "tim_timebase"
    "UART/UART_Printf"   = "uart_printf"
}

# AC5->AC6 patches + opt injection. Uses explicit XmlNode methods (SelectSingleNode
# / InnerText) instead of PowerShell's ETS dot-access, which stringifies leaf
# elements and broke InsertAfter. The -O flag targets ONLY Cads (C compiler).
function Set-UvprojxAc6Opt {
    param([string]$Template, [string]$Dest, [string]$OptLevel)

    $xml = [xml](Get-Content -Raw -LiteralPath $Template)
    foreach ($t in $xml.SelectNodes('/Project/Targets/Target')) {
        # Patch 1: <uAC6>1</uAC6> as a direct child of <Target>, after <ToolsetName>.
        if (-not $t.SelectSingleNode('uAC6')) {
            $u = $xml.CreateElement('uAC6'); $u.InnerText = '1'
            $ref = $t.SelectSingleNode('ToolsetName')
            if ($ref) { [void]$t.InsertAfter($u, $ref) } else { [void]$t.AppendChild($u) }
        }
        # Patch 3: drop AC5 language/LTO fields (else Keil forces C90 on AC6).
        $tco = $t.SelectSingleNode('TargetOption/TargetCommonOption')
        if ($tco) {
            foreach ($field in @('v6Lang', 'v6LangP', 'v6Lto')) {
                $node = $tco.SelectSingleNode($field)
                if ($node) { [void]$tco.RemoveChild($node) }
            }
        }
        # Patch 2+4 (C compiler only): drop AC5 --C99 and stale -O, set the opt flag.
        $mc = $t.SelectSingleNode('TargetOption/Cads/VariousControls/MiscControls')
        if ($mc) {
            $val = $mc.InnerText
            $val = $val -replace '--C99\s*', '' -replace '(^|\s)-O[0-3z]+\s*', ' '
            $mc.InnerText = ($val.Trim() + ' -' + $OptLevel).Trim()
        }
    }
    $enc = New-Object System.Text.UTF8Encoding($false)
    $writer = New-Object System.Xml.XmlTextWriter($Dest, $enc)
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
        $optFlag = '-' + $opt   # "O0" -> "-O0"
        # Copy the whole MDK-ARM dir per opt so outputs/objects are isolated.
        # Copy-Item won't create $workDir for a wildcard source, so make it first.
        $workDir = "$mdkArm-$opt"
        New-Item -ItemType Directory -Force -Path $workDir | Out-Null
        Copy-Item -Path "$mdkArm\*" -Destination $workDir -Recurse -Force
        $log     = Join-Path $workDir "build.$opt.log"
        $fixture = "$OutRoot\nucleo_f103rb_$stem.ac6-$opt.axf"
        Write-Host "`n=== $stem  $optFlag ===" -ForegroundColor Cyan

        Set-UvprojxAc6Opt -Template $tmpl -Dest (Join-Path $workDir "Project.uvprojx") -OptLevel $opt
        if ($DryRun) {
            $chk = [xml](Get-Content -Raw -LiteralPath (Join-Path $workDir "Project.uvprojx"))
            $c = $chk.SelectSingleNode('/Project/Targets/Target/TargetOption/Cads/VariousControls/MiscControls')
            Write-Host ("  MiscControls(C) = '" + $(if ($c) { $c.InnerText } else { '<none>' }) + "'")
            $summary += "$stem.$opt (dry-run)"; continue
        }

        # UV4 exit: 0 clean, 1 warnings-but-.axf-built, 2+ error. 0/1 = success.
        # Start-Process needs -PassThru + .ExitCode (it does NOT set $LASTEXITCODE).
        $p = Start-Process -FilePath $Uv4 -ArgumentList "-b", (Join-Path $workDir "Project.uvprojx"), "-o", $log -NoNewWindow -Wait -PassThru
        $code = $p.ExitCode
        $axf = Join-Path $workDir "STM32F103RB_Nucleo\STM32F103RB_Nucleo.axf"
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

Write-Host "`n==== summary ===="; $summary | ForEach-Object { Write-Host "  $_" }
Write-Host "`nNext (from WSL):  cp /mnt/d/mf/out/*.ac6-*.axf test/firmware/armcc/"
Write-Host "Then add BootsClean cases per opt variant in test/test_firmware_armcc.cpp (notes 018)."
