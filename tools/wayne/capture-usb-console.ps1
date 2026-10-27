param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$DurationSeconds = 220
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$events = Join-Path $OutputDirectory 'serial-events.jsonl'
$stopFile = Join-Path $OutputDirectory 'capture.stop'
$encoding = [System.Text.UTF8Encoding]::new($false)
$deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
$serial = $null
$stream = $null
$connection = 0
$total = 0L
function Record-Event($kind, $details) {
    $item = @{ time=[DateTime]::UtcNow.ToString('o'); kind=$kind; details=$details }
    [System.IO.File]::AppendAllText($events, (($item | ConvertTo-Json -Compress -Depth 5) + "`n"), $encoding)
}
Record-Event 'start' @{ expected='VID_0525&PID_A4A7'; duration=$DurationSeconds }
Write-Output 'Waiting for the Linux CDC-ACM gadget; other COM ports are excluded.'
try {
    while ([DateTime]::UtcNow -lt $deadline -and !(Test-Path -LiteralPath $stopFile)) {
        if ($null -eq $serial) {
            $ports = @(Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match 'VID_0525&PID_A4A7' })
            if ($ports.Count -ne 1) {
                if ($ports.Count -gt 1) { Record-Event 'ambiguous' @{ count=$ports.Count } }
                Start-Sleep -Milliseconds 750
                continue
            }
            $serial = [System.IO.Ports.SerialPort]::new($ports[0].DeviceID,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
            $serial.ReadTimeout = 500
            $serial.DtrEnable = $true
            $serial.RtsEnable = $true
            try {
                $serial.Open()
                $connection++
                $name = ('console-{0:D2}.log' -f $connection)
                $path = Join-Path $OutputDirectory $name
                $stream = [System.IO.FileStream]::new($path,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
                Record-Event 'port-open' @{ port=$ports[0].DeviceID; file=$name }
                Write-Output ('USB console opened: ' + $ports[0].DeviceID)
            } catch {
                Record-Event 'open-error' @{ message=$_.Exception.Message }
                if ($null -ne $stream) { $stream.Dispose(); $stream=$null }
                $serial.Dispose(); $serial=$null
                Start-Sleep -Milliseconds 750
                continue
            }
        }
        $buffer = New-Object byte[] 8192
        try {
            $count = $serial.Read($buffer,0,$buffer.Length)
            if ($count -gt 0) {
                $stream.Write($buffer,0,$count)
                $stream.Flush()
                $total += $count
            }
        } catch [System.TimeoutException] {
            continue
        } catch {
            Record-Event 'port-closed' @{ message=$_.Exception.Message; received=$total }
            if ($null -ne $stream) { $stream.Dispose(); $stream=$null }
            $serial.Dispose(); $serial=$null
        }
    }
} finally {
    if ($null -ne $stream) { $stream.Dispose() }
    if ($null -ne $serial) { $serial.Dispose() }
    Record-Event 'finish' @{ received=$total; connections=$connection }
    Write-Output ('Capture finished; bytes=' + $total + '; connections=' + $connection)
}

