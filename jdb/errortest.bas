sub atomi(data)
    PRINT "Error occurred!"
    PRINT "Code: "; ERR
    PRINT "Line: "; ERL
    PRINT "Message: "; ERRMSG$
    RESUME NEXT ' Continue after the line that caused the error
endsub

ON "ERROR" CALL Atomi

DIM A[5]
A[6] = 10 ' This will cause an "Array out of bounds" error

PRINT "Program finished normally."
