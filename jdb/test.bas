' Import the MATH module. The interpreter will look for "MATH.bas"
' and compile it before running this program.
import MATH

print "--- Module Test Program ---"
print

' Call the exported 'add' function from the MATH module
print "Calling MATH.ADD(15, 7)..."
x = MATH.ADD(15, 7)
print "Result:"; x
print

' Call the exported 'add_and_double' function
print "Calling MATH.ADD_AND_DOUBLE(10, 5)..."
y = MATH.ADD_AND_DOUBLE(10, 5)
print "Result:"; y
print

' Call the exported 'print_sum' procedure
print "Calling MATH.PRINT_SUM 100, 23..."
MATH.PRINT_SUM 100, 23

print

print "--- Module Test Complete ---"

Print "For Next Test"

for i = 1 to 5
 print "lall: "; i
next i

print "done"
print "------------------------"

Print "Nested For Next Test"

for i = 1 to 5
    for j = 1 to 5
        print "lall: "; i*j
    next j
    print
next i

print "done"
print "------------------------"

PRINT "Goto/Label and if then else Test"

i = 0
fluppi:
i = i + 1

print i;

if i > 20 then
   goto ende
else
   print "luli"
endif

GoTo fluppi

ende:
print "done"
print "------------------------"

PRINT "Array Test"

Dim a[20]

for i = 0 to 19
  a[i] = i*2
next i

for i = 0 to 19
  print "Zahl #",i, " ist: ", a[i]
next i

my_numbers = [10, 20, 30, 40, 50]
my_strings$ = ["alpha", "beta", "gamma"]

PRINT "--- Strings ---"
FOR i = 0 TO len(my_strings$)-1
    PRINT my_strings$[i]
NEXT i

PRINT "--- Numbers ---"
FOR i = 0 TO len(my_numbers)-1
    PRINT my_numbers[i]
NEXT i

print "done"
print "------------------------"

PRINT "--- String and Math Function Test ---"

GREETING$ = "   Hello, World!   "
PRINT "Original: '"; GREETING$; "'"
PRINT "Trimmed: '"; TRIM$(GREETING$); "'"
PRINT "Length of trimmed: "; LEN(TRIM$(GREETING$))
PRINT

PRINT "LEFT 5: "; LEFT$(TRIM$(GREETING$), 5)
PRINT "RIGHT 6: "; RIGHT$(TRIM$(GREETING$), 6)
PRINT "MID from 8 for 5: "; MID$(TRIM$(GREETING$), 8, 5)

PRINT "LOWER: "; LCASE$("THIS IS A TEST")
PRINT "UPPER: "; UCASE$("this is a test")
PRINT

PRINT "ASCII for 'A' is "; ASC("A")
PRINT "Character for 66 is "; CHR$(66)
PRINT

PRINT "VAL of '100.00' is "; VAL("100.00")
PRINT "STR of 100.00 is "; STR$(100.00)
PRINT


HAYSTACK$ = "the quick brown fox jumps over the lazy dog"
PRINT "Position of 'fox' is "; INSTR(HAYSTACK$, "fox")
PRINT "Position of 'the' after pos 5 is "; INSTR(5, HAYSTACK$, "the")
PRINT

PRINT "--- Math Test ---"
X = 90
PRINT "The square root of ";X;" is "; SQR(X)

' Note: SIN/COS/TAN work in radians, not degrees

PRINT "The sine of PI/2 is roughly "; SIN(PI / 2)

print "10 random, Numbers: ";
for i = 1 to 10
PRINT RND(1); " ";
Next i
print
PRINT "--- DATE Test ---"
Print "Tick: "; Tick()
Print "Now: "; Now()

DIM deadline AS DATE
DIM name AS STRING

name = "Project Apollo"
deadline = CVDate("2025-07-01")

PRINT "Deadline for "; name; " is "; deadline

deadline = DATEADD("D", 10, deadline)
PRINT "Extended deadline is "; deadline

If deadline > Now() then
   print "Deadline is greater"
endif

print "done"
PRINT "--- String, Math and DATE Function Test COMPLETE ---"


funci:
Print "Functional tests"
print
Print "Function Definition and call functions"

func lall(a,b)
    return a*b
endfunc

print "func call:"
b=lall(5,3)
print b

print
print "Done"
print "------------------------"

print "Using higher order functions"
print

func inc(ab)
    return ab+1
endfunc
func dec(ac)
    return ac-1
endfunc

func apply(fa,cc)
    return fa(cc)
endfunc

print apply(inc@,10)
print apply(dec@,12)

print
print "Done"
print "------------------------"

print "Factional recursion"
print

func factorial(a)
    if a > 1 then
        return factorial(a-1)*a
    else
        return a
    endif
endfunc

lall =  factorial(5)
print "erg: ", lall

print
print "Done"
print "------------------------"

print "Simple recursion"
print

func count_asc(n)
   if n < 0 then return n
   count_asc(n-1)
   print n
endfunc

func count_desc(n)
    if n >= 0 then
        print n
	r=count_desc(n - 1)
    endif
endfunc

print "Result ASC: "
count_asc(5)
print "Result DEC: "
count_desc(5)

print
print "Done"
print "------------------------"

print "Map and Filter"
print

Dim o_map[20]

s_map = [1,5,8,7,45,66,12]

sub printresult(result)
   for i = 0 to len(result)-1
       if result[i]>0 then
           print result[i], " ";
       endif
   next i
print
endsub

func iseven(a)
   if a mod 2 = 0 then
      r=1
   else
      r=0
   endif
   return r
endfunc

func filter(fu,in[],out[])
   j=0
   for i = 0 to len(in) - 1
      m=fu(in[i])
      if m = 1 then
         out[j]=in[i]
         j=j+1
      endif
   next i
endfunc

filter(iseven@,s_map,o_map)

print "Result filter: "
printresult o_map

print
print "Done"
print "------------------------"

PRINT "Single Line IF Test"
print

FOR i = 0 to 10
	IF I = 5 THEN PRINT "LALL"; I
	PRINT "LULL"; i
NEXT I
PRINT "ENDE"

print "--- do/while/loop test ---"
i=0
do while i < 5
 i=i+1
 print "Achim: "; I
loop
print

print "--- do/until/loop test ---"
i=0
do until i > 5
 i=i+1
 print "Achim: "; I
loop
print

print "--- do/loop/while test ---"
i=0
do
 i=i+1
 print "Achim: "; I
loop while i < 5
print

print "--- do/loop/until exit do test ---"
i=0
do
 i=i+1
 if i = 3 THEN
    print "Exit do at i: "; I
    exitdo
 endif
 j = 0
 do until j > 4
    j = j + 1
    if j = 2 THEN
        print "inner exit do by: "; j
        exitdo
    endif
 loop
 print "Atomi's loop# "; I
loop until i > 5

print "--- test done ---"

