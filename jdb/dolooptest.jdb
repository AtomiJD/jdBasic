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
 print "Achim: "; I
loop until i > 5

print "--- test done ---"

