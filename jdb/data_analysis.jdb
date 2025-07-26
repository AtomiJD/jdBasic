' --- CSV Data Analysis Program ---

PRINT "Loading temperature data from temps.csv..."
' Load the CSV file, skipping the header row.
TEMP_DATA = CSVREADER("temps.csv", ",", TRUE)
PRINT "Data loaded. Matrix is:"
PRINT TEMP_DATA
PRINT ""

' --- Isolate Data Columns using SLICE ---
' Column 0 is the day, 1 is min_temp, 2 is max_temp.

PRINT "Isolating data columns..."
MIN_TEMPS = SLICE(TEMP_DATA, 1, 1)
MAX_TEMPS = SLICE(TEMP_DATA, 1, 2)

PRINT "Min Temp Vector: "; MIN_TEMPS
PRINT "Max Temp Vector: "; MAX_TEMPS
PRINT ""

' --- Analyze Minimum Temperatures (Column 1) ---
PRINT "--- Min Temp Analysis ---"
MIN_AVG = SUM(MIN_TEMPS) / LEN(MIN_TEMPS)
MIN_MIN = MIN(MIN_TEMPS)
MIN_MAX = MAX(MIN_TEMPS)

PRINT "Average Minimum Temp: "; MIN_AVG
PRINT "Lowest Minimum Temp:  "; MIN_MIN
PRINT "Highest Minimum Temp: "; MIN_MAX
PRINT ""

' --- Analyze Maximum Temperatures (Column 2) ---
PRINT "--- Max Temp Analysis ---"
MAX_AVG = SUM(MAX_TEMPS) / LEN(MAX_TEMPS)
MAX_MIN = MIN(MAX_TEMPS)
MAX_MAX = MAX(MAX_TEMPS)

PRINT "Average Maximum Temp: "; MAX_AVG
PRINT "Lowest Maximum Temp:  "; MAX_MIN
PRINT "Highest Maximum Temp: "; MAX_MAX