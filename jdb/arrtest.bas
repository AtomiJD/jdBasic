PRINT "Array Test"

Dim a[20]

for i = 0 to 19
  a[i] = i*2
next i

for i = 0 to 19
  print "Zahl #",i, " ist: ", a[i]
next i

DIM my_numbers[10]
DIM my_strings$[5]

my_numbers = [10, 20, 30, 40, 50]
my_strings$ = ["alpha", "beta", "gamma"]

PRINT "--- Numbers ---"
FOR i = 0 TO 9
    PRINT my_numbers[i]
NEXT i

PRINT "--- Strings ---"
FOR i = 0 TO 4
    PRINT my_strings$[i]
NEXT i

print "done"
print "------------------------"
