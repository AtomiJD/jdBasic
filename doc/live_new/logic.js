class Stdio {
    constructor(onStdout, onStderr) {
        this.stdinBuffer = []; // Comes from JS layer
        this.stdinBufferIdx = 0; // Position in the stdin buffer
        this.stdoutBuffer = ""; // Comes from native layer
        this.stderrBuffer = ""; // Comes from native layer

        // Callbacks
        this.onStdout = onStdout;
        this.onStderr = onStderr;

        this._resolve = null; 
    }

    puts(data) { // Writes to native stdin
        for (const c of data) {
            this.stdinBuffer.push(c.charCodeAt(0));
        }
    }

    flush() { // Flushes to native stdin
        if (this._resolve) {
            this._resolve();
            this._resolve = null;
        }
    }

    _flushstdin() { // Invoked from native
        // Clear old input before waiting
        this.stdinBuffer.length = 0;
        this.stdinBufferIdx = 0;
        return new Promise((resolve) => {
            this._resolve = resolve;
        });
    }

    _flushstdout() { // Invoked from native
        this.onStdout(this.stdoutBuffer);
        this.stdoutBuffer = "";
    }

    _flushstderr() { // Invoked from native
        this.onStderr(this.stderrBuffer);
        this.stderrBuffer = "";
    }

    _handlestdin() {
        if (this.stdinBufferIdx >= this.stdinBuffer.length) {
            return null;
        }
        let ret = this.stdinBuffer[this.stdinBufferIdx];
        this.stdinBufferIdx++;
        if (this.stdinBufferIdx >= this.stdinBuffer.length) {
            // Resetting buffer
            this.stdinBuffer.length = 0;
            this.stdinBufferIdx = 0;
        }
        return ret
    }

    _handlestdout(c) {
        this.stdoutBuffer += (c == 10) ? "\r\n" : String.fromCharCode(c); // For xterm compatibility
    }

    _handlestderr(c) {
        this.stderrBuffer += (c == 10) ? "\r\n" : String.fromCharCode(c); // For xterm compatibility
    }

    init() {
        window._STDIO = this;
        FS.init(this._handlestdin.bind(this), this._handlestdout.bind(this), this._handlestderr.bind(this));
    }
}

const stdio = new Stdio();
let term = null;

(function (stdio) {
    var theme = {
        foreground: "#eff0eb",
        background: "#2e2e2eff",
        selection: "#00000030",
        selectionForeground: '#dcdcdcff',
        black: "#2e2e2eff",
        brightBlack: "#686868",
        red: "#ff5c57",
        brightRed: "#ff5c57",
        green: "#5af78e",
        brightGreen: "#5af78e",
        yellow: "#f3f99d",
        brightYellow: "#f3f99d",
        blue: "#57c7ff",
        brightBlue: "#57c7ff",
        magenta: "#ff6ac1",
        brightMagenta: "#ff6ac1",
        cyan: "#9aedfe",
        brightCyan: "#9aedfe",
        white: "#f1f1f0",
        brightWhite: "#eff0eb"
    };
    term = new Terminal({
        fontFamily: '"Consolas", "Courier New", monospace',
        theme: theme,
        cursorBlink: true
    });

    const fitAddon = new FitAddon.FitAddon();
    term.loadAddon(fitAddon);
    term.open(document.getElementById("terminal"));
    fitAddon.fit();

    window.addEventListener("resize", () => {
        fitAddon.fit();
    });


    let isWaitingForProgramInput = false;
    let lineBuffer = "";

    term.onData(e => {
        switch (e) {
            case "\r": 
                term.write("\r\n");
                if (isWaitingForProgramInput) {
                    processProgramInput(lineBuffer);
                } else {
                    processReplLine(lineBuffer);
                }
                lineBuffer = "";
                break;
            case "\u007F": // Backspace
                if (lineBuffer.length > 0) {
                    term.write("\b \b");
                    lineBuffer = lineBuffer.substring(0, lineBuffer.length - 1);
                }
                break;
            default:
                if (e >= String.fromCharCode(0x20) && e <= String.fromCharCode(0x7E) || e >= "\u00a0") {
                    lineBuffer += e;
                    term.write(e);
                }
        }
    });
    term.focus();


    function processReplLine(text) {
        Module.ccall('run_repl_line', null, ['string'], [text]);
    }

    function processProgramInput(text) {
        isWaitingForProgramInput = false; // Reset the flag
        Module.ccall('resume_from_input', null, ['string'], [text]);
    }

    window.enter_input_mode = () => {
        isWaitingForProgramInput = true;
    };

    stdio.onStdout = function (text) {
        term.write(text);
    }

    stdio.onStderr = function (text) {
        console.log(text);
    }
})(stdio);

function startInterpreter() {
    console.log("Emscripten runtime initialized. Attaching terminal I/O.");
    Module.ccall('init_repl', null, [], []);
    console.log("Attempting to focus the terminal.");
    term.focus();
};

var Module = {
    "preRun": [ // <--- Wrap the function in an array
        () => {
            stdio.init();
        }
    ],
    onRuntimeInitialized: startInterpreter
};

