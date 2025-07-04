' Program to combine all .jdb files into a single file.

PRINT "Starting to combine all .jdb files..."

' 1. Get a list of all files with the .jdb extension
jdb_files$ = DIR$("*.bas")

' Check if any files were found
IF LEN(jdb_files$) = 0 THEN
    PRINT "No .jdb files found in the current directory."
    STOP
ENDIF

PRINT "Found " + STR$(LEN(jdb_files$)) + " files to combine."

' 2. Initialize an empty string to hold all the code
combined_code$ = ""
file_separator$ = CHR$(13) + CHR$(10) + "' --- End of File ---" + CHR$(13) + CHR$(10) + CHR$(13) + CHR$(10)

' 3. Loop through the list of files
FOR i = 0 TO LEN(jdb_files$) - 1
    current_file$ = jdb_files$[i]
    PRINT "Reading: "; current_file$
    
    ' Read the entire content of the current file
    file_content$ = TXTREADER$(current_file$)
    
    ' Append the content to our master string, followed by a separator
    combined_code$ = combined_code$ + file_content$ + file_separator$
NEXT i

' 4. Write the combined string to a new file
output_filename$ = "all_code.txt"
PRINT "Writing combined code to: "; output_filename$
TXTWRITER output_filename$, combined_code$

PRINT "Done. All files have been combined into 'all_code.jdb'."