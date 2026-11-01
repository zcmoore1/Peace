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
// cl_vm.c -- shared cgame/UI rendering boundary

#include "client.h"

// QVM pointers are 32-bit offsets, not host pointers. All fields through
// rotation have the same layout, but the trailing pose pointer may also have
// extra alignment padding in a 64-bit native refEntity_t. Marshal the prefix
// and resolve the nested pointer before handing the entity to either renderer.
// The same boundary is used by cgame and UI, including entities with no pose.
void CL_AddRefEntityFromVM( vm_t *vm, intptr_t entityAddress ) {
	const byte *source = VM_ExplicitArgPtr(vm, entityAddress);
	refEntity_t entity;
	int poseOffset;
	const size_t prefixSize = offsetof(refEntity_t, rotation) + sizeof(float);
	if ( !source ) return;
	if ( VM_IsNative(vm) ) {
		re.AddRefEntityToScene((const refEntity_t *)source);
		return;
	}
	Com_Memset(&entity, 0, sizeof(entity));
	Com_Memcpy(&entity, source, prefixSize);
	Com_Memcpy(&poseOffset, source + prefixSize, sizeof(poseOffset));
	entity.pose = VM_ExplicitArgPtr(vm, poseOffset);
	re.AddRefEntityToScene(&entity);
}
