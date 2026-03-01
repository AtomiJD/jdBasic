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

DIM Mass[3]
DIM Pos[3, 2]
DIM Vel[3, 2]
DIM Acc[3, 2] ' This will hold the calculated acceleration for each body

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

    ' Periodically print the state of the system
    IF A MOD PRINT_EVERY = 0 THEN
        FOR i = 0 TO NUM_BODIES - 1
            PRINT A, ",", i, ",", Pos[i, 0], ",", Pos[i, 1]
        NEXT i
    ENDIF
NEXT step

PRINT ""
PRINT "--- Simulation Complete ---"


