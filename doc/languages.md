# jdBasic Language Reference

This document describes the syntax, commands, and functions for the jdBasic interpreter.

## Data Types

jdBasic supports a variety of data types. While variables are variants and can hold any type, they can be explicitly created using the `DIM` statement.

* **Boolean**: `TRUE` or `FALSE`.
* **Number**: 64-bit double-precision floating-point numbers.
* **String**: Text of variable length. String variable names traditionally end with a `$` suffix (e.g., `A$`).
* **DateTime**: A type for storing date and time values, created with `NOW()` or `CVDATE()`.
* **Array**: A multi-dimensional array of other Basic values.
* **Map**: A key-value dictionary where keys are strings and values can be any Basic value. Used for creating complex data structures.
* **Tensor**: An opaque data type that holds multi-dimensional floating-point data and tracks computational history for automatic differentiation (autodiff). It is the core of the AI functions and enables building and training neural networks.
* **JsonObject**: A special type returned by `JSON.PARSE$`, which can be accessed like a Map or Array.
* **ComObject**: A special type returned by `CREATEOBJECT`, representing an instance of a COM Automation object.

## Numeric Semantics

jdBasic has two scalar numeric types:

* **INTEGER** — signed 64-bit: `-9,223,372,036,854,775,808 … 9,223,372,036,854,775,807`
* **DOUBLE** — IEEE-754 64-bit floating point

### Literals

* Plain digits (no decimal/exponent) → **INTEGER**: `0`, `42`, `9223372036854775807`
* With decimal point → **DOUBLE**: `1.0`
* Hex/bin: `$FF`, `%1010` → **INTEGER**

### Built-in Constants

These are special keywords that hold predefined, constant values.

* **`PI`**: A high-precision value of Pi ($\\pi \\approx 3.141592653589793$).
* **`VBNEWLINE`**: A string representing the carriage return and line feed characters (`CHR$(13) + CHR$(10)`), commonly used for creating multi-line strings for Windows systems.

```basic
PRINT "The value of PI is: " + PI
PRINT "Area of a circle with radius 5: " + (PI * 5^2)

MultiLine$ = "First line." + VBNEWLINE + "Second line."
PRINT MultiLine$
```

### Conversions

* **Promotion:** In mixed expressions, `INTEGER` promotes to `DOUBLE`.
* **Narrowing:** `DOUBLE → INTEGER` occurs when assigning to an integer variable or when using integer-only operators; conversion **truncates toward zero**.
* **Overflow:** If a result doesn’t fit in 64-bit signed range → DOUBLE.

### Operators matrix

| Operator                              | Operands                   | Result  | Notes                                                                                           |
| ------------------------------------- | -------------------------- | ------- | ----------------------------------------------------------------------------------------------- |
| `+` `-` `*`                           | `INTEGER, INTEGER`         | INTEGER | Integer arithmetic (overflow policy applies).                                                   |
| `+` `-` `*`                           | otherwise                  | DOUBLE  | If any side is DOUBLE, result is DOUBLE.                                                        |
| `/`                                   | any                        | DOUBLE  | Floating division.                                                                              |
| `\`                                   | numeric (integer division) | INTEGER | **Truncates toward zero**; division by zero is an error.                                        |
| `MOD`                                 | numeric remainder          | INTEGER | `a - trunc(a/b) * b`; division by zero is an error.                                             |
| `^`                                   | numeric                    | DOUBLE  | Power.                                                                                          |
| `BAND` `BOR` `BXOR` `NOT` `SHL` `SHR` | numeric                    | INTEGER | Operands coerced to 64-bit integer (trunc toward 0 first). Shift counts are clamped to `0..63`. |

**Arrays:** `+ - * / \ MOD` and bitwise ops apply **element-wise** for arrays of equal shape. Scalar–array operations broadcast the scalar.
**Tensors:** `/` is supported; `\` (integer division) and `MOD` are **not supported** for tensors.

### Comparisons

* `INTEGER` vs `INTEGER` → integer comparison
* `DOUBLE` vs `DOUBLE` → double comparison
* Mixed → promote to **DOUBLE** for comparison

### `TYPEOF`

Returns `"INTEGER"` for 64-bit integers and `"DOUBLE"` for floating point values.

### Interop (COM)

* `VT_I8` maps to jdBasic **INTEGER**
* `VT_UI8` maps to **INTEGER** when ≤ `2^63-1`, otherwise to **DOUBLE**
* Other COM numeric types map to **DOUBLE**

### Examples

```basic
' Basic arithmetic
PRINT 5 / 2          ' 2.5   (DOUBLE)
PRINT 5 \ 2          ' 2     (INTEGER, trunc toward 0)
PRINT -5 \ 2         ' -2
PRINT 5 MOD 2        ' 1

' Mixed numeric types
PRINT 2 * 3          ' 6     (INTEGER)
PRINT 2 * 3.0        ' 6     (DOUBLE)
PRINT TYPEOF(2 * 3), TYPEOF(2 * 3.0)  ' INTEGER, DOUBLE

' Bitwise are integer-only
PRINT 5 BAND 3       ' 1
PRINT TYPEOF(5 BAND 3)  ' INTEGER
PRINT SHL(1, 65)     ' shift count clamped to 63

' Arrays (element-wise; scalar broadcast)
PRINT [1,2,3] \ 2           ' [0 1 1]
PRINT 10 \ [3,4]            ' [3 2]
PRINT [5,6,7,8] MOD [2,3,2,3]  ' [1 0 1 2]
```

> **Notes for users coming from other BASICs:** `/` always returns a floating result; use `\` for integer division. Bitwise operators return **INTEGER** results and truncate floating operands to integers before operating.

## Variables and Assignment

Variables are created on their first use or explicitly with `DIM`.

**`DIM var [AS type] [=Initializer]`**
Declares a variable. The `AS` clause is used for specific types.

```basic
DIM X AS INTEGER
DIM N AS DOUBLE
DIM S AS STRING
DIM D AS DATE
DIM M AS MAP
DIM T AS TENSOR
DIM A AS INTEGER = 2
DIM M AS MAP = {"Name":"Atomi"}
```

**`DIM array[size1, size2, ...]`**
Declares an N-dimensional array with given sizes.

```basic
' A vector with 10 elements (0-9)
DIM A[10]

' A 5x3 matrix
DIM M[5, 3]
```

**Literal Assignment**
Variables can be created by assigning a literal value. This is the modern way to create arrays.

```basic
A = 10
B$ = "hello"
C = TRUE
MyArray = [1, 2, 3, 4] ' Creates an array
EmptyArray = []
```

## Destructuring Assignment

Destructuring allows you to unpack values from an array into individual variables in a single statement. It is a concise way to assign multiple variables at once.

[var1, var2, ...]
Assigns variables from an array expression.

```basic
DIM A, B, C

' Simple assignment from a literal array
[A, B] = [1, 2]
PRINT A, B ' Output: 1 2

' Swapping variables in a single line
[B, A] = [A, B]
PRINT A, B ' Output: 2 1

' Unpacking the result of a function that returns an array
FUNC GetValues()
    RETURN [10, 20, 30]
ENDFUNC

[A, B, C] = GetValues()
PRINT A, B, C ' Output: 10 20 30
```

## Reactive Variables and Assignment

Reactive variables needs to be created explicitly with `DIM`.

**`DIM var AS REACT type]`**
Declares a reactive variable. The `AS REACT ` clause is used for specific types.

**`DIM array[size1, size2, ...] AS REACT INTEGER`**
Declares an N-dimensional array with given sizes as reactive integer variable.

```basic
DIM A AS REACT INTEGER
DIM B AS REACT INTEGER

B = 2
A -> B * 2 'A is reactive and depends on B ' The -> Operator mark this term as reactive

PRINT A 'Prints 4

B = 4

PRINT A 'Prints 8, A is automatically recalculated when B changes
```

## Enumerations

To improve code clarity, you can define named integer constants using `ENUM`.

* **`ENUM name ... ENDENUM`**: Defines a new enumeration. Members are assigned incrementing integer values starting from 0 by default. You can also assign explicit integer values.

```basic
' Using automatic values (Car = 0, Truck = 1, Boat = 2)
ENUM VehicleType
    Car
    Truck
    Boat
ENDENUM

' Using explicit values
ENUM WebStatus
    OK = 200
    NotFound = 404
    Error = 500
ENDENUM

PRINT "Vehicle: ", VehicleType.Car  ' Prints 0
PRINT "Status: ", WebStatus.NotFound ' Prints 404

CurrentVehicle = VehicleType.Truck
IF CurrentVehicle = VehicleType.Truck THEN
    PRINT "It's a truck!"
ENDIF
```

## User-Defined Types (TYPE...ENDTYPE)

You can create your own complex data structures, similar to a `struct` in C or a simple class, using the **`TYPE...ENDTYPE`** block. This allows you to group related variables into a single object.

* **`TYPE TypeName`**: Begins the definition of a new custom type.
* **`MemberName AS Type`**: Inside the block, you declare the data members (properties) of the type. Supported data types include `INTEGER`, `DOUBLE`, `STRING`, `BOOLEAN`, `DATE`, and `MAP`.
* **`SUB` / `FUNC`**: You can define methods (procedures and functions) that operate on the type's data. Inside a method, use the **`THIS`** keyword to refer to the specific object instance the method was called on.
* **`ENDTYPE`**: Ends the type definition.

### Instantiation and Usage

You create an instance of your custom type using the `DIM` command. You can then access its members and call its methods using dot notation (`.`).

### Example

Here is a complete example defining a `Character` type, creating instances of it, and using its members and methods.

```basic
' --- 1. Define the custom data type ---
TYPE Character
    Name AS STRING
    Health AS INTEGER
    Position AS MAP ' UDTs can contain other complex types like Maps

    ' A method to display the character's info
    SUB PrintInfo()
        PRINT "Name: " + THIS.Name
        PRINT "Health: " + THIS.Health
        PRINT "Position: (" + THIS.Position{"x"} + ", " + THIS.Position{"y"} + ")"
    ENDSUB

    ' A method to deal damage
    SUB TakeDamage(damage_amount)
        THIS.Health = THIS.Health - damage_amount
        IF THIS.Health < 0 THEN THIS.Health = 0
    ENDSUB
ENDTYPE

' --- 2. Create instances of the new type ---
DIM Player1 AS Character
DIM Enemy AS Character

' --- 3. Assign values to the members ---
Player1.Name = "Hero"
Player1.Health = 100
Player1.Position = {"x": 10, "y": 20}

Enemy.Name = "Goblin"
Enemy.Health = 30
Enemy.Position = {"x": 50, "y": 60}

' --- 4. Call methods on the instances ---
Player1.PrintInfo()
PRINT ""
Enemy.PrintInfo()

PRINT ""
PRINT Enemy.Name + " takes 12 damage!"
Enemy.TakeDamage(12)
Enemy.PrintInfo()

' You can also create arrays of your custom types
DIM NPCs[2] AS Character
NPCs[0].Name = "Villager"
NPCs[0].Health = 10
NPCs[1].Name = "Guard"
NPCs[1].Health = 80
PRINT ""
PRINT "First NPC is: " + NPCs[0].Name
```

## Operators

jdBasic supports a rich set of operators for arithmetic, string manipulation, and logical comparisons.

### String Operators

Standard arithmetic operators are overloaded for powerful string manipulation.

**`+`**: (Concatenation): Joins two strings.

```basic
"Hello " + "World!" -> "Hello World!"
```

**`-`**: (Replacement): Removes all occurrences of the right string from the left string.

```basic
"abababac" - "ab" -> "ac"
```

**`*`**: (Repetition): Repeats a string a specified number of times.

```basic
"-" * 10 -> "----------"
```

**`/`**: (Slicing): Extracts a substring from the left or right.

```basic
5 / "Welcome" -> "Welco" (Left part)
"Welcome" / 4 -> "come" (Right part)
```

**`-`**: (Unary Split): Splits a string into an array of its characters.

```basic
-"ABC" -> ["A", "B", "C"]
```

### Bitwise Operators / Operations

These operators perform bit-level calculations on numeric values, which are treated as 64-bit integers.

**`BAND`**: (Bitwise AND): 5 BAND 3 -> 1 (%0101 & %0011 = %0001)

**`BOR`**: (Bitwise OR): 5 BOR 3 -> 7 (%0101 | %0011 = %0111)

**`BXOR`**: (Bitwise XOR): 5 BXOR 3 -> 6 (%0101 ^ %0011 = %0110)

**`SHL(value or array, bits to shift) -> number or array`**: Bitwise shift left

**`SHR(value or array, bits to shift) -> number or array`**: Bitwise shift right

### Logical Operators

These operators are used in conditional logic, such as IF statements.

**`AND`**:, **`OR`**:, **`XOR`**:: Standard logical operators. They evaluate both sides of the expression and support element-wise operations on arrays.

**`ANDALSO`**:: A short-circuiting version of AND. If the left side is FALSE, the right side is never evaluated. This is safer for chained conditions.

**`ORELSE`**:: A short-circuiting version of OR. If the left side is TRUE, the right side is never evaluated.

```basic
' This is safe because the second part is never run if MyMap is NULL
IF MyMap <> NULL ANDALSO MAP.EXISTS(MyMap, "key") THEN ...
```

**`IN`**: Evaluates if the left hand value exists in the right hand expression.

```basic
DIM MyMap As MAP 
MyMap = {"name": "jd", "value": 100}
IF "Lall" IN MyMap THEN PRINT "Key exists!"

MyArray = [10, 20, 30]
IF 20 IN MyArray THEN PRINT "Value exists!"
```

## Chained Access Syntax

jdBasic supports a modern, chained syntax for accessing elements within nested data structures, which is especially useful for JSON, COM objects, and Tensors.

**JSON and Map/Array Chaining**
You can chain `{"key"}` and `[index]` accessors to navigate complex objects returned by `JSON.PARSE$`.

```basic
RESPONSE_JSON = JSON.PARSE$(RESPONSE$)

' Access nested data in a single line
AI_MESSAGE$ = RESPONSE_JSON{"choices"}[0]{"message"}{"content"}
```

**COM Chaining**
You can chain property accesses and method calls for COM objects.

```basic
objXL = CREATEOBJECT("Excel.Application")
objXL.Visible = TRUE
wb = objXL.Workbooks.Add()
objXL.ActiveSheet.Cells(1, 1).Value = "Hello from a jdBasic!"
```

**Tensor Gradient Access**
You can access the gradient of a tensor after backpropagation using dot notation.

```basic
' After TENSOR.BACKWARD has been called on a loss
gradient_of_weights = MyModel{"layer1"}{"weights"}.grad
```

## Functional Syntax

**Function chaining (The Pipe Operator **`|>`**)**

```basic
PRINT "--- Processing the Pipe Way (with Pipe Operator) ---"
final_result$ = FILTER_GT_150(SALES_DATA) |> SUM_ARRAY(?) |> FORMAT_RESULT$(?)
PRINT final_result$
```

**Lambda Function chaining (The Pipe Operator **`|>`**)**

```basic
PRINT "--- Processing the Pipe / Lambda Way (with Pipe Operator) ---"
PRINT SELECT(lambda i -> i + 1, IOTA(10))
PRINT SELECT(lambda i -> i + 1, IOTA(10)) |> FILTER(lambda val -> val > 5, ?) |> SELECT(lambda v -> v * 10, ?)
```

### Function as operators

```basic
'AtomiJD Divider
FUNC JD(x,y)
    IF Y = 0 THEN
        RETURN "Infinity"
    ELSE
        RETURN x/y
    ENDIF
ENDFUNC

PRINT 10 JD@ 5, 10 JD@ 0, IOTA(10) jd@ 2, IOTA(10) jd@ IOTA(10)*2, 2 jd@ [1,2,4]
'Should return:
'2       Infinity        [0.5 1 1.5 2 2.5 3 3.5 4 4.5 5] [2 2 2 2 2 2 2 2 2 2]   [2 1 0.5]
```

### Higher Order Function

```basic
print "Using higher order functions"
print

func inc(ab)
    return ab+1
endfunc
func dec(ac)
    return ac-1
endfunc

func apply(fa,cc)
    return fa(cc)
endfunc

print apply(inc@,10) ' Should return 11
print apply(dec@,12) ' Should return 11
```

## Commands

### Console I/O Functions

* **`INPUT [Prompt], variable`**: Prompts the user for a line input. Value is returned in variable
* **`PRINT [Vairable,String,function,...] [;|,] ...`**: Prints the given arguments on screen "," places a tab between arguments ";" for direct concating or at the end of PRINT supresses the Newline

#### `LOCATE row, col`

Moves the text cursor to a specific position on the console screen. The top-left corner is position 1, 1. This is a procedure.

* **`row`**: The row number (1-based).
* **`col`**: The column number (1-based).

```basic
CLS
LOCATE 5, 10
PRINT "This text starts at row 5, column 10."
LOCATE 20, 1
```

-----

#### `GETX() -> Number` and `GETY() -> Number`

These functions return the current horizontal (`GETX`) or vertical (`GETY`) position of the text cursor.

* **Returns**: An integer representing the current column (`GETX`) or row (`GETY`).

```basic
CLS
LOCATE 8, 12
PRINT "Cursor is at: " + GETY() + ", " + GETX()
' Output: Cursor is at: 8, 28
' (The position is after the text has been printed)
```

-----

### User Input Functions

#### `INKEY$() -> String`

Checks the keyboard buffer for a key press. This function is **non-blocking**; it returns immediately, whether a key has been pressed or not.

* **Returns**: A single-character string if a key has been pressed since the last check, otherwise an empty string `""`.

```basic
PRINT "Press 'q' to quit..."
DO
    ' Your main program logic would go here
    
    KeyPressed$ = INKEY$()
    IF KeyPressed$ <> "" THEN
        PRINT "You pressed: " + KeyPressed$
    ENDIF
LOOP UNTIL LCASE$(KeyPressed$) = "q"
```

-----

#### `WAITKEY$() -> String`

Pauses program execution and waits for the user to press any key. This function is **blocking**.

* **Returns**: A single-character string representing the key that was pressed.

```basic
PRINT "Press any key to continue..."
AnyKey$ = WAITKEY$()
PRINT "You pressed '" + AnyKey$ + "'. Program will now resume."
```

-----

### System & Flow Control

* **`CLS`**: Clears the console screen.
* **`COLOR fg, bg`**: Sets the foreground and background colors for text.
* **`CURSOR state`**: Turns the cursor on (`TRUE`) or off (`FALSE`).
* **`GOTO label`**: Jumps execution to a `label:`.
* **`IF condition THEN ... [ELSE ...] ENDIF`**: Conditional execution block. Single-line `IF condition THEN statement` is also supported.
* **`FOR variable TO ... STEP ... NEXT`**: Defines a loop that repeats a specific number of times.
* **`FOR EACH variable IN collection`**: This command provides a simple way to iterate over every element in a collection, such as an Array or a Map.
* **`DO ... LOOP [WHILE/UNTIL condition]`**: Defines a loop that continues as long as a condition is met or until a condition is met.
* **`TRY ... CATCH ... FINALLY ... ENDTRY`**: Structured error handling. See section below.
* **`EXITFUNC`, `EXITDO`, `EXITFOR`**: Exiting functions and loops.
* **`OPTION option$`**: Sets a VM option.
  * `OPTION "NOPAUSE"` disables the ESC/Space break/pause functionality.
  * `OPTION "EXPLICIT"` enforces declarations: variables **must** be introduced with `DIM` before first use (read or write). With **EXPLICITOFF** (default), variables are created on first use and default to `0` (numeric) or `""` (string).
    * **Reads** of undeclared names: error 27 “Undeclared variable”.
    * **Writes** to undeclared names: error 27.
    * `DIM` always declares (even with EXPLICIT on).
    * `FOR` / `FOR EACH` loop variables must be declared when EXPLICIT is on.
    * Disable with `OPTION "NOEXPLICIT"` or `OPTION "EXPLICITOFF"`.
* **`SLEEP milliseconds`**: Pauses execution for a specified duration.
* **`STOP`**: Halts program execution and returns to the `Ready` prompt, preserving variable state. Execution can be continued with `RESUME`.
* **`IMPORT [module]`**: Loads the jdBasic module. Ex. IMPORT MATH imports the file math.jdb
* **`EXPORT MODULE [module]`**: Marks a file as EXPORT for importing with IMPORT
* **`DLLIMPORT [funcfile]`**: Loads the funcfile.dll or funcfile.so as dynmaic library and register all included functions for jdBasic.

### SWITCH...CASE...ENDSWITCH

Provides a clear way to execute one of several blocks of code based on the value of a single expression. It is a more readable alternative to a long series of `IF...ELSEIF` statements.

* **`SWITCH expression`**: Evaluates the `expression` once at the beginning.
* **`CASE value_expression`**: Compares its `value_expression` to the main switch expression. If they are equal, the code block following the `CASE` is executed.
* **`DEFAULT`**: An optional block that executes if no preceding `CASE` statement matches.
* **`ENDSWITCH`**: Marks the end of the `SWITCH` block.

**Note**: The interpreter does not "fall through" cases. Once a `CASE` or `DEFAULT` block is executed, control jumps immediately to the statement following `ENDSWITCH`.

**Example:**

```basic
INPUT "Enter a command (start, stop, pause): ", command$

SWITCH UCASE$(command$)
    CASE "START"
        PRINT "Starting process..."
        ' ... code to start ...

    CASE "STOP"
        PRINT "Stopping process..."
        ' ... code to stop ...

    CASE "PAUSE"
        PRINT "Pausing process."

    DEFAULT
        PRINT "Unknown command: " + command$
ENDSWITCH

PRINT "Switch block finished."
```

### Error Handling (TRY...CATCH)

jdBasic uses a modern, structured error handling system. The old ON ERROR system is no longer supported.

* **`TRY`**: Begins a block of code that is protected.
* **`CATCH`**: If an error occurs inside the TRY block, execution jumps to the CATCH block.
* **`FINALLY`**: This block of code is always executed after the TRY or CATCH block, regardless of whether an error occurred. It's ideal for cleanup tasks like closing files.
* **`ENDTRY`**: Ends the error handling block.

Inside a CATCH block, you can use the following built-in variables:

* **`ERR`**: The numeric error code.
* **`ERL`**: The line number where the error occurred.
* **`ERRMSG$`**: The descriptive error message string.
* **`STACK$`**: The call stack .

```basic
TRY
    PRINT "Opening file..."
    ' Code that might fail, e.g., file operations
    A = 10 / 0
CATCH
    PRINT "An error occurred!"
    PRINT "Code: "; ERR; ", Line: "; ERL; ", Message: "; ERRMSG$
FINALLY
    PRINT "Closing file (this always runs)."
ENDTRY
```

#### `THROW [error_message]`

Manually triggers a runtime error that can be caught by a `TRY...CATCH` block. This is useful for creating custom error conditions in your own functions.

* **`error_message`** (Optional): A string or number that will become the value of `ERRMSG$` in the `CATCH` block. If omitted, a default message is used.

```basic
SUB SetAge(age)
    IF age < 0 THEN
        THROW "Age cannot be negative."
    ENDIF
    ' ... set the age ...
ENDSUB

TRY
    SetAge(-5)
CATCH
    PRINT "Error caught!"
    PRINT "Message: " + ERRMSG$
    PRINT "At line: " + ERL
ENDTRY

' Output:
' Error caught!
' Message: Age cannot be negative.
' At line: 3
```

-----

### Dynamic Code Functions

* **`EXECUTE(code_string$)`**: Compiles and executes a string of jdBasic code at runtime.
* **`EVAL(expression_string$) -> value`**: Compiles and evaluates a string as a single expression, returning its result. This is the functional counterpart to the `EXECUTE` command.

```basic
    X = 10
    Y = 20
    MyFormula$ = "SQR(X^2 + Y^2)"
    PRINT EVAL(MyFormula$) ' Evaluates the formula using current X and Y
```

### Development & Debugging

* **`COMPILE`**: Compiles the source code currently in memory into p-code.
* **`PRETTY [PREVIEW] [STYLE UPPER|VB] [WIDTH n]`**:  Formats loaded source code in-place (unless PREVIEW).
* **`LINT`**: Check the loaded source code for extra LINT conditions like Unclosed block, unused parameter and unused variable.
* **`DUMP [arg]`**: Dumps the p-code of the main program. "GLOBAL" -> Dumps global vars, "LOCAL" -> Dumps local vars, "STACK" -> Dumps the call stack, "REACT", "VARNAME" -> Dumps the react graph., "A MODULE NAME" ->  Dumps the p-code of a loaded module
* **`EDIT`**: Opens the integrated text editor with the current source code.
* **`LIST`**: Lists the current source code in memory to the console.
* **`LOAD "filename"`**: Loads a source file from disk into memory.
* **`SAVE "filename"`**: Saves the source code in memory to a file on disk.
* **`RUN`**: Compiles and runs the program currently in memory.
* **`TRON` / `TROFF`**: Turns instruction tracing on or off.
* **`LOADWS "workspacename"`**: Loads a source file and all variables of an saved workspace from disk into memory.
* **`SAVEWS "workspacename"`**: Saves the source code and variable (Workspace) in memory to a file on disk.
* **`CLEARWS`**: Empties source code, p-code, and all global variables
* **`NEW`**: Empties the source code, compiled p-code, and user-defined function tables.
* **`UNREACT(name$)`**: Remove reactive variable. name$ can be a plain var (e.g., "A"), a dotted member (e.g., "PLAYER.X") or special "ALL"/"*" to clear the entire reactive graph.

### Filesystem

* **`DIR [path]`**: Lists files and directories. Supports wildcards like `*` and `?`.
* **`DIR$([path])->String`**: Lists files and directories and return them as string array. Supports wildcards like `*` and `?`.
* **`CD "path"`**: Changes the current working directory.
* **`PWD`**: Prints the current working directory.
* **`MKDIR "path"`**: Creates a new directory.
* **`KILL "filename"`**: Deletes a file.

### OS Functions

* **`OS.GETOS() -> string$`**: Returns a string identifying the current operating system. Possible values are `"WINDOWS"`, `"LINUX"`, and `"MACOS"`.

    ```basic
    PRINT "Running on: " + OS.GETOS()
    ```

  * **`OS.ARGS() -> array`**: Returns a 1D array of strings containing the command-line arguments passed to the jdBasic interpreter.

    ```basic
    CmdArgs = OS.ARGS()
    PRINT "Launched with " + LEN(CmdArgs) + " arguments."
    ```

* **`OS.EXEC(command$, [args_array$]) -> map`**: Executes an external program or shell command. It returns a `Map` containing two keys: `"output"` (the captured standard output and error text) and `"exit_code"` (the integer return code from the program).

    > **Note on Windows**: Internal commands like `dir` or `cls` are not standalone programs. To run them, you must execute the command shell `cmd.exe` with the `/c` flag, like this: `OS.EXEC("cmd /c dir")`.

    ```basic
    ' On Linux/macOS
    Result = OS.EXEC("ls -l")

    ' On Windows
    Result = OS.EXEC("ping", ["-n", "4", "google.com"])

    PRINT "Exit Code: " + Result{"exit_code"}
    PRINT "--- Output ---"
    PRINT Result{"output"}
    ```

* **`OS.HOSTNAME$() -> STRING"`**: Returns the network hostname of the local machine.
* **`OS.IP$() -> STRING`**: Returns the primary local IPv4 address of the machine.

## Functions

### Map Functions

This suite of functions provides powerful tools for manipulating `Map` data structures.

#### `MAP.EXISTS(map, key$) -> Boolean`

Checks if a given key exists within a map.

* **`map`**: The Map variable to check.
* **`key$`**: The string key to look for.
* **Returns**: `TRUE` if the key is found, otherwise `FALSE`.

```basic
DIM MyMap AS MAP = {"Name": "Atomi", "Version": 1.2}

PRINT MAP.EXISTS(MyMap, "Name")    ' Output: TRUE
PRINT MAP.EXISTS(MyMap, "Version") ' Output: TRUE
PRINT MAP.EXISTS(MyMap, "Author")  ' Output: FALSE
```

-----

#### `MAP.KEYS(map) -> Array`

Retrieves all of the keys from a map and returns them as a 1D array of strings.

* **`map`**: The Map variable from which to extract keys.
* **Returns**: A 1D array containing all the keys from the map. The order is not guaranteed.

```basic
DIM MyMap AS MAP = {"Name": "Atomi", "Version": 1.2, "Active": TRUE}
DIM KeysArray

KeysArray = MAP.KEYS(MyMap)

PRINT "Keys in the map:"
FOR EACH Key IN KeysArray
    PRINT "- " + Key
NEXT
' Possible Output:
' Keys in the map:
' - Active
' - Name
' - Version
```

-----

#### `MAP.VALUES(map) -> Array`

Retrieves all of the values from a map and returns them as a 1D array.

* **`map`**: The Map variable from which to extract values.
* **Returns**: A 1D array containing all the values from the map. The order corresponds to the order from `MAP.KEYS`.

```basic
DIM MyMap AS MAP = {"Name": "Atomi", "Version": 1.2, "Active": TRUE}
DIM ValuesArray

ValuesArray = MAP.VALUES(MyMap)

PRINT "Values in the map:"
FOR EACH Value IN ValuesArray
    PRINT "- " + Value
NEXT
' Possible Output:
' Values in the map:
' - TRUE
' - Atomi
' - 1.2
```

-----

#### `MAP.ITEMS(map) -> Array`

Retrieves all key-value pairs from a map and returns them as a 2D array.

* **`map`**: The Map variable from which to extract items.
* **Returns**: A 2D array where each row is a 2-element array of the form `[key, value]`.

```basic
DIM MyMap AS MAP = {"Name": "Atomi", "Version": 1.2}
DIM ItemsArray, Item

ItemsArray = MAP.ITEMS(MyMap)

PRINT "Items in the map:"
' ItemsArray is now a 2x2 matrix: [["Name", "Atomi"], ["Version", 1.2]]
FOR EACH Item IN ItemsArray
    PRINT "Key: " + Item[0] + ", Value: " + Item[1]
NEXT
' Output:
' Items in the map:
' Key: Name, Value: Atomi
' Key: Version, Value: 1.2
```

-----

#### `MAP.DELETE(map, key$)`

Removes a key-value pair from a map. This is a procedure that modifies the map in place.

* **`map`**: The Map variable to modify.
* **`key$`**: The string key of the item to remove. If the key does not exist, nothing happens.

```basic
DIM MyMap AS MAP = {"Name": "Atomi", "Version": 1.2}
PRINT "Map size before delete: " + MAP.SIZE(MyMap) ' Output: 2

MAP.DELETE MyMap, "Version"

PRINT "Map size after delete: " + MAP.SIZE(MyMap)  ' Output: 1
PRINT MAP.EXISTS(MyMap, "Version")                 ' Output: FALSE
```

-----

#### `MAP.CLEAR(map)`

Removes all key-value pairs from a map, leaving it empty. This is a procedure.

* **`map`**: The Map variable to clear.

```basic
DIM MyMap AS MAP = {"Name": "Atomi", "Version": 1.2}
PRINT "Map size before clear: " + MAP.SIZE(MyMap) ' Output: 2

MAP.CLEAR MyMap

PRINT "Map size after clear: " + MAP.SIZE(MyMap)  ' Output: 0
```

-----

#### `MAP.SIZE(map) -> Number`

Returns the number of key-value pairs in a map.

* **`map`**: The Map variable to measure.
* **Returns**: The integer count of items in the map.

```basic
DIM MyMap AS MAP = {"A": 1, "B": 2, "C": 3}
PRINT MAP.SIZE(MyMap) ' Output: 3

DIM EmptyMap AS MAP
PRINT MAP.SIZE(EmptyMap) ' Output: 0
```

-----

#### `MAP.MERGE(destination_map, source_map)`

Copies all key-value pairs from a source map into a destination map. This is a procedure. If a key from the source map already exists in the destination, its value will be overwritten.

* **`destination_map`**: The Map variable to be modified.
* **`source_map`**: The Map variable to copy items from.

```basic
DIM Map1 AS MAP = {"Name": "Atomi", "Version": 1.0}
DIM Map2 AS MAP = {"Author": "JD", "Version": 1.2}

PRINT "Merging Map2 into Map1..."
MAP.MERGE Map1, Map2

' Map1 is now {"Name": "Atomi", "Version": 1.2, "Author": "JD"}
PRINT FRMV$(MAP.ITEMS(Map1))
' Output:
'    Name Atomi
' Version 1.2
'  Author JD
```

### JSON Functions

* **`JSON.PARSE$(json_string$)`**: Parses a JSON string and returns a special `JsonObject`. This object can be accessed like a `Map` or an `Array`.
* **`JSON.STRINGIFY$(map_or_array)`**: Takes a `Map` or `Array` variable and returns its compact JSON string representation. Ideal for creating API payloads.

### COM Automation Functions

* **`CREATEOBJECT(progID$)`**: Creates a COM Automation object (e.g., "Excel.Application") and returns a `ComObject`.

### String Functions

* **`LEFT$(str$, n)`**, **`RIGHT$(str$, n)`**, **`MID$(str$, start, [len])`**: Extracts parts of a string. The start position is 0 - based.
* **`LEN(expression)`**: Returns the length of the string representation of an expression.
* **`LCASE$(str$)`**, **`UCASE$(str$)`**, **`TRIM$(str$)`**: Manipulates string case and whitespace.
* **`STR$(number)`**, **`VAL(string$)`**: Converts between numbers and strings.
* **`CHR$(ascii_code)`**, **`ASC(char$)`**: Converts between ASCII codes and characters.
* **`INSTR$([start, ]haystack$, needle$)`**: Finds the position of one string within another. Positions are 0-based. Returns -1 if not found.
* **`INSERT$(target_string or array, text_to_insert$ string or array, position or array) -> string or array`**: Inserts a text_to_insert$ in target at position.
* **`SPLIT(source$, delimiter$)`**: Splits a string by a delimiter and returns a 1D array of strings.
* **`FRMV$(array, [format_string$]) -> string$`**: Formats a 1D or 2D array into a string. If format_string$ is provided, it's used to format each row. Otherwise, it creates a right-aligned string matrix.
* **`FORMAT$(format_string$, arg1, arg2, ...) -> string$`**: Formats a string using C++20-style format specifiers.
* **`REPLACE$(source_string or array, find_string$ or array, replace_with_string$ or array) -> string or array)`**: Returs a string where all found find_string$ are preplaced with replace_with_string$.
* **`REVERSE$(string or array) -> string or array`**: Returns a reversed string.

### Math/Arithmetic/Round Functions

* **`SIN(numeric expression or array)`**: Returns the Sinus.
* **`COS(numeric expression or array)`**: Returns the Cosinus.
* **`TAN(numeric expression or array)`**: Returns the Tangens.
* **`SQR(numeric expression or array)`**: Returns the Square Root
* **`RND(numeric expression or array)`**: Returns a random value
* **`LOG(n)`**: Returns the natural logarithm of `n`.
* **`LOG10(n)`**: Returns the base-10 logarithm of `n`.
* **`FAC(numeric expression or array)`**: Factorial Function.
* **`INT(numeric expression or array)`**: Traditional BASIC integer function (floor)
* **`FLOOR(numeric expression or array)`**: Rounds down.
* **`CEIL(numeric expression or array)`**: Rounds up.
* **`ROUND(n, decimals)`**: Rounds the number `n` to the specified number of decimal places.
* **`CLAMP(value_or_array, min, max) -> number or array`**: Clamps the value in the given range.
* **`TRUNC(numeric expression or array)`**: Truncates toward zero.
* **`ABS(numeric expression or array)`**: Standard absolute value function.

### Regular Expression Functions

* **`REGEX.MATCH(pattern$, text$) -> Boolean or Array`**: Checks if the entire `text$` string matches the `pattern$`.
  * Returns `TRUE` or `FALSE` if the pattern has no capture groups.
  * If the pattern contains capture groups `(...)`, it returns a 1D array of the captured substrings upon a successful match, otherwise `FALSE`.
* **`REGEX.FINDALL(pattern$, text$) -> Array`**: Finds all non-overlapping occurrences of `pattern$` in `text$`.
  * Returns a 1D array of all matches found.
  * If the pattern contains capture groups, it returns a 2D array where each row contains the groups for a single match.
* **`REGEX.REPLACE(pattern$, text$, replacement$) -> String`**: Replaces all occurrences of `pattern$` in `text$` with `replacement$`. The replacement string can use backreferences like `$1`, `$2` to insert captured group content.

### Array & Matrix Functions

* **`APPEND(array, value)`**: Appends a scalar value or all elements of another array to a given array, returning a new flat 1D array.
* **`DIFF(array1, array2)`**: Returns a new array containing elements that are in `array1` but not in `array2`.
* **`IOTA(N, [B=1], [S=1]) -> vector`**: Generates a vector of N numbers starting from B with step S. B,S defaults to 1 if not provided.
* **`Reduction (SUM, PRODUCT, MIN, MAX, ANY, ALL)`**: Functions that reduce an array to a single value (e.g., `SUM(my_array)`) or a vector (`SUM(my_array, dimension)`). Dimension is 0 for reduce along rows and 1 for columns.
* **`SCAN(operator, array) -> array`**: Performs a cumulative reduction (scan) along the last axis of an array.
* **`SELECT(function@, array, [row_wise_bool]) -> array`**: Applies a user-defined function to each element of an array, returning a new array with the same dimensions containing the transformed elements. The provided function must accept exactly one argument. If the optional third argument 'row_wise_bool' is TRUE, it applies the function to each row of a 2D matrix instead. The result of a row-wise select is always a 1D array.
* **`FILTER(function@, array) -> array`**: Filters an array by applying a user-defined predicate function to each element. It returns a new 1D array containing only the elements for which the predicate function returned `TRUE`. The provided function must accept one argument and should return a boolean value.
* **`REDUCE(function@, array, [initial_value]) -> value`**: Performs a cumulative reduction on an array using a user-provided function.
* **`TAKE(N, array)`**, **`DROP(N, array)`**: Takes or drops N elements from the beginning (or end if N is negative) of an array.
* **`RESHAPE(array, shape_vector)`**: Creates a new array with new dimensions from the data of a source array.
* **`REVERSE(array)`**: Reverses the elements of an array.
* **`TRANSPOSE(matrix)`**: Transposes a 2D matrix.
* **`MATMUL(matrixA, matrixB)`**: Performs matrix multiplication.
* **`MVLET(matrix, dimension, index, vector) -> matrix`**: Replaces a row or column in a matrix with a vector, returning a new matrix.
* **`INTEGRATE(function@, limits, rule)`**: It parses arguments, performs the coordinate transformation, and loops through the Gauss points to calculate the final sum.
* **`SOLVE(matrix A, vextor b) -> vector_x`**: Solves the linear system Ax = b for the unknown vector x.
* **`INVERT(matrix) -> matrix`**: Computes the inverse of a square matrix.
* **`NORMALIZE(array) -> array`**: Scales the elements of a numeric array to the range [0.0, 1.0].
* **`UNIQUE(array) -> array`**: Returns a new array containing only the unique elements from the source.
* **`SHUFFLE(array) -> array`**: Returns a new array with the elements of the source array randomly shuffled.
* **`FIND_IN_ARRAY(array, value) -> number`**: Finds the first 0-based index of a value in an array. Returns -1 if not found.
* **`DISTANCE(point1_array, point2_array) -> number`**: Calculates the Euclidean distance between two points.
* **`STACK(dimension, array1, array2, ...) -> matrix`**: Stacks 1D vectors into a 2D matrix.
* **`SLICE(matrix, dim, index)`**: Extracts a row (`dim=0`) or column (`dim=1`) from a 2D matrix.
* **`LERP(start, end, alpha) -> number or array`**: Performs linear interpolation.
* **`GRADE(vector)`**: Returns the indices that would sort the vector.
* **`OUTER(vecA, vecB, op$ or funcref)`**: Creates an outer product table using an operator (+, -, \*, /, MOD, \>, \<, =, ^) or a reference to a function (srq@).
* **`ROTATE(array, shift_vector) -> array`**: Cyclically shifts an N-dimensional array.
* **`SHIFT(array, shift_vector, [fill_value]) -> array`**: Non-cyclically shifts an N-dimensional array.
* **`XSORT(array, [dimension], [descending_bool]) -> array`**: A high-performance sort that can operate along a dimension of a 2D matrix.
* **`CONVOLVE(array, kernel, wrap_mode) -> array`**: Performs a 2D convolution of an array with a kernel.
* **`PLACE(destination_array, source_array, coordinates_vector) -> array`**: Places a source array into a destination array at a given coordinate.

### File I/O Functions

* **`TXTREADER$(filename$)`**: Reads an entire text file into a single string variable.
* **`TXTWRITER filename$, content$`**: Writes a string variable to a text file.
* **`CSVREADER(filename$, [delimiter$], [has_header])`**: Reads a CSV file into a 2D array of numbers.
* **`CSVWRITER filename$, array, [delimiter$], [header_array]`**: Writes a 2D array to a CSV file.

### System and Time Functions

* **`GETENV$(var_name$)`**: Gets the value of a system environment variable.
* **`SETLOCALE("locale_string")`**: Sets the locale for number formatting (e.g., "en-US" or "de-DE").
* **`TICK()`**: Returns the number of milliseconds since the program started.
* **`DATE$` / `TIME$`**: Returns the current system date/time as a string.
* **`NOW()`**: Returns a `DateTime` object for the current moment.
* **`DATEADD(part$, num, date)`**: Adds an interval to a `DateTime` object. Interval part$: D,H,N,S
* **`DATEDIFF(part$, date1, date2) -> number`**: Calculates the difference between two dates in the specified unit. Interval part$: D,H,N,S
* **`CVDATE(date_string$)`**: Converts a string ("YYYY-MM-DD") to a `DateTime` object.

#### `HELP [topic$]` and `HELP$()`

Provides access to the built-in help system.

* **`HELP`** (Procedure):
  * Without arguments, `HELP` lists all available help topics.
  * With a `topic$` argument, it prints the detailed help for that specific command or function.
* **`HELP$()`** (Function):
  * Returns a 1D array of strings, where each element is an available help topic.

```basic
' Example 1: List all topics
HELP

' Example 2: Get help for a specific command
HELP "PRINT"

' Example 3: Use HELP$() to get the list as data
DIM Topics AS ARRAY
Topics = HELP$()
PRINT "There are " + LEN(Topics) + " help topics available."
```

### HTTP Functions

* **`HTTP.GET$(url$)`**: Performs an HTTP GET request and returns the response body as a string.
* **`HTTP.POST$(url$, data$, contentType$)`**: Performs an HTTP POST request with the given data and content type, returning the response body.
* **`HTTP.PUT$(url$, data$, contentType$)`**: Performs an HTTP PUT request.
* **`HTTP.SETHEADER(name$, value$)`**: Sets a custom header for subsequent HTTP requests.
* **`HTTP.CLEARHEADERS()`**: Clears all custom HTTP headers.
* **`HTTP.STATUSCODE()`**: Returns the HTTP status code from the last request.
* **`HTTP.SERVER.START(port)`**: Starts a non-blocking HTTP server on the specified port, returning `TRUE` on success.
* **`HTTP.SERVER.STOP`**: Stops the running HTTP server.
* **`HTTP.SERVER.ON_GET(path$, function_name$)`**: Registers a `jdBasic` function to handle incoming `GET` requests for a specific URL path.
* **`HTTP.SERVER.ON_POST(path$, function_name$)`**: Registers a `jdBasic` function to handle incoming `POST` requests for a specific URL path.

### Building a Web Server & API

The built-in HTTP server allows `jdBasic` to serve websites and create simple JSON APIs. The server runs in the background, handling requests by calling user-defined `jdBasic` functions.

Handler functions receive one argument: a `Map` containing details about the incoming request (e.g., path, headers, body). The `RETURN` value of the function is sent back to the client as the response.

* If the function returns a `Map`, it is automatically converted to a JSON string with `Content-Type: application/json`.
* If the function returns a `String`, it is sent with `Content-Type: text/html`.

<!-- end list -->

```basic
' --- Web Server and API Example ---
ExitMe = FALSE

SUB HandleKeys(data)
    ExitMe = TRUE
ENDSUB

ON "KEYDOWN" CALL HandleKeys
' Handler for the main web page at "/"
FUNC HandleWebsite(request)
    ' Build an HTML string to return as the website
    html$ = "<!DOCTYPE html><html><head><title>My jdBasic Site</title></head>"
    html$ = html$ + "<body><h1>Welcome!</h1><p>This page is served by jdBasic.</p>"
    html$ = html$ + "</body></html>"

    ' Return the HTML string. The server will send it with Content-Type: text/html.
    RETURN html$
ENDFUNC

' Handler for a JSON API endpoint at "/api/info"
FUNC HandleApi(request)
    ' Create a response map
    response_map = {
        "server_time": NOW(),
        "status": "ok",
        "request_path": request{"path"}
    }
    
    ' Return the map. The server will convert it to a JSON string.
    RETURN response_map
ENDFUNC

' --- Main Program ---
PRINT "Setting up HTTP server..."

HTTP.SERVER.ON_GET "/", "HandleWebsite"
HTTP.SERVER.ON_POST "/api/info", "HandleApi"

IF HTTP.SERVER.START(8080) THEN
    PRINT "Server is running at http://localhost:8080"
    PRINT "Press any key to stop."
    
    ' Loop to keep the main program alive while the server runs in the background
    DO
        SLEEP 100
    LOOP UNTIL ExitMe = TRUE
ELSE
    PRINT "Error starting server: "; ERRMSG$
ENDIF

PRINT "Shutting down server..."
HTTP.SERVER.STOP
```

### Graphics and Multimedia Functions

#### Graphics

* **`SCREEN width, height, [title$], scalefactor`**: Initializes a graphics window of the specified size.
* **`SCREENFLIP`**: Updates the screen to show all drawing operations performed since the last flip.
* **`DRAWCOLOR r, g, b`**: Sets the current drawing color using RGB values (0-255).
* **`SETFONT filename$, size`**: Sets the current font to filename$ and size.
* **`PSET x, y, [r, g, b] OR PSET matrix, [colors]`**: Draws a single pixel at the specified coordinates. Can also take a matrix of points.
* **`LINE x1, y1, x2, y2, [r, g, b] OR LINE matrix, [colors]`**: Draws a line between two points. Can also take a matrix of lines.
* **`RECT x, y, w, h, [fill], [r, g, b] OR RECT matrix, [fill], [colors]`**: Draws a rectangle. `fill` is a boolean. Can also take a matrix of rectangles.
* **`CIRCLE x, y, r, [fill], [r, g, b] OR CIRCLE matrix, [fill], [colors]`**: Draws a circle. Can also take a matrix of circles.
* **`ELLIPSE cx, cy, rx, ry, [fill], [r, g, b] OR ELLIPSE matrix, [fill], [colors]`**: Draws a ellipse. Can also take a matrix of circles.
* **`ROUNDED_RECT x, y, w, h, radius, [fill], [r, g, b] OR ROUNDED_RECT matrix, [fill], [colors]`**: Draws a rounded rect. Can also take a matrix of circles.
* **`CIRCLE_SECTOR cx, cy, radius, start_angle, end_angle, [fill], [r, g, b] OR CIRCLE_SECTOR matrix, [fill], [colors]`**: Draws a circle sector. Can also take a matrix of circles.
* **`TEXT x, y, content$, [r, g, b]`**: Draws a string of text on the graphics screen.
* **`PLOTRAW x, y, matrix, [scaleX, scaleY]`**: Draws a matrix of color values directly to the screen at a given position and scale.

Here is the new section for your `languages.md` file, formatted to match the existing documentation style.

### ImGui Functions

This suite of functions provides immediate-mode GUI capabilities using the Dear ImGui library. These functions allow you to create windows, inputs, plots, and complex layouts directly from your code.

#### Windows & Containers

* **`GUI.BEGIN(title$, [x, y, w, h], [p_open], [flags]) -> boolean`**: Starts a new window. Returns `TRUE` if the window is visible (not collapsed). If a `p_open` variable is passed, it returns the new state of that variable (handling the close 'X' button).
* **`GUI.END`**: Ends the current window. Must be called for every `GUI.BEGIN`.
* **`GUI.BEGIN_CHILD(id$, [width, height], [border], [flags])`**: Starts a scrolling child region.
* **`GUI.END_CHILD`**: Ends a child region.
* **`GUI.COLLAPSING_HEADER(label$, [visible_bool]) -> boolean`**: Displays a collapsible header. Returns `TRUE` if the header is currently open.
* **`GUI.TREE_NODE(label$) -> boolean`**: Displays a tree node. Returns `TRUE` if the node is open. If open, you must call `GUI.TREE_POP` after rendering children.
* **`GUI.TREE_POP`**: Ends a tree node.

#### Layout

* **`GUI.SAME_LINE`**: Places the next widget on the same horizontal line as the previous one.
* **`GUI.SEPARATOR`**: Draws a horizontal line separator.
* **`GUI.SEPARATOR_TEXT(text$)`**: Draws a separator with centered text.
* **`GUI.DUMMY(width, height)`**: Adds an invisible spacer of the specified size.

#### Basic Widgets

* **`GUI.TEXT(text$)`**: Displays text in the UI.
* **`GUI.BUTTON(label$, [width, height]) -> boolean`**: Displays a button. Returns `TRUE` if clicked.
* **`GUI.CHECKBOX(label$, checked_bool) -> boolean`**: Displays a checkbox. Returns the new boolean state.
* **`GUI.RADIO(label$, current_value, button_value) -> value`**: Displays a radio button. Returns `button_value` if selected, otherwise returns `current_value`.
* **`GUI.SLIDER(label$, value, min, max) -> number`**: Displays a slider. Returns the new value.
* **`GUI.PROGRESS(fraction, [overlay_text$])`**: Displays a progress bar (0.0 to 1.0).
* **`GUI.COLOR(label$, color_array) -> boolean`**: Displays a color picker. Updates the array (`[r, g, b, a]`) in place. Returns `TRUE` if the color changed.
* **`GUI.HELPMARKER(text$)`**: Displays a `(?)` icon that shows a tooltip when hovered.
* **`GUI.TOOLTIP(text$)`**: Sets a tooltip for the item immediately preceding this call.

#### Input Widgets

* **`GUI.INPUT(label$, current_value$) -> string$`**: Displays a text input field. Returns the new string value.
* **`GUI.INPUT_INT(label$, value) -> number`**: Displays an integer input field. Returns the new value.
* **`GUI.INPUT_DOUBLE(label$, value) -> number`**: Displays a double-precision input field. Returns the new value.
* **`GUI.COMBO(label$, current_index, items_array) -> number`**: Displays a combo box (dropdown). Returns the new selected index.
* **`GUI.LISTBOX(label$, current_index, items_array, [height]) -> number`**: Displays a selectable list box. Returns the new selected index.
* **`GUI.SELECTABLE(label$, [selected], [flags], [w], [h]) -> boolean`**: Displays a selectable item (row), useful for custom lists. Returns `TRUE` if clicked.

#### Menus

* **`GUI.BEGIN_MAIN_MENU_BAR() -> boolean`**: Creates a full-screen menu bar at the top of the viewport.
* **`GUI.END_MAIN_MENU_BAR`**: Ends the main menu bar.
* **`GUI.BEGIN_MENU_BAR() -> boolean`**: Creates a menu bar attached to the current window.
* **`GUI.END_MENU_BAR`**: Ends the window menu bar.
* **`GUI.BEGIN_MENU(label$, [enabled]) -> boolean`**: Creates a sub-menu (e.g., "File"). Returns `TRUE` if open.
* **`GUI.END_MENU`**: Ends a menu.
* **`GUI.MENU_ITEM(label$, [shortcut], [selected], [enabled]) -> boolean`**: Creates a menu item. Returns `TRUE` if activated.

#### Popups & Modals

* **`GUI.OPEN_POPUP(str_id$)`**: Marks a popup identifier as open.
* **`GUI.BEGIN_POPUP(str_id$) -> boolean`**: Starts a popup window. Returns `TRUE` if open.
* **`GUI.BEGIN_POPUP_MODAL(name$, [p_open]) -> boolean`**: Starts a modal popup that blocks interaction behind it.
* **`GUI.END_POPUP`**: Ends a popup.
* **`GUI.CLOSE_CURRENT_POPUP`**: Manually closes the currently active popup.

#### Tabs

* **`GUI.BEGIN_TAB_BAR(str_id$, [flags]) -> boolean`**: Starts a tab bar container. Returns `TRUE` if successful.
* **`GUI.END_TAB_BAR`**: Ends a tab bar.
* **`GUI.BEGIN_TAB_ITEM(label$, [p_open], [flags]) -> boolean`**: Starts a tab item. Returns `TRUE` if the tab is currently selected/active.
* **`GUI.END_TAB_ITEM`**: Ends a tab item.

#### Plots & Data Visualization

* **`GUI.PLOT_LINES(label$, values_array, [overlay], [min], [max])`**: Draws a simple line chart from an array of numbers.
* **`GUI.PLOT_HISTOGRAM(label$, values_array, [overlay], [min], [max])`**: Draws a histogram chart from an array of numbers.

#### Utilities & Styling

* **`GUI.THEME(theme_name$)`**: Sets the global UI theme. Options: `"DARK"`, `"LIGHT"`, `"CLASSIC"`.
* **`GUI.FLAG(flag_name$) -> number`**: Returns the integer value of an ImGui flag (e.g., `"MENUBAR"`, `"NO_RESIZE"`, `"NO_TITLEBAR"`).
* **`GUI.PUSH_ID(id)`**: Pushes an integer or string ID to the stack to prevent ID collisions in loops.
* **`GUI.POP_ID`**: Pops the last ID from the stack.
* **`GUI.SHOW_FONT_ATLAS`**: Opens the built-in ImGui font visualizer for debugging.

#### Sound

* **`SOUND.INIT()`**: Initializes the audio system. Must be called before other sound functions.
* **`SOUND.VOICE track, waveform$, attack, decay, sustain, release`**: Configures the ADSR envelope and waveform for a sound track.
* **`SOUND.PLAY track, frequency`**: Plays a note at a specific frequency on the given track.
* **`SOUND.RELEASE track`**: Starts the release phase of the note on the given track.
* **`SOUND.STOP track`**: Immediately stops the note on the given track.
* **`SFX.LOAD id, "filepath.wav"`**: Loads a WAV file to slot id.
* **`SFX.PLAY id`**: Plays a WAV file with slot id.
* **`MUSIC.PLAY id`**: Plays a WAV file as background music in slot id.
* **`MUSIC.STOP`**: Immediately stops the background music.

#### Sprites and Maps

* **`SPRITE.LOAD type_id, "filename.png"`**: Loads a sprite image from a file and assigns it a type ID.
* **`SPRITE.LOAD_ASEPRITE type_id, "filename.json"`**: Loads a sprite sheet and animation data from an Aseprite export.
* **`SPRITE.CREATE(type_id, x, y)`**: Creates an instance of a sprite at a given position and returns its unique instance ID.
* **`SPRITE.MOVE instance_id, x, y`**: Moves a sprite instance to a new position.
* **`SPRITE.SET_VELOCITY instance_id, vx, vy`**: Sets the velocity for a sprite instance for use with `SPRITE.UPDATE`.
* **`SPRITE.DELETE instance_id`**: Removes a sprite instance.
* **`SPRITE.SET_ANIMATION instance_id, "animation_name$"`**: Sets the current animation for a sprite instance.
* **`SPRITE.SET_FLIP instance_id, flip_boolean`**: Sets the horizontal flip state of a sprite.
* **`SPRITE.UPDATE`**: Updates the positions of all sprites based on their velocities.
* **`SPRITE.DRAW_ALL wx,wy`**: Draws all active sprite instances to the screen. If wx,wy is set it renderes as world coodinates.
* **`SPRITE.GET_X(instance_id)` / `SPRITE.GET_Y(instance_id)`**: Returns the X or Y coordinate of a sprite instance.
* **`SPRITE.COLLISION(id1, id2)`**: Returns `TRUE` if the bounding boxes of two sprite instances are colliding.
* **`SPRITE.CREATE_GROUP() -> group_id`**: Creates a new, empty sprite group.
* **`SPRITE.COLLISION_GROUPS(group_id1, group_id2) -> array[hit_id1, hit_id2]`**: Checks for collision between two groups of sprites.
* **`SPRITE.COLLISION_GROUP(instance_id, group_id) -> hit_instance_id`**: Checks for collision between a single sprite and a group.
* **`MAP.LOAD "map_name", "filename.json"`**: Loads a Tiled map file.
* **`MAP.DRAW_LAYER "map_name", "layer_name", [world_offset_x], [world_offset_y]`**: Draws a specific tile layer from a loaded map.
* **`MAP.GET_OBJECTS("map_name", "object_type") -> Array of Objects`**: Retrieves all objects of a certain type from an object layer.
* **`MAP.COLLIDES(sprite_id, "map_name", "layer_name") -> boolean`**: Checks if a sprite is colliding with any solid tile on a given layer.
* **`MAP.GET_TILE_ID "mapname", "layername", tileX, tileY`**: Returns the tile id from the given position.
* **`MAP.DRAW_DEBUG_COLLISIONS player_id, "map", "layer"`**: For debug purpose. Draws a rect around the tile near x,y. CAM_X and CAM_Y must be set.

#### Turtle
  
* **`TURTLE.FORWARD distance`**: Moves the turte forward with the distance at the given angle.
* **`TURTLE.BACKWARD distance`**: Moves the turte backward with the distance at the given angle.
* **`TURTLE.LEFT degrees`**: Subtract degrees to the turles angle.
* **`TURTLE.RIGHT degrees`**: Adds degrees to the turles angle.
* **`TURTLE.PENUP`**: Stop drawing while moving.
* **`TURTLE.PENDOWN`**: Begins drawing while moving.
* **`TURTLE.SETPOS x, y`**: Set the turle position to x,y
* **`TURTLE.SETHEADING degrees`**: Set the turtles angle to the degrees
* **`TURTLE.HOME`**: Move the turtles position to the center of the canVas
* **`TURTLE.DRAW`**: Redraws the entire path the turtle has taken so far.
* **`TURTLE.CLEAR`**: Clears the turtle's path memory. Does not clear the screen.
* **`TURTLE.SET_COLOR r, g, b`**: Set the turtles draw color to r,g,b

### Type Functions

* **`TYPEOF(AnyVar)`**: Returns the type of an object as string.

### Thread Functions

This section describes functions for low-level, background-threaded tasks, distinct from the `ASYNC`/`AWAIT` pattern. A function launched with `THREAD` will run in parallel.

* **`THREAD.ISDONE(handle)`**: Returns `TRUE` if the background thread associated with the handle has finished its execution.
* **`THREAD.GETRESULT(handle)`**: Waits for the thread to complete and returns its result. This is a blocking call.

### Async Functions

* **`ASYNC FUNC FUNCTIONNAME(args)`**: Marks a function as asynchronius.
* **`AWAIT task`**: Waits for the given task to be completed and returns the result of the function.

<!-- end list -->

```basic
' This function simulates a "download" that takes some time.
ASYNC FUNC DOWNLOADFILE(url$, duration)
  PRINT "  [Task 1] Starting download from "; url$
  ' Simulate work by looping
  FOR i = 1 TO duration
    PRINT "  [Task 1] ... downloading chunk "; i; " of "; duration; " ..."
  NEXT i
  PRINT "  [Task 1] Download finished."
  RETURN "Download of " + url$ + " successful."
ENDFUNC

' This function simulates a "data processing" job.
ASYNC FUNC PROCESSDATA(dataset$, duration)
  PRINT "    [Task 2] Starting to process data from "; dataset$
  ' Simulate work by looping
  FOR i = 1 TO duration
    PRINT "    [Task 2] ... processing record block "; i; " of "; duration; " ..."
  NEXT i
  PRINT "    [Task 2] Data processing finished."
  RETURN "Processed " + dataset$ + " and found 42 insights."
ENDFUNC

task1 = DOWNLOADFILE("https://example.com/data.zip", 5)
task2 = PROCESSDATA("some_large_dataset.csv", 3)

PRINT "Main: Now doing other work while tasks run in the background."
FOR i = 1 TO 4
  PRINT "Main: ... processing main task step "; i; " ..."
  ' In a real program, you could do other things here,
  ' like updating the UI or handling user input.
NEXT i
PRINT "Main: Finished with other work."

PRINT "Main: Now waiting for Task 1 to complete..."
result1 = AWAIT task1
PRINT "Main: Task 1 finished with result: '"; result1; "'"
PRINT

PRINT "Main: Now waiting for Task 2 to complete..."
result2 = AWAIT task2
PRINT "Main: Task 2 finished with result: '"; result2; "'"
PRINT
```

### Tensor & AI Functions

This suite of functions provides the building blocks for creating and training neural networks directly within jdBasic. The core component is the `Tensor` data type, which supports automatic differentiation.

#### Core & Conversion

* **`TENSOR.FROM(array)`**: Converts a standard `Array` into a `Tensor`, enabling it to be used in the neural network graph.
* **`TENSOR.TOARRAY(tensor)`**: Converts a `Tensor` back into a standard `Array`, allowing you to inspect its data or use it with other array functions.
* **Tensor Operations**: Standard arithmetic operators are overloaded to work element-wise with Tensors and support backpropagation.
  * **`+`**, **`-`**, **`*`**: Perform tensor addition, subtraction, and element-wise multiplication. Broadcasting rules (e.g., matrix + vector) apply.
  * **`/`**, **`^`**: Perform division and power operations between a tensor and a scalar.
* **`tensor.grad`**: Accesses the gradient of a tensor after `TENSOR.BACKWARD` has been called. This is not a function but property access using dot notation.

#### Model Building

* **`TENSOR.CREATE_LAYER(type$, options_map)`**: A factory for creating neural network layers. It returns a `Map` containing the initialized weight and bias tensors.
  * `type$`: "DENSE", "EMBEDDING", "LAYER\_NORM", "ATTENTION".
  * `options_map`: A `Map` with layer-specific parameters.
  * **DENSE**: `{"input_size": ..., "units": ...}`
  * **EMBEDDING**: `{"vocab_size": ..., "embedding_dim": ...}`
  * **LAYER\_NORM**: `{"dim": ...}`
  * **ATTENTION**: `{"embedding_dim": ...}`
* **`TENSOR.CREATE_OPTIMIZER(type$, options_map)`**: A factory for creating optimizers.
  * `type$`: "SGD".
  * `options_map`: `{"learning_rate": ...}`

#### Training & I/O

* **`TENSOR.BACKWARD loss_tensor`**: A procedure that performs backpropagation on the computational graph, starting from the final loss tensor. It computes the gradients for all parent tensors.
* **`TENSOR.UPDATE(model_map, optimizer_map)`**: Updates the model's parameters (weights and biases) using the gradients calculated by `TENSOR.BACKWARD` and the specified optimizer's learning rate. Returns the updated model map.
* **`TENSOR.SAVEMODEL model_map, "filename.json"`**: Saves a model (a `Map` containing parameter tensors) to a human-readable JSON file.
* **`TENSOR.LOADMODEL("filename.json")`**: Loads a model from a JSON file, restoring the tensors and model structure.

#### Layers & Activations

* **`TENSOR.SIGMOID(tensor)`**: Applies the element-wise sigmoid activation function.
* **`TENSOR.RELU(tensor)`**: Applies the element-wise Rectified Linear Unit (ReLU) activation function.
* **`TENSOR.SOFTMAX(tensor, [is_causal])`**: Applies the softmax function to the last dimension of the input tensor. If the optional `is_causal` argument is `TRUE`, it applies a causal mask for use in autoregressive models.
* **`TENSOR.LAYERNORM(input, gain, bias)`**: Applies layer normalization to the input tensor.
* **`TENSOR.CONV2D(input, kernel, bias, stride, padding)`**: Performs a 2D convolution operation, essential for CNNs.
* **`TENSOR.MAXPOOL2D(input, pool_size, stride)`**: Performs a 2D max pooling operation.

#### LLM & Loss Helpers

* **`TENSOR.CROSS_ENTROPY_LOSS(logits, actual_one_hot)`**: Calculates the cross-entropy loss between the model's raw output (logits) and the true target labels (in one-hot encoded format).
* **`TENSOR.TOKENIZE(text$, vocab_map)`**: Converts a string into a 1D `Array` of integer token IDs based on the provided vocabulary map.
* **`TENSOR.POSITIONAL_ENCODING(seq_len, d_model)`**: Generates a sinusoidal positional encoding `Tensor`, used to give the model information about the position of tokens in a sequence.

## The Integrated Editor

The `EDIT` command launches a simple, built-in text editor.

### Keyboard Shortcuts

* **Arrow Keys, PageUp, PageDown**: Navigate text.
* **`Ctrl+X`**: Exit the editor.
* **`Ctrl+S`**: Save the current file. If the file is unnamed, you will be prompted for a name.
* **`Ctrl+F`**: Find text. You will be prompted for a search query.
* **`F3`**: Find the next occurrence of the last search query.
* **`Ctrl+G`**: Go to a specific line number.