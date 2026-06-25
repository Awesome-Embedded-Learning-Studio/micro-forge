# build_corpus_opt.ps1
# ============================================================================
# Build the STM32F103RB-Nucleo firmware corpus at multiple AC6 optimization
# levels for the micro-forge E2 Tier-2 conformance gate.
#
# >>> RUN ON WINDOWS (local NTFS). <<<  Keil's .__i response files cannot be
# created on the 9p filesystem, so the BUILD must be native Windows. Pass
# -NoProfile to skip your $PROFILE (e.g. oh-my-posh) noise:
#   powershell -NoProfile -ExecutionPolicy Bypass -File build_corpus_opt.ps1 -DryRun
# (Re)build the source subtree first from WSL:  bash scripts/prepare_corpus.sh
#
# Prereqs (REGENERATE.md): Keil MDK + AC6 (armclang) + STM32F1xx_DFP 2.4.1 +
# CMSIS 6.x pack; UV4 at D:\MDK\UV4\UV4.exe. Source tree at D:\mf\STM32CubeF1.
#
# Design: for each opt level, write a patched Project.<opt>.uvprojx INTO the
# existing MDK-ARM dir (its parent always exists — the template lives there),
# and isolate that level's output via <OutputDirectory>build-<opt>\</...> and
# <OutputName>STM32F103RB_Nucleo-<opt></...>. No dir copying (Copy-Item of a
# wildcard source to a fresh target misbehaved on this box).
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
    [string[]] $Examples  = @("GPIO/GPIO_IOToggle", "TIM/TIM_TimeBase", "UART/UART_Printf"),
    # Level strings already include the O (O0/O2/Oz); the armclang flag is
    # "-"+level ("-O0"/"-O2"/"-Oz"). AC6 has NO -Os; min-size is -Oz.
    [string[]] $OptLevels = @("O0", "O2", "Oz"),
    [switch]   $DryRun
)
$ErrorActionPreference = "Stop"

# Banner — confirms you run a current copy (rev 2026-06-25e: GetElementsByTagName
# for C MiscControls so --C99 is actually removed). If you do NOT see this line,
# D:\mf\build_corpus_opt.ps1 is stale — re-copy from the repo.
Write-Host "build_corpus_opt.ps1 rev 2026-06-25e . Root=$Root . DryRun=$([bool]$DryRun) . OptLevels=$($OptLevels -join ',')" -ForegroundColor DarkGray

$ExampleMap = @{
    "GPIO/GPIO_IOToggle" = "gpio_iotoggle"
    "TIM/TIM_TimeBase"   = "tim_timebase"
    "UART/UART_Printf"   = "uart_printf"
}

# Patch the template -> $Dest (inside the existing MDK-ARM dir). XML via explicit
# XmlNode methods (ETS stringifies leaf elements and broke InsertAfter). The -O
# flag targets ONLY Cads (C compiler). Output dir/name isolate each opt level.
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
        $tco = $t.SelectSingleNode('TargetOption/TargetCommonOption')
        if ($tco) {
            # Patch 3: drop AC5 language/LTO fields (else Keil forces C90 on AC6).
            foreach ($field in @('v6Lang', 'v6LangP', 'v6Lto')) {
                $node = $tco.SelectSingleNode($field)
                if ($node) { [void]$tco.RemoveChild($node) }
            }
            # Isolate output per opt level (no dir copying needed).
            $od = $tco.SelectSingleNode('OutputDirectory')
            if ($od) { $od.InnerText = "build-$OptLevel\" }
            $on = $tco.SelectSingleNode('OutputName')
            if ($on) { $on.InnerText = "STM32F103RB_Nucleo-$OptLevel" }
        }
        # Patch 2+4 (C compiler only): drop AC5 --C99 and stale -O, set opt flag.
        # GetElementsByTagName dodges the exact Cads nesting depth (a bare XPath
        # TargetOption/Cads/.../MiscControls failed to match on this .uvprojx).
        foreach ($mc in $t.GetElementsByTagName('MiscControls')) {
            $gp = $mc.ParentNode.ParentNode   # MiscControls -> VariousControls -> Cads|Aads
            if ($gp -and $gp.LocalName -eq 'Cads') {
                $val = $mc.InnerText
                $val = $val -replace '--C99\s*', '' -replace '(^|\s)-O[0-3z]+\s*', ' '
                $mc.InnerText = ($val.Trim() + ' -' + $OptLevel).Trim()
            }
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
        $optFlag = '-' + $opt
        $proj    = Join-Path $mdkArm "Project.$opt.uvprojx"
        $log     = Join-Path $mdkArm "build.$opt.log"
        $fixture = "$OutRoot\nucleo_f103rb_$stem.ac6-$opt.axf"
        Write-Host "`n=== $stem  $optFlag ===" -ForegroundColor Cyan

        Set-UvprojxAc6Opt -Template $tmpl -Dest $proj -OptLevel $opt
        if ($DryRun) {
            $chk = [xml](Get-Content -Raw -LiteralPath $proj)
            $c  = $chk.SelectSingleNode('/Project/Targets/Target/TargetOption/Cads/VariousControls/MiscControls')
            $od = $chk.SelectSingleNode('/Project/Targets/Target/TargetOption/TargetCommonOption/OutputDirectory')
            Write-Host ("  MiscControls(C) = '" + $(if ($c)  { $c.InnerText  } else { '<none>' }) + "'")
            Write-Host ("  OutputDirectory = '" + $(if ($od) { $od.InnerText } else { '<none>' }) + "'")
            $summary += "$stem.$opt (dry-run)"; continue
        }

        # UV4 exit: 0 clean, 1 warnings-but-.axf-built, 2+ error. 0/1 = success.
        $p = Start-Process -FilePath $Uv4 -ArgumentList "-b", $proj, "-o", $log -NoNewWindow -Wait -PassThru
        $code = $p.ExitCode
        $axf = Join-Path $mdkArm "build-$opt\STM32F103RB_Nucleo-$opt.axf"
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
