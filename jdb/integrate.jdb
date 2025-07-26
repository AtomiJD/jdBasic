' Test program for the new INTEGRATE function

' 1. Define the function we want to integrate.
'    It must take exactly one argument.
FUNC SQUARE(X)
    RETURN X * X
ENDFUNC

' 2. Set up the integration parameters
PRINT "Integrating f(x) = x^2 from 0 to 3..."
DIM limits[2]
limits[0] = 0 ' Lower bound
limits[1] = 3 ' Upper bound

' 3. Call the INTEGRATE function
'    - @SQUARE is a reference to our function
'    - limits is the array with the domain [0, 3]
'    - 3 is the order (number of Gauss points). Higher is more accurate.
result = INTEGRATE(SQUARE@, limits, 3)

PRINT "Analytical result should be 9.0"
PRINT "Numerical result:", result
PRINT ""

' --- Another example: f(x) = sin(x) from 0 to PI ---
' The analytical result is -cos(x) | 0 to PI = (-(-1)) - (-1) = 2
PRINT "Integrating f(x) = SIN(x) from 0 to PI..."
limits[0] = 0
limits[1] = 3.14159265

result = INTEGRATE(SIN@, limits, 5)

PRINT "Analytical result should be 2.0"
PRINT "Numerical result:", result