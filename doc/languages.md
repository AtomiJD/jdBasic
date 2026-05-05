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
* **`E`**: Euler's number ($e \\approx 2.718281828459045$).
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
| `BAND` `BOR` `BXOR` `BNOT` `SHL` `SHR` | numeric                    | INTEGER | Operands coerced to 64-bit integer (trunc toward 0 first). `BNOT` is unary prefix. Shift counts are clamped to `0..63`. |

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

## STATIC Local Variables

`STATIC DIM` inside a `FUNC` or `SUB` declares a per-function persistent
slot. The initializer runs on the first execution of the line; on every
subsequent call the slot keeps its previous value. Storage hangs off the
function definition (per-VM), so all recursive depths and cross-module
callers share the same slot.

**`STATIC DIM name [AS type] [= initializer]`** (inside FUNC/SUB only)

```basic
FUNC counter() AS INTEGER
    STATIC DIM n AS INTEGER = 0     ' init runs once on first call
    n = n + 1
    RETURN n
ENDFUNC

PRINT counter()    ' 1
PRINT counter()    ' 2
PRINT counter()    ' 3
```

* Allowed types: `INTEGER`/`LONG`, `DOUBLE`/`SINGLE`, `STRING`, `BOOLEAN`,
  `ARRAY`, `MAP`, plus the usual integer aliases. Initializer can be any
  expression — literals, calls, array/map literals.
* Recursion shares the slot. A `STATIC DIM hits = 0` increment in a
  recursive `FUNC` accumulates across every depth in one call chain and
  persists into the next call.
* Cross-module: a `STATIC DIM` in a module's exported function resolves
  to the same slot regardless of which file calls it (storage is keyed
  on the function identity, not the call site).
* Top-level `STATIC DIM` is a parse error — STATIC has meaning only
  inside a function body.
* Recursive `STATIC` initializer (the init expression calls back into the
  enclosing function): the guard is set **before** the initializer runs,
  so the inner call sees a default-zero slot rather than re-entering the
  init block. Don't write initializers that depend on a fully-resolved
  STATIC slot of the same function.
* `STATIC` slots are private to their function — they're not reactive
  (`REACT` does not track them) and they aren't currently persisted by
  `SAVEWS` / `LOADWS`. A workspace reload starts every static fresh.

## Constants

The `CONST` statement declares a named constant whose value cannot be changed after initialization. Constants are always global, even when declared inside a function.

**`CONST name = expression`**

```basic
CONST MAX_HEALTH = 100
CONST GREETING$ = "Hello, World!"
CONST TAX_RATE = 0.19
CONST GRID_SIZE = 8 * 8
```

Any attempt to reassign a constant will cause a runtime error:

```basic
CONST SPEED = 5
SPEED = 10          ' Runtime error: Cannot assign to constant 'SPEED'
```

Constants are case-insensitive, just like all jdBasic variables:

```basic
CONST myVal = 42
myval = 99          ' Runtime error: Cannot assign to constant 'MYVAL'
```

Constants can reference other constants and use any valid expression:

```basic
CONST RADIUS = 10
CONST AREA = PI * RADIUS ^ 2
```

> **Note:** The built-in constants `PI`, `E`, and `VBNEWLINE` (see [Built-in Constants](#built-in-constants)) are also protected against reassignment using the same mechanism.

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
Declares a reactive variable. The `AS REACT` clause is used for specific types.

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

For dynamic programmatic use, the following functions let you register/unregister reactive bindings without using the `->` operator:

* **`REACT_BIND(var_name$, formula$)`**: Programmatically binds a global variable to a formula string. Equivalent to writing `var -> formula` in source.
* **`UNREACT(var_name$)`**: Removes a previously established reactive binding.

## Array Slicing and Vectorized Assignment

jdBasic supports powerful slicing and vectorized assignments for arrays and matrices, allowing you to manipulate sub-sections or broadcast scalars across dimensions.

```basic
DIM A = RESHAPE(IOTA(8), [2,2,2])

' 1. Deep Slicing
PRINT A[1]       ' Extracts a 2D slice
PRINT A[1][0]    ' Extracts a 1D slice (vector)

' 2. Scalar Broadcasting
A[0] = 99        ' Replaces all elements in the first 2D slice with 99

' 3. Cyclic Vectorized Assignment
A[1] = [42, 84]  ' Assigns the vector [42, 84] cyclically across the target slice
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
* **`SUB INIT([params])`**: Optional **constructor**. Runs automatically after the implicit field defaults whenever the type is instantiated with `DIM x AS TypeName(args)`. If `INIT` takes no parameters, it also runs for the bare form `DIM x AS TypeName`. If `INIT` takes parameters, the bare form leaves the object default-initialised so legacy code that calls `obj.INIT(args)` manually keeps working.
* **`SUB DISPOSE()`**: Optional **destructor**. Always parameter-less. The interpreter runs `DISPOSE` automatically when the object loses its last reference (going out of scope, being re-assigned, last copy released). The native compiler runs `DISPOSE` when a tracked local goes out of scope (function return / end of `main`); it does *not* fire on re-assignment, since native UDTs are not refcounted.
* **`ENDTYPE`**: Ends the type definition.

### Instantiation and Usage

You create an instance of your custom type using the `DIM` command. You can then access its members and call its methods using dot notation (`.`).

For arrays of UDTs, constructor arguments are supplied as **vectors** of the same length as the array shape: each slot `i` receives `(vec1[i], vec2[i], …)` and `INIT` is invoked once per slot.

```basic
DIM hero AS Player("Hero", 100)                                  ' scalar
DIM npc[3] AS T_NPC(["Monster", "Trader", "Quest"], [100, 20, 10]) ' array, vectorised
```

### Constructor / Destructor example

```basic
TYPE FileLogger
    Path  AS STRING
    Open  AS BOOLEAN

    SUB INIT(p AS STRING)
        THIS.Path = p
        THIS.Open = TRUE
        PRINT "open  " + p
    ENDSUB

    SUB DISPOSE()
        IF THIS.Open THEN
            PRINT "close " + THIS.Path
            THIS.Open = FALSE
        ENDIF
    ENDSUB
ENDTYPE

SUB use_it()
    DIM log AS FileLogger("trace.txt")
    PRINT "doing work"
ENDSUB

use_it()
' Output:
'   open  trace.txt
'   doing work
'   close trace.txt
```

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

**`BNOT`**: (Bitwise NOT, unary prefix): `BNOT 0` -> -1, `BNOT 5` -> -6, `(BNOT $AA) BAND $FF` -> $55. Vectorises element-wise over arrays.

**`SHL`** — Bitwise shift left. Both **infix** and **function** form work:
* `1 SHL 8` -> 256
* `SHL(1, 8)` -> 256 (also vectorises over arrays as the function form)

**`SHR`** — Bitwise shift right (arithmetic, sign-preserving):
* `256 SHR 4` -> 16
* `-8 SHR 1` -> -4
* `SHR(value, n)` — function form also accepts arrays

Shift precedence is **looser than addition**, **tighter than comparison and BAND/BOR/BXOR**. So `1 SHL 2 + 1` parses as `1 SHL (2 + 1)` = 8, and `5 BAND 3 SHL 1` is `5 BAND (3 SHL 1)` = 4. Use parentheses if in doubt.

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

**Lambda Function Closures with USE()**

```basic
FUNC MakeAdder(base_value)
    ' The returned Lambda captures 'base_value' in its backpack.
    ' Even after MakeAdder finishes, the lambda remembers it.
    RETURN LAMBDA USE(base_value) x -> x + base_value
ENDFUNC

Add5 = MakeAdder(5)
Add100 = MakeAdder(100)

PRINT "5 + 10 = "; Add5(10)      ' Output: 15
PRINT "100 + 10 = "; Add100(10)  ' Output: 110
PRINT
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
* **`CONTINUEFOR`, `CONTINUEDO`, `CONTINUELOOP**`: Skips the rest of the current loop iteration and continues with the next one.
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
* **`DECLARE FUNC name LIB "lib" ALIAS "export_name" (params) AS rettype`**: Declares a foreign function from a shared library so it can be called from jdBasic. See the **Foreign Function Interface** section below.
* **`CLIPBOARD.SET text$`**: Sets the system clipboard text.
* **`CLIPBOARD.GET$() -> string$`**: Returns the text currently in the system clipboard.
* **`END`**: Immediately terminates the program execution (unlike `STOP` which pauses for debugging).
* **`YIELD`**: Pauses execution and yields to the host environment's event loop for one frame (critical for Web/WASM environments to prevent freezing).
* **`ON event_name$ func_name$`**: Registers a subroutine to handle system or custom events. The handler must be a `SUB`. For `ON "ERROR"`, the function must accept exactly one argument.
* **`RAISEEVENT event_name$, [event_data]`**: Triggers a custom event, passing optional data to the registered event handler.

### SWITCH...CASE...ENDSWITCH

Provides a clear way to execute one of several blocks of code based on the value of a single expression. It is a more readable alternative to a long series of `IF...ELSEIF` statements.

* **`SWITCH expression`**: Evaluates the `expression` once at the beginning.
* **`CASE value_expression[, value_expression]...`**: Compares each value to the main switch expression. If any match, the body runs. Each value can also be a range `lo TO hi` (inclusive both ends), and you may mix singletons and ranges in one `CASE`: `CASE 1, 5 TO 9, 12`.
* **`DEFAULT`**: An optional block that executes if no preceding `CASE` statement matches.
* **`ENDSWITCH`**: Marks the end of the `SWITCH` block.

**Note**: The interpreter does not "fall through" cases. Once a `CASE` or `DEFAULT` block is executed, control jumps immediately to the statement following `ENDSWITCH`.

**Multi-case examples:**

```basic
SWITCH n
    CASE 1, 3, 5, 7, 9
        PRINT "odd single-digit"
    CASE 10 TO 19
        PRINT "teen"
    CASE 20 TO 29, 40 TO 49
        PRINT "20s or 40s"
    DEFAULT
        PRINT "other"
ENDSWITCH
```

String values work identically: `CASE "red", "orange", "yellow"`. `lo TO hi` ranges are intended for numeric values only.

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

### REPL Keyboard Shortcuts

The interactive REPL hosts up to four parallel workspaces, each with its own VM.

* **`F1` … `F4`**: Switch to workspace 1–4. Works at the prompt and while a console-mode program is running (the keys are intercepted before reaching `INKEY$`).
* **`Ctrl+F1` … `Ctrl+F4`**: Same as `F1`…`F4`, but also works while a graphics program is running. The chord is consumed before ImGui or `ON KEYDOWN` handlers see it, so the running program is free to bind plain `F1`…`F4` for its own use (help screens, save slots, etc.).
* **`F5`**: Run the current source buffer.
* **`F7`**: Show command history.
* **`F8`**: Search command history (incremental, by prefix of current line).

The Ctrl+F1…F4 hook is only active when jdBasic was launched as the REPL. Standalone runs (`jdbasic foo.jdb`) install no hook, so the chord has zero overhead and `F1`…`F4` reach the program normally.

### Filesystem

* **`DIR [path]`**: Lists files and directories. Supports wildcards like `*` and `?`.
* **`DIR$(wildcard$, [extended_info]) -> Array`**: Lists files and directories matching the pattern.
  * If `extended_info` is `FALSE` (default), it returns a **1D Array** of filenames.
  * If `extended_info` is `TRUE`, it returns a **2D Matrix** (Nx5) containing details for each file:
    * **Col 0**: Filename (String)
    * **Col 1**: Size in bytes (Number)
    * **Col 2**: Type ("FILE", "DIR", "LINK")
    * **Col 3**: Date (YYYY-MM-DD HH:MM:SS)
    * **Col 4**: Attributes ("R", "W", "X", etc.)
* **`CD "path"`**: Changes the current working directory.
* **`PWD`**: Prints the current working directory.
* **`MKDIR "path"`**: Creates a new directory.
* **`RMDIR "path"`**: Removes an empty directory.
* **`KILL "filename"`**: Deletes a file.

#### Path Functions

* **`PATH.JOIN$(part1$, part2$, ...) -> string$`**: Joins multiple file path components using the correct separator for the current OS (e.g., `\` on Windows, `/` on Linux).
* **`PATH.BASENAME$(path$) -> string$`**: Returns the filename part of a path (e.g., `"file.txt"` from `"/dir/file.txt"`).
* **`PATH.EXT$(path$) -> string$`**: Returns the file extension including the dot (e.g., `".txt"`).
* **`PATH.DIRNAME$(path$) -> string$`**: Returns the directory part of a path (e.g., `"a/b"` from `"a/b/c.txt"`). Returns `""` for a bare filename and `"/"` for a root-level path like `"/x"`.
* **`PATH.NORMALIZE$(path$) -> string$`**: Normalizes a path by resolving `.` and `..` segments and converting to the OS-native separator. Preserves drive prefixes on Windows (e.g., `"C:\"`).

#### File Inspection Functions

* **`FILE.EXISTS(path$) -> integer`**: Returns `1` if the file or directory at `path$` exists, `0` otherwise.
* **`FILE.SIZE(path$) -> integer`**: Returns the size of the file in bytes, or `-1` if the file does not exist.
* **`FILE.ISDIR(path$) -> integer`**: Returns `1` if `path$` is an existing directory, `0` otherwise.
* **`FILE.STAT(path$) -> map`**: Returns a map describing the file with keys:
  * `"exists"` (boolean), `"size"` (integer bytes), `"is_dir"` (boolean), `"readonly"` (boolean), `"hidden"` (boolean), `"mtime"` (string `YYYY-MM-DD HH:MM:SS`). For missing files, `exists` is `FALSE` and other fields are default values.

### OS Functions

* **`OS.GETOS() -> string$`** / **`OS.GETOS$()`**: Returns a string identifying the current operating system. Possible values are `"WINDOWS"`, `"LINUX"`, and `"MACOS"`. Both forms are equivalent.

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
* **`OS.LOAD() -> Number`**: Returns the current system-wide CPU load as a percentage (0.0 to 100.0). Accuracy and behavior are OS-dependent.
* **`OS.FEATURE(name$) -> BOOLEAN`**: Returns `TRUE` when the running binary advertises the named build feature, `FALSE` otherwise. Useful to gate code paths the current backend cannot run — for example, programs that should skip reactive variables or `EXECUTE`/`EVAL` blocks when running from a natively compiled `.exe`. Recognised feature names: `"NATIVEC"` (running from `--compile` output), `"INTERPRETER"` (running in the VM), `"COM"`, `"HTTP"`, `"SERIAL"`, `"GFX"`, `"IMGUI"`, `"LLM"`, `"ONNX"`, `"LLVMC"` (compiler available). Unknown names return `FALSE`.

    ```basic
    IF NOT OS.FEATURE("NATIVEC") THEN
        ' VM-only features go here (REACTIVE bindings, EXECUTE, EVAL, ...)
    ENDIF
    ```

#### Setting EXE file properties (`.jdb.props` sidecar)

When building a standalone `.exe` with `jdBasic --compile myprog.jdb`, the
compiler looks for an optional sidecar file `myprog.jdb.props` next to the
source. If present, its contents are baked into the produced `.exe` as a
standard Win32 `VERSIONINFO` resource (visible in *Properties → Details* and
queryable via `GetFileVersionInfo`). Without the sidecar, the `.exe` is built
exactly as before — the file is purely additive.

The format is one `key = value` per line; lines starting with `#` are
comments, and surrounding double-quotes around values are stripped. Recognised
keys:

| Key                | Purpose                                                 |
| ------------------ | ------------------------------------------------------- |
| `FileVersion`      | "1.2.3.4"-style four-part version (the binary version). |
| `ProductVersion`   | Product-level version. Defaults to `FileVersion`.       |
| `CompanyName`      | Publisher / company.                                    |
| `FileDescription`  | Short description shown in tooltips and Task Manager.   |
| `ProductName`      | Marketing name of the product.                          |
| `LegalCopyright`   | Copyright string.                                       |
| `OriginalFilename` | Filename the binary was originally built as.            |
| `InternalName`     | Internal name. Defaults to `OriginalFilename`.          |
| `Icon`             | Path to a `.ico` file embedded as the EXE icon.         |

Example `myprog.jdb.props`:

```
# EXE properties for myprog.exe
FileVersion     = 2.5.1.0
ProductVersion  = 2.5.0.0
CompanyName     = Acme Industries
FileDescription = Widget Builder
ProductName     = Acme Widget Builder
LegalCopyright  = Copyright (C) 2026 Acme Industries
OriginalFilename= myprog.exe
Icon            = resources/myprog.ico
```

If `rc.exe` (the Windows resource compiler) is unavailable or fails, the
linker continues without the version resource and a warning is printed to
stderr — compilation never fails because of a bad props file.

### Foreign Function Interface (DECLARE FUNC)

`DECLARE FUNC` / `DECLARE SUB` lets jdBasic call any C-style function exported
from a shared library — Win32 APIs, your own bridge DLLs (e.g. for SQLite,
ZeroMQ, OpenSSL), or third-party libraries. There is no preprocessor and no
header file: each function is declared inline.

**Syntax**

```basic
DECLARE FUNC name LIB "library" ALIAS "export_name" (p1 AS type, ...) AS ret_type
DECLARE SUB  name LIB "library" ALIAS "export_name" (p1 AS type, ...)
```

* **`name`** — the identifier you call from jdBasic. Does not have to match the export.
* **`LIB "library"`** — base library name. The runtime appends the platform extension automatically:
  * Windows: `name.dll`
  * Linux:   `libname.so`
  * macOS:   `libname.dylib`
  * If the string already contains a path separator or one of these extensions, it is used verbatim.
* **`ALIAS "export_name"`** — symbol exported by the library (case-sensitive). Default is `name`.
* **Parameter types**: `INTEGER`, `STRING`, `RETURN`. Up to 8 parameters.
* **Return types**: `INTEGER` (default for FUNC), `STRING`, `ARRAY`, `VOID` (SUB).

**Parameter types in detail**

| Type      | C-side                          | jdBasic-side                                    |
|-----------|---------------------------------|-------------------------------------------------|
| `INTEGER` | `intptr_t` (any int / pointer)  | Numeric value                                   |
| `STRING`  | `const char*` (NUL-terminated)  | jdBasic string is copied into a temp buffer     |
| `RETURN`  | `char*` writable output buffer  | The integer passed by the caller is the **buffer size in bytes** (0 = default 64 KB; clamped at 64 MB) |

When a function uses one or more `RETURN` parameters, **or** declares
`AS ARRAY`, the call returns an array: `[function_return, return_buf_1,
return_buf_2, ...]`. Each `RETURN` slot is decoded as a NUL-terminated
string. Use array destructuring to unpack:

```basic
[bytes_written, json$] = sqlb_query_json(db, "SELECT * FROM users", 1024*1024, 1024*1024)
```

The first `1024*1024` here both *requests* a 1 MB output buffer **and** is
the integer the C function receives as its size argument — a single value
serves both ends, which is the typical Win32 / POSIX pattern.

**Calling convention**: x86-64 only — Win64 ABI on Windows and System V on
Linux/macOS. Pass everything as `intptr_t`-sized values. There is no
`STDCALL` / `CDECL` / `double` support today; floats must be marshalled as
strings or bit-pattern integers in the bridge.

**Example — Win32 API**

```basic
DECLARE FUNC MessageBox LIB "user32.dll" ALIAS "MessageBoxA" _
    (hwnd AS INTEGER, text AS STRING, title AS STRING, type AS INTEGER) AS INTEGER

result = MessageBox(0, "Hello from jdBasic!", "FFI demo", 0)
```

**Example — your own bridge DLL** (`bridges/sqlitebridge/sqlitebridge.c`):

```basic
DECLARE FUNC sqlb_open LIB "sqlitebridge" ALIAS "sqlb_open" (path AS STRING) AS INTEGER
DECLARE FUNC sqlb_exec LIB "sqlitebridge" ALIAS "sqlb_exec" (h AS INTEGER, sql AS STRING) AS INTEGER

db = sqlb_open("test.db")
n  = sqlb_exec(db, "CREATE TABLE t(id INTEGER, name TEXT)")
```

Wrap your bridge in an `EXPORT MODULE` file so callers see a clean namespace:
see `jdb/sqlite.jdb` and `jdb/sqlite_demo.jdb` for the full pattern.

**Platform notes**

* **Windows**: production-tested. Loads via `LoadLibraryA` / `GetProcAddress`.
* **Linux/macOS**: `dlopen` / `dlsym` path is in place; build & validation
  pending — see `src/ffi.cpp`.

### Python Integration (FFI)

Available when the interpreter is compiled with `PYTHON` support. These functions allow seamless data exchange and code execution between jdBasic and a sandboxed Python environment.

* **`PYTHON$(python_code$) -> string$`**: Executes a block of Python code in an isolated workspace dictionary and returns the captured standard output (`stdout`) as a string.
* **`PY.SET("python_var_name", jdbasic_value)`**: Injects a jdBasic value (scalar, Array, Map, Tensor) directly into the Python environment's memory.
* **`PY.GET("python_var_name") -> value`**: Retrieves a variable from the Python environment and converts it back to native jdBasic types.
* **`PY.EVAL("expression_string") -> value`**: Evaluates a Python expression and returns the result directly as a jdBasic variant.
* **`PY.DIR$([target$]) -> string$`**: Returns a comma-separated string listing the local workspace variables, or methods available on a specific Python object if `target$` is provided.
* **`PY.HELP$("target") -> string$`**: Returns the Python documentation (docstring) for a specified module or function (e.g., `"math.cos"`).

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

#### `MAP.FROM(json_object_string$) -> Map`

Creates a Map directly from a string formatted as a JSON object (e.g., `{"key":"value"}`).

* **`json_object_string$`**: The JSON-formatted string to parse.
* **Returns**: A new Map containing the parsed key-value pairs.

### JSON Functions

* **`JSON.PARSE$(json_string$)`**: Parses a JSON string and returns a special `JsonObject`. This object can be accessed like a `Map` or an `Array`.
* **`JSON.STRINGIFY$(map_or_array)`**: Takes a `Map` or `Array` variable and returns its compact JSON string representation. Ideal for creating API payloads.

### COM Automation Functions

* **`CREATEOBJECT(progID$)`**: Creates a COM Automation object (e.g., "Excel.Application") and returns a `ComObject`.
* **`RELEASEOBJECT(com_obj)`**: Explicitly releases a single COM object reference. Usually not required — objects are released automatically when they go out of scope — but useful to free expensive resources like Office applications deterministically.
* **`RELEASEALL`**: Releases every COM object currently held by the runtime. Handy to clean up at the end of a script or on error.

### String Functions

* **`LEFT$(str$, n)`**, **`RIGHT$(str$, n)`**, **`MID$(str$, start, [len])`**: Extracts parts of a string. The start position is 0 - based. Also available as `LEFT`, `RIGHT`, `MID` without the `$`.
* **`LEN(expression)`**: Returns a scalar length. For strings, the byte count; for arrays, the element count of the outermost dimension. Always returns a scalar — use `LENV` when you need the full shape of a nested array.
* **`LENV(expression)`**: Returns a shape vector `[dim0, dim1, ...]` describing the full extent of a nested array. For a 1D array returns `[n]`; for a string returns `[byte_count]`.
* **`LCASE$(str$)`**, **`UCASE$(str$)`**, **`TRIM$(str$)`**: Manipulates string case and whitespace. Also available as `LCASE`, `UCASE`, `TRIM`.
* **`LTRIM$(str$)`** / **`RTRIM$(str$)`**: Trims whitespace from the left or right end only.
* **`STARTSWITH(str$, prefix$) -> bool`** / **`ENDSWITH(str$, suffix$) -> bool`**: Returns `TRUE` if `str$` starts/ends with the given substring.
* **`SPACE$(n) -> string$`**: Returns a string of `n` spaces — handy for padding.
* **`REPEAT$(str$, n) -> string$`**: Returns `str$` concatenated `n` times. `n <= 0` or empty input returns `""`.
* **`LPAD$(str$, width [, pad$]) -> string$`** / **`RPAD$(str$, width [, pad$]) -> string$`**: Left- or right-pads `str$` to `width` characters using `pad$` (default `" "`). Multi-character `pad$` cycles (e.g. `LPAD$("x", 5, "-=")` -> `"-=-=x"`). If `str$` is already `>= width`, it is returned unchanged.
* **`BIN$(n)` / `HEX$(n)` / `OCT$(n)` -> string$**: Converts an integer into its binary, hexadecimal or octal string representation.
* **`STR$(number)`**, **`VAL(string$)`**: Converts between numbers and strings.
* **`CHR$(ascii_code)`**, **`ASC(char$)`**: Converts between ASCII codes and characters.
* **`INSTR([start, ]haystack$, needle$)` / `INSTR$()`**: Finds the position of one string within another. Positions are 0-based. Returns -1 if not found. *(Both variants are supported)*.
* **`INSERT$(target_string or array, text_to_insert$ string or array, position or array) -> string or array`**: Inserts a text_to_insert$ in target at position.
* **`SPLIT(source$, delimiter$)`**: Splits a string by a delimiter and returns a 1D array of strings.
* **`JOIN(array, delimiter$) -> string$`**: Inverse of `SPLIT` — concatenates the elements of an array into a single string, joined by `delimiter$`.
* **`FRMV$(array, [format_string$]) -> string$`**: Formats a 1D or 2D array into a string. If format_string$ is provided, it's used to format each row. Otherwise, it creates a right-aligned string matrix.
* **`FORMAT$(format_string$, arg1, arg2, ...) -> string$`**: Formats a string using C++20-style format specifiers.
* **`REPLACE$(source_string or array, find_string$ or array, replace_with_string$ or array) -> string or array)`**: Returs a string where all found find_string$ are preplaced with replace_with_string$.
* **`REVERSE$(string or array) -> string or array`**: Returns a reversed string.
* **`BYTEAT(str$, index) -> Integer`**: Returns the numeric byte value (0-255) at the specified 0-based index in a string. This provides fast O(1) access to raw string data, which is essential when processing binary data loaded via `BINREADER$`.
* **`PACK$(format$, v1, v2, ...) -> string$`**: Packs numbers into a binary string based on a format.
  * Format specifiers: `<` (Little Endian), `>` (Big Endian), `b` (Byte), `s` (Short), `i` (Integer), `l` (Long), `f` (Float), `d` (Double).
* **`UNPACK(format$, binary_data$) -> Array`**: Unpacks a binary string into an Array of numbers based on the format string.

### Math/Arithmetic/Round Functions

All numeric functions are vectorized — they also accept arrays and apply element-wise.

#### Trigonometry

* **`SIN(x)`**, **`COS(x)`**, **`TAN(x)`**: Standard trig functions (radians).
* **`ASIN(x)`**, **`ACOS(x)`**, **`ATAN(x)`**: Inverse trig functions.
* **`ATAN2(y, x)`**: Two-argument arctangent returning the correct quadrant.
* **`SINH(x)`**, **`COSH(x)`**, **`TANH(x)`**: Hyperbolic functions.

#### Exponentials & Logs

* **`SQR(x)`**: Square root.
* **`EXP(x)`**: Natural exponential e^x.
* **`LOG(x)`**: Natural logarithm.
* **`LOG10(x)`**: Base-10 logarithm.
* **`POW(base, exp)`**: Power function. Also available as the `^` operator.
* **`FAC(n)`**: Factorial.
* **`GCD(a, b, ...)`**: Greatest common divisor of two or more integers. Any-zero input yields the non-zero operand's magnitude.
* **`LCM(a, b, ...)`**: Least common multiple of two or more integers.
* **`ROTL(x, n [, bits])`** / **`ROTR(x, n [, bits])`**: Bit rotation of integer `x` by `n` positions. Optional `bits` argument sets the width (defaults to 64). `ROTL(1,1) = 2`, `ROTR(16,4) = 1`.

#### Rounding & Sign

* **`INT(x)`**: Traditional BASIC integer function (floor for positives).
* **`FLOOR(x)`**: Rounds down toward `-∞`.
* **`CEIL(x)`**: Rounds up toward `+∞`.
* **`ROUND(n, [decimals])`**: Rounds to the specified number of decimal places (default 0).
* **`TRUNC(x)`**: Truncates toward zero.
* **`ABS(x)`**: Absolute value.
* **`SGN(x)`**: Returns -1, 0, or +1 depending on the sign of `x`.
* **`CLAMP(value_or_array, min, max) -> number or array`**: Clamps the value in the given range.

#### Random Numbers

* **`RND([max])`**: Returns a pseudo-random number. Without arguments, a double in `[0, 1)`; with an integer argument, an integer in `[0, max)`. Also available as `RANDOM`.
* **`RANDOMSEED(seed)`**: Seeds the PRNG. Using the same seed twice produces the same sequence — useful for reproducible tests.

#### Conversion

Classic BASIC cast family — each takes any numeric/convertible value:

* **`CINT(x)`**: Cast to 32-bit integer (truncates toward zero). Overflow wraps like C `int32_t`.
* **`CLNG(x)`**: Cast to 64-bit integer (truncates toward zero).
* **`CSNG(x)`**: Roundtrip through 32-bit float — useful to force single-precision loss on doubles.
* **`CDBL(x)`**: Cast to double-precision float.
* **`CBOOL(x)`**: Returns `0` for zero, `1` for any non-zero value.
* **`CSTR(x) -> string$`** / **`TOSTR(x) -> string$`** / **`STR(x) -> string$`**: Converts any value to its string form.
* **`CDATE(str$)`** / **`CVDATE(str$)`**: Parses `"YYYY-MM-DD"` (optionally with time) into a `DateTime`.
* **`TONUM(str$) -> number`** / **`VAL(str$) -> number`**: Parses a string as a number.

Integer-to-string formatting helpers live under **[Strings](#strings)**: `HEX$`, `BIN$`, `OCT$`, `FORMAT$`, `FORMAT_DATE$`.

#### Utility

* **`IIF(condition, value_if_true, value_if_false) -> value or array`**: A vectorized ternary operator. Evaluates a condition (scalar or array) and returns the corresponding true/false value element-wise.

### Regular Expression Functions

* **`REGEX.MATCH(pattern$, text$) -> Boolean or Array`**: Checks if the entire `text$` string matches the `pattern$`.
  * Returns `TRUE` or `FALSE` if the pattern has no capture groups.
  * If the pattern contains capture groups `(...)`, it returns a 1D array of the captured substrings upon a successful match, otherwise `FALSE`.
* **`REGEX.FINDALL(pattern$, text$) -> Array`**: Finds all non-overlapping occurrences of `pattern$` in `text$`.
  * Returns a 1D array of all matches found.
  * If the pattern contains capture groups, it returns a 2D array where each row contains the groups for a single match.
* **`REGEX.REPLACE(pattern$, text$, replacement$) -> String`**: Replaces all occurrences of `pattern$` in `text$` with `replacement$`. The replacement string can use backreferences like `$1`, `$2` to insert captured group content.

For backwards compatibility, the underscore forms `REGEX_MATCH(pattern$, text$)` and `REGEX_REPLACE$(pattern$, text$, replacement$)` are also accepted and map to `REGEX.MATCH` / `REGEX.REPLACE`.

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
* **`TAKE_WHILE(predicate@, array) -> array`**: Returns the longest prefix of `array` for which `predicate(element)` is true. Stops at the first false. (Interpreter only — not yet supported in native compile.)
* **`DROP_WHILE(predicate@, array) -> array`**: Drops the longest prefix where `predicate(element)` is true, returning the remainder. (Interpreter only.)
* **`CHUNK(array, size) -> array`**: Splits `array` into sub-arrays of length `size`; the last chunk may be shorter. `size >= 1`.
* **`ENUMERATE(array) -> array`**: Pairs each element with its 0-based index, returning `[[0, a0], [1, a1], ...]`.
* **`GROUPBY(key_fn@, array) -> map`**: Buckets elements into a map keyed by `key_fn(element)` (coerced to string). Each value is the list of matching elements. (Interpreter only.)
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

#### Constructors & Flattening

* **`ZEROS(shape_vector) -> array`**: Creates an array of the given shape filled with zeros.
* **`ONES(shape_vector) -> array`**: Creates an array of the given shape filled with ones.
* **`RANGE(start, stop, [step=1]) -> vector`**: Python-style range — returns `[start, start+step, ..., stop)`.
* **`LINSPACE(start, stop, n) -> vector`**: Returns `n` evenly spaced samples over `[start, stop]` inclusive.
* **`FLATTEN(array) -> vector`**: Flattens a multi-dimensional array into a 1D vector.
* **`ZIP(arr1, arr2, ...) -> 2D matrix`**: Combines multiple vectors into a matrix where each row is `[arr1[i], arr2[i], ...]`.

#### Searching & Counting

* **`INDEXOF(array, value) -> number`**: Same as `FIND_IN_ARRAY` — returns the first 0-based index of a value, or -1 if not found.
* **`COUNT(array, [value]) -> number`**: Counts occurrences of `value` in `array`. Without the second argument, returns the total length.
* **`POP(array) -> value`** / **`PUSH(array, value)`**: Stack-like operations on an array.
* **`HISTOGRAM(array, bins) -> [counts, edges]`**: Builds a histogram with the given number of bins. Returns counts and bin edges as two arrays.

#### Statistics

* **`MEAN(array)`**, **`MEDIAN(array)`**: Central tendency.
* **`VARIANCE(array)`**, **`STDEV(array)`**: Dispersion.

#### Vector Math

* **`DOT(a, b) -> number`**: Dot product of two vectors.
* **`CROSS(a, b) -> vector`**: Cross product of two 3D vectors.
* **`CUMSUM(array) -> array`**: Cumulative sum along the last axis.
* **`CUMPROD(array) -> array`**: Cumulative product along the last axis.

### File I/O Functions

* **`TXTREADER$(filename$)`**: Reads an entire text file into a single string variable.
* **`TXTWRITER filename$, content$, [append]`**: Writes a string variable to a text file. If the optional third argument `append` is `TRUE`, content is appended to the existing file; otherwise the file is overwritten (default). The file is written in binary mode, so no newline translation is performed.
* **`CSVREADER(filename$, [delimiter$], [has_header])`**: Reads a CSV file into a 2D array of numbers.
* **`CSVWRITER filename$, array, [delimiter$], [header_array]`**: Writes a 2D array to a CSV file.
* **`BINREADER$(filename$) -> string$`**: Reads the entire content of a binary file into a single string. Unlike `TXTREADER$`, this preserves raw bytes (including null bytes `0x00`) and performs no newline translation.
* **`BINWRITER filename$, data$`**: Writes a raw string of bytes to a file, overwriting it.

### System and Time Functions

* **`GETENV$(var_name$)`**: Gets the value of a system environment variable.
* **`SETENV name$, value$`**: Sets an environment variable for the current process. Passing an empty `value$` (or omitting it) removes the variable.
* **`MKTEMP$([prefix$])`**: Returns a unique, freshly created-and-released path in the OS temp directory. Default prefix is `"jdb"`.
* **`SETLOCALE("locale_string")`**: Sets the locale for number formatting (e.g., "en-US" or "de-DE").
* **`TICK()`**: Returns the number of milliseconds since the program started.
* **`DATE$()` / `TIME$()`**: Returns the current system date/time as a string.
* **`NOW()`**: Returns a `DateTime` object for the current moment.
* **`DATEADD(part$, num, date [, tz_hours])`**: Adds an interval to a `DateTime` object. Interval part$: D,H,N,S. Optional numeric UTC offset (hours, may be fractional e.g. `5.5`) is accepted for symmetry but has no effect on the arithmetic.
* **`DATEDIFF(part$, date1, date2 [, tz_hours]) -> number`**: Calculates the difference between two dates in the specified unit. Interval part$: D,H,N,S. Optional `tz_hours` accepted but has no effect (difference is TZ-independent).
* **`CVDATE(date_string$ [, tz_hours])`**: Converts a string (`"YYYY-MM-DD[ HH:MM[:SS]]"`) to a `DateTime` object. When `tz_hours` is given, the input string is interpreted as wall-clock time in that UTC offset (e.g. `CVDATE("2024-01-15 14:00:00", 2)` yields the same instant as `CVDATE("2024-01-15 12:00:00", 0)`).
* **`FORMAT_DATE(date, format_string$ [, tz_hours]) -> string$`**: Formats a `DateTime` using C-style specifiers (`%Y`, `%m`, `%d`, `%H`, `%M`, `%S`, ...). Without `tz_hours` the wall-clock is local time; with `tz_hours` the output reflects the chosen UTC offset (`0` = UTC, `2` = UTC+2, `-5` = UTC−5, `5.5` = UTC+5:30).
* **`DATE.UTC(year, month, day [, hour [, minute [, second]]]) -> DateTime`**: Builds a `DateTime` from UTC components. Month is 1–12, day is 1–31. Omitted time components default to zero.
* **`DATE.PARTS(date [, tz_hours]) -> object`**: Breaks a `DateTime` into a map with keys `year`, `month`, `day`, `hour`, `minute`, `second`, `weekday` (0=Sunday ... 6=Saturday), `yday` (1..366). With `tz_hours` the wall-clock fields reflect the chosen offset.
* **`YEAR(date)`**, **`MONTH(date)`**, **`DAY(date)`**: Extract the year (four digits), month (1-12), or day of month (1-31) from a `DateTime`.
* **`HOUR(date)`**, **`MINUTE(date)`**, **`SECOND(date)`**: Extract the time-of-day components.
* **`WEEKDAY(date) -> number`**: Returns the day of the week (0=Sunday ... 6=Saturday).

### Type Inspection

* **`TYPEOF(value) -> string$`**: Returns the type name as a string: `"NUMBER"`, `"STRING"`, `"ARRAY"`, `"OBJECT"`, `"FUNCREF"`, `"NONE"`, etc.
* **`ISNUM(v) -> bool`**: `TRUE` if `v` is a number (integer or double).
* **`ISSTR(v) -> bool`**: `TRUE` if `v` is a string.
* **`ISARR(v) -> bool`**: `TRUE` if `v` is an array.
* **`ISMAP(v) -> bool`**: `TRUE` if `v` is a map/object.
* **`ISBOOL(v) -> bool`**: `TRUE` if `v` is a boolean.
* **`ISNONE(v) -> bool`** / **`ISNULL(v) -> bool`**: `TRUE` if `v` is the `NONE` value.
* **`VARS() -> array`**: Returns an array of names of all currently defined global variables — useful for live debugging and the REPL.

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
* **`HTTP.GET_ASYNC$(url$) -> task_id`**: Non-blocking variant of `HTTP.GET$`. Returns an async task id that can be polled with `THREAD.ISDONE` / `THREAD.GETRESULT` or awaited with `AWAIT`.
* **`HTTP.POST_ASYNC$(url$, data$, contentType$) -> task_id`**: Non-blocking POST.
* **`HTTP.PUT_ASYNC$(url$, data$, contentType$) -> task_id`**: Non-blocking PUT.
* **`HTTP.SETHEADER(name$, value$)`**: Sets a custom header for subsequent HTTP requests.
* **`HTTP.CLEARHEADERS()`**: Clears all custom HTTP headers.
* **`HTTP.STATUSCODE()`**: Returns the HTTP status code from the last request.
* **`HTTP.SETTIMEOUT(seconds)`**: Sets the connection / read / write timeout (seconds) for subsequent client calls. Default is 10s.
* **`HTTP.FOLLOWREDIRECTS(flag)`**: Enables (default) or disables automatic follow-redirect on client calls.
* **`HTTP.SETPARAM(name$, value$)`**: Appends (or updates) a URL query parameter applied to all subsequent client calls. Values are url-encoded automatically.
* **`HTTP.CLEARPARAMS()`**: Removes all query parameters set via `HTTP.SETPARAM`.
* **`HTTP.SETCOOKIE(name$, value$)`**: Stores a cookie sent as a `Cookie:` header on subsequent requests.
* **`HTTP.CLEARCOOKIES()`**: Removes all client-side cookies.
* **`HTTP.GETCOOKIE$(name$)`**: Returns the stored cookie value, or empty string if the key is unknown.
* **`HTTP.DELETE$(url$)`**: Performs an HTTP DELETE request and returns the response body. Status is available via `HTTP.STATUSCODE()`.
* **`HTTP.REQUEST(method$, url$ [, body$ [, content_type$]]) -> map`**: Generic HTTP call. Returns a map with `status`, `body`, and `headers`. Unlike the shortcut forms this does not throw on HTTP-level errors (4xx/5xx) — only on transport failure. `method$` accepts `GET`, `DELETE`, `HEAD`, `POST`, `PUT`, `PATCH`.
* **`HTTP.SERVER.START(port [, host$])`**: Starts a non-blocking HTTP server on the specified port, returning `TRUE` on success. `host$` defaults to `"127.0.0.1"` (loopback only); pass `"0.0.0.0"` to expose the server to the LAN.
* **`HTTP.SERVER.STOP`**: Stops the running HTTP server.
* **`HTTP.SERVER.ON_GET(path$, function_name$)`**: Registers a `jdBasic` function to handle incoming `GET` requests for a specific URL path.
* **`HTTP.SERVER.ON_POST(path$, function_name$)`**: Registers a `jdBasic` function to handle incoming `POST` requests for a specific URL path.

### Output capture

These three natives redirect `PRINT`/all script output to an in-memory string buffer instead of letting it leak to stdout. Captures are stacked: each `OUTPUT.CAPTURE_BEGIN` saves the previous output handler so nested captures and host-installed routers (e.g. the Console workspace buffer) restore correctly.

* **`OUTPUT.CAPTURE_BEGIN`**: Begins capturing all subsequent output to a fresh buffer.
* **`OUTPUT.CAPTURE_END$() -> string$`**: Stops the most recent capture and returns the buffered text. Errors if no capture is active.
* **`OUTPUT.CAPTURE_PEEK$() -> string$`**: Returns the current buffer contents without stopping capture.

### Encoding & Hashing (CODEC)

* **`CODEC.BASE64_ENCODE$(string$) -> string$`**: Encodes a string into Base64 format. Useful for API authentication headers.
* **`CODEC.BASE64_DECODE$(string$) -> string$`**: Decodes a Base64 encoded string back to its original format.
* **`CODEC.SHA256$(string$) -> string$`**: Calculates the SHA256 hash of a string and returns it as a 64-character hex string.
* **`CODEC.UUID$() -> string$`**: Generates a random Version 4 UUID (e.g., `"550e8400-e29b-41d4-a716-446655440000"`).

### Building a Web Server & API

The built-in HTTP server allows `jdBasic` to serve websites and create simple JSON APIs. The server runs in the background, handling requests by calling user-defined `jdBasic` functions.

Handler functions receive one argument: a `Map` containing details about the incoming request:

* `PATH`, `METHOD`, `BODY`, `PARAMS` — request path, method, body bytes, and parsed query parameters.
* `HEADERS` — a map of incoming request headers. **Keys are normalised to lowercase**, since HTTP header names are case-insensitive (RFC 7230). Look up `req{"HEADERS"}{"content-type"}`, `req{"HEADERS"}{"mcp-session-id"}`, etc.

The `RETURN` value of the function is sent back to the client as the response.

* If the function returns a `String`, it is sent with `Content-Type: text/html`.
* If the function returns a `Map`, the default behaviour is to auto-convert it to JSON with `Content-Type: application/json` and HTTP 200.
* For custom status codes / response headers / body bytes, return a "rich response" map carrying any of these reserved keys (auto-JSON encoding is suppressed when `__http_status` is present):
    * `__http_status` (number) — the HTTP status code (e.g. `202`, `404`).
    * `__http_body` (string) — the raw response body. Omit for an empty body.
    * `__http_headers` (map) — extra response headers as `{name: value}`.
    * `__http_content_type` (string) — defaults to `application/json` when a body is present.

```basic
' Custom response: 202 Accepted with no body and a custom session header
RETURN { _
    "__http_status": 202,                              _
    "__http_headers": {"Mcp-Session-Id": new_sid$}     _
}
```

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

### Serial Communication (COM)

Available when compiled with `USE_COM`.

* **`SERIAL.OPEN(port$, baud_rate) -> Handle`**: Opens a serial port (e.g., "COM3" on Windows, "/dev/ttyUSB0" on Linux) and returns a handle.
* **`SERIAL.CLOSE(handle)`**: Closes an open serial port.
* **`SERIAL.WRITE(handle, data$)`**: Writes a string (or binary bytes) to the serial port.
* **`SERIAL.READ$(handle, max_bytes) -> string$`**: Reads up to `max_bytes` from the port. Returns an empty string if no data is available (non-blocking).
* **`SERIAL.AVAILABLE(handle) -> number`**: Returns the number of bytes currently waiting in the input buffer.
* **`SERIAL.FLUSH(handle)`**: Clears the input and output buffers.

```basic
' Arduino Communication Example
hCom = SERIAL.OPEN("COM3", 9600)
IF TypeOf(hCom) <> "SERIAL_PORT" THEN
    SERIAL.WRITE hCom, "LED_ON" + CHR$(10)
    SLEEP 100
    Response$ = SERIAL.READ$(hCom, 256)
    PRINT "Arduino said: " + Response$
    SERIAL.CLOSE hCom
ENDIF
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
* **`TOGGLE_FULLSCREEN`**: Toggles the graphics window between fullscreen and windowed mode.

#### Graphics Window & Event Handling

In addition to the high-level drawing commands above, the `GFX.*` namespace exposes lower-level window, timing, image, and input access.

* **`GFX.CLOSE`**: Closes the graphics window and releases SDL resources.
* **`GFX.POLLEVENT() -> object`**: Polls one pending SDL event and returns it as an object, or `NONE` if the queue is empty. Common types are `"quit"`, `"keydown"`, `"keyup"`, `"mousemove"`, `"mousebutton"`, `"windowresized"`.
* **`GFX.KEYSTATE(scancode) -> boolean`**: Returns `TRUE` if the given SDL scancode is currently held down.
* **`GFX.DELAY(ms)`**: Pauses for `ms` milliseconds using SDL's high-resolution timer.
* **`GFX.TICKS() -> number`**: Returns a monotonically increasing millisecond counter since the SDL subsystem was initialised — ideal for delta-time computations in game loops.
* **`GFX.MOUSEX() -> number`** / **`GFX.MOUSEY() -> number`**: Returns the current mouse position inside the graphics window.
* **`GFX.MOUSEBUTTON(btn) -> boolean`**: Returns `TRUE` if mouse button `btn` (1=L, 2=M, 3=R) is currently pressed.

#### Image Loading (SDL_image)

* **`GFX.LOADIMAGE(path$) -> image_id`**: Loads a PNG/JPG/BMP/etc. image via SDL_image and returns a handle.
* **`GFX.DRAWIMAGE(image_id, x, y, [w], [h])`**: Blits the image at `(x, y)`, optionally scaled to `w × h`.
* **`GFX.FREEIMAGE(image_id)`**: Releases the image.
* **`GFX.TEXTSIZE(text$, [size]) -> [w, h]`**: Measures the rendered size of a string with the current font.

#### Audio File Playback (SDL_mixer)

The `AUDIO.*` family provides file-based playback for sound effects (WAV) and music (MP3/OGG/FLAC), powered by SDL_mixer. This is separate from the `SOUND.*` live-coding sequencer/synth described further below — use `AUDIO.*` to play pre-recorded audio files, and `SOUND.*` to programmatically synthesize notes and rhythms.

* **`AUDIO.INIT`**: Initialises the audio subsystem (mixer opens at 44.1 kHz, stereo). Must be called before any other `AUDIO.*` function.
* **`AUDIO.CLOSE`**: Frees all loaded chunks and music and shuts down the audio subsystem.

##### Sound Effects (Chunks)

* **`AUDIO.LOADWAV(path$) -> sfx_id`**: Loads a short audio file (WAV, OGG, etc.) into a chunk suitable for repeated fire-and-forget playback.
* **`AUDIO.PLAY(sfx_id, [loops=0], [channel=-1]) -> channel`**: Plays the chunk. `loops=0` means play once; `-1` means loop forever. Returns the channel index it was scheduled on.
* **`AUDIO.STOP([channel])`**: Halts playback on the given channel. With no argument, halts all channels.
* **`AUDIO.PAUSE([channel])`** / **`AUDIO.RESUME([channel])`**: Pauses/resumes the given channel (or all if omitted).
* **`AUDIO.VOLUME(level, [channel]) -> previous_volume`**: Sets the channel volume (0..128). With no channel, sets the master SFX volume. Returns the previous value.
* **`AUDIO.FREE(sfx_id)`**: Releases the chunk memory.

##### Music

* **`AUDIO.LOADMUS(path$) -> music_id`**: Loads a music file (MP3, OGG, FLAC, etc.) as a streaming music track.
* **`AUDIO.PLAYMUS(music_id, [loops=-1])`**: Starts playing the music. `-1` (default) loops forever, `0` plays once.
* **`AUDIO.STOPMUS`**: Halts the current music track.
* **`AUDIO.PAUSEMUS`** / **`AUDIO.RESUMEMUS`**: Pauses/resumes the music track.
* **`AUDIO.VOLUMEMUS(level) -> previous_volume`**: Sets the music volume (0..128).
* **`AUDIO.FREEMUS(music_id)`**: Releases the music memory.

```basic
AUDIO.INIT
DIM shoot = AUDIO.LOADWAV("sfx/laser.wav")
DIM bgm   = AUDIO.LOADMUS("music/theme.ogg")

AUDIO.VOLUMEMUS 64
AUDIO.PLAYMUS bgm          ' loops forever

' In the game loop, fire the SFX on demand
AUDIO.PLAY shoot

' On exit
AUDIO.CLOSE
```

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

* **`GUI.TEXT(text$, [wrap_bool])`**: Displays text in the UI. If `wrap_bool` is `TRUE`, long lines are wrapped at the right edge of the current window/child region.
* **`GUI.TEXT_WRAPPED(text$)`**: Convenience form — always wraps long lines at the right edge.
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

#### Tables

* **`GUI.BEGIN_TABLE(id$, columns, [flags], [outer_w], [outer_h]) -> bool`**: Starts an ImGui table. Returns `TRUE` if the table is visible and should be populated.
* **`GUI.END_TABLE`**: Ends a table started with `GUI.BEGIN_TABLE`.
* **`GUI.TABLE_SETUP_COLUMN(label$, [flags], [init_width_or_weight])`**: Declares a table column. Call once per column before `GUI.TABLE_HEADERS_ROW`.
* **`GUI.TABLE_HEADERS_ROW`**: Submits a header row based on the columns configured with `GUI.TABLE_SETUP_COLUMN`.
* **`GUI.TABLE_NEXT_ROW([row_flags], [min_row_height])`**: Advances to the next row in the current table.
* **`GUI.TABLE_SET_COLUMN_INDEX(index) -> bool`**: Switches the current table column. Returns `TRUE` if the column is visible.
* **`GUI.TABLE_NEXT_COLUMN() -> bool`**: Switches to the next column in the current table. Returns `TRUE` if the column is visible.

#### Utilities & Styling

* **`GUI.THEME(theme_name$)`**: Sets the global UI theme. Options: `"DARK"`, `"LIGHT"`, `"CLASSIC"`.
* **`GUI.FLAG(flag_name$) -> number`**: Returns the integer value of an ImGui configuration flag.
  * Options: `"MENUBAR"`, `"NO_RESIZE"`, `"NO_TITLEBAR"`, `"NO_MOVE"`, `"NO_SCROLLBAR"`, `"NO_COLLAPSE"`, `"ALWAYS_AUTO_RESIZE"`, `"NO_SAVED_SETTINGS"`.
* **`GUI.COL(color_name$) -> number`**: Returns the integer index for a specific ImGui interface color (ImGuiCol_ enum). This is used with style pushing functions to customize specific UI elements.
  * Options: `"TEXT"`, `"WINDOWBG"`, `"BUTTON"`, `"BUTTONHOVERED"`, `"BUTTONACTIVE"`, `"HEADER"`, `"HEADERHOVERED"`, `"HEADERACTIVE"`, `"FRAMEBG"`, `"FRAMEBGHOVERED"`, `"FRAMEBGACTIVE"`, `"TITLEBG"`, `"TITLEBGACTIVE"`, `"CHECKMARK"`, `"SLIDERGRAB"`, `"SLIDERGRABACTIVE"`.
* **`GUI.PUSH_ID(id)`**: Pushes an integer or string ID to the stack to prevent ID collisions in loops.
* **`GUI.POP_ID`**: Pops the last ID from the stack.
* **`GUI.SHOW_FONT_ATLAS`**: Opens the built-in ImGui font visualizer for debugging.
* **`GUI.ITEM_RECT() -> array`**: Returns the rectangle of the last submitted GUI item as `[minx, miny, maxx, maxy]` in **screen coordinates**.
* **`GUI.SET_CURSOR_SCREEN_POS(x, y)`**: Sets the cursor position in screen coordinates for the next widget (useful for overlay editors).
* **`GUI.SET_NEXT_ITEM_WIDTH(width)`**: Sets the width of the next input widget.
* **`GUI.SET_KEYBOARD_FOCUS`**: Gives keyboard focus to the next input widget that is created.
* **`GUI.ITEM_DEACTIVATED_AFTER_EDIT() -> bool`**: Returns `TRUE` if the last item became inactive after an edit (e.g., user pressed Enter/Esc or focus moved away).

#### Layout Queries (for responsive layouts)

* **`GUI.AVAIL_WIDTH() -> number`**: Returns the width still available in the current window/child region, after accounting for widgets already submitted this frame. Use together with `GUI.SET_NEXT_ITEM_WIDTH` to build layouts that resize with the window.
* **`GUI.AVAIL_HEIGHT() -> number`**: Same as above but for vertical space.
* **`GUI.WINDOW_WIDTH() -> number`**: Returns the full width of the current window (including padding, title bar etc.).
* **`GUI.WINDOW_HEIGHT() -> number`**: Returns the full height of the current window.

```basic
' Responsive input that grows with the window
IF GUI.BEGIN("Panel", 10, 35, 400, 600) THEN
  DIM w = GUI.AVAIL_WIDTH() - 10
  GUI.SET_NEXT_ITEM_WIDTH w
  query = GUI.INPUT("##q", query)
ENDIF
GUI.END
```

#### Sound

* **`SOUND.INIT`**: Initializes the audio system. Must be called before other sound functions.
* **`SOUND.VOICE track, waveform$, attack, decay, sustain, release`**: Configures the ADSR envelope and waveform for a sound track.
  * **Waveforms**: `"SINE"`, `"SQUARE"`, `"SAW"`, `"TRIANGLE"`, `"NOISE"`, `"SAMPLE"`.

* **`SOUND.SAMPLE track, sample_id, [base_note$], [loop_bool]`**: Assigns a loaded `SFX` ID to a track for pitched playback.
  * `base_note$`: The root pitch of the original sample (e.g., "C3").
  * `loop_bool`: If `TRUE`, the sample loops continuously while the key is held.

* **`SOUND.PLAY track, frequency`**: Plays a note at a specific frequency (or note name like "C4") on the given track.
* **`SOUND.RELEASE track`**: Starts the release phase of the note on the given track.
* **`SOUND.STOP track`**: Immediately stops the note on the given track.
* **`SOUND.PLAYBUFFER samples, [sample_rate], [channels]`**: Pushes a 1D float array (-1..1) directly onto a separate PCM stream that mixes alongside the synth tracks. Use this for hand-rolled waveforms, emulator audio (Apple II speaker), or anything that doesn't fit the ADSR/voice model. `sample_rate` defaults to 44100, `channels` to 1; if either changes between calls the underlying SDL stream is reopened so the resampler does the conversion. Calls are non-blocking — SDL queues until the device drains.
* **`SOUND.QUEUED() -> integer`**: Returns the number of bytes still waiting in the PLAYBUFFER queue. Useful for keeping the buffer between min/max watermarks without overflowing or underrunning. Returns 0 if `SOUND.INIT` hasn't run.
* **`SFX.LOAD id, "filepath.wav"`**: Loads a WAV file into memory slot `id`.
* **`SFX.PLAY id`**: Plays a loaded WAV file once (fire-and-forget).
* **`MUSIC.PLAY id, [loop_bool]`**: Plays a loaded WAV file as background music. Defaults to looping.
* **`MUSIC.STOP`**: Immediately stops the background music.

### Live Coding Sequencer

The live coding sequencer allows you to program rhythmic musical patterns and manipulate sound in real-time.

#### `SOUND.SEQ layer_id, pattern$, waveform$` Programs a rhythmic musical pattern into the live-coding sequencer for a specific layer

* **`layer_id`** (Integer): The index of the sequencer layer (corresponds to Track ID).
* **`pattern$`** (String): A rhythm string using "mini-notation".
* **`waveform$`** (String): Defines the sound source.
* **"VOICE"**: Uses the track's existing design (ADSR, Filter, Sample, etc.).
* **"SINE", "SQUARE", etc.**: Overrides the track's sound with a raw waveform.

##### Pattern SyntaxThe sequencer divides time into "cycles". You can arrange events within a cycle using space-separated tokens

* **Notes**: Plays a musical note.
* **Frequency**: `"c3"`, `"f#4"`
* **Scale Degree**: `"0"`, `"1"`, `"-1"` (Requires `SOUND.SCALE` to be set).


* **Rests** (`"~"`): A step of silence.
* **Subdivision** (`"[... ...]" `): Groups multiple steps into the timespan of a single step. This allows you to create fast rhythms (tuplets).
* `"c4 c4"` = Two quarter notes (if cycle is 1 bar).
* `"[c4 c4] c4"` = Two eighth notes followed by one quarter note.
* `"c4 [c4 c4 c4]"` = One quarter note followed by eighth note triplets.

##### Sequencer Examples

```basic

' 1. Basic 4-step techno kick (Square wave)
SOUND.SEQ 0, "c2 ~ c2 ~", "SQUARE"

' 2. Fast hi-hats using subdivision (White Noise)
'    "[c4 c4]" fits two hits into one step
SOUND.SEQ 1, "[c4 c4] [c4 c4] [c4 c4] [c4 c4]", "NOISE"

' 3. Melodic pattern using custom Voice design
'    First, design the sound:
SOUND.VOICE 2, "SAW", 0.01, 0.2, 0.0, 0.2
SOUND.FILTER 2, 800
'    Then sequence it using "VOICE" to keep the filter/envelope settings:
SOUND.SEQ 2, "c3 [e3 g3] ~ b3", "VOICE"

```

#### Sound Design (Track Specific)Apply these effects to specific tracks (0-7)

* **`SOUND.GAIN track, volume`**: Sets track volume (1.0 = standard).
* **`SOUND.PAN track, pan`**: Sets stereo panning (0.0=Left, 0.5=Center, 1.0=Right).
* **`SOUND.FILTER track, cutoff_hz`**: Applies a Low-Pass Filter.
* **`SOUND.EQ track, low, mid, high`**: 3-band Equalizer gains (1.0 = Flat).
* **`SOUND.LFO track, freq, depth`**: Applies Vibrato (pitch modulation).
* **`SOUND.FM track, amount, ratio`**: Frequency Modulation for metallic/bell tones.
* **`SOUND.UNISON track, voices, detune, spread`**: Stacks multiple voices for a "Super-Saw" effect.
  * `voices`: 1-16.
  * `detune`: 0.0-1.0.

* **`SOUND.BITCRUSH track, bits, rate`**: Lo-Fi effect.
  * `bits`: 1-16 (Resolution).
  * `rate`: 0.0-1.0 (Sample rate reduction).

* **`SOUND.RINGMOD track, freq, mix`**: Robotic/Sci-Fi modulation.

### Sound Design (Track Specific)

These commands allow you to control how individual tracks interact with global effects and other tracks.

* **`SOUND.REVERBSEND track, amount`**: Sets the amount of the track's signal sent to the global reverb bus.
* `amount`: 0.0 (Dry) to 1.0 (Full Wet).

* **`SOUND.DELAYSEND track, amount`**: Sets the amount of the track's signal sent to the global delay bus.
* `amount`: 0.0 (Dry) to 1.0 (Full Wet).

* **`SOUND.SIDECHAIN target_track, source_track, amount`**: Dynamically "ducks" the volume of the `target_track` based on the volume of the `source_track`.
* `amount`: 0.0 (No ducking) to 1.0 (Full silence when source plays).
* *Example*: `SOUND.SIDECHAIN 1, 0, 0.8` (Makes a synth on track 1 duck when the kick on track 0 hits).

#### Global Effects (Master Bus)

* **`SOUND.DELAY active_bool, time_ms, feedback, mix`**: Stereo Delay

* **`SOUND.REVERB room_size, damping, width, wet`**: Stereo Reverb.
  * `room_size`: 0.0-0.98.
  * `width`: 0.0 (Mono) to 1.0 (Wide).

* **`SOUND.COMPRESSOR thresh, ratio, attack, release, gain`**: Master Dynamics.
  * `thresh`: 0.0-1.0.
  * `ratio`: 1.0-20.0.
  * `attack`/`release`: In milliseconds.

* **`SOUND.DISTORTION amount`**: Master saturation/overdrive.
* **`SOUND.RESET`**: Silences audio, clears sequencer, and resets effects.
* **`SOUND.SHUTDOWN`**: Releases audio resources.
* **`SOUND.DEBUG(enabled_bool)`**: Enables or disables verbose debug tracing from the audio engine — useful for diagnosing voice allocation or sequencer timing issues.

#### Scale QuantizationMaps numbers in patterns (e.g., "0", "1") to musical scales

**`SOUND.SCALE track, root_note$, scale_mode$`**

* **`root_note$`**: E.g., "C3", "F#2".
* **`scale_mode$`**: The type of scale to use.

##### Available Scales

| Scale Mode | Description |
| --- | --- |
| `"CHROMATIC"` | All 12 semitones. |
| `"MAJOR"` | The standard happy/bright scale. |
| `"MINOR"` | The standard sad/emotional scale. |
| `"DORIAN"` | Jazzy, sophisticated minor. |
| `"PHRYGIAN"` | Dark, exotic, "Spanish" flavor. |
| `"LYDIAN"` | Dreamy, sci-fi, "floaty" major. |
| `"MIXOLYDIAN"` | Bluesy major (rock/pop). |
| `"LOCRIAN"` | Tense, dissonant, unstable. |
| `"PENT_MAJ"` | 5-note major scale (very safe, folk/pop). |
| `"PENT_MIN"` | 5-note minor scale (blues/rock riffs). |
| `"BLUES"` | Hexatonic blues scale. |
| `"ARABIC"` | Hijaz scale (Middle-Eastern feel). |

### Visualization & Analysis

Use these functions to retrieve audio data for custom ImGui oscilloscopes or debug monitors.

* **`SOUND.GET_WAVE() -> Array`**: Returns a 1D array of the current master stereo mix (averaged to mono).
* **`SOUND.GET_BUS_WAVE(bus_id) -> Array`**: Returns a 1D array of the audio data currently residing in a specific effect bus.
* `bus_id`: `0` for Reverb, `1` for Delay.

### Example: Custom Studio Monitor

You can combine these new features with **ImGui** to create a live dashboard.

```basic
SCREEN 1280, 720, "jdBasic Sequencer", 2
SOUND.INIT
SOUND.BPM 120
' Setup a pulsing synth with sidechain
SOUND.SEQ 0, "c2 ~ c2 ~", "SQUARE"       ' Kick
SOUND.SEQ 1, "c4 c4 c4 c4", "SAW"        ' Synth
SOUND.SIDECHAIN 1, 0, 0.7               ' Duck synth to kick

' Send synth to reverb
SOUND.REVERB 0.8, 0.5, 1.0, 0.4
SOUND.REVERBSEND 1, 0.6

DO
    CLS
    IF GUI.BEGIN("Master Mixer", 0, 0, 400, 300) THEN
        ' 1. Master Output
        WaveData = SOUND.GET_WAVE()
        GUI.TEXT "Master Output (Stereo Mix)"
        GUI.PLOT_LINES("Output", WaveData, "Live Audio", -1.0, 1.0)
        
        GUI.SEPARATOR()
        
        ' 2. Reverb Bus (Bus ID 0)
        ReverbWave = SOUND.GET_BUS_WAVE(0)
        GUI.PLOT_LINES "Reverb", ReverbWave, "", -0.5, 0.5
    ENDIF
    GUI.END()

    SCREENFLIP
    
    k$ = INKEY$()
    SLEEP 16 
LOOP  UNTIL k$ = "q" OR k$ = "Q"

```

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
* **`TILEMAP.LOAD "map_name", "filename.json"`**: Loads a Tiled map file.
* **`TILEMAP.DRAW_LAYER "map_name", "layer_name", [world_offset_x], [world_offset_y]`**: Draws a specific tile layer from a loaded map.
* **`TILEMAP.GET_OBJECTS("map_name", "object_type") -> Array of Objects`**: Retrieves all objects of a certain type from an object layer.
* **`TILEMAP.COLLIDES(sprite_id, "map_name", "layer_name") -> boolean`**: Checks if a sprite is colliding with any solid tile on a given layer.
* **`TILEMAP.GET_TILE_ID "mapname", "layername", tileX, tileY`**: Returns the tile id from the given position.
* **`TILEMAP.DRAW_DEBUG_COLLISIONS player_id, "map", "layer"`**: For debug purpose. Draws a rect around the tile near x,y. CAM_X and CAM_Y must be set.

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

Here is the documentation extension for **`languages.md`** covering the new Joystick/Gamepad commands.

### Mouse / Joystick / Gamepad Input

* **`MOUSEX -> number`**: Returns the current X coordinate of the mouse in the graphics window.
* **`MOUSEY -> number`**: Returns the current Y coordinate of the mouse in the graphics window.
* **`MOUSEB(button_index)-> number`**: Returns TRUE if the specified mouse button (1=L, 2=M, 3=R) is pressed.

* **`JOY.COUNT() -> number`**: Returns the number of connected joysticks/gamepads.
* **`JOY.NAME$(id) -> string$`**: Returns the name of the joystick at index `id` (0-based).
* **`JOY.BUTTON(id, button_index) -> boolean`**: Returns `TRUE` if the specified button is pressed. Common mapping: 0=A, 1=B, 2=X, 3=Y (mappings vary by controller).
* **`JOY.AXIS(id, axis_index) -> number`**: Returns the axis position value normalized between `-1.0` and `1.0`.
* `0`: Left Stick X
* `1`: Left Stick Y
* `2`: Right Stick X
* `3`: Right Stick Y
* *Note: Mappings may vary slightly depending on the OS and controller driver.*

* **`JOY.HAT(id, hat_index) -> number`**: Returns the hat (D-Pad) state as a bitmask.
* `1`: Up
* `2`: Right
* `4`: Down
* `8`: Left
* *Example: `3` means Up-Right.*

```basic
' Simple Gamepad Loop
PRINT "Connect a gamepad..."
DO
    IF JOY.COUNT() > 0 THEN
        ' Read Left Stick (Axis 0 and 1)
        X = JOY.AXIS(0, 0) 
        Y = JOY.AXIS(0, 1)
        
        ' Read Button A (usually index 0)
        IF JOY.BUTTON(0, 0) THEN PRINT "Fire!"
        
        ' Check D-Pad Up (Bitmask 1)
        IF (JOY.HAT(0, 0) BAND 1) <> 0 THEN PRINT "Going Up!"

        PRINT "Stick: "; X; ", "; Y
    ENDIF
    SLEEP 16
LOOP
```

### Type Functions

* **`TYPEOF(AnyVar)`**: Returns the type of an object as string.

### Thread Functions

This section describes functions for low-level, background-threaded tasks, distinct from the `ASYNC`/`AWAIT` pattern. A function launched with `THREAD` will run in parallel.

* **`THREAD.ISDONE(handle)`**: Returns `TRUE` if the background thread associated with the handle has finished its execution.
* **`THREAD.GETRESULT(handle)`**: Waits for the thread to complete and returns its result. This is a blocking call.

### Background Tasks & Timers

* **`RECUR(interval_ms, code_string$) -> task_id`**: Starts a recursive background task that evaluates and executes a string of jdBasic code every `interval_ms` milliseconds. Returns an integer `task_id`.
* **`CLEAR_RECUR(task_id)`**: Stops and removes an active recursive background task by its ID.
* **`LIST_RECUR() -> array`**: Returns a list of all currently active recurring tasks with their `id`, `interval_ms` and `code`.

### Async Functions

* **`ASYNC FUNC FUNCTIONNAME(args)`**: Marks a function as asynchronius.
* **`AWAIT task`**: Waits for the given task to be completed and returns the result of the function.

### LLM Streaming via Channel (sugar)

`AI.CHAT_TOKENS(llm_id, prompt$, [capacity])` is a channel-flavoured
companion to the callback-based `AI.CHAT_STREAM`. It opens a fresh
channel, spawns a generation thread that pushes each token into it, and
closes the channel when generation completes. Returns the channel
handle immediately — the caller drains it with the standard
`DO/RECV/IS_EOF` idiom. `SEND` blocks the LLM thread when the buffer
fills, so consumer slowness applies natural backpressure to token
generation. Closing the channel from outside cancels the run.

```basic
DIM ch = AI.CHAT_TOKENS(my_llm, "Erkläre Channels in 3 Sätzen.", 64)
DO
    DIM tok$ = CHAN.RECV(ch)
    IF CHAN.IS_EOF(tok$) THEN EXITDO
    PRINT tok$;                  ' stream live to console / GUI
LOOP
PRINT
```

`AI.CHAT_TOKENS` works in **both interp and native compile**. The
generation thread is spawned inside the native handler itself (not via
an `ASYNC FUNC`), so the native-compile gap around `ASYNC FUNC` doesn't
apply.

### File streaming (handle-based reads)

Alongside the slurp-style `TXTREADER$` / `BINREADER$`, jdBasic exposes a
small handle-based API for line-by-line reads and `tail -f` style
follow. Lives in a process-global registry, so a handle opened in one
ASYNC FUNC is usable from any other.

| Native | Signature | Behaviour |
|---|---|---|
| `FILE.OPEN_LINES(path$)` | `STRING → handle` | Open for line-by-line reading. Throws if the file cannot be opened. |
| `FILE.OPEN_TAIL(path$)` | `STRING → handle` | Same as `OPEN_LINES` but `READLINE$` blocks polling for newly-appended data instead of returning EOF. |
| `FILE.READLINE$(handle)` | `handle → STRING` | Returns the next line (CRLF and LF stripped). Tail-mode: blocks until data arrives or the handle is closed. |
| `FILE.AT_EOF(handle)` | `handle → BOOLEAN` | Non-tail: true after the last line. Tail: only true once `FILE.CLOSE` has been called. |
| `FILE.CLOSE(handle)` | `handle` | Idempotent. Wakes any tail reader within ~50ms. |
| `FILE.STREAM_LINES(path$, ch [, cap])` | `STRING, channel handle` | Spawns a producer thread that reads `path$` line by line and pushes each line into `ch`. Closes `ch` when the file is exhausted (or when the consumer closes it from outside). Returns the channel handle for chaining. |
| `FILE.STREAM_TAIL(path$, ch [, cap])` | `STRING, channel handle` | Like `STREAM_LINES` but follows growth. Stops when the consumer closes the channel. |

```basic
' Slurp replaced with streaming — constant memory regardless of file size.
DIM h = FILE.OPEN_LINES("huge.log")
DIM hits = 0
DO
    DIM line$ = FILE.READLINE$(h)
    IF FILE.AT_EOF(h) THEN EXITDO
    IF INSTR(line$, "ERROR") > 0 THEN hits = hits + 1
LOOP
FILE.CLOSE h
PRINT "ERROR lines: "; hits
```

```basic
' Tail-and-grep with channels. Line producer + matcher run in parallel,
' constant memory, capacity 256 buffers a small spike.
DIM ch = CHAN.OPEN(256)
FILE.STREAM_TAIL "app.log", ch
ASYNC FUNC matcher(ch)
    DO
        DIM line$ = CHAN.RECV(ch)
        IF CHAN.IS_EOF(line$) THEN EXITDO
        IF INSTR(line$, "FATAL") > 0 THEN PRINT line$
    LOOP
ENDFUNC
DIM m = matcher(ch)
SLEEP 60000                        ' watch for 60 seconds
CHAN.CLOSE ch                      ' producer + matcher both exit
DIM r = AWAIT m
```

Single-thread use (one thread reads + processes) works in **both interp
and native compile**. The `FILE.STREAM_*` sugars + `tail -f` style and
the concurrent producer/consumer pattern require an `ASYNC FUNC`
running on a separate thread; `ASYNC FUNC` itself is interp-only today
(native compile runs the async body synchronously), so concurrent file
streaming is interp-only until that gap is closed.

### Channels (Phase 1)

Channels are bounded MP/MC queues for ASYNC tasks. Each `ASYNC FUNC` runs
on its own OS thread with a fresh VM copy of globals/funcs; channels live
in a process-global registry indexed by an `i64` handle, so workers can
look up the same Channel regardless of which VM they belong to.

`CHAN.SEND` blocks the calling thread when the buffer is full. `CHAN.RECV`
blocks while empty. `CHAN.CLOSE` wakes everyone — pending RECVers drain
the rest of the buffer and then keep returning the **EOF marker** (a
`MAP { __chan_eof__: TRUE }`, recognised by `CHAN.IS_EOF`). Send on a
closed channel throws.

Native compile supports single-thread use of `CHAN.*` (open / send /
recv / close + the polymorphic `FOR EACH v IN ch` works in interp;
native compile runs single-thread channel access via the same VM-bridge
path as every other native). **Concurrent** channel use across an
`ASYNC FUNC` spawned consumer/producer pair is interp-only today —
native compile currently runs `ASYNC FUNC` synchronously, so a bounded
channel deadlocks if the producer's `SEND` can't drain before the
consumer starts. The channel API itself is identical between modes;
only the spawn primitive lags.

Native channel `RECV` returns the underlying value via the VM-handle
path (so mixed-type payloads survive intact). Numeric values come back
through `f64`, which means STRICT-mode native code needs DOUBLE
accumulators (`DIM total AS DOUBLE = 0.0`) when summing recv'd
integers. Interp keeps the original tag and accepts either.

| Native | Signature | Behaviour |
|---|---|---|
| `CHAN.OPEN(capacity)` | `INTEGER → handle` | `capacity = 0` → unbuffered rendezvous; `> 0` → bounded queue. |
| `CHAN.SEND(ch, value)` | `handle, ANY` | Blocks while buffer full. Throws on a closed channel. |
| `CHAN.RECV(ch)` | `handle → value` | Blocks while empty. Returns the EOF marker on a closed-and-drained channel. |
| `CHAN.CLOSE(ch)` | `handle` | Idempotent. Wakes every parked SEND/RECV. |
| `CHAN.IS_EOF(value)` | `ANY → BOOLEAN` | Tests if a `RECV` result is the EOF marker. |
| `CHAN.IS_CLOSED(ch)` | `handle → BOOLEAN` | Status query. Returns `TRUE` for unknown handles. |
| `CHAN.LEN(ch)` | `handle → INTEGER` | Current buffer depth. |
| `CHAN.CAP(ch)` | `handle → INTEGER` | Configured capacity (for diagnostics). |

```basic
ASYNC FUNC produce(handle, n)
    FOR i = 1 TO n
        CHAN.SEND handle, i
    NEXT
    CHAN.CLOSE handle
ENDFUNC

ASYNC FUNC consume(handle)
    DIM total = 0
    DO
        DIM v = CHAN.RECV(handle)
        IF CHAN.IS_EOF(v) THEN EXITDO
        total = total + v
    LOOP
    RETURN total
ENDFUNC

DIM ch = CHAN.OPEN(4)             ' tiny buffer → real backpressure
DIM p  = produce(ch, 1000)
DIM c  = consume(ch)
PRINT AWAIT c                       ' 500500
```

`FOR EACH v IN ch` iterates the channel until EOF — same syntax as
`FOR EACH x IN [1,2,3]`. The loop blocks on RECV between iterations, so
producer/consumer backpressure works naturally:

```basic
DIM total = 0
FOR EACH v IN ch
    total = total + v
NEXT
```

The explicit `DO ... LOOP` with `CHAN.IS_EOF` is still useful when you
need to inspect the EOF marker directly or interleave multiple channels
manually.

Worker-pool / fan-in / fan-out flows fall out naturally:

```basic
ASYNC FUNC worker(jobs_ch, results_ch)
    FOR EACH path$ IN jobs_ch        ' drains until producer closes jobs_ch
        CHAN.SEND results_ch, parse_one(path$)
    NEXT
ENDFUNC

DIM jobs    = CHAN.OPEN(0)         ' unbuffered → producer waits for a free worker
DIM results = CHAN.OPEN(64)
DIM w1 = worker(jobs, results)
DIM w2 = worker(jobs, results)
FOR EACH path$ IN DIR$("data/*.csv")
    CHAN.SEND jobs, path$
NEXT
CHAN.CLOSE jobs                    ' workers drain, see EOF, exit
DIM r1 = AWAIT w1
DIM r2 = AWAIT w2
```

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

## AI & Machine Learning

jdBasic ships with a full AI stack that runs entirely on the local machine:
ONNX Runtime for classical ML models, llama.cpp for local LLMs (with CUDA
support), dense embeddings, RAG, structured output via GBNF grammars, and a
ready-to-use text classifier. All features are exposed through the `AI.*`
function family.

The stack is optional at build time — the relevant builds are `ONNX` for
ONNX Runtime and `LLM` for llama.cpp (see `build.bat ONNX LLM`).

### ONNX Runtime (Classical ML)

Load any ONNX model (e.g. exported from PyTorch, TensorFlow, scikit-learn)
and run inference on it. Models are referenced by an integer id.

* **`AI.LOAD(path$) -> id`**
  Loads an `.onnx` file and returns a model id.

* **`AI.INFO(id) -> object`**
  Returns `{inputs, outputs}` where each entry contains `name`, `shape` and
  `type` — useful for understanding what a model expects.

* **`AI.RUN(id, input[, input2, ...]) -> result`**
  Runs inference. For single-input models, just pass the input array/tensor;
  for multi-input models, pass one arg per input or a single array of inputs.
  The return type matches the model output shape: a scalar for a 0-D output,
  a flat array for a 1-D output, nested arrays for higher ranks.

* **`AI.FREE(id)`** — releases the model.
* **`AI.LIST() -> array`** — returns the ids of all currently loaded ONNX models.

#### Tensor helpers

* **`AI.TENSOR(data, [shape])`** — creates a tensor value from nested arrays,
  optionally reshaping.
* **`AI.SOFTMAX(vec) -> vec`** — applies softmax to a probability vector.
* **`AI.ARGMAX(vec) -> index`** — returns the index of the largest element.
* **`AI.TOPK(vec, k) -> array`** — returns the top-k `{index, score}` pairs.

##### Example: MNIST digit classification

```basic
DIM m = AI.LOAD("models/mnist.onnx")
PRINT AI.INFO(m){"inputs"}          ' e.g. [{name:"Input3", shape:"1x1x28x28", type:"FLOAT"}]

' Prepare a 28x28 pixel image as a flat float array (0..1)
DIM pixels = ...                    ' length 784
DIM logits = AI.RUN(m, pixels)
DIM probs  = AI.SOFTMAX(logits)
PRINT "Predicted digit: "; AI.ARGMAX(probs)
AI.FREE m
```

### Local LLMs (llama.cpp)

jdBasic embeds llama.cpp with full CUDA acceleration. Models are loaded from
GGUF files and referenced by an integer id. Two model flavors are supported:

1. **Generative models** (Phi-3, Llama, Mistral, ...): `AI.LOAD_LLM`
2. **Embedding models** (nomic-embed, bge-m3, MiniLM, ...): `AI.LOAD_EMBEDDINGS`

Both return an id that the other `AI.*` functions consume.

#### Loading & Configuration

* **`AI.LOAD_LLM(path$, [n_ctx=2048], [n_gpu_layers=99]) -> id`**
  Loads a generative GGUF model. `n_gpu_layers=99` puts the whole model on
  the GPU; set to `0` for pure CPU. `n_batch` is automatically set to
  `n_ctx` so long RAG/tool-call prompts fit in a single decode.

* **`AI.LOAD_EMBEDDINGS(path$, [n_ctx=512], [n_gpu_layers=99]) -> id`**
  Loads an embedding model in encoder mode (mean pooling over the sequence).
  For BERT-based embedders (bge-m3, nomic-embed) set `n_ctx` to the
  model's training context (e.g. 2048 for nomic, 8192 for bge-m3) to avoid
  quality degradation.

* **`AI.SET(id, key$, value)`** — sets a generation parameter. Keys:
  `"temperature"`, `"top_p"`, `"top_k"`, `"min_p"`, `"max_tokens"`, `"seed"`,
  `"system"` (system prompt).

* **`AI.LLM_INFO(id) -> object`** — returns `{n_ctx, n_vocab, n_embd, ...}`.

* **`AI.FREE_LLM(id)`** — releases the model and frees GPU memory.

#### Chat

* **`AI.CHAT(id, prompt$) -> response$`**
  Multi-turn chat. The prompt is built from the model's chat template plus
  the running history maintained in the model itself.

* **`AI.CHAT_STREAM(id, prompt$, callback) -> response$`**
  Same as `AI.CHAT` but tokens are streamed to a user callback
  `FUNC OnToken(piece$) RETURN TRUE`.
  Returning `FALSE` from the callback aborts generation.

* **`AI.CHAT_RAW(id, raw_prompt$) -> text$`**
  Bypasses the chat template and history — send a raw prompt, receive raw
  tokens. Useful for custom templating or completion-style use cases.

* **`AI.CLEAR_HISTORY(id)`** — resets the chat history.
* **`AI.GET_HISTORY(id) -> array`** — returns the raw history
  `[{role, content}, ...]`.
* **`AI.TOKEN_COUNT(id, text$) -> n`** — counts tokens without generating.

#### Tokenizer

* **`AI.TOKENIZE(id, text$) -> array`** — returns the token ids.
* **`AI.DETOKENIZE(id, tokens) -> text$`** — converts ids back to text.

#### Structured Output (GBNF Grammars)

llama.cpp's grammar sampler lets you **force** the model output to match a
context-free grammar, making tool-calling and JSON output reliable even on
small models.

* **`AI.SET_GRAMMAR(id, gbnf$)`**
  Sets an arbitrary GBNF grammar. All subsequent `CHAT`/`CHAT_RAW` calls
  will only produce outputs that match.

* **`AI.CLEAR_GRAMMAR(id)`** — removes the grammar.

* **`AI.SET_JSON_MODE(id)`**
  Convenience — installs the built-in JSON grammar so the model can only
  emit valid JSON.

* **`AI.CHAT_JSON(id, prompt$) -> object`**
  One-shot: temporarily enables JSON mode, generates, parses the response
  via `JSON.PARSE$`, returns the parsed object directly. Any previously
  set grammar is restored afterwards.

##### Example: constrained output

```basic
llm = AI.LOAD_LLM("models/Phi-3-mini-4k-instruct-q4.gguf")

' Option A — one-shot JSON convenience
DIM obj = AI.CHAT_JSON(llm, "Give me Berlin as a JSON with name, country, population.")
PRINT obj{"name"}       ' "Berlin"
PRINT obj{"population"} ' 3769000

' Option B — custom GBNF: yes or no only
AI.SET_GRAMMAR llm, "root ::= (\"yes\" | \"no\")"
PRINT AI.CHAT(llm, "Is Berlin the capital of Germany?")  ' -> "yes"
AI.CLEAR_GRAMMAR llm
```

#### Function Calling / Tool Use

Register jdBasic functions as tools; when the LLM decides it needs one, it
emits `<TOOL>name|arg1|arg2</TOOL>`, which jdBasic intercepts, calls the
function, and feeds the result back into the conversation.

* **`AI.TOOL_ADD(id, name$, params$, description$, funcref)`**
  Registers a tool. `funcref` is a jdBasic function reference (e.g. `MyFunc@`).
  `params$` is a human-readable argument description like `"city_name"` or `"x, y"`.

* **`AI.TOOL_REMOVE(id, name$)`** — unregisters a tool.
* **`AI.TOOL_LIST(id) -> array`** — returns `[{name, params, description}, ...]`.
* **`AI.TOOL_CHAT(id, prompt$, [max_rounds=5]) -> response$`**
  Like `AI.CHAT` but with automatic tool execution. The loop runs up to
  `max_rounds` iterations: LLM → tool call → result → LLM → ...

##### Example: weather tool

```basic
llm = AI.LOAD_LLM("models/Phi-3-mini-4k-instruct-q4.gguf")

FUNC GetWeather(city)
  IF city = "Berlin" THEN RETURN "15C, cloudy"
  IF city = "Miami"  THEN RETURN "32C, sunny"
  RETURN "Unknown city"
ENDFUNC

AI.TOOL_ADD llm, "WEATHER", "city_name", "Get current weather for a city", GetWeather@
PRINT AI.TOOL_CHAT(llm, "What is the weather in Berlin right now?")
```

#### Dense Embeddings

Embedding models produce dense L2-normalized vectors for semantic similarity.

* **`AI.EMBED_LLM(id, text$) -> array of float`**
  Returns the embedding for a text. Works with both embedding-only models
  (recommended) and generative models (lower quality but possible).

* **`AI.EMBED(text$) -> array`**
  Fallback TF-IDF embedding that doesn't need a loaded model. Sparse,
  represented as `[[word, weight], ...]`. Good for simple similarity without
  llama.cpp.

* **`AI.COSINE_SIM(a, b) -> number`** — cosine similarity of two vectors.
* **`AI.NORMALIZE(vec) -> vec`** — L2 normalization.
* **`AI.SIMILARITY(text1$, text2$) -> number`** — quick text similarity using TF-IDF.

##### Example

```basic
emb = AI.LOAD_EMBEDDINGS("models/bge-m3-Q4_K_M.gguf", 2048, 99)
DIM v1 = AI.EMBED_LLM(emb, "Berlin ist die Hauptstadt von Deutschland")
DIM v2 = AI.EMBED_LLM(emb, "Paris ist die Hauptstadt von Frankreich")
PRINT AI.COSINE_SIM(v1, v2)         ' ~0.90 — semantically very close
```

### RAG — Retrieval Augmented Generation

The `RAG_*` family builds a chunked document store, computes embeddings for
each chunk, and lets the LLM answer questions grounded in the retrieved
context. Dense or TF-IDF modes are supported, as is an optional HNSW index
for fast approximate search on large corpora.

#### Creating a store

* **`AI.RAG_CREATE(llm_id, [chunk_size=500], [overlap=50], [embed_llm_id=0]) -> rag_id`**
  `llm_id` is used for answer generation. If `embed_llm_id` is provided and
  points to an `AI.LOAD_EMBEDDINGS` model, the store runs in **dense mode**;
  otherwise it falls back to TF-IDF.

* **`AI.RAG_INFO(rag_id) -> object`**
  Returns statistics including `chunks`, `num_labels`, `mode`
  (`"dense"`/`"tfidf"`), `embed_dim`, `chunk_size`, `index`
  (`"linear"`/`"hnsw"`/`"hnsw_stale"`).

* **`AI.RAG_CLEAR(rag_id)`** — empties the store.
* **`AI.RAG_FREE(rag_id)`** — destroys the store.

#### Adding content

* **`AI.RAG_ADD(rag_id, text$, [source$="inline"]) -> chunks`**
  Adds a single text, splitting it into overlapping chunks. `source$` is
  stored with each chunk as an attribution label.

* **`AI.RAG_ADD_FILE(rag_id, filepath$) -> chunks`**
  Loads a file and adds its contents. Files with a `.pdf` extension are
  automatically parsed with the built-in PDF extractor (supports
  FlateDecode-compressed streams).

* **`AI.RAG_ADD_DIR(rag_id, dirpath$, [pattern$], [recursive=1]) -> stats`**
  Indexes a whole directory. `pattern$` is a simple glob like `*.txt` or
  `*.{md,txt,pdf,cpp,h}`; if omitted, a default list of text file
  extensions plus `.pdf` is used. Returns
  `{files_added, files_failed, total_chunks}`.

#### Searching and querying

* **`AI.RAG_SEARCH(rag_id, query$, [top_k=3]) -> array`**
  Raw similarity search. Returns `[{score, text, source, index}, ...]`.

* **`AI.RAG_QUERY(rag_id, question$, [top_k=3]) -> answer$`**
  Full pipeline: search + build prompt + generate. The system prompt is
  taken from the LLM's `system` setting (via `AI.SET`); if unset, a minimal
  neutral default is used.

* **`AI.RAG_QUERY_FULL(rag_id, question$, [top_k=3]) -> object`**
  Like `AI.RAG_QUERY` but returns `{answer, sources}` where `sources` is the
  list of chunks used (with their `score`, `source`, `text`, `index`).

* **`AI.RAG_QUERY_STREAM(rag_id, question$, callback, [top_k=3]) -> answer$`**
  Streaming variant — tokens are delivered to the callback as they are
  generated.

#### HNSW fast index

For large corpora (>10k chunks) build an HNSW index once, then all searches
become approximate-but-fast.

* **`AI.RAG_BUILD_INDEX(rag_id, [M=16], [ef_construction=200])`**
  Builds the index from the currently stored chunks. Requires dense mode.
  New chunks added afterwards invalidate the index (shown as `hnsw_stale`)
  — call again to rebuild.

#### Persistence

Indexes are serialized to a single binary file (magic `JRAG`). The optional
HNSW graph is saved too, so a restart doesn't need to rebuild it.

* **`AI.RAG_SAVE(rag_id, path$)`** — writes the store to disk.
* **`AI.RAG_LOAD(path$, [llm_id], [embed_llm_id]) -> rag_id`**
  Loads a previously saved store. For dense indexes, the `embed_llm_id`
  must point to the same kind of embedding model used at save time.

##### Example: RAG over a directory

```basic
llm = AI.LOAD_LLM("models/Phi-3-mini-4k-instruct-q4.gguf")
emb = AI.LOAD_EMBEDDINGS("models/bge-m3-Q4_K_M.gguf", 2048, 99)

AI.SET llm, "system", "You are an expert on the jdBasic source code."

rag = AI.RAG_CREATE(llm, 500, 50, emb)
AI.RAG_ADD_DIR rag, "src", "*.{cpp,h}", 1
AI.RAG_BUILD_INDEX rag

' Query with sources
DIM r = AI.RAG_QUERY_FULL(rag, "How does the HALT opcode work?")
PRINT r{"answer"}
FOR i = 0 TO LEN(r{"sources"}) - 1
  PRINT "  - "; r{"sources"}[i]{"source"}; " ("; r{"sources"}[i]{"score"}; ")"
NEXT i

' Persist for next run
AI.RAG_SAVE rag, "src_index.idx"
```

### Text Classifier (k-NN on Embeddings)

A full-featured nearest-neighbor text classifier — a **training-free**
alternative to fine-tuning BERT for sentence classification. Works with any
`AI.LOAD_EMBEDDINGS` model; bge-m3 gives excellent results on multilingual
text.

Predictions are majority votes weighted by similarity over the top-k
neighbors, so the classifier returns both a **label** and a **confidence**.
It supports batch import, HNSW acceleration, and persistence in the same
way as the RAG store.

#### API

* **`AI.CLASSIFIER_CREATE(embed_llm_id) -> cls_id`**
  Creates an empty classifier bound to an embedding model.

* **`AI.CLASSIFIER_ADD(cls_id, text$, label$) -> count`**
  Adds a single training sample (embeds it immediately).

* **`AI.CLASSIFIER_ADD_BATCH(cls_id, texts_arr, labels_arr) -> count`**
  Batch-imports two arrays of equal length. Prints progress every ~5%.

* **`AI.CLASSIFIER_PREDICT(cls_id, text$, [k=5]) -> object`**
  Returns:
  ```
  {
    label       : best-scoring class,
    confidence  : share of the winning class's weight (0..1),
    neighbors   : [{score, label, text, index}, ...],
    votes       : [{label, count}, ...]  (sorted by count desc)
  }
  ```

* **`AI.CLASSIFIER_BUILD_INDEX(cls_id, [M=16], [ef_construction=200])`**
  Builds an HNSW index over the training samples for fast prediction on
  large datasets (10k+ samples).

* **`AI.CLASSIFIER_INFO(cls_id) -> object`**
  Returns `{samples, embed_dim, num_labels, labels, index}` where `labels`
  is a list of `{label, count}` entries.

* **`AI.CLASSIFIER_SAVE(cls_id, path$)`** — serializes to a binary file
  (magic `JCLF`, includes the optional HNSW graph).

* **`AI.CLASSIFIER_LOAD(path$, embed_llm_id) -> cls_id`**
  Loads a previously saved classifier. Must use the same embedding model.

* **`AI.CLASSIFIER_FREE(cls_id)`** — releases the classifier.

##### Example: ticket categorization

```basic
emb = AI.LOAD_EMBEDDINGS("models/bge-m3-Q4_K_M.gguf", 2048, 99)
clf = AI.CLASSIFIER_CREATE(emb)

' Train from a CSV: label in column 0, text in column 1
DIM rows = CSVREADER("tickets.csv", ";", 0)
DIM texts AS ARRAY
DIM labels AS ARRAY
FOR i = 0 TO LEN(rows) - 1
  PUSH labels, rows[i][0]
  PUSH texts,  rows[i][1]
NEXT i
AI.CLASSIFIER_ADD_BATCH clf, texts, labels

' Fast index
AI.CLASSIFIER_BUILD_INDEX clf

' Predict a new ticket
DIM p = AI.CLASSIFIER_PREDICT(clf, "My printer is showing a red blinking light.", 7)
PRINT p{"label"}        ' e.g. "Hardware | Printer | LED"
PRINT p{"confidence"}   ' 0.71

' Persist for later
AI.CLASSIFIER_SAVE clf, "tickets.clf"
```

### Tensor & Auto-Diff (experimental)

jdBasic also ships a small in-process autograd engine for experimentation
with neural networks written directly in BASIC. See the source in
`src/tensor.cpp` / `src/autograd.cpp` for the currently supported ops — this
subsystem is considered experimental and may change.

## The Integrated Editor

The `EDIT` command launches a simple, built-in text editor.

### Keyboard Shortcuts

* **Arrow Keys, PageUp, PageDown**: Navigate text. Holding SHIFT for marking the text.
* **`Ctrl+Q`**: Exit the editor.
* **`Ctrl+S`**: Save the current file. If the file is unnamed, you will be prompted for a name.
* **`Ctrl+F`**: Find text. You will be prompted for a search query.
* **`Ctrl+P`**: Fast paste clipboard text (preserves formatting!)
* **`Ctrl+C`**: Copy selected test
* **`Ctrl+X`**: Cut selected test
* **`Ctrl+V`**: Paste selected test
* **`F3`**: Find the next occurrence of the last search query.
* **`Ctrl+G`**: Go to a specific line number.
