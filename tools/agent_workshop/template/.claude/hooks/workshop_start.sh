#!/usr/bin/env bash
# SessionStart hook: finds jdbasic the way .mcp.json does, runs workshop_start.jdb
# with it, and says why when it cannot.

dir="${CLAUDE_PROJECT_DIR:-$PWD}"
tag="[jdBasic workshop]"
off="The jdb_* tools are unavailable this session; tell the user before relying on them."

if [ -n "$JDBASIC" ]; then
    exe="$JDBASIC"
    if [ ! -f "$exe" ]; then
        echo "$tag JDBASIC is set to '$exe', but no such file exists. Correct the path, then start Claude Code from a new terminal (setx only reaches new terminals). $off"
        exit 0
    fi
else
    # Windows: prefer the .exe, as the MCP client does, over an extensionless file on PATH.
    case "$OSTYPE" in
        msys*|cygwin*) exe="jdbasic.exe" ;;
        *) exe="jdbasic" ;;
    esac
    if ! command -v "$exe" >/dev/null 2>&1; then
        echo "$tag jdbasic is not on PATH and JDBASIC is not set. Set JDBASIC to the full path of the executable, then start Claude Code from a new terminal. $off"
        exit 0
    fi
fi

"$exe" "$dir/.claude/hooks/workshop_start.jdb" "$dir" 2>/dev/null \
    || echo "$tag '$exe' did not run (exit $?). $off"
exit 0
