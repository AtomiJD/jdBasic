' ==========================================================
' == jdBASIC Interactive Synthesizer Demo
' ==========================================================
'
' This program demonstrates the SOUND.* commands by creating
' a playable on-screen piano keyboard.
'
' --- CONTROLS ---
' Keys A,S,D,F,G,H,J,K map to the white keys (C4 to C5).
' Keys W,E,T,Y,U map to the black keys.
' Press ESC to quit.
'
' ----------------------------------------------------------

' --- Initialization ---
OPTION "NOPAUSE"
SCREEN 800, 300, "jdBASIC Synthesizer"
SOUND.INIT

' Configure a "Rhodes" like electric piano sound on track 0
' waveform$, attack, decay, sustain, release
SOUND.VOICE 0, "SINE", 0.01, 0.3, 0.4, 0.4

' Configure a "Synth Bass" sound on track 1
SOUND.VOICE 1, "SAW", 0.02, 0.1, 0.9, 0.2


' --- Data Setup ---
' Create a MAP to store note frequencies for easy lookup.
DIM NOTES As MAP

NOTES{"C4"}  = 261.63 : NOTES{"C#4"} = 277.18
NOTES{"D4"}  = 293.66 : NOTES{"D#4"} = 311.13
NOTES{"E4"}  = 329.63
NOTES{"F4"}  = 349.23 : NOTES{"F#4"} = 369.99
NOTES{"G4"}  = 392.00 : NOTES{"G#4"} = 415.30
NOTES{"A4"}  = 440.00 : NOTES{"A#4"} = 466.16
NOTES{"B4"}  = 493.88
NOTES{"C5"}  = 523.25

' Map keyboard keys to note names.
DIM KEY_MAP As MAP
KEY_MAP{"A"}="C4"  : KEY_MAP{"S"}="D4" : KEY_MAP{"D"}="E4"
KEY_MAP{"F"}="F4"  : KEY_MAP{"G"}="G4" : KEY_MAP{"H"}="A4"
KEY_MAP{"J"}="B4"  : KEY_MAP{"K"}="C5"
' Black keys
KEY_MAP{"W"}="C#4" : KEY_MAP{"E"}="D#4" : KEY_MAP{"T"}="F#4"
KEY_MAP{"Y"}="G#4" : KEY_MAP{"U"}="A#4"


' --- Drawing Subroutine ---
SUB DRAW_KEYBOARD(LAST_KEY$)
  CLS 30, 30, 40
  
  ' Draw White Keys
  WHITE_KEYS$ = "ASDFGHJK"
  FOR i = 0 TO LEN(WHITE_KEYS$) - 1
    key_char$ = MID$(WHITE_KEYS$, i + 1, 1)
    x = 50 + (i * 80)
    
    ' Highlight the currently pressed key
    IF key_char$ = UCASE$(LAST_KEY$) THEN
      RECT x, 50, 78, 180, 200, 200, 255, TRUE
    ELSE
      RECT x, 50, 78, 180, 255, 255, 255, TRUE
    ENDIF
    RECT x, 50, 78, 180, 0, 0, 0, FALSE ' Border
    TEXT x + 30, 200, key_char$, 0, 0, 0
  NEXT i
  
  ' Draw Black Keys
  BLACK_KEYS$ = "WETYU"
  BLACK_KEY_POS = [1.5, 2.5, 4.5, 5.5, 6.5]
  FOR i = 0 TO LEN(BLACK_KEYS$) - 1
      key_char$ = MID$(BLACK_KEYS$, i + 1, 1)
      x = 50 + (BLACK_KEY_POS[i] * 80)
      
      IF key_char$ = UCASE$(LAST_KEY$) THEN
        RECT x, 50, 50, 120, 150, 150, 150, TRUE
      ELSE
        RECT x, 50, 50, 120, 0, 0, 0, TRUE
      ENDIF
      TEXT x + 18, 150, key_char$, 255, 255, 255
  NEXT i
  
  TEXT 10, 270, "Press keyboard keys to play notes. ESC to quit.", 200, 200, 200
  SCREENFLIP
ENDSUB


' --- Main Program Loop ---
LAST_KEY_PLAYED$ = ""

QUIT = false

DO
  ' Initial draw
  DRAW_KEYBOARD(LAST_KEY_PLAYED$)

  ' Loop while waiting for a key press
  DO
    KEY_PRESSED$ = UCASE$(INKEY$())
    IF KEY_PRESSED$ <> "" THEN EXITDO
    
    ' If a note was playing but no key is pressed now, release it.
    IF LAST_KEY_PLAYED$ <> "" THEN
      SOUND.RELEASE 0
      LAST_KEY_PLAYED$ = ""
      DRAW_KEYBOARD(LAST_KEY_PLAYED$) ' Redraw to un-highlight
    ENDIF
    
    SLEEP 10 ' Small delay to prevent high CPU usage
  LOOP UNTIL QUIT

  IF QUIT THEN EXITDO

  ' Check for ESC key to quit
  IF ASC(KEY_PRESSED$) = 27 THEN
    QUIT = TRUE
  ENDIF

  ' Check if the pressed key is a valid note and is a *new* note
  IF MAP.EXISTS(KEY_MAP, KEY_PRESSED$) AND KEY_PRESSED$ <> LAST_KEY_PLAYED$ THEN
      NOTE_NAME$ = KEY_MAP{KEY_PRESSED$}
      FREQ = NOTES{NOTE_NAME$}
      
      ' Play the new note
      SOUND.PLAY 0, FREQ
      
      ' Play a bass note an octave lower on track 1
      SOUND.PLAY 1, FREQ / 2
      
      LAST_KEY_PLAYED$ = KEY_PRESSED$
  ENDIF
  
LOOP UNTIL QUIT
