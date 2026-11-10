# start_all.ps1 - Orchestrates C++ Engine, Node Gateway & Launches Dashboard
param(
    [switch]$Live = $false,
    [double]$Speed = 1.0,
    [string]$ReplayFile = "data/polymarket_recording.jsonl"
)

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Polymarket Orderbook Engine | Full Stack Launcher       " -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Start C++ Live Engine
$engineArgs = if ($Live) {
    "--live"
} else {
    "--replay `"$ReplayFile`" --speed $Speed"
}

Write-Host "[1/3] Launching C++ Core Engine (live_engine.exe $engineArgs)..." -ForegroundColor Green
$engineProc = Start-Process -FilePath ".\build\live_engine.exe" -ArgumentList $engineArgs -PassThru -NoNewWindow

Start-Sleep -Milliseconds 600

# 2. Start Node.js Gateway
Write-Host "[2/3] Launching Node.js Gateway (port 4000)..." -ForegroundColor Green
$env:PORT = "4000"
$env:CORE_TCP_PORT = "9001"
$gatewayProc = Start-Process -FilePath "node" -ArgumentList "gateway/server.js" -PassThru -NoNewWindow

Start-Sleep -Milliseconds 1200

# 3. Open Web Browser
Write-Host "[3/3] Opening Dashboard at http://localhost:4000..." -ForegroundColor Green
Start-Process "http://localhost:4000"

Write-Host ""
Write-Host "System is LIVE!" -ForegroundColor Cyan
Write-Host "  - Core Engine TCP IPC : 127.0.0.1:9001" -ForegroundColor Gray
Write-Host "  - Node Gateway REST   : http://127.0.0.1:4000/api/health" -ForegroundColor Gray
Write-Host "  - React Dashboard UI  : http://localhost:4000" -ForegroundColor Gray
Write-Host ""
Write-Host "Press Ctrl+C to terminate both processes and shut down cleanly." -ForegroundColor Yellow

try {
    while ($true) {
        Start-Sleep -Seconds 1
        if ($engineProc.HasExited -or $gatewayProc.HasExited) {
            break
        }
    }
}
finally {
    Write-Host "`nStopping C++ Engine and Gateway processes..." -ForegroundColor Red
    if (!$engineProc.HasExited) { Stop-Process -Id $engineProc.Id -Force -ErrorAction SilentlyContinue }
    if (!$gatewayProc.HasExited) { Stop-Process -Id $gatewayProc.Id -Force -ErrorAction SilentlyContinue }
    Write-Host "Clean shutdown complete." -ForegroundColor Green
}
