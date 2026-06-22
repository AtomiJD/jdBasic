jdBasic MCP-native build (Windows x64)

Includes the LLVM-18 native compiler (~70 MB DLL) so
jdb_run_native can turn jdBasic source into a real
standalone .exe via `jdBasic.exe -c file.jdb`.

Run as MCP stdio server:
    jdBasic.exe --mcp

Run as MCP HTTP server (loopback only by default):
    jdBasic.exe --mcp-http 7321

Compile a script to a native EXE:
    jdBasic.exe -c file.jdb
    copy jdbrt.dll alongside file.exe (or PATH)

NATIVE COMPILE REQUIREMENTS:
    -c invokes the MSVC linker to fuse the LLVM-emitted
    object with jdb_runtime.obj + jdbrt.lib. You need:
      - Visual Studio 2022 17.10 or newer (Community
        is fine), MSVC v14.40+ with the "Desktop
        development with C++" workload installed.
        Older MSVC (VS2019, early VS2022) fails at
        link time with LNK2019 unresolved symbols
        __std_find_trivial_1 / __std_find_last_of_*
        because jdb_runtime.obj uses the vectorized
        STL helpers introduced in MSVC v14.40.
      - Windows SDK 10 (any recent version)
    The MCP server itself (jdb_eval / jdb_doc / etc.)
    does NOT need MSVC - only the -c compile path does.

doc\languages.md is read by jdb_doc at runtime;
it's looked up next to the EXE first, so no "cwd"
configuration is required in your MCP client.

Full client-config and tool reference: see doc\MCP.md.
Source / issues: https://github.com/AtomiJD/jdBasic
