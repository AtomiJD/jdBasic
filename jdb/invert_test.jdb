' Test program for the new INVERT function

PRINT "--- INVERT Function Test ---"

' Define a 3x3 matrix that is known to be invertible
DIM A[3, 3]
A[0,0] = 1 : A[0,1] = 2 : A[0,2] = 3
A[1,0] = 0 : A[1,1] = 1 : A[1,2] = 4
A[2,0] = 5 : A[2,1] = 6 : A[2,2] = 0

PRINT "Original Matrix A:"
PRINT A
PRINT ""

' Calculate the inverse of A
PRINT "Calculating Inverse of A..."
DIM A_inv[0,0] ' Define, will be resized by the function
A_inv = INVERT(A)

PRINT "Inverse Matrix A_inv:"
PRINT A_inv
PRINT ""

' To verify, multiply A by its inverse. The result should be the identity matrix.
PRINT "Verifying by calculating A * A_inv..."
DIM Identity[0,0]
Identity = MATMUL(A, A_inv)

PRINT "Result of A * A_inv (should be Identity Matrix):"
PRINT Identity
PRINT ""

' --- Test a singular matrix ---
PRINT "--- Testing a Singular Matrix (should fail) ---"
DIM S[2,2]
S[0,0] = 1 : S[0,1] = 1
S[1,0] = 1 : S[1,1] = 1
PRINT "Singular matrix S:"
PRINT S
PRINT "Calling INVERT(S)..."
DIM S_inv[0,0]
S_inv = INVERT(S) ' This line will cause a runtime error

' The program will stop at the line above if error handling is correct.
' If it continues, the error was not caught.
PRINT "This line should not be printed."
