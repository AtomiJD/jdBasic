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
