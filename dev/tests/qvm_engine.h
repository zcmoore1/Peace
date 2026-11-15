/* The engine a QVM boundary test runs a real cgame.qvm in: the client state
 * cl_cgame.c and cl_input.c name, and what the real vm.c needs to load one
 * QVM from disk and interpret it (qvm_engine.c). kick_qvm_test.c and
 * pose_qvm_test.c share it. */
#ifndef QVM_ENGINE_H
#define QVM_ENGINE_H
#include "../../code/client/client.h"
#include "../../code/botlib/botlib.h"
#include "../../code/qcommon/vm_local.h"
#include <setjmp.h>

extern const char *qvmPath;	/* the .qvm VM_Create loads */
/* Com_Error's ERR_DROP longjmps here while it is set, with its message in
 * qvmDropMessage; any other Com_Error fails the test. */
extern jmp_buf *qvmDrop;
extern char qvmDropMessage[1024];

intptr_t CL_CgameSystemCalls( intptr_t *args );	/* cl_cgame.c's */
#endif
