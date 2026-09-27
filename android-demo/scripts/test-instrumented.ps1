#requires -Version 7.4
param(
    [string]$Adb = 'adb',
    [string]$Serial,
    [string]$TestApk = (Join-Path $PSScriptRoot '../output/pluscode-demo-test.apk'),
    [string]$Output = (Join-Path ([IO.Path]::GetTempPath()) 'shengshixian4pluscode-android/instrumented')
)
$ErrorActionPreference = 'Stop'
$package = 'com.askcodex.pluscodedemo'
if (-not $Serial) {
    $devices = @(& $Adb devices | Select-String '^([^\s]+)\s+device$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($devices.Count -ne 1) { throw 'Specify -Serial when there is not exactly one authorized device.' }
    $Serial = $devices[0]
}
$deviceUser = ((& $Adb -s $Serial shell am get-current-user) -join '').Trim()
New-Item -ItemType Directory -Force $Output | Out-Null
& $Adb -s $Serial install -r $TestApk
if ($LASTEXITCODE -ne 0) { throw 'Test APK installation failed' }
$log = (& $Adb -s $Serial shell am instrument --user $deviceUser -w "$package.test/$package.DemoInstrumentation") -join "`n"
$log | Set-Content (Join-Path $Output 'instrumentation.txt') -Encoding utf8
if (-not $log.Contains('PASS: 14 JNI checks')) { throw $log }
foreach ($name in @('self-test.json','window-beijing.png','window-pluscode.png','window-cache-cleared.png','window-simulation.png')) {
    # PowerShell 7.4 preserves native stdout bytes during redirection.
    & $Adb -s $Serial exec-out run-as $package --user $deviceUser cat "files/$name" > (Join-Path $Output $name)
    if ($LASTEXITCODE -ne 0) { throw "Export failed: $name" }
}
& $Adb -s $Serial shell am start -W -n "$package/.MainActivity" | Out-Null
$log
