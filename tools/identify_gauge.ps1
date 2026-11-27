# Reports which gauge firmware is on the board connected over USB.
# Reading the flash restarts the board. Nothing is written to it.
param([string]$Port)

# installed under core_dir (see platformio.ini)
$esptool = "C:\pio\penv\Scripts\esptool.exe"
if (-not (Test-Path $esptool)) {
    Write-Host "esptool not found at $esptool. Build the project once, then retry."
    exit 1
}

if (-not $Port) {
    $boards = @(Get-CimInstance Win32_SerialPort | Where-Object PNPDeviceID -like '*VID_303A&PID_1001*')
    if ($boards.Count -ne 1) {
        Write-Host "Found $($boards.Count) boards $($boards.DeviceID -join ', '). Connect one, or add -Port COMx."
        exit 1
    }
    $Port = $boards[0].DeviceID
}

$dump = Join-Path $env:TEMP "gauge_flash_$PID.bin"
try {
    # the text we look for is near the start of the application, so 64 KB is plenty
    $log = & $esptool --chip esp32s3 --port $Port read-flash 0x10000 0x10000 $dump
    if ($LASTEXITCODE -ne 0) {
        $log | Select-Object -Last 5
        Write-Host "Could not read the board on $Port. Close any serial monitor and retry."
        exit 1
    }
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dump))
}
finally {
    if (Test-Path $dump) { Remove-Item $dump }
}

# text compiled into each build: the on-screen caption and the CAN or simulation task name
$captions = [ordered]@{
    "COOLANT"  = "coolant_temp"
    "OIL TEMP" = "oil_temp"
    "OIL PSI"  = "oil_pressure"
}
$found = @($captions.Keys | Where-Object { $text.Contains("$_`0") } | ForEach-Object { $captions[$_] })
$can = $text.Contains("Receive_CAN_Task`0")
$simulated = $text.Contains("Simulate_CAN_Task`0")

if ($found.Count -ne 1 -or $can -eq $simulated) {
    Write-Host "${Port}: firmware not recognized as one of this project's gauges."
    exit 1
}

if ($can) {
    Write-Host "${Port}: $($found[0])_can (reads the CAN bus)"
} else {
    Write-Host "${Port}: $($found[0]) (simulated data)"
}
