# Assemble a self-contained jdRPG bundle for a USB stick / another Windows PC.
#
#   .\make_usb_bundle.ps1 -Dest E:\            # writes E:\jdRPG\
#   .\make_usb_bundle.ps1 -Dest E:\ -Model Phi-3-mini-4k-instruct-q4.gguf
#
# Bundles: the rpg-native project (scripts + scenes + assets + prebuilt addon
# DLLs), the chosen LLM model placed into the project's models/ folder, and the
# Godot 4.6.3 editor. No build, no git - a plain copy the son can run.

param(
    [Parameter(Mandatory=$true)] [string]$Dest,
    [string]$Model  = "qwen2.5-7b-instruct-q4_k_m.gguf",
    [string]$Godot  = "D:\usr\dev\godot\Godot_v4.6.3-stable_win64.exe",
    [string]$ModelsDir = "D:\usr\dev\cc\models"
)

$ErrorActionPreference = "Stop"
$proj   = Split-Path -Parent $MyInvocation.MyCommand.Path   # ...\godot\rpg-native
$outRoot = Join-Path $Dest "jdRPG"
$outProj = Join-Path $outRoot "rpg-native"

Write-Host "Bundling jdRPG -> $outRoot" -ForegroundColor Cyan

# 1) Project tree. Skip the Godot editor cache (.godot, re-created on first
#    open) and stray dll-replace temp files.
robocopy $proj $outProj /MIR /XD ".godot" /XF "*.tmp" "~*.TMP" | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# 2) The LLM model into the project's models/ folder.
$srcModel = Join-Path $ModelsDir $Model
if (-not (Test-Path $srcModel)) { throw "Model not found: $srcModel" }
$dstModels = Join-Path $outProj "models"
New-Item -ItemType Directory -Force -Path $dstModels | Out-Null
Copy-Item $srcModel (Join-Path $dstModels $Model) -Force

# Make sure dialog.jdb's g_model_file matches the model we shipped.
$dlg = Join-Path $outProj "dialog.jdb"
(Get-Content $dlg -Raw) -replace 'DIM g_model_file\s*=\s*"[^"]*"', "DIM g_model_file     = `"$Model`"" |
    Set-Content $dlg -Encoding utf8

# 3) The Godot editor.
if (Test-Path $Godot) { Copy-Item $Godot $outRoot -Force }
else { Write-Warning "Godot editor not found at $Godot - copy it onto the stick yourself." }

$size = (Get-ChildItem $outRoot -Recurse | Measure-Object Length -Sum).Sum / 1GB
Write-Host ("Done. Bundle size: {0:N1} GB at {1}" -f $size, $outRoot) -ForegroundColor Green
Write-Host "On the son's PC: read jdRPG\rpg-native\README_SETUP.md" -ForegroundColor Green
