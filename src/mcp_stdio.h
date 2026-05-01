#pragma once
#ifdef MCPSERVER

class VM;

// Run jdBasic as an MCP (Model Context Protocol) server over stdio.
// Reads newline-delimited JSON-RPC frames (one object per line, '\n'
// terminated — the MCP spec, NOT LSP-style Content-Length framing) from
// stdin, dispatches them on the persistent VM, writes responses to
// stdout. Returns the process exit code.
//
// POSIX + Windows: gated behind #ifdef MCPSERVER which build.sh and
// build.bat enable when the user passes MCPSERVER=1.
int run_mcp_stdio(VM& vm);

#endif // MCPSERVER
