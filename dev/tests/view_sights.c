/* The real cg_sights.c for view_test.c, in its own TU (Q3 headers have no
 * include guards), with its aim-in time read out so the test never copies one. */
#include "../../code/cgame/cg_sights.c"
int ViewTestSightIn( int weapon ) { return (int)cg_sights[weapon].inTime; }
