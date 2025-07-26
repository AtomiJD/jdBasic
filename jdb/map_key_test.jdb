DIM my_map AS MAP
my_map{"name"} = "Atomi"
my_map{"level"} = 99

PRINT MAP.EXISTS(my_map, "level")   ' Prints TRUE
PRINT MAP.EXISTS(my_map, "armor")   ' Prints FALSE

IF MAP.EXISTS(my_map, "name") THEN
    PRINT "Name is: "; my_map{"name"}
ENDIF

DIM my_map AS MAP
my_map{"name"} = "Atomi"
my_map{"level"} = 99
my_map{"class"} = "Interpreter"

key_array = MAP.KEYS(my_map)

PRINT "Map contains the following keys:"
FOR i = 0 TO LEN(key_array) - 1
    PRINT "- "; key_array[i]
NEXT i
' Output:
' - CLASS
' - LEVEL
' - NAME
' (Note: std::map stores keys in alphabetical order)

DIM my_map AS MAP
my_map{"name"} = "Atomi"
my_map{"level"} = 99
my_map{"class"} = "Interpreter"

value_array = MAP.VALUES(my_map)

PRINT "Map contains the following values:"
FOR i = 0 TO LEN(value_array) - 1
    PRINT "- "; value_array[i]
NEXT i
' Output:
' - Interpreter
' - 99
' - Atomi
