PRINT "--- Testing TXTREADER$ ---"
MY_STORY$ = TXTREADER$("story.txt")
PRINT "The story file contains:"
PRINT MY_STORY$
PRINT "The story file split in lines:"
V = SPLIT(MY_STORY$,chr$(10))
PRINT "There are "; len(v); " lines:"
print "This is line 2: "; v[1]
print


PRINT "--- Testing CSVREADER ---"
' Load the data, skipping the header row
TEMP_DATA = CSVREADER("temps.csv", ",", TRUE)

PRINT "Temperature data loaded."
PRINT "Shape of data:"; LEN(TEMP_DATA)
PRINT "Full data matrix:"; TEMP_DATA
PRINT ""

' Let's analyze the average max temperature (column 2)
' (This is a conceptual example for now)
PRINT "Analysis would go here."


PRINT "--- Testing SPLIT ---"
SENTENCE$ = "this is a sentence with several words"
WORDS = SPLIT(SENTENCE$, " ")
PRINT "The words are: "; WORDS
PRINT "The third word is: "; WORDS[2]
PRINT ""

CSV_LINE$ = "2025-06-17,45.3,102.1"
DATA_POINTS = SPLIT(CSV_LINE$, ",")
PRINT "Data points: "; DATA_POINTS


PRINT "--- Testing SLICE ---"
' Create a 3x4 matrix
M = RESHAPE(IOTA(12), [3, 4])
PRINT "Original Matrix M:"; M
PRINT ""

' Extract row 1 (the second row, index 1)
ROW1 = SLICE(M, 0, 1)
PRINT "Row 1 of M: "; ROW1  ' Should be [5 6 7 8]
PRINT ""

' Extract column 2 (the third column, index 2)
COL2 = SLICE(M, 1, 2)
PRINT "Column 2 of M: "; COL2 ' Should be [3 7 11]

