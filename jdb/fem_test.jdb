' NeReLaBasic - 1D Finite Element Method Example
' ------------------------------------------------
' This program calculates the displacement of nodes in a simple
' 1D bar system made of two elements, fixed at one end and with
' a force applied at the other.
'
' System:
'
'  Node 0 -------- Elem 0 -------- Node 1 -------- Elem 1 -------- Node 2
'  (Fixed)                                                        (Force ->)
'
'=============================================================================
' 1. SETUP AND DEFINE CONSTANTS
'=============================================================================
PRINT "--- 1. Defining Material and Geometric Properties ---"
E = 210000  ' Young's Modulus (e.g., for steel, in N/mm^2)
A = 100     ' Cross-sectional Area (in mm^2)
L = 200     ' Length of one element (in mm)

'=============================================================================
' 2. ELEMENT-LEVEL CALCULATIONS (USING NUMERICAL INTEGRATION)
'=============================================================================
PRINT "--- 2. Calculating Element Stiffness Matrix using INTEGRATE ---"

' For a 2-node 1D bar element, the strain-displacement matrix [B] is derived
' from the derivatives of the shape functions, N1 and N2.
' N1(xi) = 0.5 * (1 - xi)  =>  dN1/dx = -1/L
' N2(xi) = 0.5 * (1 + xi)  =>  dN2/dx =  1/L
' So, [B] = [ -1/L   1/L ]

' The stiffness matrix [k] is the integral of [B]^T * E * [B] * A over the element length.
' This gives us a function to integrate. For k(1,1), the integrand is (-1/L) * E * (-1/L) * A
' which simplifies to a constant: E*A/L^2

FUNC INTEGRAND(X)
    ' x is the coordinate along the element, but our integrand is constant.
    RETURN (E * A) / (L * L)
ENDFUNC

' The domain for integration is the length of the element [0, L]
DIM domain[2]
domain[0] = 0
domain[1] = L

' Integrate the constant function over the domain.
' The result will be (E*A/L^2) * L = E*A/L
k11 = INTEGRATE(INTEGRAND@, domain, 1)

' The element stiffness matrix for a 1D bar is:
' k = (E*A/L) * [[ 1, -1],
'                [-1,  1]]
' We calculated the k11 term, the others follow from it.
DIM k_elem[2, 2]
k_elem[0,0] = k11
k_elem[0,1] = -k11
k_elem[1,0] = -k11
k_elem[1,1] = k11

PRINT "Element Stiffness Matrix [k]:"
PRINT k_elem
PRINT ""

'=============================================================================
' 3. ASSEMBLY OF GLOBAL STIFFNESS MATRIX
'=============================================================================
PRINT "--- 3. Assembling Global Stiffness Matrix [K] ---"

' Define the number of nodes and elements in the system
num_nodes = 3
num_elements = 2

' Define the element connectivity table. Each row represents an element,
' and the columns contain the global node indices it connects.
DIM connect[2, 2]
connect[0, 0] = 0 : connect[0, 1] = 1 ' Element 0 connects nodes 0 and 1
connect[1, 0] = 1 : connect[1, 1] = 2 ' Element 1 connects nodes 1 and 2

' Our system has 3 nodes, so the global matrix is 3x3.
DIM K[3, 3] ' Initializes to all zeros

' --- Loop-based Assembly Procedure ---
' This is a more general approach that can scale to any number of elements.
FOR e = 0 TO num_elements - 1
    FOR i = 0 TO 1 ' Local node i
        FOR j = 0 TO 1 ' Local node j
            ' Get the global degrees of freedom (DOF) indices
            global_row = connect[e, i]
            global_col = connect[e, j]
            
            ' Add the contribution from the local element matrix to the global matrix
            K[global_row, global_col] = K[global_row, global_col] + k_elem[i, j]
        NEXT j
    NEXT i
NEXT e

PRINT "Global Stiffness Matrix [K] before BC:"
PRINT K
PRINT ""

'=============================================================================
' 4. APPLYING BOUNDARY CONDITIONS AND LOADS
'=============================================================================
PRINT "--- 4. Applying Boundary Conditions and Loads ---"

' Define the global force vector {F}
DIM F[3]
F[0] = 0 ' Reaction force at fixed support (unknown for now)
F[1] = 0 ' No force at node 1
F[2] = 5000 ' Applied force of 5000 N at node 2

' Apply Boundary Conditions (BC): Node 0 is fixed (displacement u0 = 0)
' This is done by modifying the [K] matrix and {F} vector.
' We zero out the row and column for the fixed DOF and put a 1 on the diagonal.
K[0,0] = 1
K[0,1] = 0
K[0,2] = 0
K[1,0] = 0
K[2,0] = 0

' And set the corresponding entry in the force vector to the known displacement.
F[0] = 0 ' Displacement at node 0 is 0.

PRINT "Global Stiffness Matrix [K] after BC:"
PRINT K
PRINT "Force Vector {F} after BC:"
PRINT F
PRINT ""

'=============================================================================
' 5. SOLVING THE SYSTEM
'=============================================================================
PRINT "--- 5. Solving the System [K]{u} = {F} ---"

' Use our new SOLVE function to find the displacement vector {u}
DIM u[3]
u = SOLVE(K, F)

PRINT "Solution (Displacement Vector) {u}:"
PRINT u
PRINT ""
PRINT "Displacement at Node 1 (u1):", u[1]; " mm"
PRINT "Displacement at Node 2 (u2):", u[2]; " mm"
