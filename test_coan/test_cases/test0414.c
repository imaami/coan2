/**ARGS: source --implicit */
/**SYSCODE: = 5 */
/**NO-OUTPUT */
#if BAR
DELETE ME
#else
KEEP ME
#endif
#if BAR(1)
KEEP ME
#else
KEEP ME
#endif
