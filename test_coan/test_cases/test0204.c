/**ARGS: includes --locate -DFOO=BAR "-DBAR=bar(x,y,z" */
/**SYSCODE: = 2 */
/**NO-OUTPUT */
#ifdef FOO
#include FOO
#elif 1
//KEEP ME
#endif



