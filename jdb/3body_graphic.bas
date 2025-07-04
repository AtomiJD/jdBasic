' NeReLaBasic - Three-Body Problem Simulation
' -------------------------------------------
' This program simulates the motion of three bodies interacting
' under their mutual gravitational attraction. It uses initial
' conditions that lead to a stable "figure-8" choreography.
'
' Method: Numerical integration using the Semi-Implicit Euler method.

'=============================================================================
' 1. SUBROUTINE TO CALCULATE ACCELERATIONS
'=============================================================================
SUB CalculateAccelerations()
    ' This subroutine calculates the acceleration on each body
    ' based on the gravitational pull from all other bodies.

    ' First, reset all accelerations to zero.
    FOR i = 0 TO NUM_BODIES - 1
        Acc[i, 0] = 0
        Acc[i, 1] = 0
    NEXT i

    ' Loop through each pair of bodies to calculate forces.
    FOR i = 0 TO NUM_BODIES - 1
        FOR j = 0 TO NUM_BODIES - 1
            IF i <> j THEN
                ' Calculate the vector from body i to body j
                rx = Pos[j, 0] - Pos[i, 0]
                ry = Pos[j, 1] - Pos[i, 1]

                ' Calculate squared distance. Add a small epsilon (softening factor)
                ' to prevent division by zero if bodies get too close.
                dist_sq = rx*rx + ry*ry + 0.001

                ' Calculate distance
                dist = SQR(dist_sq)

                ' Calculate magnitude of the force: F = G * m1 * m2 / d^2
                force_mag = (G * Mass[i] * Mass[j]) / dist_sq

                ' The acceleration is Force / mass_i. Note Mass[i] cancels out.
                accel_mag = (G * Mass[j]) / dist_sq

                ' Add this acceleration to body i's total acceleration vector.
                ' The direction is (rx/dist, ry/dist).
                Acc[i, 0] = Acc[i, 0] + accel_mag * rx / dist
                Acc[i, 1] = Acc[i, 1] + accel_mag * ry / dist
            ENDIF
        NEXT j
    NEXT i
ENDSUB

'=============================================================================
' 2. SETUP AND DEFINE SIMULATION CONSTANTS
'=============================================================================
PRINT "--- 1. Setting up Simulation Constants ---"

' --- GRAPHICS CONSTANTS ---
SCREEN_WIDTH = 800
SCREEN_HEIGHT = 600
VIEW_SCALE = 220   ' Scales the simulation space to fit the screen (pixels per unit)
BODY_RADIUS = 5    ' Radius of the circle for each body in pixels
TRAIL_LENGTH = 50  ' How many past positions to store for the trail effect

G = 1.0           ' Gravitational constant (set to 1 for simplicity)
DT = 0.001        ' Time step for the simulation. Smaller is more accurate.
NUM_STEPS = 2000  ' Number of steps to simulate.
PRINT_EVERY = 200 ' How often to print the state (every N steps).
NUM_BODIES = 3
DIMS = 2          ' 2 for 2D simulation (x, y)

'=============================================================================
' 3. INITIALIZE DATA STRUCTURES AND SET INITIAL CONDITIONS
'=============================================================================
PRINT "--- 2. Initializing Body States (Mass, Position, Velocity) ---"

' --- Initialize Graphics Window ---
SCREEN SCREEN_WIDTH, SCREEN_HEIGHT, "Three-Body Problem"

DIM Mass[3]
DIM Pos[3, 2]
DIM Vel[3, 2]
DIM Acc[3, 2] ' This will hold the calculated acceleration for each body

' --- Array to store the orbital trails ---
DIM Trail[3, TRAIL_LENGTH, 2] ' For each body, store X and Y for TRAIL_LENGTH steps
trail_idx = 0 ' The current index in our trail's circular buffer

' Set masses (equal mass system)
Mass[0] = 1
Mass[1] = 1
Mass[2] = 1

' Set initial positions for the "figure-8" orbit
Pos[0, 0] = 0.97000436
Pos[0, 1] = -0.24308753

Pos[1, 0] = -0.97000436
Pos[1, 1] = 0.24308753

Pos[2, 0] = 0.0
Pos[2, 1] = 0.0

' Set initial velocities for the "figure-8" orbit
Vel[0, 0] = 0.466203685
Vel[0, 1] = 0.43236573

Vel[1, 0] = 0.466203685
Vel[1, 1] = 0.43236573

Vel[2, 0] = -0.93240737
Vel[2, 1] = -0.86473146

'=============================================================================
' 4. THE MAIN SIMULATION LOOP
'=============================================================================
PRINT "--- 3. Starting Simulation ---"
PRINT "Step, Body, PosX, PosY" ' CSV Header for easy plotting

FOR A = 1 TO NUM_STEPS
    ' First, calculate the accelerations on all bodies based on their current positions.
    CalculateAccelerations

    ' Next, update the velocity and position for each body.
    Vel = Vel + acc * DT
    Pos = Pos + Vel * DT
    FOR i = 0 TO NUM_BODIES - 1
        ' Store the new position in the trail buffer
        Trail[i, trail_idx, 0] = Pos[i, 0]
        Trail[i, trail_idx, 1] = Pos[i, 1]
    NEXT i

    trail_idx = (trail_idx + 1) MOD TRAIL_LENGTH


    ' --- DRAWING LOGIC ---
    ' Clear the screen to black
    CLS 0, 0, 0

    ' Draw the trails first, so the bodies appear on top
    FOR i = 0 TO NUM_BODIES - 1
        ' Set trail color based on body index
        IF i = 0 THEN r = 100: g = 20: b = 20 
        IF i = 1 THEN r = 20: g = 100: b = 20 
        IF i = 2 THEN r = 20: g = 20: b = 100 
        
        FOR t = 0 TO TRAIL_LENGTH - 2
            ' Get two consecutive points from the trail buffer
            idx1 = (trail_idx + t) MOD TRAIL_LENGTH
            idx2 = (trail_idx + t + 1) MOD TRAIL_LENGTH

            ' Get simulation coordinates
            sim_x1 = Trail[i, idx1, 0] : sim_y1 = Trail[i, idx1, 1]
            sim_x2 = Trail[i, idx2, 0] : sim_y2 = Trail[i, idx2, 1]
            
            ' Don't draw lines from uninitialized parts of the trail buffer
            IF sim_x1 <> 0 AND sim_x2 <> 0 THEN
                ' Convert simulation coords to screen coords for both points
                screen_x1 = (sim_x1 * VIEW_SCALE) + (SCREEN_WIDTH / 2)
                screen_y1 = (sim_y1 * VIEW_SCALE) + (SCREEN_HEIGHT / 2)
                screen_x2 = (sim_x2 * VIEW_SCALE) + (SCREEN_WIDTH / 2)
                screen_y2 = (sim_y2 * VIEW_SCALE) + (SCREEN_HEIGHT / 2)
                LINE screen_x1, screen_y1, screen_x2, screen_y2, r, g, b
            ENDIF
        NEXT t
    NEXT i


    ' Draw the bodies themselves
    FOR i = 0 TO NUM_BODIES - 1
        ' Get the body's current simulation coordinates
        sim_x = Pos[i, 0]
        sim_y = Pos[i, 1]

        ' Convert simulation coordinates to screen pixel coordinates
        screen_x = (sim_x * VIEW_SCALE) + (SCREEN_WIDTH / 2)
        screen_y = (sim_y * VIEW_SCALE) + (SCREEN_HEIGHT / 2)

        ' Set color based on body index
        IF i = 0 THEN r = 255: g = 50: b = 50 
        IF i = 1 THEN r = 50: g = 255: b = 50 
        IF i = 2 THEN r = 50: g = 50: b = 255 

        ' Draw the body as a circle
        CIRCLE screen_x, screen_y, BODY_RADIUS, r, g, b
    NEXT i

    ' Flip the screen to show what we've drawn
    SCREENFLIP
    SLEEP 1

NEXT A

PRINT ""
PRINT "--- Simulation Complete ---"


