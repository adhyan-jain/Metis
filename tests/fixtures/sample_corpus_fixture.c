/* Hand-written C test fixture for corpus event extraction validation
 * (CLAUDE_RESEARCH.md Section 10.2)
 *
 * Exercises:
 *  1. Global declarations (globalVar, globalFunc)
 *  2. Function parameters (paramA, paramB)
 *  3. Local declarations (localVar1, localVar2)
 *  4. Nested blocks ({ ... })
 *  5. Shadowing (inner localVar1 shadows outer localVar1)
 *  6. Redeclaration (function prototypes / extern decls)
 *  7. Repeated use (multiple accesses to paramA, globalVar)
 *  8. Scope exit (inner block variables reclaimed)
 */

extern int globalVar;
int globalVar = 42;  /* redeclaration in same scope */

int globalFunc(int paramA, char paramB) {
    int localVar1 = paramA + 1;  /* local declaration + repeated use of paramA */
    int localVar2 = paramB * 2;

    if (localVar1 > 0) {
        /* Nested block: scope entry */
        int localVar1 = 100;     /* shadowing: inner localVar1 shadows outer localVar1 */
        int innerVar = 200;      /* local declaration in nested scope */
        localVar1 = innerVar + globalVar; /* repeated use of innerVar, globalVar */
        /* Scope exit: innerVar and inner localVar1 go out of scope */
    }

    return localVar1 + localVar2; /* outer localVar1 is visible again */
}
