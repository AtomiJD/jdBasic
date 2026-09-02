// Syntax colour for one line of jdBasic, spoken as SGR escapes: the
// panel's parser and any USB terminal both understand them. The scheme
// is the one the editors use: comments green, keywords magenta, numbers
// and constants cyan, strings orange, everything else in the default
// ink. Printing always ends on the default.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static const char* const KEYWORDS[] = {
    "AND", "AS", "CATCH", "CLS", "CONTINUEDO", "CONTINUEFOR", "DIM",
    "DO", "ELSE", "ELSEIF", "ENDFUNC", "ENDIF", "ENDSUB", "ENDTRY",
    "ENDTYPE", "EXITDO", "EXITFOR", "EXITFUNC", "FALSE", "FOR", "FUNC",
    "GOSUB", "GOTO", "IF", "INPUT", "LAMBDA", "LET", "LOOP", "MOD",
    "NEXT", "NOT", "OR", "ANDALSO", "ORELSE", "PRINT", "REM",
    "RETURN", "SLEEP", "STEP", "SUB", "THEN", "TO", "TRY",
    "TYPE", "UNTIL", "WHILE",
};

// Coloured with the numbers, because that is what they are.
static const char* const CONSTANTS[] = {
    "TRUE", "FALSE", "NONE", "PI", "E", "TAU",
};

static int in_list(const char* const* list, unsigned count, const char* s, int n) {
    for (unsigned i = 0; i < count; i++) {
        if ((int)strlen(list[i]) == n && strncasecmp(s, list[i], n) == 0)
            return 1;
    }
    return 0;
}

static int is_keyword(const char* s, int n) {
    return in_list(KEYWORDS, sizeof KEYWORDS / sizeof *KEYWORDS, s, n);
}

static int is_constant(const char* s, int n) {
    return in_list(CONSTANTS, sizeof CONSTANTS / sizeof *CONSTANTS, s, n);
}

#define C_RESET   "\x1b[0m"
#define C_KEYWORD "\x1b[95m"
#define C_STRING  "\x1b[33m"
#define C_NUMBER  "\x1b[96m"
#define C_COMMENT "\x1b[92m"

static int is_word(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '$' || c == '.';
}

void syntax_print(const char* s, int n) {
    int i = 0;
    while (i < n) {
        char c = s[i];
        if (c == '\'') {
            printf(C_COMMENT "%.*s" C_RESET, n - i, s + i);
            break;
        }
        if (c == '"') {
            int j = i + 1;
            while (j < n && s[j] != '"') j++;
            if (j < n) j++;
            printf(C_STRING "%.*s" C_RESET, j - i, s + i);
            i = j;
            continue;
        }
        if (isdigit((unsigned char)c) &&
            (i == 0 || !is_word(s[i - 1]))) {
            int j = i;
            while (j < n && (isdigit((unsigned char)s[j]) || s[j] == '.')) j++;
            printf(C_NUMBER "%.*s" C_RESET, j - i, s + i);
            i = j;
            continue;
        }
        if (isalpha((unsigned char)c) && (i == 0 || !is_word(s[i - 1]))) {
            int j = i;
            while (j < n && is_word(s[j])) j++;
            if (is_keyword(s + i, j - i))
                printf(C_KEYWORD "%.*s" C_RESET, j - i, s + i);
            else if (is_constant(s + i, j - i))
                printf(C_NUMBER "%.*s" C_RESET, j - i, s + i);
            else
                printf("%.*s", j - i, s + i);
            i = j;
            continue;
        }
        printf("%c", c);
        i++;
    }
}
