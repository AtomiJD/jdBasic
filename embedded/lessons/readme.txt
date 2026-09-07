Train jdBasic - the lessons, on the board
=========================================
One file per segment of a video lesson.
RUN lessons/hello1  starts one;
TYPE lessons/hello1.jdb shows it, EDIT
the same name opens it.

01 hello1        PRINT and variables
02 iffor1-7      IF, ELSEIF, FOR, STEP,
                 FizzBuzz
03 array1-2      literals, indexing
04 string1-2     LEN, MID$, a CSV parser
05 func1-4       FUNC, SUB, recursion
06 maps1-2       literals, FOR EACH
07 input1-4      INPUT and DO loops -
                 these ask you to type
08 fileio1-3     TXTWRITER, TXTREADER$;
                 writes hello.txt and
                 todo.txt on the store
09 gfx1-3        the board's screen, 320
                 by 240, three seconds
                 each
10 module1       IMPORT MATHX from
                 mathx.jdb in this folder
11 repl1         a program to poke at
12 hiord1-6      SELECT, FILTER, REDUCE,
                 the pipe, closures
13 http1-3       HTTP.GET$ and JSON -
                 needs wifi.txt on the
                 store: ssid, then
                 password, one per line;
                 http3 needs https, which
                 a PicoCalc has not
14 native1       the benchmark; the
                 compiler is on the
                 desktop

Ctrl-C ends a running lesson.
