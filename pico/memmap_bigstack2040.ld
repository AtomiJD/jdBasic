/* The default map, with one change: jdBasic runs a recursive parser and
   a recursive interpreter, and the 4 KB scratch bank is no home for
   that. The core-0 stack moves to the top of main RAM and the heap ends
   where the stack begins. The RP2040's 256 KB spare a smaller stack
   than the RP2350's 512 KB. */

INCLUDE "memmap_default.incl"

JDB_STACK_SIZE = 64K;
__StackTop = ORIGIN(RAM) + LENGTH(RAM);
__StackBottom = __StackTop - JDB_STACK_SIZE;
__StackLimit = __StackBottom;
