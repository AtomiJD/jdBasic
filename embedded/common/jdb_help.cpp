// HELP at the prompt: the board's own manual, embedded in the image and
// shaped for a forty-column screen. Topics cover exactly what this build
// registers, so the same file answers for a PicoCalc, a Fruit Jam and an
// ES3C28P without any of them claiming the others' verbs. Output pages
// against the console, and any key continues, q stops.

#include <stdio.h>
#include <string.h>
#include <strings.h>

extern "C" int repl_read_key(void);
extern "C" void jdb_con_size(int* cols, int* rows);

#if defined(FRUITJAM)
#define BOARD_NAME "the Fruit Jam"
#elif defined(PICOCALC)
#define BOARD_NAME "the PicoCalc"
#elif defined(ESP32)
#define BOARD_NAME "the ES3C28P"
#else
#define BOARD_NAME "this board"
#endif

static const char* HELP_INDEX =
"jdBasic on " BOARD_NAME " - HELP <topic>\n"
"  BASIC   the language core\n"
"  FUNCS   numbers and logic\n"
"  STRING  text functions\n"
"  ARRAY   array functions\n"
"  FILES   programs, flash and SD\n"
"  GFX     graphics and sound\n"
"  HW      pins and buses\n"
"  BOARD   what is soldered to it\n"
"  EVENTS  timers, keys, pin edges\n"
"  WIFI    network\n"
"  SYS     memory and diagnostics\n"
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
#ifndef ESP32
"CD dir  CD ..  CD    current dir\n"
#endif
"DIR [\"*.jdb\"]    name and size\n"
"TYPE f  DEL f  COPY a b  REN a b\n"
"MD d  RD d     unquoted args ok\n"
"LOAD prog  RUN [prog]  EDIT prog\n"
"LIST [prog]  NEW prog  SAVE name\n"
"  a name with no dot means .jdb\n"
"AUTORUN name   run it at power-on\n"
"AUTORUN        show;  AUTORUN OFF\n"
"RECV name      take a file off the\n"
"  wire raw, no echo, ends Ctrl-D\n"
#ifdef JDB_HAS_CYW43
"INSTALL url [name]  fetch into /\n"
#endif
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
"GFX.WIDTH GFX.HEIGHT GFX.CLEAR(i)\n"
"GFX.PALETTE(i, r, g, b)  i 0-15\n"
"GFX.CONSIZE  [columns, rows]\n"
#if defined(FRUITJAM)
"screen 320x240, text 40x30\n"
"the console and a drawing program\n"
"  share one framebuffer:\n"
"  GFX.CONSOLE 0 takes the screen,\n"
"  GFX.CONSOLE 1 gives it back;\n"
"  the prompt stays on USB\n"
"SCREENFLIP does nothing - the\n"
"  framebuffer is already on wire\n"
"DVI.FRAMES DVI.FRAMEUS DVI.CLOCK\n"
#elif defined(PICOCALC)
"screen 320x320, text 40x40\n"
"GFX.BUFFER(x,y,w,h) buffers that\n"
"  rectangle, 16 colours, 4 bits a\n"
"  pixel; answers with the bytes,\n"
"  -1 if there was no room\n"
"SCREENFLIP sends what changed\n"
"GFX.BUFFER(0) frees it again\n"
"GFX.BUFFERED()  is one held?\n"
"a whole screen does not fit -\n"
"  buffer the strip that moves\n"
"  and check SYS.FREE first\n"
#elif defined(ESP32)
"screen 320x240, text 40x30\n"
"SCREEN starts the panel; every\n"
"  primitive writes a framebuffer\n"
"  and SCREENFLIP puts it on glass\n"
"GFX.CONSOLE 0/1  panel as console\n"
"GFX.LIGHT(on)    backlight\n"
#endif
"BEEP [freq [, ms]]   blocks\n"
"PLAY \"T120 O4 CDEFGAB\"  plays in\n"
"  the background: L4 length,\n"
"  P rest, # sharp, - flat,\n"
"  . dotted, < > octave down/up\n"
"PLAY.STOP()   PLAY.BUSY()\n"
"PLAY.VOLUME(0-100)   TONE(hz)\n";

static const char* HELP_BOARD =
#if defined(FRUITJAM)
"BUTTON.GET(n)  1 is BOOT, 2 and 3\n"
"  the pair beside it; TRUE = down\n"
"BUTTON.COUNT   how many there are\n"
"NEOPIXEL.SET(i, r, g, b)\n"
"NEOPIXEL.SHOW  nothing lights up\n"
"  until this; NEOPIXEL.CLEAR\n"
"NEOPIXEL.COUNT five of them\n"
"IR.RAW()  the receiver's pin, raw\n"
"KBD.LAYOUT      \"US\" or \"DE\"\n"
"KBD.LAYOUT(\"DE\") sets it; put it\n"
"  in an AUTORUN program to keep\n"
"USB.KEYBOARDS USB.DEVICES\n"
"USB.KEYS USB.PENDING USB.START\n"
"SND.OUT(1) speaker, SND.OUT(0)\n"
"  headphones - one or the other\n"
"SND.PROBE SND.STAT SND.PINS\n"
"SD card is /sd, SD.TEST() probes\n";
#elif defined(PICOCALC)
"KEY.GET()   one key code, 0 if\n"
"  nothing is waiting\n"
"KBD.RAW$()  what the controller\n"
"  actually sent\n"
"LCD.STAT$() LCD.ROW$(y)\n"
"LCD.TAP$()  LCD.TAPARM()\n"
"SD card is /sd, SD.TEST() probes\n"
"  it, SD.BB() bit-bangs the wire\n"
"keys: ESC is 177, arrows 180-183\n";
#elif defined(ESP32)
"TOUCH       [count, x, y]\n"
"TOUCH.RAW   before the mapping\n"
"TOUCH.ID    [chip, vendor, 0]\n"
"MIC([ms])   [peak, mean], 0-100\n"
"MIC.GAIN(0-7) six dB a step\n"
"SD.MOUNT    card at /sd, in MB\n"
"SD.UNMOUNT  SD.INFO\n"
"GFX.DIAG GFX.PANELSTATE\n"
"GFX.READBACK(x,y) GFX.PANELREG(c)\n";
#else
"LED(0/1)  and whatever you wire\n"
"  to the pins - see HELP HW\n";
#endif

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
"GPIO.READ  GPIO.PULLUP\n"
#ifndef ESP32
"LED(0/1)\n"
"ADC.READ(ch)  0-3 = GP26-29\n"
#else
"ADC.READ(pin) GPIO 1-10 in order\n"
"PIN.FREE      what is left to use\n"
#endif
"ADC.TEMP()  degrees C\n"
"PWM.SET(pin, hz [,duty%])  PWM.OFF\n"
"I2C.SETUP(bus, sda, scl [,khz])\n"
"I2C.WRITE(bus, adr, bytes)\n"
"I2C.READ(bus, adr, n)  I2C.SCAN(b)\n"
"SPI.SETUP(bus,sck,mosi,miso[,khz])\n"
"SPI.XFER(bus, bytes)  full duplex\n"
#ifndef ESP32
"PIO.LOAD(words) -> offset\n"
"PIO.START(sm, off, len, pin [,div])\n"
"PIO.PUT(sm, v)  PIO.GET(sm)\n"
"  nonblocking     PIO.STOP(sm)\n"
#endif
#if defined(FRUITJAM)
"taken already: PIO0 USB host,\n"
"  PIO1 sound, PIO2 the LEDs;\n"
"  GP29 is the IR receiver\n"
#elif defined(PICOCALC)
"taken already: i2c1 keyboard,\n"
"  spi0 SD, spi1 display - user\n"
"  peripherals go on i2c0\n"
#elif defined(ESP32)
"GPIO 26-32 are flash and 33-37\n"
"  PSRAM: every verb refuses them\n"
"  by name, because touching one\n"
"  takes the board down silently\n"
#endif
;

static const char* HELP_WIFI =
#if defined(JDB_HAS_CYW43) || defined(ESP32)
"WIFI.CONNECT(ssid$, pw$ [,ms])\n"
"  0 = connected (WPA2)\n"
"WIFI.AUTO()  reads wifi.txt: ssid\n"
"  on one line, password the next\n"
"WIFI.IP$()   WIFI.STATUS()\n"
"WIFI.DIAG$()\n"
#ifdef ESP32
"WIFI.AP(ssid$ [,pw$ [,channel]])\n"
"  the board's own net on\n"
"  192.168.4.1, WPA2 with 8+ chars\n"
"WIFI.SCAN()  WIFI.CLIENTS()\n"
"WIFI.MAC$()  WIFI.OFF()\n"
#endif
"HTTP.GET$(\"http://host/p\" [,ms])\n"
"  plain http, empty on failure\n"
"HTTP.POST$(url$, body$ [,type$])\n"
"HTTP.SERVER.ON_GET(path$, FN$)\n"
"HTTP.SERVER.ON_POST(path$, FN$)\n"
"HTTP.SERVER.ON_NOTFOUND(FN$)\n"
"HTTP.SERVER.START(port)  .STOP()\n"
"HTTP.SERVER.WAIT([ms])  serves,\n"
"  ESC stops it;\n"
"  .POLL() for your own loop\n"
"handler names UPPERCASE, gets a\n"
"  map: PATH METHOD BODY HEADERS\n"
"  PARAMS; return text, or a map\n"
"  for JSON\n"
#ifndef ESP32
"NTP.SYNC([server$] [,hours])\n"
"  sets the clock, hours is your\n"
"  offset from UTC; 0 = no answer\n"
"  then DATE$ TIME$ NOW are real\n"
#endif
;
#else
"this board has no radio\n";
#endif

static const char* HELP_SYS =
"SYS.FREE()    bytes left, total\n"
"SYS.LARGEST() biggest single block\n"
"  - the number a growing array\n"
"  hits first, long before FREE\n"
"SYS.DF()      the flash store\n"
"SYS.FREEDISK() the same as bytes\n"
#ifndef ESP32
"SYS.CHUNKS()  where a loaded\n"
"  program's memory actually went\n"
"FS.TEST()  flash store selftest\n"
"FS.NUKEPT() wipes flash to BOOTSEL\n"
"PIN.DIAG$() edge ISR + queue state\n"
#else
"SYS.MEM()     both pools, with the\n"
"  low-water mark since boot\n"
"SYS.INTERNAL() SYS.PSRAM()\n"
"SYS.NATIVES()  what the builtins\n"
"  cost to register\n"
"PIN.DIAG$() edge ISR + queue state\n"
#endif
;

struct HelpTopic { const char* name; const char* text; };

static const HelpTopic TOPICS[] = {
    { "BASIC",  HELP_BASIC },
    { "FUNCS",  HELP_FUNCS },
    { "STRING", HELP_STRING },
    { "ARRAY",  HELP_ARRAY },
    { "FILES",  HELP_FILES },
    { "GFX",    HELP_GFX },
    { "BOARD",  HELP_BOARD },
    { "HW",     HELP_HW },
    { "EVENTS", HELP_EVENTS },
    { "WIFI",   HELP_WIFI },
    { "SYS",    HELP_SYS },
};

// Page-print against whatever the console is, minus prompt room.
static void help_print(const char* text) {
    int cols = 40, rows = 40;
    jdb_con_size(&cols, &rows);
    int page = rows > 6 ? rows - 6 : 10;
    int lines = 0;
    while (*text) {
        const char* nl = strchr(text, '\n');
        int n = nl ? (int)(nl - text) : (int)strlen(text);
        printf("%.*s\n", n, text);
        text += n + (nl ? 1 : 0);
        if (++lines % page == 0 && *text) {
            printf("-- more (q stops) --");
            fflush(NULL);
            int c = repl_read_key();
            printf("\r                    \r");
            if (c == 'q' || c == 'Q' || c == 0x1B) return;
        }
    }
}

void jdb_help(const char* topic) {
    while (*topic == ' ') topic++;
    if (!*topic) { help_print(HELP_INDEX); return; }
    for (auto& t : TOPICS) {
        if (strcasecmp(topic, t.name) == 0) { help_print(t.text); return; }
    }
    printf("no such topic: %s\n", topic);
    help_print(HELP_INDEX);
}
