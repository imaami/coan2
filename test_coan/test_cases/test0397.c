/**ARGS: source -DX */
/**SYSCODE: = 19 */
#define FOO BARX
#define BARX FOO
#if FOO == BARX
KEEP_ME
#else
KEEP ME
#endif
