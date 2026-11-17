/**ARGS: source -DX */
/**SYSCODE: = 19 */
#define FOO BAR
#define BAR FOO
#if FOO || BAR
KEEP ME
#else
KEEP ME
#endif
