#!/usr/bin/env python3
"""
mcp_resume_chain.py - regression test for the stop/resume chain bug.

Spawns jdbasic --mcp, loads tests/mcp_resume_chain.jdb (a fixture with
5 in-script STOPs), and drives 5 cycles of jdb_eval (counter += 10) +
jdb_resume. Asserts the chain completes without hangs and the final
counter is 50.

The bug being regressed: VM::resume() didn't re-stash stopped_* state
after run() returned with is_stopped=true (a second STOP from the same
chain). The 2nd resume then moved an EMPTY stopped_frames into frames
and crashed on the first opcode fetch - silently swallowed by
mcp_stdio's catch(...){} and surfacing as a hung SDL window with the
VM mysteriously back at idle.

Exit codes:
    0 - all 5 cycles green, counter==50
    1 - any cycle failed (hang, missing resume, wrong counter, etc.)
    2 - test infrastructure broke (binary missing, JSON-RPC malformed, ...)
"""
import json, pathlib, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent
BIN = REPO / "build" / "jdBasic.exe"
FIXTURE = HERE / "mcp_resume_chain.jdb"
N_CYCLES = 5
STEP = 10
EXPECTED_FINAL = N_CYCLES * STEP

if not BIN.exists():
    print(f"FAIL: binary not found: {BIN}", file=sys.stderr)
    sys.exit(2)
if not FIXTURE.exists():
    print(f"FAIL: fixture not found: {FIXTURE}", file=sys.stderr)
    sys.exit(2)


class MCP:
    def __init__(self, exe):
        self.proc = subprocess.Popen(
            [str(exe), "--mcp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,
        )
        self.next_id = 1

    def call(self, method, params=None, timeout=5.0):
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            msg["params"] = params
        line = (json.dumps(msg) + "\n").encode("utf-8")
        self.proc.stdin.write(line)
        self.proc.stdin.flush()
        # Read responses until we get the one matching our id.
        deadline = time.monotonic() + timeout
        target_id = msg["id"]
        while time.monotonic() < deadline:
            raw = self.proc.stdout.readline()
            if not raw:
                raise RuntimeError(f"MCP closed stdout while waiting for id {target_id}")
            try:
                resp = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError:
                continue
            if resp.get("id") == target_id:
                return resp
        raise TimeoutError(f"no response for id {target_id} within {timeout}s")

    def tool(self, name, args=None, timeout=5.0):
        params = {"name": name}
        if args is not None:
            params["arguments"] = args
        return self.call("tools/call", params, timeout=timeout)

    def close(self):
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=2.0)
        except Exception:
            self.proc.kill()


def text_of(resp):
    """Pull the first text payload out of a tools/call response."""
    result = resp.get("result")
    if not result:
        return ""
    content = result.get("content") or []
    for item in content:
        if item.get("type") == "text":
            return item.get("text", "")
    return ""


def main():
    mcp = MCP(BIN)
    try:
        # Handshake.
        init = mcp.call(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "resume-chain-test", "version": "1"},
            },
        )
        if "error" in init:
            print(f"FAIL: initialize errored: {init['error']}", file=sys.stderr)
            return 1
        mcp.call("notifications/initialized") if False else None
        # ^ Some servers want this; jdbasic doesn't care. Skipped.

        # Load the fixture - script runs until first STOP.
        load = mcp.tool("jdb_load", {"path": str(FIXTURE.relative_to(REPO))})
        if "error" in load:
            print(f"FAIL: jdb_load errored: {load['error']}", file=sys.stderr)
            return 1
        # Give the worker a moment to hit STOP.
        time.sleep(0.2)

        # 5 stop/eval/resume cycles. Each cycle MUST find the VM in
        # 'stopped' state at the top - that's the regression guard.
        for i in range(1, N_CYCLES + 1):
            status = text_of(mcp.tool("jdb_status"))
            if not status.startswith("stopped"):
                print(
                    f"FAIL cycle {i}: VM is {status!r}, expected 'stopped'",
                    file=sys.stderr,
                )
                return 1
            mcp.tool("jdb_eval", {"code": f"counter = counter + {STEP}"})
            mcp.tool("jdb_resume")
            time.sleep(0.2)  # let worker hit the next STOP or finish

        # Final state: script ran past the 5th STOP, fell off the end.
        # VM should be idle, counter == 50.
        status = text_of(mcp.tool("jdb_status"))
        if not status.startswith("idle"):
            print(
                f"FAIL: post-chain status is {status!r}, expected 'idle'",
                file=sys.stderr,
            )
            return 1
        # counter is global; read via jdb_eval.
        ev = text_of(mcp.tool("jdb_eval", {"code": "PRINT counter"}))
        final = ev.strip().splitlines()[-1] if ev.strip() else ""
        if final != str(EXPECTED_FINAL):
            print(
                f"FAIL: counter is {final!r}, expected {EXPECTED_FINAL}",
                file=sys.stderr,
            )
            return 1
        print(f"OK: {N_CYCLES} stop/resume cycles green, counter={final}")
        return 0
    finally:
        mcp.close()


if __name__ == "__main__":
    sys.exit(main())
