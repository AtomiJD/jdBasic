# jdBasic Language Reference

This document describes the syntax, commands, and functions for the jdBasic (NeReLa Basic) interpreter.

## Data Types

jdBasic supports a variety of data types. While variables are variants and can hold any type, they can be explicitly created using the `DIM` statement.

* **Boolean**: `TRUE` or `FALSE`.
* **Number**: 64-bit double-precision floating-point numbers.
* **String**: Text of variable length. String variable names traditionally end with a `$` suffix (e.g., `A$`).
* **DateTime**: A type for storing date and time values, created with `NOW()` or `CVDATE()`.
* **Array**: A multi-dimensional array of other Basic values.
* **Map**: A key-value dictionary where keys are strings and values can be any Basic value. Used for creating complex data structures.
* **JsonObject**: A special type returned by `JSON.PARSE$`, which can be accessed like a Map or Array.
* **ComObject**: A special type returned by `CREATEOBJECT`, representing an instance of a COM Automation object.

## Variables and Assignment

Variables are created on their first use or explicitly with `DIM`.

**`DIM var [AS type]`**
Declares a variable. The `AS` clause is used for specific types.

```
DIM X AS INTEGER
DIM N AS DOUBLE
DIM S AS STRING
DIM D AS DATE
DIM M AS MAP
```

**`DIM array[size1, size2, ...]`**
Declares an N-dimensional array with given sizes.

```
' A vector with 10 elements (0-9)
DIM A[10]

' A 5x3 matrix
DIM M[5, 3]
```

**Literal Assignment**
Variables can be created by assigning a literal value. This is the modern way to create arrays.

```
A = 10
B$ = "hello"
C = TRUE
MyArray = [1, 2, 3, 4] ' Creates an array
EmptyArray = []
```

## Chained Access Syntax

jdBasic supports a modern, chained syntax for accessing elements within nested data structures, which is especially useful for JSON and COM objects.

**JSON and Map/Array Chaining**
You can chain `{"key"}` and `[index]` accessors to navigate complex objects returned by `JSON.PARSE$`.

```
RESPONSE_JSON = JSON.PARSE$(RESPONSE$)

' Access nested data in a single line
AI_MESSAGE$ = RESPONSE_JSON{"choices"}[0]{"message"}{"content"}
```

**COM Chaining**
You can chain property accesses and method calls for COM objects.

```
objXL = CREATEOBJECT("Excel.Application")
objXL.Visible = TRUE
wb = objXL.Workbooks.Add()
objXL.ActiveSheet.Cells(1, 1).Value = "Hello from a jdBasic!"
```

## Commands (Procedures)

### System & Flow Control

* **`CLS [r, g, b]`**: Clears the console screen, or the graphics window to the specified RGB color if active.
* **`COLOR fg, bg`**: Sets the foreground and background colors for console text.
* **`CURSOR state`**: Turns the console cursor on (`1`) or off (`0`).
* **`GOTO label`**: Jumps execution to a `label:`.
* **`IF condition THEN ... [ELSE ...] ENDIF`**: Conditional execution block. Single-line `IF condition THEN statement` is also supported.
* **`FOR ... TO ... STEP ... NEXT`**: Defines a loop that repeats a specific number of times.
* **`DO ... LOOP [WHILE/UNTIL condition]`**: Defines a loop that continues as long as a condition is met or until a condition is met.
* **`EXITFOR`**: Immediately exits the current `FOR...NEXT` loop.
* **`EXITDO`**: Immediately exits the current `DO...LOOP`.
* **`ON ERROR CALL sub_name`**: Sets a global error handler. If an error occurs, the specified subroutine is called.
* **`OPTION option$`**: Sets a VM option. `OPTION "NOPAUSE"` disables the ESC/Space break/pause functionality.
* **`LOCATE row, col`**: Moves the console text cursor to a specific row and column.
* **`RESUME [NEXT | "label"]`**: Used within an error handler to resume execution. `RESUME` retries the failed line, `RESUME NEXT` continues on the next line, and `RESUME "label"` jumps to a label.
* **`SLEEP milliseconds`**: Pauses execution for a specified duration.
* **`STOP`**: Halts program execution and returns to the `Ready` prompt, preserving variable state. Execution can be continued with `RESUME`.

### Development & Debugging

* **`COMPILE`**: Compiles the source code currently in memory into p-code.
* **`DUMP`**: Dumps the p-code of the main program or a loaded module to the console for debugging.
* **`EDIT`**: Opens the integrated text editor with the current source code.
* **`LIST`**: Lists the current source code in memory to the console.
* **`LOAD "filename"`**: Loads a source file from disk into memory.
* **`RUN`**: Compiles and runs the program currently in memory.
* **`SAVE "filename"`**: Saves the source code in memory to a file on disk.
* **`TRON` / `TROFF`**: Turns instruction tracing on or off.

### Filesystem

* **`DIR [path]`**: Lists files and directories. Supports wildcards like `*` and `?`.
* **`CD "path"`**: Changes the current working directory.
* **`PWD`**: Prints the current working directory.
* **`MKDIR "path"`**: Creates a new directory.
* **`KILL "filename"`**: Deletes a file.

## Functions

### Graphics (SDL)

* **`SCREEN width, height, [title$]`**: Initializes and shows a graphics window.
* **`PSET x, y, [r, g, b]`**: Sets a single pixel to a specified RGB color.
* **`LINE x1, y1, x2, y2, [r, g, b]`**: Draws a line.
* **`RECT x, y, w, h, [r, g, b], [fill_bool]`**: Draws a rectangle. `fill_bool` is `TRUE` for a filled rectangle.
* **`CIRCLE x, y, radius, [r, g, b]`**: Draws a circle.
* **`TEXT x, y, content$, [r, g, b]`**: Renders text onto the graphics screen.
* **`SCREENFLIP`**: Updates the graphics window to display all drawing commands issued since the last flip.
* **`MOUSEX()` / `MOUSEY()`**: Returns the current X or Y coordinate of the mouse cursor within the graphics window.
* **`MOUSEB(button)`**: Returns `TRUE` if the specified mouse button is pressed (`1`: left, `2`: middle, `3`: right).

### Sound (SDL)

* **`SOUND.INIT`**: Initializes the audio system. Must be called before any other sound command.
* **`SOUND.VOICE track, waveform$, attack, decay, sustain, release`**: Configures a sound voice on a track (0-7).
  * `waveform$`: "SINE", "SQUARE", "SAW", "TRIANGLE"
  * `attack`, `decay`, `release`: Time in seconds.
  * `sustain`: Volume level from 0.0 to 1.0.
* **`SOUND.PLAY track, frequency`**: Plays a note with a given frequency (in Hz) on the specified track.
* **`SOUND.RELEASE track`**: Begins the 'release' phase of the ADSR envelope for the note on the specified track.
* **`SOUND.STOP track`**: Immediately stops the note on the specified track.

### String Functions

* **`LEFT$(str$, n)`**, **`RIGHT$(str$, n)`**, **`MID$(str$, start, [len])`**: Extracts parts of a string.
* **`LEN(expression)`**: Returns the length of the string representation of an expression, or a vector of the dimensions if the argument is an array.
* **`LCASE$(str$)`**, **`UCASE$(str$)`**, **`TRIM$(str$)`**: Manipulates string case and whitespace.
* **`STR$(number)`**, **`VAL(string$)`**: Converts between numbers and strings.
* **`CHR$(ascii_code)`**, **`ASC(char$)`**: Converts between ASCII codes and characters.
* **`INSTR([start, ]haystack$, needle$)`**: Finds the position of one string within another.
* **`SPLIT(source$, delimiter$)`**: Splits a string by a delimiter and returns a 1D array of strings.
* **`FORMAT$(format_string$, arg1, ...)`**: Formats a string using C++20-style `{}` placeholders.
* **`FRMV$(array)`**: Formats a 1D or 2D array into a right-aligned string matrix for printing.
* **`INKEY$`**: Returns a single character from the keyboard buffer if a key has been pressed, otherwise returns an empty string. Works for both console and SDL graphics window.

### Math Functions

* **`SIN(n)`**, **`COS(n)`**, **`TAN(n)`**: Standard trigonometric functions (argument in radians).
* **`SQR(n)`**: Returns the square root of a number.
* **`RND(n)`**: Returns a random number between 0.0 and 1.0.
* **`FAC(n)`**: Returns the factorial of an integer.

### Array & Matrix Functions

* **`APPEND(array, value)`**: Appends a scalar value or all elements of another array to a given array, returning a new flat 1D array.
* **`DIFF(array1, array2)`**: Returns a new array containing elements that are in `array1` but not in `array2`.
* **`IOTA(N)`**: Generates a 1D array of numbers from 1 to N.
* **`Reduction (SUM, PRODUCT, MIN, MAX, ANY, ALL)`**: Functions that reduce an array to a single value (e.g.,  `SUM(my_array)` or a vector `SUM(my_array, dimension)`). Dimension is 0 for reduce along rows and 1 for columns.
* **`TAKE(N, array)`**, **`DROP(N, array)`**: Takes or drops N elements from the beginning (or end if N is negative) of an array.
* **`RESHAPE(array, shape_vector)`**: Creates a new array with new dimensions from the data of a source array.
* **`REVERSE(array)`**: Reverses the elements of an array.
* **`TRANSPOSE(matrix)`**: Transposes a 2D matrix.
* **`MATMUL(matrixA, matrixB)`**: Performs matrix multiplication.
* **`MVLET(matrix, dimension, index, vector) -> matrix`**: Replaces a row or column in a matrix with a vector, returning a new matrix.
* **`INTEGRATE(function@, limits, rule)`**: It parses arguments, performs the coordinate transformation, and loops through the Gauss points to calculate the final sum.
* **`SOLVE(matrix A, vextor b) -> vector_x`**: Solves the linear system Ax = b for the unknown vector x.
* **`INVERT(matrix) -> matrix`**: Computes the inverse of a square matrix.
* **`SLICE(matrix, dim, index)`**: Extracts a row (`dim=0`) or column (`dim=1`) from a 2D matrix.
* **`GRADE(vector)`**: Returns the indices that would sort the vector.
* **`OUTER(vecA, vecB, op$ or funcref)`**: Creates an outer product table using an operator (+, -, *, /, MOD, >, <, =, ^) or a reference to a function (srq@).

### Map Functions

* **`MAP.EXISTS(map, key$)`**: Returns `TRUE` if the specified key exists in the map.
* **`MAP.KEYS(map)`**: Returns a 1D array containing all the keys from the map.
* **`MAP.VALUES(map)`**: Returns a 1D array containing all the values from the map.

### JSON, COM, and HTTP Functions

* **`JSON.PARSE$(json_string$)`**: Parses a JSON string and returns a special `JsonObject`.
* **`JSON.STRINGIFY$(map_or_array)`**: Takes a `Map` or `Array` variable and returns its compact JSON string representation.
* **`CREATEOBJECT(progID$)`**: Creates a COM Automation object (e.g., "Excel.Application") and returns a `ComObject`.
* **`HTTP.GET$(URL$)`**: Performs an HTTP GET request and returns the response body as a string.
* **`HTTP.POST$(URL$, Data$, ContentType$)`**: Performs an HTTP POST request.
* **`HTTP.PUT$(URL$, Data$, ContentType$)`**: Performs an HTTP PUT request.
* **`HTTP.SETHEADER(Name$, Value$)`**: Sets a custom header for the next HTTP request.
* **`HTTP.CLEARHEADERS()`**: Clears all custom headers.
* **`HTTP.STATUSCODE()`**: Returns the HTTP status code from the last request.

### File I/O Functions

* **`TXTREADER$(filename$)`**: Reads an entire text file into a single string variable.
* **`TXTWRITER filename$, content$`**: Writes a string variable to a text file.
* **`CSVREADER(filename$, [delimiter$], [has_header])`**: Reads a CSV file into a 2D array.
* **`CSVWRITER filename$, array, [delimiter$], [header_array]`**: Writes a 2D array to a CSV file.

### System and Time Functions

* **`GETENV$(var_name$)`**: Gets the value of a system environment variable.
* **`SETLOCALE("locale_string")`**: Sets the locale for number formatting (e.g., "en-US" or "de-DE").
* **`TICK()`**: Returns the number of milliseconds since the program started.
* **`DATE$` / `TIME$`**: Returns the current system date/time as a string.
* **`NOW()`**: Returns a `DateTime` object for the current moment.
* **`DATEADD(part$, num, date)`**: Adds an interval to a `DateTime` object.
* **`CVDATE(date_string$)`**: Converts a string ("YYYY-MM-DD") to a `DateTime` object.

## The Integrated Editor

The `EDIT` command launches a simple, built-in text editor.

### Keyboard Shortcuts

* **Arrow Keys, PageUp, PageDown**: Navigate text.
* **`Ctrl+X`**: Exit the editor.
* **`Ctrl+S`**: Save the current file. If the file is unnamed, you will be prompted for a name.
* **`Ctrl+F`**: Find text. You will be prompted for a search query.
* **`F3`**: Find the next occurrence of the last search query.
* **`Ctrl+G`**: Go to a specific line number.