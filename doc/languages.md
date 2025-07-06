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

## Commands

### System & Flow Control

  * **`CLS`**: Clears the console screen.
  * **`COLOR fg, bg`**: Sets the foreground and background colors for text.
  * **`CURSOR state`**: Turns the cursor on (`1`) or off (`0`).
  * **`GOTO label`**: Jumps execution to a `label:`.
  * **`IF condition THEN ... [ELSE ...] ENDIF`**: Conditional execution block. Single-line `IF condition THEN statement` is also supported.
  * **`FOR ... TO ... STEP ... NEXT`**: Defines a loop that repeats a specific number of times.
  * **`DO ... LOOP [WHILE/UNTIL condition]`**: Defines a loop that continues as long as a condition is met or until a condition is met.
  * **`ON ERROR CALL sub_name`**: Sets a global error handler. If an error occurs, the specified subroutine is called.
  * **`OPTION option$`**: Sets a VM option. `OPTION "NOPAUSE"` disables the ESC/Space break/pause functionality.
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
  * **`DIR$([path])->String`**: Lists files and directories and return them as string array. Supports wildcards like `*` and `?`.
  * **`CD "path"`**: Changes the current working directory.
  * **`PWD`**: Prints the current working directory.
  * **`MKDIR "path"`**: Creates a new directory.
  * **`KILL "filename"`**: Deletes a file.

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

### Array & Matrix Functions

  * **`APPEND(array, value)`**: Appends a scalar value or all elements of another array to a given array, returning a new flat 1D array.
  * **`DIFF(array1, array2)`**: Returns a new array containing elements that are in `array1` but not in `array2`.
  * **`IOTA(N)`**: Generates a 1D array of numbers from 1 to N.
  * \*\*Reduction (SUM, PRODUCT, MIN, MAX, ANY, ALL)` **: Functions that reduce an array to a single value (e.g.,   `SUM(my\_array)`or a vector`SUM(my\_array, dimension)\`). Dimension is 0 for reduce along rows and 1 for columns.
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
  * **`OUTER(vecA, vecB, op$ or funcref)`**: Creates an outer product table using an operator (+, -, \*, /, MOD, \>, \<, =, ^) or a reference to a function (srq@).
  * **`APPEND(array, value) -> array`**: Appends a value or another array to an array, returning a new array.
  * **`ROTATE(array, shift_vector) -> array`**: Cyclically shifts an N-dimensional array.
  * **`SHIFT(array, shift_vector, \[fill_value\]) -> array`**: Non-cyclically shifts an N-dimensional array.
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
  * **`DATEADD(part$, num, date)`**: Adds an interval to a `DateTime` object.
  * **`CVDATE(date_string$)`**: Converts a string ("YYYY-MM-DD") to a `DateTime` object.

  ### Type Functions

  * **`TYPEOF(AnyVar)`**: Returns the type of an object as string.

  ### Async Functions

  * **`ASYNC FUNC FUNCTIONNAME(args)`**: Marks a function as asynchronius.
  * **`AWAIT task`**: Waits for the given task to be completed and returns the result of the function.

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