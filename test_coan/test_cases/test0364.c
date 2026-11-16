/**ARGS: symbols --explain "-DFOO=(1+1)" -DBAR=3 */
/**SYSCODE: = 6 */
#define SYM ((FOO) And (FOO SYM BAR) And (BAR))
#if SYM
KEEP ME
#endif
