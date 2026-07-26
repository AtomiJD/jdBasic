#pragma once
#ifdef MCPSERVER

#include <string>

class VM;

// Run jdBasic as an MCP (Model Context Protocol) server over stdio.
// Reads newline-delimited JSON-RPC frames (one object per line, '\n'
// terminated - the MCP spec, NOT LSP-style Content-Length framing) from
// stdin, dispatches them on the persistent VM, writes responses to
// stdout. Returns the process exit code.
//
// `user_tools_dir`: optional directory of JSON tool-manifests to load
// at startup. Each *.json describes { name, description, inputSchema,
// handler, module }; the handler FUNC is invoked with the args-as-JSON-
// string and its return value becomes the tool's text result. Empty =
// only the built-in jdb_* tools are exposed.
//
// POSIX + Windows: gated behind #ifdef MCPSERVER which build.sh and
// build.bat enable when the user passes MCPSERVER=1.
int run_mcp_stdio(VM& vm, const std::string& user_tools_dir = "");

#endif // MCPSERVER
