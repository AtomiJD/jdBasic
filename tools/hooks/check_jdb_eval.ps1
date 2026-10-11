# OS.EXEC / OS.LOAD inside jdb_eval deadlock the stdio MCP server: the eval
# blocks the single worker thread that would have to answer. Shell work goes
# through the agent's own Bash tool instead.
$raw = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }

try {
  $payload = $raw | ConvertFrom-Json
} catch {
  exit 0
}

$code = $payload.tool_input.code
if ($null -eq $code) { exit 0 }

if ($code -match 'OS\.(EXEC|LOAD)') {
  [Console]::Error.WriteLine('Blocked: OS.EXEC/OS.LOAD inside jdb_eval deadlocks the stdio MCP server. Run shell commands with your own Bash tool instead.')
  exit 2
}
exit 0
