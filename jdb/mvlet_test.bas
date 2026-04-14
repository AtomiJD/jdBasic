M = RESHAPE(IOTA(12),[3,4])
V1 = IOTA(3)*2-1
V2 = IOTA(4)*2-1

PRINT "Original Matrix:"
PRINT M

' Replace row 1 (the second row) with V
M2 = MVLET(M, 0, 1, V2) 
PRINT "After replacing row 1:"
PRINT M2

' Replace column 2 (the third column) with COL_V
M3 = MVLET(M2, 1, 2, v1)
PRINT "After replacing column 2:"
PRINT M3


