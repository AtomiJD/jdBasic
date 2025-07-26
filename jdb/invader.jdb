REM =============================================================
REM JD'S INVADERS, TRANSLATED TO NERELA BASIC
REM Original Python version by 'jd'
REM Translation and adaptation by Gemini
REM Correct Gemini's hallucination and misunderstanding by jd
REM =============================================================

REM --- GAME CONFIGURATION ---
MAX_ALIENS = 40
MAX_BLOCKS = 4
ALIEN_COLS = 8
ALIEN_ROWS = 5

REM --- DATA STRUCTURES (REPLACING PYTHON CLASSES) ---

REM -- ALIEN DATA --
DIM AX[MAX_ALIENS]
DIM AY[MAX_ALIENS]
DIM AIX[MAX_ALIENS]
DIM AIY[MAX_ALIENS] ' Initial positions
DIM AP[MAX_ALIENS] ' Points
DIM AD[MAX_ALIENS] ' Direction
DIM AT[MAX_ALIENS] ' Type (0, 1, or 2)
DIM AV[MAX_ALIENS] ' Visible (1=true, 0=false)

REM -- PLAYER DATA --
DIM PX AS INTEGER
DIM PY AS INTEGER
DIM PDIR AS INTEGER
DIM PVIS AS INTEGER
DIM PRES AS INTEGER

REM -- PLAYER MISSILE DATA --
DIM PMX AS INTEGER
DIM PMY AS INTEGER
DIM PMDIR AS INTEGER
DIM PMVIS AS INTEGER
DIM PMFIRE AS INTEGER

REM -- ALIEN MISSILE DATA --
DIM AMX AS INTEGER
DIM AMY AS INTEGER
DIM AMDIR AS INTEGER
DIM AMVIS AS INTEGER
DIM AMFIRE AS INTEGER
CANDIDATE = -1

REM -- BLOCK DATA --
DIM BX[MAX_BLOCKS]
DIM BY[MAX_BLOCKS]
DIM BH[MAX_BLOCKS] ' Hits / Frame
DIM BV[MAX_BLOCKS] ' Visible

REM -- GLOBAL GAME STATE --
DIM SCORE AS INTEGER
DIM LIVES AS INTEGER
DIM SPEED AS INTEGER
DIM GAMEOVER AS INTEGER
DIM DIFFICULTY AS INTEGER
DIM FRAME AS INTEGER
DIM SFRAME AS INTEGER

REM --- SPRITE GRAPHICS ---
DIM PLAYER_T[2]
PLAYER_T[0] = " ▞▚ " ' Player, Line 0
PLAYER_T[1] = "▛▀▀▜" ' Player, Line 1

' Define Block Sprites as 2-line arrays
DIM BLOCK_A_T[2]
BLOCK_A_T[0] = "▗▇▇▖" ' Block A (Full), Line 0
BLOCK_A_T[1] = "████" ' Block A (Full), Line 1

DIM BLOCK_B_T[2]
BLOCK_B_T[0] = "    " ' Block B (Half), Line 0 (top damaged)
BLOCK_B_T[1] = "████" ' Block B (Half), Line 1 (bottom remains)

DIM BLOCK_C_T[2]
BLOCK_C_T[0] = "    " ' Block C (More Damaged), Line 0
BLOCK_C_T[1] = "▚▞▚▞" ' Block C (More Damaged), Line 1

DIM ALIEN_T[12]

ALIEN_T[0] = "▗▇▇▖"
ALIEN_T[1] = "▞  ▚"
ALIEN_T[2] = "▗▇▇▖"
ALIEN_T[3] = "▚  ▞"
ALIEN_T[4] = "▗▇▇▖"
ALIEN_T[5] = " ▚▞ "
ALIEN_T[6] = "▗▇▇▖"
ALIEN_T[7] = " ▞▚"
ALIEN_T[8] = "▛▀▀▜"
ALIEN_T[9] = "▚▅▅▞"
ALIEN_T[10] = "▛▀▀▜"
ALIEN_T[11] = "▚  ▞"

ALIEN_A1$ = "▗▇▇▖▞  ▚"
ALIEN_A2$ = "▗▇▇▖▚  ▞"
ALIEN_B1$ = "▗▇▇▖ ▚▞ "
ALIEN_B2$ = "▗▇▇▖ ▞▚"
ALIEN_C1$ = "▛▀▀▜▚▅▅▞"
ALIEN_C2$ = "▛▀▀▜▚  ▞"
PLAYER$ = " ▞▚ ▛▀▀▜"
P_MISSILE$ = "|"
A_MISSILE$ = "┇"
BLOCK_A$ = "▗▇▇▖████"
BLOCK_B$ = "    ████"
BLOCK_C$ = "    ▚▞▚▞"

REM ======================================================================
REM INITIALIZATION
REM ======================================================================


SUB INIT_LEVEL()
    GAMEOVER = 0
    SPEED = 6
    FRAME = 0
    SFRAME = 0
    
    REM --- CREATE ALIENS ---
    I = 0
    FOR R = 0 TO ALIEN_ROWS - 1
        FOR C = 0 TO ALIEN_COLS - 1
            AIX[I] = 8 + (C * 8)
            AIY[I] = 4 + (R * 3)
            AX[I] = AIX[I]
            AY[I] = AIY[I]
            AD[I] = 1
            AV[I] = 1
            IF R = 0 THEN
                AT[I] = 0
                AP[I] = 500
            ENDIF
            IF R = 1 OR R = 2 THEN
                AT[I] = 1
                AP[I] = 300
            ENDIF
            IF R > 2 THEN
                AT[I] = 2
                AP[I] = 100
            ENDIF
            I = I + 1
        NEXT C
    NEXT R
    
    REM --- CREATE BLOCKS ---
    FOR J = 0 TO 3
        BX[J] = 12 + (J * 17)
        BY[J] = 30
        BH[J] = 0
        BV[J] = 1
    NEXT J

    REM --- INIT PLAYER ---
    PX = 40
    PY = 35
    PDIR = 0
    PVIS = 1
    PRES = 0
    
    REM --- INIT MISSILES ---
    PMVIS = 0
    PMFIRE = 0
    AMVIS = 0
    AMFIRE = 0
ENDSUB

SUB INIT_GAME()
    OPTION "NOPAUSE"
    LIVES = 3
    SCORE = 0
    DIFFICULTY = 10
    INIT_LEVEL
ENDSUB

SUB FIRE_PLAYER_MISSILE()
    PMX = PX + 2
    PMY = PY - 1
    PMVIS = 1
    PMFIRE = 1
ENDSUB

SUB FIRE_ALIEN_MISSILE(I)
    AMX = AX[I] + 2
    AMY = AY[I] + 2
    AMVIS = 1
    AMFIRE = 1
ENDSUB

SUB PLAYER_DIE()
    PVIS = 0
    PRES = 5 ' Respawn timer
    LIVES = LIVES - 1
    IF LIVES < 0 THEN 
       GAMEOVER = 2
    ENDIF
ENDSUB

REM ======================================================================
REM GAME LOGIC AND UPDATES
REM ======================================================================

SUB UPDATE_GAME()

    REM --- MOVE PLAYER ---
    IF PVIS = 1 THEN
        PX = PX + PDIR
        IF PX < 2 THEN 
           PX = 2
        ENDIF
        IF PX > 75 THEN 
           PX = 75
        ENDIF
        PDIR = 0
    ELSE
        PRES = PRES - 1
        IF PRES = 0 THEN 
           PVIS = 1
        ENDIF
    ENDIF

    REM --- MOVE PLAYER MISSILE ---
    IF PMVIS = 1 THEN
        PMY = PMY - 1
        IF PMY < 3 THEN 
	   PMVIS = 0 
           PMFIRE = 0
        ENDIF
        ' Check for hit
        FOR I = 0 TO MAX_ALIENS - 1
            IF AV[I] = 1 THEN
                IS_HIT = HIT(PMX, PMY, AX[I], AY[I])
                IF IS_HIT = 1 THEN
                    AV[I] = 0
                    PMVIS = 0
                    PMFIRE = 0
                    SCORE = SCORE + AP[I]
                ENDIF
            ENDIF
        NEXT I
    ENDIF

    REM --- MOVE ALIENS ---
    MOVE_DOWN = 0
    FOR I = 0 TO MAX_ALIENS - 1
        IF AV[I] = 1 THEN
            IF AX[I] + AD[I] < 2 OR AX[I] + AD[I] > 75 THEN MOVE_DOWN = 1
        ENDIF
    NEXT I

    IF MOVE_DOWN = 1 THEN
        FOR I = 0 TO MAX_ALIENS - 1
            AY[I] = AY[I] + 1
            AD[I] = AD[I] * -1
        NEXT I
        IF SPEED > DIFFICULTY THEN 
           SPEED = SPEED - 1
        ENDIF
    ELSE
        FOR I = 0 TO MAX_ALIENS - 1
            IF AV[I] = 1 THEN 
		AX[I] = AX[I] + AD[I]
	    ENDIF
        NEXT I
    ENDIF

    

    REM --- ALIEN LOGIC (FIRE, GAME OVER CHECK) ---
    ALIVE_COUNT = 0
    FOR I = 0 TO MAX_ALIENS - 1
        IF AV[I] = 1 THEN
            ALIVE_COUNT = ALIVE_COUNT + 1
            IF AY[I] > 32 THEN 
               GAMEOVER = 2 ' Aliens reached bottom
            ENDIF
            ' Randomly decide to fire
            IF AMFIRE = 0 AND RND(1) * 100 < 5 THEN
                CANDIDATE = I
            ENDIF
        ENDIF
    NEXT I

    IF AMFIRE = 0 AND CANDIDATE > -1 THEN 
       FIRE_ALIEN_MISSILE CANDIDATE
    ENDIF
    IF ALIVE_COUNT = 0 AND GAMEOVER = 0 THEN 
       GAMEOVER = 1 ' Level complete
    ENDIF
    REM --- MOVE ALIEN MISSILE ---
    IF AMVIS = 1 THEN
        AMY = AMY + 1
        IF AMY > 36 THEN 
           AMVIS = 0
           AMFIRE = 0
        ENDIF
        ' Check player hit
        IF PVIS = 1 AND HIT(AMX, AMY, PX, PY) = 1 THEN
            PLAYER_DIE
            AMVIS = 0
            AMFIRE = 0
        ENDIF
        ' Check block hit
        FOR I = 0 TO MAX_BLOCKS - 1
            IF BV[I] = 1 AND HIT(AMX, AMY, BX[I], BY[I]) = 1 THEN
                BH[I] = BH[I] + 1
                IF BH[I] > 2 THEN 
		   BV[I] = 0
                ENDIF
                AMVIS = 0
                AMFIRE = 0
            ENDIF
        NEXT I
    ENDIF
ENDSUB

FUNC HIT(X1, Y1, X2, Y2)
    REM Simple box collision. Returns 1 if hit, 0 otherwise.
    IF Y1 >= Y2 AND Y1 < Y2 + 2 THEN
        IF X1 >= X2 AND X1 < X2 + 4 THEN
            RETURN 1
        ENDIF
    ENDIF
    RETURN 0
ENDFUNC



REM ======================================================================
REM RENDERING
REM ======================================================================

sub print_alien(i, f, t)
    LOCATE AY[I], AX[I]
    print ALIEN_T[t*4+f*2]
    LOCATE AY[I]+1, AX[I]
    print ALIEN_T[t*4+1+f*2]
endsub

SUB PRINT_PLAYER()
    LOCATE PY, PX
    PRINT PLAYER_T[0]
    LOCATE PY+1, PX
    PRINT PLAYER_T[1]
ENDSUB

SUB PRINT_BLOCK(I, HITS)
    LOCATE BY[I], BX[I]
    IF HITS = 0 THEN
        PRINT BLOCK_A_T[0]
        LOCATE BY[I]+1, BX[I]
        PRINT BLOCK_A_T[1]
    ENDIF
    IF HITS = 1 THEN
        PRINT BLOCK_B_T[0]
        LOCATE BY[I]+1, BX[I]
        PRINT BLOCK_B_T[1]
    ENDIF
    IF HITS = 2 THEN
        PRINT BLOCK_C_T[0]
        LOCATE BY[I]+1, BX[I]
        PRINT BLOCK_C_T[1]
    ENDIF
ENDSUB


SUB RENDER_SCREEN()
    CLS
    COLOR 7, 0
    LOCATE 1, 1
    PRINT "LIVES: " + STR$(LIVES);
    LOCATE 1, 35
    PRINT "JD INVADERS";
    LOCATE 1, 68
    PRINT "SCORE: " + STR$(SCORE);
    LOCATE 2, 1
    PRINT "--------------------------------------------------------------------------------";

    REM -- DRAW ALIENS --
    FOR I = 0 TO MAX_ALIENS - 1
        IF AV[I] = 1 THEN
            TYPE = AT[I]
            IF TYPE = 0 THEN 
               COLOR 1, 0 ' Red
	    ENDIF
            IF TYPE = 1 THEN 
               COLOR 2, 0 ' Green
	    ENDIF
            IF TYPE = 2 THEN 
               COLOR 6, 0 ' Cyan
            ENDIF
	    PRINT_ALIEN I, FRAME, TYPE
        ENDIF
    NEXT I
    
    REM -- DRAW BLOCKS --
    COLOR 1, 0 ' Gray
    FOR I = 0 TO MAX_BLOCKS - 1 
        IF BV[I] = 1 THEN 
            HITS = BH[I] 
            PRINT_BLOCK I, HITS 
        ENDIF
    NEXT I 

    REM -- DRAW PLAYER --
    IF PVIS = 1 THEN
        COLOR 4, 0 ' Blue
	PRINT_PLAYER 
    ENDIF
    
    REM -- DRAW MISSILES --
    IF PMVIS = 1 THEN
        COLOR 7, 0 ' White
        LOCATE PMY, PMX
	PRINT P_MISSILE$;
    ENDIF
    IF AMVIS = 1 THEN
        COLOR 1, 0 ' Red
        LOCATE AMY, AMX
	PRINT A_MISSILE$;
    ENDIF

    REM -- DRAW GAME OVER MESSAGES --
    IF GAMEOVER > 0 THEN
        COLOR 7, 0
        LOCATE 20, 35
	PRINT "GAME OVER"
        IF GAMEOVER = 1 THEN
            LOCATE 22, 28
	    PRINT "PRESS 'N' FOR NEXT LEVEL"
        ELSE
            LOCATE 22, 36
	    PRINT "YOU LOST"
            LOCATE 23, 29
	    PRINT "PRESS 'N' TO RESTART"
        ENDIF
    ENDIF
ENDSUB


REM ======================================================================
REM MAIN GAME LOOP
REM ======================================================================
SUB GAME_LOOP()
MAIN_LOOP:
    
    K$ = INKEY$()
    IF K$ = "z" or asc(k$) = 27 THEN 
	GOTO END_GAME
    ENDIF
    IF GAMEOVER > 0 THEN
        IF K$ = "n" THEN
            IF GAMEOVER = 1 THEN
                LIVES = LIVES + 1
                INIT_LEVEL
            ELSE
                INIT_GAME
            ENDIF
        ENDIF
    ENDIF

    IF PVIS = 1 THEN
        IF K$ = "a" THEN 
	   PDIR = -1
	ENDIF
        IF K$ = "d" THEN 
	   PDIR = 1
	ENDIF
        IF K$ = " " AND PMFIRE = 0 THEN 
	   FIRE_PLAYER_MISSILE
	ENDIF
    ENDIF
    
    IF GAMEOVER = 0 THEN
        SFRAME = SFRAME + 1
        IF SFRAME > SPEED THEN
            UPDATE_GAME
	    RENDER_SCREEN
            SFRAME = 0
	    FRAME = FRAME + 1
	    IF FRAME > 1 then
	       FRAME = 0
	    ENDIF
        ENDIF
    ENDIF

    
    
    SLEEP 20
    GOTO MAIN_LOOP

END_GAME:
    CURSOR 1
    CLS
ENDSUB



CLS
CURSOR 0
INIT_GAME
GAME_LOOP
