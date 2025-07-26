# NeReLa Basic Language Reference

This document describes the syntax, commands, and functions for the NeReLa Basic interpreter.

## Data Types

NeReLa Basic supports a variety of data types. While variables are variants and can hold any type, they can be explicitly created using the `DIM` statement.

* **Boolean**: `TRUE` or `FALSE`.
* **Number**: 64-bit double-precision floating-point numbers.
* **String**: Text of variable length. String variable names traditionally end with a `$` suffix (e.g., `A$`).
* **DateTime**: A type for storing date and time values, created with `NOW()` or `CVDATE()`.
* **Array**: A multi-dimensional array of other Basic values.
* **Map**: A key-value dictionary where keys are strings and values can be any Basic value. Used for creating complex data structures.
* **Tensor**: An opaque data type that holds multi-dimensional floating-point data and tracks computational history for automatic differentiation (autodiff). It is the core of the AI functions and enables building and training neural networks.
* **JsonObject**: A special type returned by `JSON.PARSE$`, which can be accessed like a Map or Array.
* **ComObject**: A special type returned by `CREATEOBJECT`, representing an instance of a COM Automation object.

## Variables and Assignment

Variables are created on their first use or explicitly with `DIM`.

**`DIM var [AS type]`**
Declares a variable. The `AS` clause is used for specific types.

```basic
DIM X AS INTEGER
DIM N AS DOUBLE
DIM S AS STRING
DIM D AS DATE
DIM M AS MAP
DIM T AS TENSOR
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

### Bitwise Operators

These operators perform bit-level calculations on numeric values, which are treated as 64-bit integers.

**`BAND`**: (Bitwise AND): 5 BAND 3 -> 1 (%0101 & %0011 = %0001)

**`BOR`**: (Bitwise OR): 5 BOR 3 -> 7 (%0101 | %0011 = %0111)

**`BXOR`**: (Bitwise XOR): 5 BXOR 3 -> 6 (%0101 ^ %0011 = %0110)

### Logical Operators

These operators are used in conditional logic, such as IF statements.

**`AND`**:, **`OR`**:, **`XOR`**:: Standard logical operators. They evaluate both sides of the expression and support element-wise operations on arrays.

**`ANDALSO`**:: A short-circuiting version of AND. If the left side is FALSE, the right side is never evaluated. This is safer for chained conditions.

**`ORELSE`**:: A short-circuiting version of OR. If the left side is TRUE, the right side is never evaluated.

```basic
' This is safe because the second part is never run if MyMap is NULL
IF MyMap <> NULL ANDALSO MAP.EXISTS(MyMap, "key") THEN ...
```

## Chained Access Syntax

NeReLa Basic supports a modern, chained syntax for accessing elements within nested data structures, which is especially useful for JSON, COM objects, and Tensors.

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
PRINT SELECT(lambda i -> i + 1, iota(10))
PRINT SELECT(lambda i -> i + 1, iota(10)) |> FILTER(lambda val -> val > 5, ?) |> SELECT(lambda v -> v * 10, ?)
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

PRINT 10 JD@ 5, 10 JD@ 0, iota(10) jd@ 2, iota(10) jd@ iota(10)*2, 2 jd@ [1,2,4]
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

### System & Flow Control

* **`CLS`**: Clears the console screen.
* **`COLOR fg, bg`**: Sets the foreground and background colors for text.
* **`CURSOR state`**: Turns the cursor on (`1`) or off (`0`).
* **`GOTO label`**: Jumps execution to a `label:`.
* **`IF condition THEN ... [ELSE ...] ENDIF`**: Conditional execution block. Single-line `IF condition THEN statement` is also supported.
* **`FOR ... TO ... STEP ... NEXT`**: Defines a loop that repeats a specific number of times.
* **`DO ... LOOP [WHILE/UNTIL condition]`**: Defines a loop that continues as long as a condition is met or until a condition is met.
* **`TRY ... CATCH ... FINALLY ... ENDTRY`**: Structured error handling. See section below.
* **`OPTION option$`**: Sets a VM option. `OPTION "NOPAUSE"` disables the ESC/Space break/pause functionality.
* **`SLEEP milliseconds`**: Pauses execution for a specified duration.
* **`STOP`**: Halts program execution and returns to the `Ready` prompt, preserving variable state. Execution can be continued with `RESUME`.
* **`IMPORT [modul]`**: Loads the jdBasic module. Ex. IMPORT MATH imports the file math.jdb
* **`EXPORT MODUL [module]`**: Marks a file as EXPORT for importing with IMPORT
* **`IMPORTDLL [funcfile]`**: Loads the funcfile.dll as dynmaic library and register all included functions for jdBasic.

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
* **`DIR$([path])->String`**: Lists files and directories and return them as string array. Supports wildcards like `*` and `?`.
* **`CD "path"`**: Changes the current working directory.
* **`PWD`**: Prints the current working directory.
* **`MKDIR "path"`**: Creates a new directory.
* **`KILL "filename"`**: Deletes a file.


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

## Functions

### JSON Functions

* **`JSON.PARSE$(json_string$)`**: Parses a JSON string and returns a special `JsonObject`. This object can be accessed like a `Map` or an `Array`.
* **`JSON.STRINGIFY$(map_or_array)`**: Takes a `Map` or `Array` variable and returns its compact JSON string representation. Ideal for creating API payloads.

### COM Automation Functions

* **`CREATEOBJECT(progID$)`**: Creates a COM Automation object (e.g., "Excel.Application") and returns a `ComObject`.

### String Functions

* **`LEFT$(str$, n)`**, **`RIGHT$(str$, n)`**, **`MID$(str$, start, [len])`**: Extracts parts of a string.
* **`LEN(expression)`**: Returns the length of the string representation of an expression.
* **`LCASE$(str$)`**, **`UCASE$(str$)`**, **`TRIM$(str$)`**: Manipulates string case and whitespace.
* **`STR$(number)`**, **`VAL(string$)`**: Converts between numbers and strings.
* **`CHR$(ascii_code)`**, **`ASC(char$)`**: Converts between ASCII codes and characters.
* **`INSTR([start, ]haystack$, needle$)`**: Finds the position of one string within another.
* **`SPLIT(source$, delimiter$)`**: Splits a string by a delimiter and returns a 1D array of strings.

### Math/Arithmetic/Round Functions

* **`SIN(numeric expression or array)`**: Returns the Sinus.
* **`COS(numeric expression or array)`**: Returns the Cosinus.
* **`TAN(numeric expression or array)`**: Returns the Tangens.
* **`SQR(numeric expression or array)`**: Returns the Square Root
* **`RND(numeric expression or array)`**: Returns a random value
* **`FAC(numeric expression or array)`**: Factorial Function.
* **`INT(numeric expression or array)`**: Traditional BASIC integer function (floor)
* **`FLOOR(numeric expression or array)`**: Rounds down.
* **`CEIL(numeric expression or array)`**: Rounds up.
* **`TRUNC(numeric expression or array)`**: Truncates toward zero.

### Regular Expression Functions

* **`REGEX.MATCH(pattern$, text$)`**: Checks if the entire `text$` string matches the `pattern$`.
  * Returns `TRUE` or `FALSE` if the pattern has no capture groups.
  * If the pattern contains capture groups `(...)`, it returns a 1D array of the captured substrings upon a successful match, otherwise `FALSE`.
* **`REGEX.FINDALL(pattern$, text$)`**: Finds all non-overlapping occurrences of `pattern$` in `text$`.
  * Returns a 1D array of all matches found.
  * If the pattern contains capture groups, it returns a 2D array where each row contains the groups for a single match.
* **`REGEX.REPLACE(pattern$, text$, replacement$)`**: Replaces all occurrences of `pattern$` in `text$` with `replacement$`. The replacement string can use backreferences like `$1`, `$2` to insert captured group content.

### Array & Matrix Functions

* **`APPEND(array, value)`**: Appends a scalar value or all elements of another array to a given array, returning a new flat 1D array.
* **`DIFF(array1, array2)`**: Returns a new array containing elements that are in `array1` but not in `array2`.
* **`IOTA(N)`**: Generates a 1D array of numbers from 1 to N.
* **`Reduction (SUM, PRODUCT, MIN, MAX, ANY, ALL)`**: Functions that reduce an array to a single value (e.g., `SUM(my_array)`) or a vector (`SUM(my_array, dimension)`). Dimension is 0 for reduce along rows and 1 for columns.
* **`SCAN(operator, array) -> array`**: Performs a cumulative reduction (scan) along the last axis of an array.
* **`SELECT(function@, array) -> array`**: Applies a user-defined function to each element of an array, returning a new array with the same dimensions containing the transformed elements. The provided function must accept exactly one argument.
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
* **`STACK(dimension, array1, array2, ...) -> matrix`**: Stacks 1D vectors into a 2D matrix.
* **`SLICE(matrix, dim, index)`**: Extracts a row (`dim=0`) or column (`dim=1`) from a 2D matrix.
* **`GRADE(vector)`**: Returns the indices that would sort the vector.
* **`OUTER(vecA, vecB, op$ or funcref)`**: Creates an outer product table using an operator (+, -, \*, /, MOD, \>, \<, =, ^) or a reference to a function (srq@).
* **`ROTATE(array, shift_vector) -> array`**: Cyclically shifts an N-dimensional array.
* **`SHIFT(array, shift_vector, [fill_value]) -> array`**: Non-cyclically shifts an N-dimensional array.
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

### HTTP Functions

* **`HTTP.GET$(url$)`**: Performs an HTTP GET request and returns the response body as a string.
* **`HTTP.POST$(url$, data$, contentType$)`**: Performs an HTTP POST request with the given data and content type, returning the response body.
* **`HTTP.PUT$(url$, data$, contentType$)`**: Performs an HTTP PUT request.
* **`HTTP.POST_ASYNC(url$, data$, contentType$)`**: Performs an HTTP POST request asynchronously, returning a task handle that can be used with `AWAIT`.
* **`HTTP.SETHEADER(name$, value$)`**: Sets a custom header for subsequent HTTP requests.
* **`HTTP.CLEARHEADERS()`**: Clears all custom HTTP headers.
* **`HTTP.STATUSCODE()`**: Returns the HTTP status code from the last request.

### Graphics and Multimedia Functions

#### Graphics

* **`SCREEN width, height, [title$]`**: Initializes a graphics window of the specified size.
* **`SCREENFLIP`**: Updates the screen to show all drawing operations performed since the last flip.
* **`DRAWCOLOR r, g, b`**: Sets the current drawing color using RGB values (0-255).
* **`SETFONT filename$, size`**: Sets the current font to filename$ and size.
* **`PSET x, y, [r, g, b] OR PSET matrix, [colors]`**: Draws a single pixel at the specified coordinates. Can also take a matrix of points.
* **`LINE x1, y1, x2, y2, [r, g, b] OR LINE matrix, [colors]`**: Draws a line between two points. Can also take a matrix of lines.
* **`RECT x, y, w, h, [r, g, b], [fill] OR RECT matrix, [fill], [colors]`**: Draws a rectangle. `fill` is a boolean. Can also take a matrix of rectangles.
* **`CIRCLE x, y, r, [r, g, b] OR CIRCLE matrix, [colors]`**: Draws a circle. Can also take a matrix of circles.
* **`TEXT x, y, content$, [r, g, b]`**: Draws a string of text on the graphics screen.
* **`PLOTRAW x, y, matrix, [scaleX, scaleY]`**: Draws a matrix of color values directly to the screen at a given position and scale.

#### Sound

* **`SOUND.INIT()`**: Initializes the audio system. Must be called before other sound functions.
* **`SOUND.VOICE track, waveform$, attack, decay, sustain, release`**: Configures the ADSR envelope and waveform for a sound track.
* **`SOUND.PLAY track, frequency`**: Plays a note at a specific frequency on the given track.
* **`SOUND.RELEASE track`**: Starts the release phase of the note on the given track.
* **`SOUND.STOP track`**: Immediately stops the note on the given track.
* **`SFX.LOAD id, "filepath.wav"`**: Loads a WAV file to slot id.
* **`FX.PLAY id`**: Plays a WAV file with slot id.
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
  
* **`TUERTLE.FORWARD distance`**: Moves the turte forward with the distance at the given angle.
* **`TUERTLE.BACKWARD distance`**: Moves the turte backward with the distance at the given angle.
* **`TUERTLE.LEFT degrees`**: Subtract degrees to the turles angle.
* **`TUERTLE.RIGHT degrees`**: Adds degrees to the turles angle.
* **`TUERTLE.PENUP`**: Stop drawing while moving.
* **`TUERTLE.PENDOWN`**: Begins drawing while moving.
* **`TUERTLE.SETPOS x, y`**: Set the turle position to x,y
* **`TUERTLE.SETHEADING degrees`**: Set the turtles angle to the degres
* **`TUERTLE.HOME`**: Move the turtles position to the center of the cancas
* **`TUERTLE.DRAW`**: Redraws the entire path the turtle has taken so far.
* **`TUERTLE.CLEAR`**: Clears the turtle's path memory. Does not clear the screen.
* **`TUERTLE.SET_COLOR r, g, b`**: Set the turtles draw color to r,g,b

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

This suite of functions provides the building blocks for creating and training neural networks directly within NeReLa Basic. The core component is the `Tensor` data type, which supports automatic differentiation.

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