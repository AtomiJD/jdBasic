' 1. Define the custom structure as you did before
TYPE T_Character
    Name AS STRING
    HitPoints AS INTEGER
    Strength AS DOUBLE
    SUB INIT()
        THIS.HitPoints = 150
        THIS.Strength = 4.5
    ENDSUB
    FUNC GETSUMMARY()
        r =  THIS.Name + ": Hitpoints: " + str$(THIS.HitPoints) + ", Strength: " + THIS.Strength
        RETURN r
    ENDFUNC
ENDTYPE

' 2. Initialize an empty array to hold the characters
Party = []
PRINT "Created an empty party array."

' 3. Create and populate the first character instance
DIM Player1 AS T_Character
Player1.INIT
Player1.Name = "Atomi"
Player1.Strength = 18.5

' 4. Append the first character to the array
Party = APPEND(Party, Player1)
PRINT Player1.Name; " has joined the party."

' 5. Create and populate a second character
DIM Player2 AS T_Character
Player2.Name = "Lira"
Player2.HitPoints = 120
Player2.Strength = 14.2

' 6. Append the second character to the array
Party = APPEND(Party, Player2)
PRINT Player2.Name; " has joined the party."
PRINT

' 7. Access and print the data from the array
PRINT "--- Current Party ---"
PRINT "Party size: "; LEN(Party)
PRINT "First Item in Array: "; 
print party[0].GETSUMMARY()

PRINT
PRINT "Full party data:"
PRINT Party