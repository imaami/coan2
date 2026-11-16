/**ARGS: symbols --explain */
/**SYSCODE: = 6 */
#define FOO(x,y) FOO(x+1,y+1)
#if FOO(0,0)
KEEP ME
#else
KEEP ME
#endif
