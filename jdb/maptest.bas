' --- Map Data Type Test ---

DIM person AS MAP

PRINT "Assigning values to the map..."
person{"name"} = "John Doe"
person{"age"} = 42
person{"city"} = "New York"
person{"verified"} = TRUE

PRINT "The complete map is: "; person
PRINT ""
PRINT "Accessing individual values:"
PRINT "Name: "; person{"name"}
PRINT "Age: "; person{"age"}

' Update a value
person{"age"} = person{"age"} + 1
PRINT "New Age: "; person{"age"}
