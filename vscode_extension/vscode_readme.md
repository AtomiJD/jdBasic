# Getting Started with the jdBasic Debug Extension (v1.0.8)

Welcome! This guide will walk you through installing, configuring, and using the `jdbasic-debug` extension for Visual Studio Code to debug your `jdBASIC` programs.

## 1. Prerequisites

Before you begin, ensure you have the following:

1.  **Visual Studio Code**: Installed and up to date.
2.  **jdBASIC.exe**: The jdBASIC interpreter must be installed on your system. For the easiest setup, make sure the folder containing `jdBASIC.exe` is included in your system's `PATH` environment variable.

## 2. Installation

1.  Open VS Code.
2.  Go to the **Extensions** view (you can use the shortcut `Ctrl+Shift+X`).
3.  Click the **...** (More Actions) menu at the top of the Extensions view.
4.  Select **Install from VSIX...**.
5.  Locate and select the `jdbasic-debug-1.0.8.vsix` file to install it.

## 3. Your First Debug Session

The quickest way to start debugging is to use the default configuration.

1.  Open your jdBasic project folder in VS Code.
2.  Open the `.bas` file you want to debug (e.g., `test.bas`).
3.  Press `F5`.
4.  VS Code will show a dropdown menu asking you to select a debugger. Choose **"jdBasic Debug"**.

The debugger will start, and because `stopOnEntry` is `true` by default, it will pause on the very first line of your `test.bas` file.

## 4. Setting Up `launch.json` for Full Control

For more complex projects and repeatable debugging sessions, you should create a `launch.json` file.

1.  Go to the **Run and Debug** view on the Activity Bar (or use the shortcut `Ctrl+Shift+D`).
2.  Click on the link **"create a launch.json file"**.
3.  Again, select **"jdBasic Debug"** from the dropdown.
4.  VS Code will create a `.vscode/launch.json` file with an initial configuration. You can now add more configurations.

### Adding Debug Configurations

Click the **"Add Configuration..."** button in the bottom-right of the `launch.json` editor. You will see snippets to help you:

* **`jdbasic Debug: Launch`**: This is a standard configuration that runs the debugger silently in the background.
* **`jdbasic Debug in Integrated Terminal`**: **(Recommended)** This runs your program inside VS Code's integrated terminal, which is very useful for seeing `PRINT` statements and other program output directly.

Here is an example `launch.json` file with both configurations:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "jdbasic",
      "request": "launch",
      "name": "jdBasic: Debug in Terminal",
      "program": "${file}",
      "stopOnEntry": true,
      "runtime": "jdBASIC.exe",
      "console": "integratedTerminal"
    },
    {
      "type": "jdbasic",
      "request": "launch",
      "name": "jdBasic: Debug a specific file",
      "program": "${workspaceFolder}/src/main.bas",
      "stopOnEntry": true,
      "runtime": "C:/path/to/your/jdBASIC.exe"
    }
  ]
}
```

## 5. Using the Debugger

Once you have a configuration in your `launch.json`, you can start debugging.

1.  Go to the **Run and Debug** view.
2.  Select the configuration you want to use from the dropdown menu at the top (e.g., "jdBasic: Debug in Terminal").
3.  Click the green **Start Debugging** play button (or press `F5`).

### Setting Breakpoints

Before starting, you can set breakpoints. Simply click in the gutter to the left of the line numbers in your `.bas` file. A red dot will appear. When the program execution reaches this line, it will pause.

### Controlling Execution

When the debugger is paused, a toolbar will appear at the top of the editor, allowing you to control the program's flow:

* **Continue (`F5`)**: Resumes execution until the next breakpoint or the end of the program.
* **Step Over (`F10`)**: Executes the current line and pauses on the next one.
* **Step In (`F11`)**: If the current line is a function/subroutine call, it will step into that function.
* **Step Out (`Shift+F11`)**: If you are inside a function, it will execute the rest of the function and pause after it returns.
* **Restart (`Ctrl+Shift+F5`)**: Stops and restarts the debugging session.
* **Stop (`Shift+F5`)**: Terminates the debugging session.

### Inspecting Variables

While the debugger is paused, the **Variables** panel on the left will show you the current values of all your variables. They are conveniently sorted into **Local** and **Global** scopes, making it easy to track the state of your program.

### Using the Debug Console (REPL)

At the bottom of the VS Code window, the **Debug Console** panel is a powerful tool for interacting directly with the running `jdBASIC.exe` interpreter while it is paused.

You can type commands here and press Enter to execute them. This is useful for:

* **Checking a variable's value**:
    ```
    print a
    ```

* **Changing a variable's value**:
    ```
    a = 5
    ```

* **Executing other runtime commands**: The `jdBASIC.exe` interpreter might support special commands. You can try commands like `pwd` or `cd "cource"`. The entirely features of `jdBASIC.exe` can be found in [""language.md""](https://github.com/AtomiJD/NeReLaBasicInterpreter/blob/master/language.md).

Happy Debugging!
