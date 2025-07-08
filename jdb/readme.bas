LA = 1
LB = 2
LC = 3

func lull(a) 
 print "lall "; a
 d = a*2
 return d
endfunc

func lall(a, b)
print "Lall: ";a,b
 c = a + b
 d = lull(c)
 return d
endfunc

print "1 "
print "2 "
print lall(1, 2)
print "done"
