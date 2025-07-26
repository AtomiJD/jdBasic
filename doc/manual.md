# The Official jdBasic Manual

-----

## 1\. Introduction

Welcome to `jdBasic`\!

`jdBasic` is not your grandfather's BASIC. It's a modern, powerful interpreter that blends the simplicity and familiarity of classic BASIC with advanced concepts from functional and array-oriented programming.

Whether you're looking to relive the nostalgia of 8-bit coding with modern conveniences or explore powerful data processing paradigms in a simple syntax, `jdBasic` offers a unique and capable environment. The language is designed to be easy to learn but powerful enough to build complex, modular applications, including simulations, games, and even advanced Artificial Intelligence models.

### Key Features

`jdBasic` is packed with features that bridge the gap between retro and modern programming:

  * **Classic BASIC Foundations**: Familiar control flow with `FOR...NEXT`, `GOTO`, `IF...THEN...ELSE`, and a rich library of string and math functions.
  * **APL-Inspired Array Programming**: A first-class, N-dimensional array system with element-wise operations, reductions, linear algebra, and powerful data transformation functions.
  * **Modern Enhancements**: Modular programming with `IMPORT`, typed variables (`DIM var AS TYPE`), and rich data types like `Map` and `DateTime`.
  * **Functional Programming Core**: Treat functions as values, pass them to other functions (higher-order functions), and write elegant recursive solutions.
  * **Built-in AI & Tensor Engine**: A complete suite of `TENSOR` functions for building and training complex neural networks, from simple RNNs to multi-layer Transformers, right inside the interpreter.

### A Glimpse of jdBasic

Here are a few samples to showcase the versatility of the language.

**1. Classic Looping**

```basic
FOR i = 1 TO 5
    PRINT "Iteration: "; i
NEXT i
```

**2. User Input**

```basic
INPUT "What is your name? "; name$
PRINT "Hello, "; name$
```

**3. Simple Array Math**

```basic
my_numbers = [10, 20, 30, 40, 50]
my_numbers = my_numbers * 2 + 5
PRINT my_numbers
```

**4. Higher-Order Functions**

```basic
FUNC SQUARE_IT(X)
    RETURN X * X
ENDFUNC

' The SELECT function applies a function to each element of an array
result = SELECT(SQUARE_IT@, [1, 2, 3, 4])
PRINT result ' Output: [1, 4, 9, 16]
```

**5. File I/O**

```basic
' Read the entire content of a file into a string
file_content$ = TXTREADER$("my_file.txt")
PRINT file_content$
```

**6. Graphics and Sprites**

```basic
' Create a window and moves an enemy sprite
SCREEN 800, 600, "My Game"
SPRITE.LOAD 1, "enemy.png"
ENEMY_ID = SPRITE.CREATE(1, 100, 100)
SPRITE.SET_VELOCITY ENEMY_ID, 20, 15

DO 
    CLS
    SPRITE.UPDATE 
    SPRITE.DRAW_ALL 0,0
    SCREENFLIP

    A$ = INKEY$()
    SLEEP 20 
LOOP UNTIL A$ > ""
```

**7. Matrix Multiplication**

```basic
A = [[1, 2], [3, 4]]
B = [[5, 6], [7, 8]]
C = MATMUL(A, B)
PRINT C ' Output: [[19, 22], [43, 50]]
```

**8. Bitwise Operator**

```basic
A = %0101  
B = %0011  

PRINT "A = 5 (%0101), B = 3 (%0011)"
PRINT "-----------------------------"
PRINT "A BAND B: "; A BAND B  ' %0101 AND %0011 = %0001 -> 1
PRINT "A BOR B:  "; A BOR B   ' %0101 OR  %0011 = %0111 -> 7
PRINT "A BXOR B: "; A BXOR B  ' %0101 XOR %0011 = %0110 -> 6
PRINT
```

**9. COM Automation**

```basic
' Automate Microsoft Excel
objXL = CREATEOBJECT("Excel.Application")
objXL.Visible = TRUE
wb = objXL.Workbooks.Add()
objcell = objXL.ActiveSheet.Cells(1, 1)
objcell.Value = "Hello from jdBasic!"
objXL.Quit()
```

**10. A Simple AI Tensor**

```basic
' Create a tensor and perform a forward/backward pass
A = TENSOR.FROM([[1, 2], [3, 4]])
B = TENSOR.FROM([[5, 6], [7, 8]])
C = TENSOR.MATMUL(A, B)
TENSOR.BACKWARD C
PRINT "Tensor:"
PRINT FRMV$(TENSOR.TOARRAY(c))
```

-----

## 2\. First Steps

This chapter covers the absolute essentials to get you writing and running your first `jdBasic` programs.

### Running a Program

To run a `jdBasic` program, save your code in a text file (e.g., `test.jdb`) and pass the filename to the interpreter from your command line:

```sh
# Replace 'jdBasic' with the actual executable name on your system
jdBasic test.jdb
```

### The `Ready` Prompt

If you run `jdBasic` without a filename, you'll enter the interactive prompt. Here you can use the integrated development commands.

  * **`EDIT`**: Opens the built-in text editor to write or modify code.
  * **`RUN`**: Compiles and executes the code currently in memory.
  * **`LIST`**: Displays the code currently in memory to the console.
  * **`SAVE "filename.bas"`**: Saves the code in memory to a file.
  * **`LOAD "filename.bas"`**: Loads a source file from disk into memory.
  * **`STOP`**: Halts program execution, preserving variables. You can inspect them and then type `RESUME` to continue.

### Your First Program: Hello, World\!

The `PRINT` command is used to display text or variable values to the console.

```basic
' This is a comment. The interpreter ignores it.
PRINT "Hello, World!"
```

### Variables and Data Types

Variables are created on their first use. Their type is determined by the value assigned. String variable names traditionally end with a `$` suffix.

**1. Numeric Variables**

```basic
A = 10
B = 20.5
C = A + B
PRINT C ' Output: 30.5
```

**2. String Variables**

```basic
GREETING$ = "Welcome to jdBasic"
PRINT GREETING$
```

**3. Concatenation**

```basic
FIRST_NAME$ = "John"
LAST_NAME$ = "Doe"
FULL_NAME$ = FIRST_NAME$ + " " + LAST_NAME$
PRINT FULL_NAME$ ' Output: John Doe
```

### Getting User Input

The `INPUT` command pauses the program and waits for the user to type something and press Enter.

**4. Simple Input**

```basic
PRINT "What is your favorite number?"
INPUT N
PRINT "Your favorite number squared is: "; N * N
```

**5. Input with a Prompt**

```basic
INPUT "Enter your age: "; AGE
PRINT "In 10 years, you will be "; AGE + 10; " years old."
```

### Simple Control Flow

`IF` statements allow you to run code conditionally.

**6. A Simple `IF` Statement**

```basic
INPUT "Enter a number: "; X
IF X > 100 THEN PRINT "That's a big number!"
```

**7. An `IF...ELSE` Block**

```basic
INPUT "Guess the secret word: "; GUESS$
IF LCASE$(GUESS$) = "basic" THEN
    PRINT "You guessed it!"
ELSE
    PRINT "Sorry, that's not correct."
ENDIF
```

### Basic Looping

The `FOR...NEXT` loop is the easiest way to repeat a block of code a specific number of times.

**8. Simple `FOR` loop**

```basic
FOR I = 1 TO 10
  PRINT "This is loop number: "; I
NEXT I
```

**9. Loop with a `STEP`**

```basic
PRINT "Counting down from 10:"
FOR I = 10 TO 0 STEP -1
    PRINT I
    SLEEP 500 ' Pause for 500 milliseconds
NEXT I
PRINT "Blast off!"
```

**10. Clearing the Screen**
The `CLS` command clears the console.

```basic
PRINT "This will be cleared."
SLEEP 2000
CLS
PRINT "The screen is now clean."
```

-----

## 3\. Beginner Guide to jdBasic

This section expands on the basics, introducing the core structures you'll use to build more complex programs.

### Control Flow

**1. The `GOTO` statement**
`GOTO` unconditionally jumps to a labeled line of code. Labels are defined by a name ending with a colon (`:`).

```basic
i = 0
my_loop:
  PRINT "i is now "; i
  i = i + 1
  IF i < 10 THEN GOTO my_loop

PRINT "Loop finished."
```

**2. `DO...LOOP`**
`DO...LOOP` provides more flexible looping with `WHILE` or `UNTIL` conditions at the beginning or end.

```basic
' do/while/loop test
i=0
do while i < 5
 i=i+1
 print "LOOP #: "; I
loop
```

**3. `EXITDO` and `EXITFOR`**
You can exit a loop prematurely with `EXITDO` or `EXITFOR`.

```basic
' Exit a loop when i is 3
i=0
do
 i=i+1
 if i = 3 THEN
    print "Exit do at i: "; I
    exitdo
 endif
 print "LOOP #: "; I
loop until i > 5
```

### Working with Arrays

Arrays are lists or tables of data. You can declare them with `DIM` or create them with a modern literal syntax.

**4. Declaring and Accessing Arrays**

```basic
' Declare an array of 20 elements, indexed 0-19
DIM A[20]

FOR i = 0 to 19
  A[i] = i * 2
NEXT i

PRINT "The 10th element is: "; A[9] ' Output: 18
```

**5. Array Literals**
This is the modern way to create an array with initial values.

```basic
my_numbers = [10, 20, 30, 40, 50]
my_strings$ = ["alpha", "beta", "gamma"]

PRINT my_strings$[1] ' Output: beta
```

**6. Getting Array Length**
The `LEN()` function returns the number of elements in an array.

```basic
data = [1, 1, 2, 3, 5, 8, 13]
PRINT "There are "; LEN(data); " elements in the array."
```

### String Functions

`jdBasic` has a rich set of functions for working with text.

**7. Manipulating Strings**

```basic
GREETING$ = "   Hello, World!   "
PRINT "Original: '"; GREETING$; "'"
PRINT "Trimmed: '"; TRIM$(GREETING$); "'"
PRINT "Lower Case: "; LCASE$(GREETING$)
PRINT "Upper Case: "; UCASE$(GREETING$)
```

**8. Extracting Substrings**

```basic
FILENAME$ = "document.txt"
PRINT "First 8 chars: "; LEFT$(FILENAME$, 8)  ' Output: document
PRINT "Last 4 chars: "; RIGHT$(FILENAME$, 4) ' Output: .txt
PRINT "Middle part: "; MID$(FILENAME$, 10, 3) ' Output: txt
```

### Functions and Subroutines

Organize your code into reusable blocks with `SUB` (procedures) and `FUNC` (functions).

**9. A Simple Subroutine**
A `SUB` performs actions but does not return a value.

```basic
SUB GreetUser(name$)
    PRINT "Hello, "; name$; "! Welcome."
ENDSUB

' Call the subroutine
GreetUser("Alice")
GreetUser("Bob")
```

**10. A Simple Function**
A `FUNC` performs calculations and returns a value using `RETURN`.

```basic
FUNC Add(a, b)
    RETURN a + b
ENDFUNC

result = Add(15, 27)
PRINT "15 + 27 = "; result ' Output: 42
```

-----

## 4\. All Language Features (Reference)

This chapter serves as a technical reference for `jdBasic`'s syntax, commands, and functions.

### Data Types

| Type        | Description                                                                 | Example Suffix/Creation                               |
|-------------|-----------------------------------------------------------------------------|-------------------------------------------------------|
| **Boolean** | `TRUE` or `FALSE`                                                           | `is_active = TRUE`                                    |
| **Number** | 64-bit double-precision floating-point number.                              | `x = 10.5`                                            |
| **String** | Text of variable length.                                                    | `A$`                                                  |
| **DateTime**| Stores date and time values.                                                | `CVDATE("2025-01-01")`, `NOW()`                     |
| **Array** | A multi-dimensional list of other Basic values.                             | `DIM A[10]`, `my_array = [1,2,3]`                     |
| **Map** | A key-value dictionary. Keys are strings.                                   | `DIM M AS MAP`, `M{"key"} = value`                    |
| **Tensor** | Opaque type for AI, supports automatic differentiation.                     | `T = TENSOR.FROM(my_array)`                           |
| **ComObject**| Represents a COM Automation object.                                         | `CREATEOBJECT("Excel.Application")`                   |

### Commands

**System & Flow Control**

  * **`CLS`**: Clears the console screen.
  * **`COLOR fg, bg`**: Sets text colors.
  * **`IF...THEN...ELSE...ENDIF`**: Conditional block.
  * **`FOR...TO...STEP...NEXT`**: Looping construct.
  * **`DO...LOOP [WHILE/UNTIL]`**: Flexible looping.
  * **`GOTO label`**: Jumps to a code label.
  * **`TRY ... CATCH ... FINALLY ... ENDTRY`**: Structured error handling. See section below.
  * **`SLEEP ms`**: Pauses for milliseconds.
  * **`STOP`**: Halts execution, can be resumed.

**Code Sample 1: Error Handling**

```basic
DIM A[5]
TRY
    A[6] = 10 ' This will cause an "Array out of bounds" error
CATCH
    PRINT "Error occurred!"
    PRINT "Code: "; ERR
    PRINT "Line: "; ERL
    PRINT "Message: "; ERRMSG$
ENDTRY

PRINT "Program finished normally."
```

**Development & Debugging**

  * **`RUN`**: Compiles and runs the code in memory.
  * **`LIST`**: Lists the source code to the console.
  * **`EDIT`**: Opens the integrated text editor.
  * **`LOAD "file"`**: Loads a source file into memory.
  * **`SAVE "file"`T**: Saves memory to a source file.
  * **`TRON` / `TROFF`**: Turns instruction tracing on/off for debugging.

**Code Sample 2: `TRON`**

```basic
TRON ' Turn trace on
A = 10
B = 20
C = A + B
PRINT C
TROFF ' Turn trace off
```

**Filesystem**

  * **`DIR [path]`**: Lists files and directories.
  * **`CD "path"`**: Changes the current directory.
  * **`PWD`**: Prints the current working directory.
  * **`MKDIR "path"`**: Creates a new directory.
  * **`KILL "file"`**: Deletes a file.

**Code Sample 3: Listing Files**

```basic
PRINT "Files in current directory:"
DIR
```

### Functions

**String Functions**

  * **`LEFT$(s$, n)`**, **`RIGHT$(s$, n)`**, **`MID$(s$, start, len)`**: Extract substrings.
  * **`LEN(expr)`**: Returns length.
  * **`LCASE$(s$)`**, **`UCASE$(s$)`**, **`TRIM$(s$)`**: Case and whitespace.
  * **`STR$(num)`**, **`VAL(str$)`**: Convert between number and string.
  * **`CHR$(code)`**, **`ASC(char$)`**: ASCII conversions.
  * **`INSTR(haystack$, needle$)`**: Find substring position.
  * **`SPLIT(source$, delim$)`**: Split string to an array.

**Code Sample 4: `SPLIT`**

```basic
CSV_LINE$ = "2025-06-17,45.3,102.1"
DATA_POINTS = SPLIT(CSV_LINE$, ",")
PRINT "Data points: "; DATA_POINTS
PRINT "The second data point is: "; DATA_POINTS[1]
```

**Date/Time Functions**

  * **`DATE$` / `TIME$`**: Returns current date/time as a string.
  * **`NOW()`**: Returns a `DateTime` object.
  * **`CVDATE(str$)`**: Converts a "YYYY-MM-DD" string to a `DateTime` object.
  * **`DATEADD(part$, num, date)`**: Adds an interval to a date.

**Code Sample 5: `DATEADD`**

```basic
deadline = CVDate("2025-07-20")
extended_deadline = DATEADD("D", 10, deadline) ' Add 10 days
PRINT "Extended deadline is "; extended_deadline
```

**File I/O Functions**

  * **`TXTREADER$(filename$)`**: Reads a whole text file into a string.
  * **`TXTWRITER file$, content$`**: Writes a string to a text file.
  * **`CSVREADER(file$, ...)`**: Reads a CSV file into a 2D array.
  * **`CSVWRITER file$, array, ...`**: Writes a 2D array to a CSV file.

**Code Sample 6: `CSVREADER`**

```basic
' Load the CSV file, skipping the header row.
TEMP_DATA = CSVREADER("temps.csv", ",", TRUE)
PRINT "Data loaded successfully."
```

**COM Automation**

  * **`CREATEOBJECT(progID$)`**: Creates a COM Automation object.

**Code Sample 7: Automating Excel**

```basic
' Initialize a COM object for Excel
objXL = CREATEOBJECT("Excel.Application")
objXL.Visible = TRUE
wb = objXL.Workbooks.Add()
objXL.ActiveSheet.Cells(1, 1).Value = "Hello from jdBasic!"
' Clean up
objXL.Quit()
```

**JSON Functions**

  * **`JSON.PARSE$(json_string$)`**: Parses a JSON string into a special `JsonObject`.
  * **`JSON.STRINGIFY$(map_or_array)`**: Converts a `Map` or `Array` into a JSON string.

**Code Sample 8: Parsing JSON**

```basic
RESPONSE$ = '{"choices":[{"message":{"content":"Hello!"}}]}'
RESPONSE_JSON = JSON.PARSE$(RESPONSE$)
AI_MESSAGE$ = RESPONSE_JSON{"choices"}[0]{"message"}{"content"}
PRINT AI_MESSAGE$ ' Output: Hello!
```

**Map Functions**

  * A `Map` is a key-value store.
  * **`MAP.EXISTS(map, key$)`**: Checks if a key exists.
  * **`MAP.KEYS(map)`**: Returns an array of all keys.
  * **`MAP.VALUES(map)`**: Returns an array of all values.

**Code Sample 9: Using a Map**

```basic
DIM person AS MAP
person{"name"} = "John Doe"
person{"age"} = 42

IF MAP.EXISTS(person, "name") THEN
    PRINT "Name is: "; person{"name"}
ENDIF
```

**Numerical Integration**

  * **`INTEGRATE(function@, limits, rule)`**: Performs numerical integration (calculus).

**Code Sample 10: `INTEGRATE`**

```basic
' Define the function we want to integrate.
FUNC SQUARE(X)
    RETURN X * X
ENDFUNC

' Integrate f(x) = x^2 from 0 to 3
limits = [0, 3]
result = INTEGRATE(SQUARE@, limits, 3)
PRINT "Numerical result:", result ' Output is approx 9.0
```

-----

## 5\. Pro Guide to Array, Vector, and Matrix Functions

This is where `jdBasic` transcends classic BASIC and becomes a powerful tool for data science, simulation, and high-performance computing. The language's APL-inspired array functions allow you to express complex operations on entire datasets without writing slow, manual loops.

### The Core Principle: Element-Wise Operations

Most standard math operators (`+`, `-`, `*`, `/`, `^`) work on entire arrays at once.

**1. Array-Scalar Math**

```basic
V = [10, 20, 30, 40]
PRINT "V * 2: "; V * 2         ' Output: [20, 40, 60, 80]
PRINT "V + 100: "; V + 100     ' Output: [110, 120, 130, 140]
```

**2. Array-Array Math**
If two arrays have the same shape, you can perform math between them element by element.

```basic
A = [1, 2, 3]
B = [10, 20, 30]
PRINT "A + B: "; A + B ' Output: [11, 22, 33]
```

### Array Creation and Shaping

**3. `IOTA`**
Generates a 1D array of integers from 1 to N. It's perfect for creating ranges.

```basic
' Create an array of numbers from 1 to 10
my_range = IOTA(10)
PRINT my_range
```

**4. `RESHAPE`**
Takes the data from a source array and pours it into a new array with different dimensions.

```basic
' Create a 1D array of 12 numbers
flat_array = IOTA(12)

' Reshape it into a 3x4 matrix (3 rows, 4 columns)
matrix = RESHAPE(flat_array, [3, 4])
PRINT FRMV$(matrix) ' FRMV$ formats matrices for nice printing
```

### Reductions: Aggregating Data

Reduction functions take an array and reduce it to a single value.

**5. Numeric and Boolean Reductions**

```basic
V = [2, 3, 4, 5, 6]
PRINT "Vector is "; V
PRINT "SUM: "; SUM(V)         ' Output: 20
PRINT "PRODUCT: "; PRODUCT(V) ' Output: 720
PRINT "MIN: "; MIN(V)         ' Output: 2
PRINT "MAX: "; MAX(V)         ' Output: 6

B = [TRUE, FALSE, TRUE]
PRINT "ANY(B): "; ANY(B)     ' Output: TRUE
PRINT "ALL(B): "; ALL(B)     ' Output: FALSE
```

**6. Reduction Along a Dimension**
For matrices, you can specify a dimension to reduce along (0 for rows, 1 for columns).

```basic
M = RESHAPE(IOTA(9), [3,3])
PRINT "Matrix M:"; M
PRINT "Sum of each column: "; SUM(M, 0) ' Output: [12 15 18]
PRINT "Sum of each row: "; SUM(M, 1)    ' Output: [6 15 24]
```

### Slicing, Dicing, and Transforming

**7. `SLICE`**
Extracts a full row or column from a 2D matrix.

```basic
M = RESHAPE(IOTA(12), [3, 4])
' Extract row 1 (the second row, index 1)
ROW1 = SLICE(M, 0, 1)
PRINT "Row 1 of M: "; ROW1 ' Output: [5 6 7 8]

' Extract column 2 (the third column, index 2)
COL2 = SLICE(M, 1, 2)
PRINT "Column 2 of M: "; COL2 ' Output: [3 7 11]
```

**8. `TAKE` and `DROP`**
Select or discard elements from the beginning or end of an array.

```basic
V = IOTA(8)
PRINT "TAKE(3, V) = "; TAKE(3, V)     ' Output: [1 2 3]
PRINT "DROP(3, V) = "; DROP(3, V)     ' Output: [4 5 6 7 8]
```

**9. `TRANSPOSE` and `REVERSE`**
`TRANSPOSE` flips a matrix over its diagonal. `REVERSE` reverses the elements in an array.

```basic
M = [[1,2],[3,4]]
PRINT "Transpose of M: "; TRANSPOSE(M) ' Output: [[1, 3], [2, 4]]

V = [10, 20, 30]
PRINT "Reverse of V: "; REVERSE(V)     ' Output: [30, 20, 10]
```

### Advanced Linear Algebra and Set Theory

**10. `MATMUL` (Matrix Multiplication)**
Performs standard (dot product) matrix multiplication.

```basic
A = [[1, 2], [3, 4]]
B = [[5, 6], [7, 8]]
C = MATMUL(A, B)
PRINT "MATMUL(A, B) ="; C  ' Output: [[19, 22], [43, 50]]
```

**11. `SOLVE` and `INVERT`**
`SOLVE` finds the vector `x` in the linear system `Ax = b`. `INVERT` computes the inverse of a square matrix.

```basic
' Solve the system Ax = b
A = [[2, 1, -1], [-3, -1, 2], [-2, 1, 2]]
b = [8, -11, -3]
x = SOLVE(A, b)
PRINT "Solution vector x:"; x ' Expected: [2, 3, -1]
```

**12. `OUTER`**
Creates an outer product table by applying an operator or function to all pairs of elements from two vectors.

```basic
V1 = [10, 20, 30]
V2 = [1, 2]
' Create a multiplication table
mult_table = OUTER(V1, V2, "*")
PRINT mult_table
' Output:
' [[10, 20],
'  [20, 40],
'  [30, 60]]
```

**13. APL-Style Prime Sieve (Example)**
This complex example shows how array functions can be combined to create elegant, high-performance solutions. This version finds primes by creating a set of odd numbers and subtracting a set of composite numbers.

```basic
LIMIT = 1000
' 1. Define potential primes: odd numbers from 3 to the limit.
ODDS = (IOTA(LIMIT/2) - 1) * 2 + 3

' 2. Define potential prime factors.
FACTORS = []
FOR I = 3 TO SQR(LIMIT) STEP 2: FACTORS = APPEND(FACTORS, I): NEXT I

' 3. Generate all odd composite multiples.
COMPOSITES = []
FOR I = 0 TO LEN(FACTORS)-1
    P = FACTORS[I]
    FOR J = P * P TO LIMIT STEP P * 2
        COMPOSITES = APPEND(COMPOSITES, J)
    NEXT J
NEXT I

' 4. Calculate the set difference.
ODD_PRIMES = DIFF(ODDS, COMPOSITES)

' 5. The final list is 2 plus all the odd primes.
ALL_PRIMES = APPEND([2], ODD_PRIMES)
PRINT ALL_PRIMES
```

-----

## 6\. Expert Guide to Artificial Intelligence

`jdBasic` includes a sophisticated, built-in engine for Artificial Intelligence, centered around the `TENSOR` object. This allows you to move beyond manual calculus and build, train, and deploy complex neural networks using high-level, declarative functions.

### Part I: From Manual to Automatic - The `TENSOR` Object

Previously, to train a network, we had to manually implement the backpropagation equations. The `TENSOR` object automates this.

  * **What is a `TENSOR`?** It's a multi-dimensional array that tracks its own computational history.
  * **`TENSOR.FROM(array)`**: Converts a regular array into a `Tensor`.
  * **`TENSOR.CREATE_LAYER(...)`**: A factory for building neural network layers (e.g., "DENSE", "ATTENTION") with correctly initialized weight and bias tensors.
  * **`TENSOR.BACKWARD loss_tensor`**: The key to autodiff. Call this on your final loss tensor, and `jdBasic` will automatically calculate the gradients for every parameter that contributed to it.
  * **`TENSOR.UPDATE(model, optimizer)`**: After `BACKWARD` has run, this function uses the calculated gradients to update the model's parameters according to an optimizer's rules.

**1. A Simple Autodiff Example**

```basic
' Create two simple tensors
A = TENSOR.FROM([[1, 2], [3, 4]])
B = TENSOR.FROM([[5, 6], [7, 8]])

' Forward Pass (this operation is tracked)
C = TENSOR.MATMUL(A, B)

' Backward Pass (calculate gradients for A and B automatically)
TENSOR.BACKWARD C

' Check the calculated gradients
PRINT "Gradient of A:"; TENSOR.TOARRAY(A.grad)
```

**2. A Trainable XOR Network with Tensors**
Compare this to the manual version in the `jdbasic_nn_course.md`. The logic is the same, but the implementation is cleaner and more powerful.

```basic
' 1. Define Training Data as Tensors
INPUTS = TENSOR.FROM([[0,0], [0,1], [1,0], [1,1]])
TARGETS = TENSOR.FROM([[0], [1], [1], [0]])

' 2. Define the Model using the layer factory
MODEL = {}
MODEL{"layers"} = [
    TENSOR.CREATE_LAYER("DENSE", {"input_size": 2, "units": 3}),
    TENSOR.CREATE_LAYER("DENSE", {"input_size": 3, "units": 1})
]

' 3. Setup Optimizer
OPTIMIZER = TENSOR.CREATE_OPTIMIZER("SGD", {"learning_rate": 0.1})

' 4. The Training Loop
FOR epoch = 1 TO 10000
    ' Forward Pass
    ACTIVATED_HIDDEN = TENSOR.SIGMOID(MATMUL(INPUTS, MODEL{"layers"}[0]{"weights"}) + MODEL{"layers"}[0]{"bias"})
    PREDICTIONS = TENSOR.SIGMOID(MATMUL(ACTIVATED_HIDDEN, MODEL{"layers"}[1]{"weights"}) + MODEL{"layers"}[1]{"bias"})

    ' Calculate Loss (Mean Squared Error)
    LOSS = SUM((TARGETS - PREDICTIONS) ^ 2) / 4

    ' Autodiff and Update!
    TENSOR.BACKWARD LOSS
    MODEL = TENSOR.UPDATE(MODEL, OPTIMIZER)
NEXT epoch
```

### Part II: Building Modern Language Models

With the `TENSOR` engine, we can build state-of-the-art architectures.

**3. A Recurrent Neural Network (RNN)**
RNNs have a form of memory, passing a "hidden state" from one time-step to the next. This makes them ideal for sequential data like text.

```basic
' Simplified RNN Forward Pass Function
FUNC FORWARD_PASS(input_token_tensor, prev_h1, prev_h2)
    ' Embedding lookup converts a token ID into a vector
    embedded = TENSOR.MATMUL(TENSOR.FROM(ONE_HOT_ENCODE(...)), MODEL[0]{"weights"})

    ' RNN Cell 1: Combine input with previous hidden state
    new_h1 = TENSOR.RELU(TENSOR.MATMUL(embedded + prev_h1, MODEL[1]{"weights"}) + MODEL[1]{"bias"})

    ' RNN Cell 2
    new_h2 = TENSOR.RELU(TENSOR.MATMUL(new_h1 + prev_h2, MODEL[2]{"weights"}) + MODEL[2]{"bias"})

    ' Output layer predicts the next token
    logits = TENSOR.MATMUL(new_h2, MODEL[3]{"weights"}) + MODEL[3]{"bias"}

    ' Return prediction and the new hidden states for the next step
    RETURN [logits, new_h1, new_h2]
ENDFUNC
```

**4. The `TENSOR.CROSS_ENTROPY_LOSS` Function**
This is the standard loss function for classification tasks, like predicting the next character in a vocabulary. It's more suitable than Mean Squared Error for this purpose.

```basic
' Inside the training loop
loss_tensor = TENSOR.CROSS_ENTROPY_LOSS(logits_tensor, target_tensor)
```

**5. `TENSOR.TOKENIZE`**
This helper function quickly converts a string into an array of integer token IDs based on a vocabulary map you provide.

```basic
TEXT_DATA$ = "hello world"
VOCAB_MAP = {"h":0, "e":1, "l":2, ...}
INPUT_TOKENS_ARRAY = TENSOR.TOKENIZE(TEXT_DATA$, VOCAB_MAP)
' Result: [0, 1, 2, 2, 3, ...]
```

**6. `TENSOR.SOFTMAX` and Sampling**
The `SOFTMAX` function converts a vector of raw prediction scores (logits) into a probability distribution. You can then `SAMPLE` from this distribution to generate creative text.

```basic
' During text generation
predicted_probs_tensor = TENSOR.SOFTMAX(logits_tensor)
probs_array = TENSOR.TOARRAY(predicted_probs_tensor)
next_token_id = SAMPLE(probs_array)
```

**7. A Transformer: The Engine of Modern LLMs**
The Transformer architecture abandons recurrence in favor of a powerful **self-attention** mechanism, allowing it to process all tokens in a sequence simultaneously and learn complex relationships between them.

```basic
' A simplified view of a Transformer Block Forward Pass
FUNC TRANSFORMER_FORWARD(x)
    ' --- 1. Pre-LN Self-Attention ---
    ' First, normalize the input
    norm_out = TENSOR.LAYERNORM(x, ...)
    ' Project input into Query, Key, and Value matrices
    Q = TENSOR.MATMUL(norm_out, Wq)
    K = TENSOR.MATMUL(norm_out, Wk)
    V = TENSOR.MATMUL(norm_out, Wv)
    ' Calculate attention scores and probabilities
    attn_scores = TENSOR.MATMUL(Q, TENSOR.TRANSPOSE(K)) / SQR(HIDDEN_DIM)
    attn_probs = TENSOR.SOFTMAX(attn_scores, TRUE) ' TRUE enables causal masking
    ' Apply attention to the Value matrix
    attention_output = TENSOR.MATMUL(attn_probs, V)
    ' Add result back to original input (residual connection)
    x = x + attention_output

    ' --- 2. Pre-LN Feed-Forward Network ---
    norm2_out = TENSOR.LAYERNORM(x, ...)
    ffn_out = TENSOR.RELU(TENSOR.MATMUL(norm2_out, FFN1_W) + FFN1_B)
    ffn_out = TENSOR.MATMUL(ffn_out, FFN2_W) + FFN2_B
    x = x + ffn_out ' Second residual connection

    RETURN x
ENDFUNC
```

**8. Advanced Layers: `POSITIONAL_ENCODING` and `LAYERNORM`**

  * `TENSOR.POSITIONAL_ENCODING(seq_len, d_model)`: Since Transformers see all tokens at once, this function generates a special tensor that gives the model information about the position of each token in the sequence.
  * `TENSOR.LAYERNORM(...)`: A key technique for stabilizing the training of deep networks like Transformers.

**9. Saving and Loading Models**
A crucial part of any ML workflow is the ability to save your trained model and load it back later for inference.

```basic
' After training is complete
PRINT "--- Saving Model ---"
TENSOR.SAVEMODEL MODEL, "llm_model.json"
PRINT "Model saved."

' In another program, or later in the same one
PRINT "--- Loading Model for Inference ---"
LOADED_MODEL = TENSOR.LOADMODEL("llm_model.json")
PRINT "Model loaded successfully."
```

**10. Convolutional Neural Networks (CNNs) for Images**
`jdBasic` also supports the core layers for image processing.

  * `TENSOR.CONV2D(...)`: Performs a 2D convolution, applying filters (kernels) to an input image to detect features like edges or textures.
  * `TENSOR.MAXPOOL2D(...)`: A down-sampling operation that reduces the size of feature maps, making the network more efficient and robust.

<!-- end list -->

```basic
' Example of a CNN forward pass
' Apply a Sobel edge detection kernel to an image
INPUT_TENSOR = TENSOR.FROM(my_image_array)
KERNEL_TENSOR = TENSOR.FROM(sobel_kernel_array)
BIAS_TENSOR = TENSOR.FROM([0, 0])

' Apply the convolutional layer
FEATURE_MAPS = TENSOR.CONV2D(INPUT_TENSOR, KERNEL_TENSOR, BIAS_TENSOR, 1, 0)

' Apply a max pooling layer
POOLED_OUTPUT = TENSOR.MAXPOOL2D(FEATURE_MAPS, 2, 2)
```