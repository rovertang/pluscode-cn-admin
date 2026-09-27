param(
    [string]$Adb = 'adb',
    [string]$Serial,
    [string]$Output = (Join-Path ([IO.Path]::GetTempPath()) 'shengshixian4pluscode-android/device-check')
)
$ErrorActionPreference = 'Stop'
if (-not $Serial) {
    $devices = @(& $Adb devices | Select-String '^([^\s]+)\s+device$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($devices.Count -ne 1) { throw 'Specify -Serial when there is not exactly one authorized device.' }
    $Serial = $devices[0]
}
New-Item -ItemType Directory -Force $Output | Out-Null
$package = 'com.askcodex.pluscodedemo'
$deviceUser = ((& $Adb -s $Serial shell am get-current-user) -join '').Trim()
$remoteXml = '/data/local/tmp/pluscode-demo-ui.xml'
function Read-Ui {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        & $Adb -s $Serial shell rm -f $remoteXml
        # This car ROM sometimes keeps the dumper alive after writing a complete XML.
        & $Adb -s $Serial shell timeout 6 uiautomator dump $remoteXml | Out-Null
        & $Adb -s $Serial shell test -s $remoteXml
        if ($LASTEXITCODE -eq 0) { return [xml]((& $Adb -s $Serial shell cat $remoteXml) -join "`n") }
        Start-Sleep -Milliseconds 500
    }
    throw 'UI dump failed after three attempts'
}
function Click-Text([string]$Text) {
    $doc = Read-Ui
    $node = $doc.SelectNodes('//node') | Where-Object { $_.text -eq $Text -and $_.enabled -eq 'true' } | Select-Object -First 1
    if (-not $node) { throw "Enabled control not found: $Text" }
    $numbers = [regex]::Matches($node.bounds, '\d+') | ForEach-Object { [int]$_.Value }
    & $Adb -s $Serial shell input tap ([int](($numbers[0]+$numbers[2])/2)) ([int](($numbers[1]+$numbers[3])/2))
}
function Wait-Text([string]$Text) {
    $deadline = (Get-Date).AddSeconds(45)
    do {
        $doc = Read-Ui
        if ($doc.SelectNodes('//node') | Where-Object { $_.text.Contains($Text) }) { return }
        Start-Sleep -Milliseconds 700
    } while ((Get-Date) -lt $deadline)
    throw "Expected screen text missing: $Text"
}
function Screenshot([string]$Name) {
    $remote = '/data/local/tmp/pluscode-demo-screen.png'
    & $Adb -s $Serial shell screencap -p $remote
    & $Adb -s $Serial pull $remote (Join-Path $Output $Name) | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Screenshot pull failed' }
    & $Adb -s $Serial shell rm $remote
}
& $Adb -s $Serial shell am start -W -n "$package/.MainActivity" | Out-Null
Wait-Text '全国数据库已就绪'
Click-Text '上海'
Wait-Text '上海市 / 上海市'
Click-Text '广州'
Wait-Text '广东省 / 广州市'
Click-Text '成都'
Wait-Text '四川省 / 成都市'
Click-Text '北京'
Wait-Text '北京市 / 北京市'
Screenshot 'device-beijing.png'
Click-Text 'Plus Code'
Click-Text '查询行政区'
Wait-Text '东城区'
Screenshot 'device-pluscode.png'
Click-Text 'WGS84 经纬度'
Click-Text '清空块缓存'
Wait-Text '块缓存已清空'
Screenshot 'device-cache-cleared.png'
Click-Text '运行设备自检'
Wait-Text '设备自检'
$report = ((& $Adb -s $Serial shell run-as $package --user $deviceUser cat files/self-test.json) -join "`n")
$parsed = $report | ConvertFrom-Json
if (-not $parsed.passed -or $parsed.cases.Count -ne 14) { throw 'JNI self-test failed' }
$report | Set-Content (Join-Path $Output 'self-test.json') -Encoding utf8
Screenshot 'device-self-test.png'
& $Adb -s $Serial shell input keyevent BACK
Start-Sleep -Milliseconds 500
# The car ROM's uiautomator service becomes unreliable after repeated dumps.
# These coordinates are the centers of the already-verified controls at 1920x720/160 dpi.
& $Adb -s $Serial shell input tap 434 469
Start-Sleep -Seconds 8
Screenshot 'device-simulation.png'
& $Adb -s $Serial shell input tap 434 469
& $Adb -s $Serial shell input tap 228 370
Start-Sleep -Seconds 2
Screenshot 'device-final.png'
& $Adb -s $Serial shell dumpsys meminfo $package | Set-Content (Join-Path $Output 'meminfo.txt') -Encoding utf8
& $Adb -s $Serial shell rm $remoteXml
[pscustomobject]@{ passed=$true; jniCases=$parsed.cases.Count; output=$Output } | ConvertTo-Json
