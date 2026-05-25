MY_NUMBER = 1234567.89

PRINT "Default 'C' Locale:"
PRINT MY_NUMBER
PRINT ""

' For Windows, locale names are "German", "French", etc.
' On Linux, they are often "de_DE.UTF-8", "fr_FR.UTF-8"
PRINT "Setting locale to German..."
SETLOCALE "German"

PRINT "German Locale:"
PRINT MY_NUMBER

' The CSVWRITER will now also use this format!
DATA = RESHAPE([PI, MY_NUMBER], [1, 2])
CSVWRITER "german_numbers.csv", DATA, ";"

PRINT "german_numbers.csv has been written."
