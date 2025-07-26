' Test program for the new SOLVE function

' We want to solve the system Ax = b
'
'      [ 2  1 -1 ] [ x1 ]   [  8 ]
'  A = [ -3 -1  2 ] [ x2 ] = [ -11 ] = b
'      [ -2  1  2 ] [ x3 ]   [ -3 ]
'
' The known solution is x = [ 2, 3, -1 ]

PRINT "Setting up the matrix A and vector b..."

DIM A[3, 3]
A[0,0] = 2 : A[0,1] = 1 : A[0,2] = -1
A[1,0] = -3 : A[1,1] = -1 : A[1,2] = 2
A[2,0] = -2 : A[2,1] = 1 : A[2,2] = 2

DIM b[3]
b[0] = 8
b[1] = -11
b[2] = -3

PRINT "Matrix A:"
PRINT A
PRINT "Vector b:"
PRINT b
PRINT ""

PRINT "Calling SOLVE(A, b)..."
DIM x[0] ' Define x, will be resized by the function result
x = SOLVE(A, b)

PRINT "Solution vector x:"
PRINT x

PRINT "Expected solution is [2 3 -1]"
