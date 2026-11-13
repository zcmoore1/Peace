/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// vm_ptr.c -- native pointers and bytecode address translation

#include "vm_local.h"

qboolean VM_IsNative( const vm_t *vm ) {
	return vm && vm->entryPoint != NULL;
}

void *VM_ArgPtr( intptr_t intValue ) {
	if ( !intValue ) {
		return NULL;
	}
	// currentVM is missing on reconnect
	if ( currentVM==NULL )
	  return NULL;

	if ( currentVM->entryPoint ) {
		return (void *)(currentVM->dataBase + intValue);
	}
	else {
		return (void *)(currentVM->dataBase + (intValue & currentVM->dataMask));
	}
}

void *VM_ExplicitArgPtr( vm_t *vm, intptr_t intValue ) {
	if ( !intValue ) {
		return NULL;
	}

	// currentVM is missing on reconnect here as well?
	if ( currentVM==NULL )
	  return NULL;

	//
	if ( vm->entryPoint ) {
		return (void *)(vm->dataBase + intValue);
	}
	else {
		return (void *)(vm->dataBase + (intValue & vm->dataMask));
	}
}

/*
============
VM_ExplicitArgBlock

A pointer to size bytes the VM handed the engine: VM_ExplicitArgPtr's pointer,
but for a bytecode VM the whole block must also lie inside the VM's data
(dataMask + 1 bytes), or the call drops with ERR_DROP. VM_ExplicitArgPtr masks
only the start, so a block near the end would run on past the VM's memory into
the engine's. A native module's pointers are its own and are not checked; NULL
stays NULL.
============
*/
void *VM_ExplicitArgBlock( vm_t *vm, intptr_t intValue, size_t size, const char *what ) {
	size_t offset, length;

	if ( !VM_ExplicitArgPtr( vm, intValue ) ) {
		return NULL;
	}
	if ( vm->entryPoint ) {
		return (void *)(vm->dataBase + intValue);
	}
	offset = (size_t)( intValue & vm->dataMask );
	length = (size_t)vm->dataMask + 1;
	if ( size > length || offset > length - size ) {
		Com_Error( ERR_DROP, "VM %s: %s (%u bytes at 0x%x) runs past the VM's memory",
			vm->name, what, (unsigned)size, (unsigned)offset );
	}
	return (void *)(vm->dataBase + offset);
}
