// DbgVision
// Copyright (c) 2024-2025 Henry++

#pragma once

#include "routine.h"

#include "resource.h"
#include "app.h"

DEFINE_GUID (GUID_TrayIcon, 0xC472A261, 0x77AC, 0x4D01, 0xB8, 0xC7, 0x3D, 0xB1, 0xC, 0xC1, 0x8A, 0x8D);

#define LANG_MENU 7

#define SECURITY_DESCRIPTOR L"D:(A;;GRGWGX;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGWGX;;;AN)(A;;GRGWGX;;;RC)(A;;GRGWGX;;;S-1-15-2-1)S:(ML;;NW;;;LW)"

typedef struct _STATIC_DATA
{
	HANDLE hdebug_global_evt;
	HANDLE hready_global_evt;
	HANDLE hdebug_local_evt;
	HANDLE hready_local_evt;
	HANDLE hsection;

	PVOID base_address;
	PVOID sd;

	LONG64 timestamp;
} STATIC_DATA, *PSTATIC_DATA;

typedef struct _DEBUG_BUFFER
{
	ULONG ProcessId; // the id of the process.
	CHAR Buffer[PAGE_SIZE - sizeof (ULONG)]; // the buffer containing the debug message.
} DEBUG_BUFFER, *PDEBUG_BUFFER;

typedef struct _PITEM_DATA
{
	PR_STRING timestamp;
	PR_STRING message;
	PR_STRING path;
	ULONG_PTR hash_code;
	COLORREF clr;
	ULONG index;
	ULONG pid;
	LONG icon_id;
} ITEM_DATA, *PITEM_DATA;
