// HELP at the prompt: the board's own manual, embedded in the image
// and shaped for the 40-column screen. Topics cover exactly what this
// build registers; output pages against the 40-row panel and any key
// continues, q stops.

#include <stdio.h>
#include <string.h>
#include <strings.h>

extern "C" int repl_read_key(void);

static const char* HELP_INDEX =
"jdBasic on the Pico - HELP <topic>\n"
"  BASIC   the language core\n"
"  FUNCS   numbers and logic\n"
"  STRING  text functions\n"
"  ARRAY   array functions\n"
"  FILES   programs, flash and SD\n"
"  GFX     graphics and sound\n"
"  HW      pins and buses\n"
"  EVENTS  timers, keys, pin edges\n"
"  WIFI    network (W boards)\n"
"  SYS     board diagnostics\n"
"Prompt: EDIT/LOAD/RUN name, DIR\n";

static const char* HELP_BASIC =
"DIM x          also a[10], m AS MAP\n"
"x = 1 : s$ = \"hi\"  strings end in $\n"
"PRINT a; b         ' comment\n"
"IF c THEN .. ELSE .. ENDIF\n"
"FOR i = 1 TO 9 STEP 2 .. NEXT i\n"
"DO .. LOOP UNTIL c   (or WHILE c)\n"
"EXITFOR EXITDO EXITFUNC\n"
"CONTINUEFOR CONTINUEDO\n"
"FUNC F(a, b) .. RETURN v .. ENDFUNC\n"
"SUB S(a) .. RETURN .. ENDSUB\n"
"f = LAMBDA x -> x * 2\n"
"TYPE P .. n AS STRING .. ENDTYPE\n"
"m = {\"k\": 1}   m{\"k\"}\n"
"a = [1, 2, 3]   a[0]   0-based\n"
"TRY .. CATCH .. ENDTRY   ERRMSG$\n"
"AND OR NOT MOD   ANDALSO ORELSE\n"
"SLEEP ms   TICK() ms   TIMER s\n"
"no \\n escapes: CHR$(10), CHR$(34)\n";

static const char* HELP_FUNCS =
"ABS INT SGN SQR EXP LOG\n"
"SIN COS TAN   RND(n)\n"
"MIN(arr) MAX(arr) - arrays only,\n"
"  two values: MAX([a, b])\n"
"VAL(s$) STR$(n) HEX$(n)\n"
"TYPEOF(v)  \"NONE\" for missing\n";

static const char* HELP_STRING =
"LEN(s$)  LEFT$ RIGHT$\n"
"MID$(s$, i, n)      0-based\n"
"INSTR(s$, t$)  0-based, -1 absent\n"
"CHR$ ASC UCASE$ LCASE$ TRIM$\n"
"SPACE$(n)  STR$  VAL\n";

static const char* HELP_ARRAY =
"IOTA(n) = [1..n]   IOTA(n,0) from 0\n"
"LEN SUM MIN MAX  reducers\n"
"a = APPEND(a, v)  returns new\n"
"SELECT(f@, a)  FILTER(f@, a)\n"
"REDUCE(f@, a)  AGG  SORT REVERSE\n"
"RESHAPE(a, [rows, cols])\n"
"vector math: a * 2, a + b\n";

static const char* HELP_FILES =
"flash store is /, SD card is /sd\n"
"CD dir  CD ..  CD    current dir\n"
"DIR [\"*.jdb\"]    name and size\n"
"TYPE f  DEL f  COPY a b  REN a b\n"
"MD d  RD d     unquoted args ok\n"
"LOAD \"prog\"  RUN [\"prog\"]  EDIT\n"
"AUTORUN name   run it at power-on\n"
"AUTORUN        show;  AUTORUN OFF\n"
"RECV name      take a file off the\n"
"  wire raw, no echo, ends Ctrl-D\n"
"KILL \"f\"   MKDIR \"d\"   RMDIR \"d\"\n"
"s$ = TXTREADER$(\"f\")\n"
"TXTWRITER \"f\", s$ [, TRUE append]\n"
"b$ = BINREADER$(\"f\")\n"
"files = DIR$(\"*.jdb\")  as array\n";

static const char* HELP_GFX =
"DRAWCOLOR r, g, b\n"
"PSET x, y      LINE x1,y1,x2,y2\n"
"RECT x,y,w,h [,fill]\n"
"CIRCLE x,y,r [,fill]\n"
"TEXT x, y, s$ [,r,g,b [,scale]]\n"
"colors also trail each call\n"
"CLS on its own line, not after a\n"
"  drawing call in one statement\n"
"screen 320x320, text 40x40\n"
"GFX.BUFFER(x,y,w,h) buffers that\n"
"  rectangle, 16 colours, 4 bits a\n"
"  pixel; answers with the bytes,\n"
"  -1 if there was no room\n"
"SCREENFLIP sends what changed\n"
"GFX.BUFFER(0) frees it again\n"
"GFX.CLEAR(idx)  GFX.BUFFERED()\n"
"GFX.PALETTE(i, r, g, b)  i 0-15\n"
"a whole screen does not fit -\n"
"  buffer the strip that moves\n"
"  and check SYS.FREE first\n"
"BEEP [freq [, ms]]   blocks\n"
"PLAY \"T120 O4 CDEFGAB\"  plays in\n"
"  the background: L4 length,\n"
"  P rest, # sharp, - flat,\n"
"  . dotted, < > octave down/up\n"
"PLAY.STOP()   PLAY.BUSY()\n"
"PLAY.VOLUME(0-100)   TONE(hz)\n";


static const char* HELP_EVENTS =
"ON \"TICK\" CALL Handler\n"
"TIMER.EVERY(ms)   TIMER.STOP()\n"
"ON \"KEY\" CALL H   KEY.WATCH(1)\n"
"  handler gets d[0] = key code\n"
"ON \"PIN\" CALL H\n"
"GPIO.WATCH(pin, edge) 1 up 2 down\n"
"  3 both, 0 off; d[0] pin d[1] lvl\n"
"handlers run between statements,\n"
"  never inside the interrupt\n";

static const char* HELP_HW =
"GPIO.MODE(pin, out)  GPIO.WRITE\n"
"GPIO.READ  GPIO.PULLUP   LED(0/1)\n"
"ADC.READ(ch)  0-3 = GP26-29\n"
"ADC.TEMP()  degrees C\n"
"PWM.SET(pin, hz [,duty%])  PWM.OFF\n"
"I2C.SETUP(bus, sda, scl [,khz])\n"
"I2C.WRITE(bus, adr, bytes)\n"
"I2C.READ(bus, adr, n)  I2C.SCAN(b)\n"
"SPI.SETUP(bus,sck,mosi,miso[,khz])\n"
"SPI.XFER(bus, bytes)  full duplex\n"
"PIO.LOAD(words) -> offset\n"
"PIO.START(sm, off, len, pin [,div])\n"
"PIO.PUT(sm, v)  PIO.GET(sm)\n"
"  nonblocking     PIO.STOP(sm)\n"
"PicoCalc uses: i2c1 keyboard,\n"
"  spi0 SD, spi1 display - user\n"
"  peripherals go on i2c0\n";

static const char* HELP_WIFI =
#ifdef JDB_HAS_CYW43
"WIFI.CONNECT(ssid$, pw$ [,ms])\n"
"  0 = connected (WPA2)\n"
"WIFI.IP$()   WIFI.STATUS()\n"
"HTTP.GET$(\"http://host/p\" [,ms])\n"
"  plain http, empty on failure\n"
"HTTP.POST$(url$, body$ [,type$])\n"
"HTTP.SERVER.ON_GET(path$, FN$)\n"
"HTTP.SERVER.ON_POST(path$, FN$)\n"
"HTTP.SERVER.ON_NOTFOUND(FN$)\n"
"HTTP.SERVER.START(port)  .STOP()\n"
"HTTP.SERVER.WAIT([ms])   serves;\n"
"  .POLL() for your own loop\n"
"handler names UPPERCASE, gets a\n"
"  map: PATH METHOD BODY HEADERS\n"
"  PARAMS; return text, or a map\n"
"  for JSON\n"
"NTP.SYNC([server$] [,hours])\n"
"  sets the clock, hours is your\n"
"  offset from UTC; 0 = no answer\n"
"  then DATE$ TIME$ NOW are real\n";
#else
"this board has no radio\n";
#endif

static const char* HELP_SYS =
"FS.TEST()  flash store selftest\n"
"SD.TEST()  card probe   SD.BB()\n"
"LCD.STAT$()  KBD.RAW$()  KEY.GET()\n"
"PIN.DIAG$() edge ISR + queue state\n"
"SYS.FREE() bytes left for the heap\n"
"FS.NUKEPT() wipes flash to BOOTSEL\n";

struct HelpTopic { const char* name; const char* text; };

static const HelpTopic TOPICS[] = {
    { "BASIC",  HELP_BASIC },
    { "FUNCS",  HELP_FUNCS },
    { "STRING", HELP_STRING },
    { "ARRAY",  HELP_ARRAY },
    { "FILES",  HELP_FILES },
    { "GFX",    HELP_GFX },
    { "HW",     HELP_HW },
    { "EVENTS", HELP_EVENTS },
    { "WIFI",   HELP_WIFI },
    { "SYS",    HELP_SYS },
};

// Page-print: 40 rows on the panel, minus prompt room.
static void help_print(const char* text) {
    int lines = 0;
    while (*text) {
        const char* nl = strchr(text, '\n');
        int n = nl ? (int)(nl - text) : (int)strlen(text);
        printf("%.*s\r\n", n, text);
        text += n + (nl ? 1 : 0);
        if (++lines % 34 == 0 && *text) {
            printf("-- more (q stops) --");
            fflush(NULL);
            int c = repl_read_key();
            printf("\r                    \r");
            if (c == 'q' || c == 'Q' || c == 0x1B) return;
        }
    }
}

void pico_help(const char* topic) {
    while (*topic == ' ') topic++;
    if (!*topic) { help_print(HELP_INDEX); return; }
    for (auto& t : TOPICS) {
        if (strcasecmp(topic, t.name) == 0) { help_print(t.text); return; }
    }
    printf("no such topic: %s\r\n", topic);
    help_print(HELP_INDEX);
}
