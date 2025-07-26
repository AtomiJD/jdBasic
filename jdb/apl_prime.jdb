' --- APL/Functional Style Prime Sieve (Set-based) ---

PRINT "--- Calculating Primes using Set Difference ---"
LIMIT = 1000

' 1. Define the set of potential primes: all odd numbers from 3 up to the limit.
ODDS = (IOTA(LIMIT/2) - 1) * 2 + 3
PRINT "Generated " ; LEN(ODDS); " odd numbers to test."

' 2. Define the set of potential prime factors.
FACTORS = []
FOR I = 3 TO SQR(LIMIT) STEP 2
    FACTORS = APPEND(FACTORS, I)
NEXT I

' 3. Define the set of composite numbers by iterating through the factors.
COMPOSITES = []
FOR I = 0 TO LEN(FACTORS)-1
    P = FACTORS[I]
    ' Generate all odd multiples of P starting from P*P.
    FOR J = P * P TO LIMIT STEP P * 2
        COMPOSITES = APPEND(COMPOSITES, J)
    NEXT J
NEXT I

' 4. Calculate the difference between the two sets.
ODD_PRIMES = DIFF(ODDS, COMPOSITES)

' 5. The final list of primes is 2, plus all the odd primes.
ALL_PRIMES = APPEND([2], ODD_PRIMES) 

PRINT "Primes up to "; LIMIT; " are:"
PRINT ALL_PRIMES
