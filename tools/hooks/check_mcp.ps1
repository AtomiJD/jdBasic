# SessionStart health check for the jdBasic stdio MCP server (jdbasic-stdio-win).
# Spawns the configured binary, sends a JSON-RPC initialize, and waits briefly for
# a valid MCP response. On failure it prints a warning (which lands in the session
# context) so the assistant can report that jdb_* / jdlab tools are unavailable.
# Always exits 0 - never blocks the session.

$ErrorActionPreference = 'SilentlyContinue'
$exe = 'D:\usr\dev\cc\mcp-runtime\jdBasic_mcp.exe'

if (-not (Test-Path $exe)) {
    Write-Output "[MCP CHECK] jdBasic MCP binary not found at $exe - the jdbasic-stdio-win server cannot start. jdb_* / jdlab tools are UNAVAILABLE. Report it (rebuild mcp-runtime)."
    exit 0
}

try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = '--mcp'
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    $sb = New-Object System.Text.StringBuilder
    $handler = { if ($EventArgs.Data) { [void]$Event.MessageData.Append($EventArgs.Data); [void]$Event.MessageData.Append("`n") } }
    Register-ObjectEvent -InputObject $p -EventName OutputDataReceived -Action $handler -MessageData $sb | Out-Null

    [void]$p.Start()
    $p.BeginOutputReadLine()

    $init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"healthcheck","version":"1"}}}'
    $p.StandardInput.WriteLine($init)
    $p.StandardInput.Flush()

    $deadline = (Get-Date).AddSeconds(5)
    $ok = $false
    while ((Get-Date) -lt $deadline) {
        if ($sb.ToString() -match 'serverInfo|protocolVersion|"result"') { $ok = $true; break }
        Start-Sleep -Milliseconds 100
    }

    try { if (-not $p.HasExited) { $p.Kill() } } catch {}
    Get-EventSubscriber | Where-Object { $_.SourceObject -eq $p } | Unregister-Event

    if (-not $ok) {
        Write-Output "[MCP CHECK] jdBasic MCP (jdbasic-stdio-win) did not answer an initialize within 5s - it is likely DOWN. jdb_* / jdlab / workshop tools will fail this session. Say so before relying on them."
    }
} catch {
    Write-Output "[MCP CHECK] jdBasic MCP (jdbasic-stdio-win) failed to start: $($_.Exception.Message). jdb_* / jdlab tools are UNAVAILABLE - say so."
}

exit 0
