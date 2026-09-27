param(
    [string]$Adb = 'adb',
    [string]$Serial,
    [string]$Apk = (Join-Path $PSScriptRoot '../output/pluscode-demo-debug.apk')
)
$ErrorActionPreference = 'Stop'
if (-not $Serial) {
    $devices = @(& $Adb devices | Select-String '^([^\s]+)\s+device$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($devices.Count -ne 1) { throw 'Specify -Serial when there is not exactly one authorized device.' }
    $Serial = $devices[0]
}
& $Adb -s $Serial install -r $Apk
if ($LASTEXITCODE -ne 0) { throw 'APK installation failed' }
& $Adb -s $Serial shell am start -W -n com.askcodex.pluscodedemo/.MainActivity
if ($LASTEXITCODE -ne 0) { throw 'Activity launch failed' }
