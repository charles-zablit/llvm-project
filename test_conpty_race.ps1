$lldb = "..\llvm-build\release\bin\lldb.exe"
$exe = "C:\Users\charleszablit\Developer\testing\main.exe"
$logFile = "conpty_trace.log"
$timeoutSec = 5
$run = 0

$env:LLDB_CONPTY_TRACE = $logFile

while ($true) {
    $run++
    # Delete stale log
    if (Test-Path $logFile) { Remove-Item $logFile }

    Write-Host "--- Run $run ---" -ForegroundColor Cyan

    $proc = Start-Process -FilePath $lldb -ArgumentList "`"$exe`"", "-o", "`"break set -p 'break here'`"", "-o", "r", "-o", "c", "-o", "q" -PassThru -NoNewWindow
    $exited = $proc.WaitForExit($timeoutSec * 1000)

    if (-not $exited) {
        Write-Host "TIMEOUT after ${timeoutSec}s on run $run — killing lldb" -ForegroundColor Red
        $proc.Kill()
        $proc.WaitForExit()
        Write-Host "Log saved to $logFile" -ForegroundColor Yellow
        break
    }

    # Fast run, no issue — delete log and continue
    if (Test-Path $logFile) { Remove-Item $logFile }
}

Remove-Item env:\LLDB_CONPTY_TRACE
