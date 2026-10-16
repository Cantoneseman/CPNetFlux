param(
    [ValidateSet('original','a','b')][string]$Variant = 'original',
    [ValidateSet('tasks','scanning','completed','failed','offline','empty')][string]$Scenario = 'tasks',
    [int]$Page = 0,
    [int]$Width = 1366,
    [int]$Height = 768,
    [ValidateSet('1','1.25','1.5')][string]$Scale = '1'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$previewPython = Join-Path $repoRoot '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $previewPython)) {
    throw '先在 CPNetFlux-Desktop 执行 python -m venv .venv，然后用 .venv\Scripts\python.exe -m pip install -r desktop\ui-comparison\requirements-preview.txt'
}
& $previewPython (Join-Path $PSScriptRoot 'preview.py') --variant $Variant --scenario $Scenario --page $Page --width $Width --height $Height --scale $Scale
if ($LASTEXITCODE -ne 0) { throw "Qt/QML 预览退出码：$LASTEXITCODE" }
