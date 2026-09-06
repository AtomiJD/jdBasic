Train jdBasic - the lessons, on the board
=========================================
One file per segment of a video lesson.
RUN lessons/lesson01_hello_a  starts one;
TYPE lessons/lesson01_hello_a.jdb shows
it, EDIT the same name opens it.

01 hello        PRINT and variables
02 if_for       IF, ELSEIF, FOR, STEP,
                FizzBuzz (a to g)
03 arrays       literals, indexing (a b)
04 strings      LEN, MID$, a CSV parser
05 func_sub     FUNC, SUB, recursion
06 maps         literals, FOR EACH
07 input_do     INPUT and DO loops -
                these ask you to type
08 file_io      TXTWRITER, TXTREADER$;
                writes hello.txt and
                todo.txt on the store
09 graphics     the board's screen, 320
                by 240, three seconds
                each
10 modules      IMPORT MATHX from
                mathx.jdb in this folder
11 repl         a program to poke at
12 higher_order SELECT, FILTER, REDUCE,
                the pipe, closures
13 http_json    HTTP.GET$ and JSON -
                needs wifi.txt on the
                store: ssid, then
                password, one per line
14 native       the benchmark; the
                compiler is on the
                desktop

Ctrl-C ends a running lesson.
