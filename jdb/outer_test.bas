' Test program for the enhanced OUTER function

PRINT "--- Testing OUTER with string operator '*' ---"
A=IOTA(4)*10
B=IOTA(4)

' Create a multiplication table
mult_table = OUTER(A, B, "*")

PRINT "Vector A:", A
PRINT "Vector B:", B
PRINT "Multiplication Table (A OUTER B, '*'):"
PRINT mult_table
PRINT ""


PRINT "--- Testing OUTER with a Function Reference ---"

' Define a custom function to use as the operator.
' It must take exactly two arguments.
FUNC HYPOT(X, Y)
    ' Calculate the hypotenuse: SQR(X^2 + Y^2)
    RETURN SQR(X*X + Y*Y)
ENDFUNC

' Use the custom function with OUTER
hypot_table = OUTER(A, B, HYPOT@)

PRINT "Hypotenuse Table (A OUTER B, HYPOT@):"
PRINT hypot_table

