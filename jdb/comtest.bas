
SUB Atomi(data)
    PRINT "An error occurred: "; ERR
    PRINT "Error Line: "; ERL
    ' You could add logic here to clean up or ask the user to retry
    RESUME NEXT ' Resume execution at the next statement
ENDSUB

On Error call Atomi

bad_object = CREATEOBJECT("NonExistent.Application") ' This will trigger an error


' Initialize a COM object
objXL = CREATEOBJECT("Excel.Application")

' Access a property
objXL.Visible = TRUE



' Call a method
wb = objXL.Workbooks.Add()

' Set a cell value
objcell = objXL.ActiveSheet.Cells(1, 1)
objcell.Value = "Hello from NeReLa Basic!"

' Quit Excel
objXL.Quit()

PRINT "Excel automation complete."

bad_object = CREATEOBJECT("NonExistent.Application") ' This will trigger an error

Print "Finish"


