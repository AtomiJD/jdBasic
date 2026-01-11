# The Official jdBasic Handbook: Ultimate Edition

**Version**: 2.5

**Philosophy**: From "Hello World" to "Artificial General Intelligence"

---

# Table of Contents

1. **Chapter 1: The Foundation**
* 1.1 Introduction & The REPL
* 1.2 Variables, Types, and Reactivity
* 1.3 Flow Control & Logic
* 1.4 Functions, Recursion, and Closures
* 1.5 Modules and Libraries
* 1.6 Error Handling (`TRY...CATCH`)

2. **Chapter 2: Data Science & APL**
* 2.1 The Array Engine (Vectors & Matrices)
* 2.2 Vectorized Mathematics
* 2.3 Functional Pipelines (`|>` & Lambdas)
* 2.4 String Manipulation & Regex
* 2.5 Maps, JSON, and Databases (SQL)
* 2.6 File I/O & CSV Processing

3. **Chapter 3: The System Interface**
* 3.1 Multitasking with `ASYNC` / `AWAIT`
* 3.2 Web Connectivity (HTTP Client/Server)
* 3.3 Hardware I/O (Serial & Gamepads)
* 3.4 Operating System Integration (Clipboard, Exec)
* 3.5 Windows Automation (COM/ActiveX)

4. **Chapter 4: Multimedia & Game Development**
* 4.1 Graphics Primitives & Turtle Geometry
* 4.2 Immediate Mode GUI (ImGui)
* 4.3 The Sprite & Tilemap Engine
* 4.4 Algorithmic Sound & Music

5. **Chapter 5: Artificial Intelligence (Deep Learning)**
* 5.1 Tensors & Automatic Differentiation
* 5.2 Building Neural Networks (Dense, CNN)
* 5.3 Transformers & LLMs (The "AI Artist")

6. **Chapter 6: Complete Language Reference (A-Z)**
* *Detailed examples for every keyword.*

---

# Chapter 1: The Foundation

## 1.1 Introduction & The REPL

**jdBasic** is a multi-paradigm language. It looks like BASIC, thinks like APL, and works like Python.

### The "Hello World"

Save this as `hello.jdb`:

```basic
PRINT "Hello, World!"
PRINT "Time is: " + TIME$

```

## 1.2 Variables, Types, and Reactivity

Variables are dynamic but can be typed for safety.

### Standard Assignment

```basic
' Implicit typing
Score = 100
Name$ = "Player 1"
IsActive = TRUE

' Destructuring Assignment (New!)
[x, y] = [10, 20]
PRINT x; ", "; y  ' 10, 20

' Swapping values
[x, y] = [y, x]

```

### Enumerations

Replace magic numbers with readable constants.

```basic
ENUM State
    Idle
    Running
    Paused
ENDENUM

Current = State.Running
IF Current = State.Running THEN PRINT "System Active"

```

### Reactive Variables (`REACT`)

The "Spreadsheet" feature. Variables update automatically.

```basic
DIM Price AS REACT DOUBLE = 10.0
DIM Qty AS REACT INTEGER = 5
DIM Total AS REACT DOUBLE

' Define the relationship
Total -> Price * Qty

PRINT Total ' 50
Qty = 10
PRINT Total ' 100 (Auto-updated)

```

## 1.3 Flow Control & Logic

### Conditional Logic (`IIF`, `IF`)

```basic
' Inline IF
Result$ = IIF(Score > 50, "Pass", "Fail")

' Block IF with Logic Short-Circuiting
IF (User <> NULL) ANDALSO (User{"role"} = "Admin") THEN
    PRINT "Welcome Admin"
ENDIF

```

### Loops

```basic
' Classic FOR
FOR i = 1 TO 10 STEP 2
    PRINT i
NEXT i

' DO ... LOOP variants
i = 0
DO
    i = i + 1
    IF i = 5 THEN EXITDO
LOOP UNTIL i > 10

```

## 1.4 Functions, Recursion, and Closures

### Recursion (Factorial)

```basic
FUNC Factorial(n)
    IF n <= 1 THEN RETURN 1
    RETURN n * Factorial(n - 1)
ENDFUNC

PRINT Factorial(5) ' 120

```

### Higher-Order Functions (Passing Functions)

```basic
FUNC Apply(fn, value)
    RETURN fn(value)
ENDFUNC

FUNC Square(x)
    RETURN x * x
ENDFUNC

' Pass 'Square' as a reference using @
PRINT Apply(Square@, 5) ' 25

```

## 1.5 Modules and Libraries

Organize code into files.

**`math_lib.jdb`**

```basic
EXPORT MODULE MATH
EXPORT FUNC Add(a, b)
    RETURN a + b
ENDFUNC

```

**`main.jdb`**

```basic
IMPORT MATH
PRINT MATH.Add(10, 20)

```

## 1.6 Error Handling

Structured `TRY...CATCH` blocks prevent crashes.

```basic
TRY
    ' Division by zero attempt
    X = 10 / 0
CATCH
    PRINT "Error Caught!"
    PRINT "Code: " + ERR
    PRINT "Message: " + ERRMSG$
    PRINT "Line: " + ERL
FINALLY
    PRINT "Cleanup operations..."
ENDTRY

```

---

# Chapter 2: Data Science & APL

## 2.1 The Array Engine

Arrays are the heart of jdBasic.

```basic
' Creation
V = [1, 2, 3]
M = [[1, 2], [3, 4]] ' 2x2 Matrix

' Generation
Seq = IOTA(10)       ' [0, 1, ... 9]
Zeros = RESHAPE([0], [5, 5])

```

## 2.2 Vectorized Mathematics

Perform math on entire datasets without loops.

```basic
Prices = [10, 20, 30]
Taxed = Prices * 1.1        ' [11, 22, 33]
Filter = Prices > 15        ' [0, 1, 1] (Boolean mask)

```

### Complex Slicing

```basic
Data = RESHAPE(IOTA(16), [4, 4])
' Get Row 1
Row = SLICE(Data, 0, 1)
' Get Column 2
Col = SLICE(Data, 1, 2)

```

## 2.3 Functional Pipelines

Chain operations using the Pipe Operator `|>` and Lambdas.

```basic
Data = [1, 5, 2, 8, 3]

' 1. Select items > 2
' 2. Sort them
' 3. Multiply by 10
Result = Data |> FILTER(lambda x -> x > 2, ?) _
              |> XSORT(?) _
              |> SELECT(lambda x -> x * 10, ?)

PRINT Result ' [30, 50, 80]

```

## 2.4 String Manipulation

Strings support arithmetic operators.

```basic
S$ = "Hello"
PRINT S$ * 3       ' "HelloHelloHello"
PRINT S$ + " World"' "Hello World"
PRINT "abc" - "b"  ' "ac" (Deletion)

```

## 2.5 Maps, JSON, and Databases

### Access Database (SQL)

Using COM to query MS Access.

```basic
SUB ExecuteSQL(db_path$, query$)
    conn = CREATEOBJECT("ADODB.Connection")
    conn.Open("Provider=Microsoft.ACE.OLEDB.12.0;Data Source=" + db_path$)
    
    rs = CREATEOBJECT("ADODB.Recordset")
    rs.Open(query$, conn)
    
    WHILE NOT rs.EOF
        PRINT rs.Fields("CustomerName").Value
        rs.MoveNext()
    WEND
    rs.Close()
ENDSUB

```

## 2.6 File I/O

**Reading CSVs**

```basic
' Read CSV into a Matrix
Data = CSVREADER("sales.csv", ",", TRUE) ' TRUE = skip header

```

---

# Chapter 3: The System Interface

## 3.1 Multitasking with ASYNC / AWAIT

Run non-blocking background tasks.

```basic
ASYNC FUNC Download(url$)
    PRINT "Downloading " + url$
    SLEEP 2000 ' Simulate lag
    RETURN "<html>...</html>"
ENDFUNC

PRINT "Main start"
Task = Download("http://example.com")
PRINT "Main working..."
Result$ = AWAIT Task
PRINT "Got: " + Result$

```

## 3.2 Web Connectivity

### HTTP Client (OpenAI Example)

```basic
Key$ = GETENV$("OPENAI_KEY")
HTTP.SETHEADER "Authorization", "Bearer " + Key$

Payload = {"model": "gpt-4", "messages": [{"role": "user", "content": "Hello!"}]}
Response$ = HTTP.POST$("https://api.openai.com/v1/chat/completions", JSON.STRINGIFY$(Payload), "application/json")

Json = JSON.PARSE$(Response$)
PRINT Json{"choices"}[0]{"message"}{"content"}

```

### HTTP Server

```basic
FUNC HandleApi(req)
    RETURN {"status": "OK", "time": NOW()}
ENDFUNC

HTTP.SERVER.ON_GET "/api/time", "HandleApi"
HTTP.SERVER.START 8080

```

## 3.3 Hardware I/O

**Arduino (Serial)**

```basic
Handle = SERIAL.OPEN("COM3", 9600)
SERIAL.WRITE Handle, "LED_ON" + CHR$(10)
PRINT SERIAL.READ$(Handle, 100)
SERIAL.CLOSE Handle

```

## 3.5 Windows Automation (Excel)

```basic
Excel = CREATEOBJECT("Excel.Application")
Excel.Visible = TRUE
Book = Excel.Workbooks.Add()
Sheet = Excel.ActiveSheet

Sheet.Cells(1, 1).Value = "Generated by jdBasic"
Sheet.Cells(1, 2).Value = 42

```

---

# Chapter 4: Multimedia & Game Development

## 4.1 Graphics & Turtle

**Recursion: The Dragon Curve**

```basic
SCREEN 1920, 1080, "Dragon Curve"
TURTLE.SETPOS 500, 500
CMD$ = "FX"
FOR i = 1 TO 12
    CMD$ = REPLACE$(CMD$, "X", "X+YF+")
    CMD$ = REPLACE$(CMD$, "Y", "-FX-Y")
NEXT i

FOR i = 1 TO LEN(CMD$)
    C$ = MID$(CMD$, i, 1)
    IF C$ = "F" THEN TURTLE.FORWARD 4
    IF C$ = "+" THEN TURTLE.RIGHT 90
    IF C$ = "-" THEN TURTLE.LEFT 90
NEXT i
SCREENFLIP
SLEEP 5000

```

## 4.2 Immediate Mode GUI (ImGui)

Create tools instantly.

```basic
SCREEN 800, 600, "Tool"
DO
    CLS
    IF GUI.BEGIN("Settings", 10, 10, 200, 200) THEN
        GUI.TEXT "Volume Control"
        GUI.SLIDER("Vol", v, 0, 100)
        IF GUI.BUTTON("Mute") THEN v = 0
    ENDIF
    GUI.END()
    SCREENFLIP
LOOP

```

## 4.3 The Sprite Engine

**Platformer Logic**

```basic
SPRITE.LOAD "hero", "hero.png"
Player = SPRITE.CREATE("hero", 100, 100)

DO
    ' Physics
    SPRITE.SET_VELOCITY Player, 0, 5 ' Gravity
    
    ' Input
    IF JOY.BUTTON(0, 0) THEN SPRITE.SET_VELOCITY Player, 0, -10 ' Jump
    
    SPRITE.UPDATE
    CLS
    SPRITE.DRAW_ALL 0, 0
    SCREENFLIP
LOOP

```

---

# Chapter 5: Artificial Intelligence

## 5.1 Tensors & Autograd

**Training a Neural Network (XOR Problem)**

```basic
' 1. Data (XOR)
X = TENSOR.FROM([[0,0], [0,1], [1,0], [1,1]])
Y = TENSOR.FROM([[0], [1], [1], [0]])

' 2. Model (2 Layers)
L1 = TENSOR.CREATE_LAYER("DENSE", {"input_size": 2, "units": 4})
L2 = TENSOR.CREATE_LAYER("DENSE", {"input_size": 4, "units": 1})
Optim = TENSOR.CREATE_OPTIMIZER("ADAM", {"learning_rate": 0.1})

' 3. Training Loop
FOR i = 1 TO 1000
    ' Forward
    H = TENSOR.SIGMOID(MATMUL(X, L1{"weights"}) + L1{"bias"})
    Out = TENSOR.SIGMOID(MATMUL(H, L2{"weights"}) + L2{"bias"})
    
    ' Loss (MSE)
    Loss = SUM((Y - Out)^2)
    
    ' Backward & Update
    TENSOR.BACKWARD Loss
    Model = TENSOR.UPDATE({"L1":L1, "L2":L2}, Optim)
NEXT i

PRINT "Prediction: "; TENSOR.TOARRAY(Out)

```

## 5.2 Convolutional Neural Networks (CNN)

For Image Processing.

```basic
Img = TENSOR.FROM(LoadImageMatrix("digit.png"))
Kernel = TENSOR.FROM(SobelFilter)

' Convolve
FeatureMap = TENSOR.CONV2D(Img, Kernel, Bias, 1, 0)
' Pool
Pooled = TENSOR.MAXPOOL2D(FeatureMap, 2, 2)

```

---

# Chapter 6: Complete Language Reference

This section contains a code example for **every** supported command.

## A

### ABS

Returns the absolute value.

```basic
PRINT ABS(-50)   ' 50
PRINT ABS([ -1, -2 ]) ' [1, 2] (Vectorized)

```

### ACOS

Returns the arccosine in radians.

```basic
PRINT ACOS(0.5) ' 1.047...

```

### ALL

Returns TRUE if *all* elements in an array are non-zero/true.

```basic
IF ALL([TRUE, TRUE, FALSE]) THEN PRINT "All true" ELSE PRINT "Not all"

```

### AND

Logical AND (Bitwise or Boolean).

```basic
PRINT 1 AND 0        ' 0
PRINT 5 BAND 3       ' 1 (Bitwise: 101 & 011 = 001)
PRINT [1,0] AND [1,1] ' [1, 0]

```

### ANDALSO

Short-circuiting logical AND.

```basic
' Safe: MAP.EXISTS is not called if M is NULL
IF M <> NULL ANDALSO MAP.EXISTS(M, "key") THEN ...

```

### ANY

Returns TRUE if *any* element in an array is true.

```basic
IF ANY([0, 0, 1]) THEN PRINT "Found one!"

```

### APPEND

Adds an element to the end of an array.

```basic
A = [1, 2]
A = APPEND(A, 3) ' [1, 2, 3]

```

### AS

Used in `DIM` to specify type.

```basic
DIM X AS INTEGER
DIM Y AS REACT DOUBLE

```

### ASC

Returns the ASCII code of a character.

```basic
PRINT ASC("A") ' 65

```

### ASIN

Returns the arcsine in radians.

```basic
PRINT ASIN(1.0) ' 1.57...

```

### ASYNC / AWAIT

Defines/Calls asynchronous functions.

```basic
ASYNC FUNC Task()
    SLEEP 1000
    RETURN "Done"
ENDFUNC
T = Task()
PRINT AWAIT T

```

### ATAN / ATAN2

Arctangent functions.

```basic
PRINT ATAN2(10, 10) ' 0.785... (45 degrees)

```

## B

### BINREADER$

Reads a binary file into a string.

```basic
Bytes$ = BINREADER$("data.bin")

```

### BINWRITER

Writes a string/bytes to a binary file.

```basic
BINWRITER "out.bin", Bytes$

```

### BREAK

Exits a loop immediately.

```basic
FOR I = 1 TO 10
    IF I = 5 THEN BREAK
NEXT I

```

## C

### CASE

Part of `SWITCH`.

```basic
SWITCH A
    CASE 1: PRINT "One"
    CASE 2: PRINT "Two"
ENDSWITCH

```

### CD

Changes current directory.

```basic
CD "C:\Data"

```

### CEIL

Rounds up to nearest integer.

```basic
PRINT CEIL(4.1) ' 5

```

### CHR$

Converts ASCII code to character.

```basic
PRINT CHR$(65) ' "A"

```

### CIRCLE

Draws a circle.

```basic
' x, y, radius, filled, [r,g,b]
CIRCLE 100, 100, 50, TRUE, [255, 0, 0]

```

### CLAMP

Constrains a value between min and max.

```basic
PRINT CLAMP(150, 0, 100) ' 100

```

### CLIPBOARD.GET$ / SET

Access system clipboard.

```basic
CLIPBOARD.SET "Copied Text"
PRINT CLIPBOARD.GET$()

```

### CLS

Clears the screen.

```basic
CLS 0, 0, 0 ' Clear to black

```

### CODEC.*

Hashing and Encoding.

```basic
Hash$ = CODEC.SHA256$("password")
B64$ = CODEC.BASE64_ENCODE$("Hello")
Uuid$ = CODEC.UUID()

```

### COMBINATIONS

Calculates nCk.

```basic
PRINT COMBINATIONS(5, 2) ' 10

```

### COMOBJECT

Type returned by `CREATEOBJECT`.

### CONTINUE

Skips to next loop iteration.

```basic
FOR I = 1 TO 5
    IF I = 3 THEN CONTINUE
    PRINT I
NEXT I

```

### COS

Cosine.

```basic
PRINT COS(PI) ' -1

```

### CREATEOBJECT

Creates Windows COM objects.

```basic
Xls = CREATEOBJECT("Excel.Application")

```

### CSVREADER / CSVWRITER

Handles CSV files.

```basic
Data = CSVREADER("file.csv", ",", TRUE)
CSVWRITER "out.csv", Data, ",", ["ID", "Name"]

```

### CVDATE

Converts string to DateTime.

```basic
D = CVDATE("2023-01-01 12:00:00")

```

## D

### DATE$

Returns current date string.

```basic
PRINT DATE$ ' "2023-10-25"

```

### DATEADD

Adds time to a date.

```basic
' Add 7 days
NewDate = DATEADD("D", 7, NOW())

```

### DATEDIFF

Difference between dates.

```basic
Days = DATEDIFF("D", CVDATE("2023-01-01"), NOW())

```

### DIM

Declares variables.

```basic
DIM A[10] AS INTEGER

```

### DIR$

Lists files.

```basic
Files = DIR$("*.txt")
PRINT Files[0]

```

### DISTANCE

Euclidean distance between vectors.

```basic
PRINT DISTANCE([0,0], [3,4]) ' 5

```

### DLLIMPORT

Loads functions from C++ DLLs.

```basic
DLLIMPORT "mylib.dll"

```

### DO ... LOOP

Loop structure.

```basic
DO
    PRINT "Looping"
LOOP WHILE INKEY$() = ""

```

## E

### EDIT

Opens the built-in code editor.

```basic
EDIT

```

### ELLIPSE

Draws an ellipse.

```basic
ELLIPSE 100, 100, 50, 30, [0, 255, 0]

```

### ELSE / ELSEIF / ENDIF

Conditional logic.

```basic
IF A > B THEN
    PRINT "A"
ELSEIF B > A THEN
    PRINT "B"
ELSE
    PRINT "Equal"
ENDIF

```

### ENUM

Defines constants.

```basic
ENUM Color
    Red
    Green
ENDENUM

```

### ERR / ERL / ERRMSG$

Error handling variables.

```basic
CATCH
    PRINT "Error " + ERR + " at line " + ERL + ": " + ERRMSG$

```

### EVAL

Evaluates a string as code.

```basic
PRINT EVAL("5 * 5") ' 25

```

### EXECUTE

Runs a string as a statement.

```basic
EXECUTE "PRINT 'Dynamic Code'"

```

### EXITFUNC / EXITDO / EXITFOR

Breaks out of structures.

### EXP

Exponential function ().

```basic
PRINT EXP(1) ' 2.718...

```

### EXPORT

Makes symbols public in a module.

```basic
EXPORT FUNC MyApi() ...

```

## F

### FAC

Factorial function.

```basic
PRINT FAC(5) ' 120

```

### FILE.EXISTS

Checks file existence.

```basic
IF FILE.EXISTS("config.ini") THEN ...

```

### FILTER

Filters array based on function.

```basic
' Keep evens
Evens = FILTER(lambda x -> x MOD 2 = 0, [1,2,3,4])

```

### FIND_IN_ARRAY

Returns index of item.

```basic
Idx = FIND_IN_ARRAY([10, 20, 30], 20) ' 1

```

### FLATTEN

Converts N-dim array to 1D.

```basic
Flat = FLATTEN([[1,2], [3,4]]) ' [1, 2, 3, 4]

```

### FLOOR

Rounds down.

```basic
PRINT FLOOR(4.9) ' 4

```

### FOR ... NEXT

Looping.

```basic
FOR I = 1 TO 10: PRINT I: NEXT

```

### FOR EACH

Iterates collections.

```basic
FOR EACH Item IN MyList
    PRINT Item
NEXT

```

### FORMAT$

Python-style string formatting.

```basic
PRINT FORMAT$("Value: {:.2f}", 3.14159) ' "Value: 3.14"

```

### FUNC

Defines a function.

```basic
FUNC Add(a, b)
    RETURN a + b
ENDFUNC

```

## G

### GETENV$

Reads environment variables.

```basic
Path$ = GETENV$("PATH")

```

### GOTO

Jumps to label.

```basic
Start:
PRINT "Loop"
GOTO Start

```

### GUI.*

Immediate Mode GUI functions (ImGui).
See Chapter 4.2.

## H

### HELP

Prints help text.

### HTTP.GET$ / POST$

Web requests.

```basic
Html$ = HTTP.GET$("https://google.com")

```

### HTTP.SERVER.*

Web server functions.
See Chapter 3.2.

## I

### IF

Conditional.

### IMPORT

Loads a module.

```basic
IMPORT "MathLib"

```

### IN

Membership test.

```basic
IF "key" IN MyMap THEN ...

```

### INKEY$

Non-blocking key read.

```basic
K$ = INKEY$()

```

### INPUT

Reads console input.

```basic
INPUT "Name: ", N$

```

### INSTR

Finds substring position.

```basic
PRINT INSTR("Hello", "el") ' 2

```

### INT

Truncates to integer.

### INVERT

Matrix inversion.

```basic
InvM = INVERT(Matrix)

```

### IOTA

Generates integer sequence.

```basic
PRINT IOTA(5) ' [0, 1, 2, 3, 4]

```

## J

### JOIN

Joins array into string.

```basic
PRINT JOIN(["A", "B"], "-") ' "A-B"

```

### JOY.*

Joystick input.

```basic
X = JOY.AXIS(0, 0)

```

### JSON.PARSE$ / STRINGIFY$

JSON handling.

```basic
Obj = JSON.PARSE$("{""a"":1}")
Str$ = JSON.STRINGIFY$(Obj)

```

## L

### LAMBDA (->)

Anonymous function.

```basic
F = lambda x -> x * 2
PRINT F(5) ' 10

```

### LCASE$

Lowercase.

```basic
PRINT LCASE$("ABC") ' "abc"

```

### LEFT$

Left substring.

```basic
PRINT LEFT$("Hello", 2) ' "He"

```

### LEN

Length of string or array.

```basic
PRINT LEN("ABC") ' 3
PRINT LEN([1,2,3,4]) ' 4

```

### LERP

Linear interpolation.

```basic
PRINT LERP(0, 100, 0.5) ' 50

```

### LINE

Draws a line.

```basic
LINE 0, 0, 100, 100, [255,255,255]

```

### LOG / LOG10

Logarithms.

## M

### MAP.*

Map manipulation.

```basic
Keys = MAP.KEYS(MyMap)
MAP.DELETE(MyMap, "OldKey")

```

### MATMUL

Matrix Multiplication.

```basic
C = MATMUL(A, B)

```

### MAX / MIN

Array extremums.

```basic
PRINT MAX([1, 5, 2]) ' 5

```

### MEAN / MEDIAN

Statistics.

```basic
PRINT MEAN([1, 2, 3]) ' 2

```

### MID$

Substring.

```basic
PRINT MID$("Hello", 2, 2) ' "el"

```

### MOD

Modulo (Remainder).

```basic
PRINT 10 MOD 3 ' 1

```

## N

### NORMALIZE

Scales array values to 0..1 range.

```basic
V = NORMALIZE([10, 20, 30])

```

### NOT

Logical NOT.

### NOW

Current DateTime.

## O

### OPTION

Compiler options.

```basic
OPTION "EXPLICIT" ' Force DIM

```

### OR / ORELSE

Logical OR.

### OS.EXEC

Runs system command.

```basic
OS.EXEC("notepad.exe")

```

### OUTER

Outer product of two arrays.

```basic
' Multiplication table
Table = OUTER(IOTA(10), IOTA(10), "*")

```

## P

### PATH.*

Path manipulation.

```basic
Full$ = PATH.JOIN("C:", "Windows", "System32")
Ext$ = PATH.EXT("image.png") ' ".png"

```

### PI

Constant 3.14159...

### PRINT

Console output.

### PRODUCT

Product of array elements.

```basic
PRINT PRODUCT([1, 2, 3, 4]) ' 24

```

### PSET

Sets pixel color.

```basic
PSET 10, 10, [255, 0, 0]

```

## R

### REACT

Declares reactive variable.

### RECT

Draws rectangle.

```basic
RECT 10, 10, 50, 50, [0,255,0], TRUE ' Filled

```

### REDUCE

Array reduction with function.

```basic
' Calculate factorial
Fac = REDUCE(lambda acc, x -> acc * x, IOTA(5)+1)

```

### REGEX.*

Regular Expressions.

```basic
Matches = REGEX.MATCH("(\d+)", "Item 123")
PRINT Matches[0] ' "123"

```

### REPLACE$

String replacement.

```basic
PRINT REPLACE$("A-B-C", "-", "") ' "ABC"

```

### RESHAPE

Changes array dimensions.

```basic
M = RESHAPE(IOTA(9), [3, 3])

```

### RETURN

Return from function.

### REVERSE

Reverses array or string.

```basic
PRINT REVERSE("ABC") ' "CBA"

```

### RIGHT$

Right substring.

### RND

Random number (0..1).

```basic
Dice = INT(RND(1) * 6) + 1

```

### ROTATE

Rotates array elements.

```basic
PRINT ROTATE([1, 2, 3], 1) ' [3, 1, 2]

```

### ROUND

Rounds to nearest integer.

### RUN

Runs a file.

```basic
RUN "game.jdb"

```

## S

### SCAN

Cumulative reduction (Prefix Sum).

```basic
PRINT SCAN("+", [1, 2, 3]) ' [1, 3, 6]

```

### SCREEN / SCREENFLIP

Graphics window control.

### SELECT

Maps function over array.

```basic
Squares = SELECT(lambda x -> x*x, [1, 2, 3])

```

### SERIAL.*

Serial port communication.

### SETFONT

Sets text font size.

```basic
SETFONT 24

```

### SHL / SHR

Bitwise shift.

```basic
PRINT SHL(1, 2) ' 4

```

### SIN

Sine function.

### SLICE

Extracts sub-array/matrix.

```basic
Col1 = SLICE(Matrix, 1, 0)

```

### SORT / XSORT

Sorts array.

```basic
Sorted = XSORT([3, 1, 2])

```

### SOUND.*

Audio engine.

```basic
SOUND.BPM 120
SOUND.SEQ 0, "c4 e4 g4", "SAW"

```

### SPLIT

String to array.

```basic
Words = SPLIT("A,B,C", ",")

```

### SPRITE.*

Sprite functions. See Chapter 4.3.

### SQR

Square root.

### STACK

Combines arrays.

```basic
Combined = STACK(0, A, B)

```

### STEP

Loop step.

### STOP

Pauses execution.

### STR$

Number to string.

### SUM

Sum of array.

### SWITCH

Multi-way branch.

## T

### TAKE

Takes first N elements.

```basic
PRINT TAKE(2, [1,2,3,4]) ' [1, 2]

```

### TAN

Tangent.

### TENSOR.*

Deep Learning engine. See Chapter 5.

### TEXT

Draws text on graphics screen.

```basic
TEXT 10, 10, "Score: 100", 255, 255, 255

```

### THROW

Raises an error.

```basic
THROW "Something went wrong"

```

### TIME$

Current time string.

### TRANSPOSE

Matrix transpose.

### TRIM$

Removes whitespace.

### TRUE

Constant 1.

### TRY

Starts error block.

### TURTLE.*

Turtle graphics. See Chapter 4.1.

### TXTREADER$ / WRITER

Text file I/O.

### TYPE

Defines custom type (struct).

## U

### UCASE$

Uppercase.

### UNIQUE

Removes duplicates.

```basic
PRINT UNIQUE([1, 1, 2]) ' [1, 2]

```

### UNPACK

Binary unpack.

### UNREACT

Stops reactivity.

```basic
UNREACT Price

```

## V

### VAL

String to number.

## W

### WAITKEY$

Waits for keypress.

### WHILE

Loop condition.

### WRITE

Console output (raw).

## X

### XOR

Logical XOR.

---
