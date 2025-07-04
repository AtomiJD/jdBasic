print "--- Web Test Program ---"
print

' Test a simple GET request to an API that returns JSON

print "LALL"


API_URL$ = "https://jsonplaceholder.typicode.com/todos/1"

print "lull"

JSON_DATA$ = HTTP.GET$(API_URL$)

IF ERR = 0 THEN
    print "HTTP Status Code: "; HTTP.STATUSCODE()
    print "Received JSON:"; JSON_DATA$
ELSE
    print "Error fetching URL: "; API_URL$; " (Error: "; ERR; ")"
ENDIF
print

' Test setting headers (e.g., for a simple authentication)
print "Testing HTTP.CLEARHEADERS..."

HTTP.CLEARHEADERS
print "Testing HTTP.SETHEADER..."

HTTP.SETHEADER "X-Custom-Header", "MyCustomValue" 
HTTP.SETHEADER "Accept", "application/json" 

' Try another GET request with headers
API_URL2$ = "https://httpbin.org/headers" ' This API echoes back headers
print "Fetching headers from httpbin.org/headers..."
HEADER_RESPONSE$ = HTTP.GET$(API_URL2$)

IF ERR = 0 THEN
    print "HTTP Status Code: "; HTTP.STATUSCODE()
    print "Headers Received:"; HEADER_RESPONSE$
ELSE
    print "Error fetching URL: "; API_URL2$; " (Error: "; ERR; ")"
ENDIF
print

print "--- Web Test Complete ---"