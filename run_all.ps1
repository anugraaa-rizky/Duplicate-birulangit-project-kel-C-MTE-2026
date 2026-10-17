# Menyalakan backend (port 8000) dan frontend (port 3000) di jendela terpisah, lalu membuka browser.
# Bila database masih kosong, simulator mengisi 3 hari data agar dashboard langsung berisi.
#   ./run_all.ps1          backend + frontend
#   ./run_all.ps1 -Live    ditambah jendela simulator yang mengirim satu slot tiap 5 detik
param([switch]$Live)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$backend = Join-Path $root "backend"
$frontend = Join-Path $root "frontend"

# Node.js yang baru dipasang belum masuk PATH sampai terminal dibuka ulang.
if (-not (Get-Command npm -ErrorAction SilentlyContinue) -and (Test-Path "C:\Program Files\nodejs\npm.cmd")) {
    $env:Path = "C:\Program Files\nodejs;" + $env:Path
}

function Wait-Url($url, $seconds) {
    for ($i = 0; $i -lt $seconds; $i++) {
        try { return Invoke-RestMethod $url -TimeoutSec 2 } catch { Start-Sleep 1 }
    }
    return $null
}

Write-Host "[1/3] Backend FastAPI  -> http://localhost:8000/docs"
Start-Process powershell -ArgumentList "-NoExit", "-Command", "`$Host.UI.RawUI.WindowTitle='langitbiru BACKEND :8000'; cd '$backend'; python -m uvicorn app.main:app --host 0.0.0.0 --port 8000"
$h = Wait-Url "http://localhost:8000/api/health" 30
if (-not $h) { Write-Host "Backend belum menjawab di port 8000"; exit 1 }
if ($h.records -eq 0) {
    Write-Host "      Database kosong, mengisi 3 hari data simulasi..."
    Push-Location $backend; python simulator.py --seed-days 3 --tor; Pop-Location
}

Write-Host "[2/3] Frontend Next.js -> http://localhost:3000"
if (-not (Test-Path (Join-Path $frontend "node_modules"))) { Push-Location $frontend; npm install; Pop-Location }
$npm = (Get-Command npm.cmd -ErrorAction SilentlyContinue).Source
if (-not $npm) { Write-Host "npm tidak ditemukan. Pasang Node.js 20+ lalu buka ulang terminal."; exit 1 }
$nodeDir = Split-Path -Parent $npm
Start-Process powershell -WorkingDirectory $frontend -ArgumentList "-NoExit", "-Command", "`$Host.UI.RawUI.WindowTitle='langitbiru FRONTEND :3000'; `$env:Path='$nodeDir;'+`$env:Path; & '$npm' run dev"
if (-not (Wait-Url "http://localhost:3000" 90)) { Write-Host "Frontend belum siap di port 3000"; exit 1 }

if ($Live) {
    Write-Host "      Simulator live: satu slot tiap 5 detik"
    Start-Process powershell -ArgumentList "-NoExit", "-Command", "`$Host.UI.RawUI.WindowTitle='langitbiru SIMULATOR'; cd '$backend'; python simulator.py --live --interval 5"
}

Write-Host "[3/3] Membuka dashboard di browser"
Start-Process "http://localhost:3000"
