// DbgVision
// Copyright (c) 2024 Henry++

#include "routine.h"

#include "main.h"
#include "rapp.h"

#include "resource.h"

STATIC_DATA config = {0};
PR_HASHTABLE exclude_table = NULL;

VOID NTAPI _app_clean_object (
	_In_ PVOID object_body
)
{
	PITEM_DATA ptr_item;

	ptr_item = object_body;

	if (ptr_item->timestamp)
		_r_obj_dereference (ptr_item->timestamp);

	if (ptr_item->message)
		_r_obj_dereference (ptr_item->message);

	if (ptr_item->path)
		_r_obj_dereference (ptr_item->path);
}

VOID _app_resizecolumns (
	_In_ HWND hwnd
)
{
	HWND hlistview;
	LONG total_width;
	LONG dpi_value;
	LONG width;

	hlistview = GetDlgItem (hwnd, IDC_LISTVIEW);

	if (!hlistview)
		return;

	dpi_value = _r_dc_getwindowdpi (hwnd);

	total_width = _r_ctrl_getwidth (hlistview, 0);

	width = _r_dc_getdpi (50, dpi_value);

	total_width -= width;

	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 0, NULL, width);

	width = _r_dc_getdpi (130, dpi_value);

	total_width -= width * 2;

	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 1, NULL, width);
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 2, NULL, width);

	width = _r_dc_getdpi (75, dpi_value);

	total_width -= width;

	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 3, NULL, width);
	_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 4, NULL, total_width);
}

VOID _app_refreshstatus (
	_In_ HWND hwnd,
	_In_ BOOLEAN is_create
)
{
	HICON hicon;
	LONG icon_small;
	LONG dpi_value;
	LONG count;

	dpi_value = _r_dc_gettaskbardpi ();

	count = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);

	icon_small = _r_dc_getsystemmetrics (SM_CXSMICON, dpi_value);
	hicon = _r_sys_loadsharedicon (_r_sys_getimagebase (), MAKEINTRESOURCEW (count ? IDI_MAIN : IDI_INACTIVE), icon_small);

	if (is_create)
	{
		_r_tray_create (hwnd, &GUID_TrayIcon, RM_TRAYICON, hicon, _r_app_getname (), FALSE);
	}
	else
	{
		_r_tray_setinfo (hwnd, &GUID_TrayIcon, hicon, _r_app_getname ());
	}

	_r_status_settextformat (hwnd, IDC_STATUSBAR, 0, L"%s: %d", _r_locale_getstring (IDS_STATUS), _r_listview_getitemcount (hwnd, IDC_LISTVIEW));
}

BOOLEAN _app_popupallowed ()
{
	if (!config.timestamp)
		return TRUE;

	return (_r_unixtime_now () - config.timestamp) >= 60;
}

VOID _app_additem (
	_In_ HWND hwnd,
	_In_ ULONG pid,
	_In_ PR_STRING path,
	_In_ PR_STRINGREF message,
	_In_ ULONG_PTR hash_code
)
{
	PITEM_DATA ptr_item;
	INT count;

	count = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);

	ptr_item = _r_obj_allocate (sizeof (ITEM_DATA), &_app_clean_object);

	ptr_item->timestamp = _r_format_unixtime (_r_unixtime_now (), FDTF_SHORTDATE | FDTF_LONGTIME);

	ptr_item->hash_code = hash_code;
	ptr_item->clr = (COLORREF)hash_code;

	ptr_item->path = path;
	ptr_item->message = _r_obj_createstring2 (message);

	ptr_item->index = count + 1;
	ptr_item->pid = pid;

	_r_path_geticon (path->buffer, &ptr_item->icon_id, NULL);

	_r_listview_additem_ex (hwnd, IDC_LISTVIEW, count, LPSTR_TEXTCALLBACK, I_IMAGECALLBACK, I_GROUPIDNONE, (LPARAM)ptr_item);
}

NTSTATUS NTAPI _app_readerthread (
	_In_ PVOID arglist
)
{
	PDEBUG_BUFFER debugger;
	HANDLE handle[2] = {0};
	R_STRINGREF remaining_part;
	R_STRINGREF first_part;
	PR_STRING message;
	PR_STRING path;
	R_BYTEREF byte;
	HWND hwnd;
	ULONG_PTR hash_code;
	NTSTATUS status;

	hwnd = arglist;

	handle[0] = config.hready_global_evt;
	handle[1] = config.hready_local_evt;

	while (TRUE)
	{
		NtSetEvent (config.hdebug_global_evt, NULL); // do not touch
		NtSetEvent (config.hdebug_local_evt, NULL); // do not touch

		// wait for data ready
		status = _r_sys_waitformultipleobjects (2, handle, INFINITE, FALSE);

		if (!NT_SUCCESS (status))
			break;

		if (status == STATUS_TIMEOUT)
			continue;

		if (status == STATUS_WAIT_0 || status == STATUS_WAIT_1)
		{
			debugger = config.base_address;

			if (!debugger->ProcessId)
				continue;

			status = _r_sys_getprocessimagepathbyid (ULongToHandle (debugger->ProcessId), TRUE, &path);

			if (NT_SUCCESS (status))
			{
				hash_code = _r_str_gethash (_r_path_getbasename (&path->sr), TRUE);

				if (_r_obj_findhashtable (exclude_table, hash_code))
					continue;
			}
			else
			{
				continue;
			}

			_r_obj_initializebyteref (&byte, debugger->Buffer);

			status = _r_str_multibyte2unicode (&byte, &message);

			if (NT_SUCCESS (status))
			{
				_r_obj_initializestringref2 (&remaining_part, &message->sr);

				while (remaining_part.length != 0)
				{
					_r_str_splitatchar (&remaining_part, L'\n', &first_part, &remaining_part);

					if (first_part.length != 0)
						_app_additem (hwnd, debugger->ProcessId, path, &first_part, hash_code);
				}

				_app_resizecolumns (hwnd);
				_app_refreshstatus (hwnd, FALSE);

				if (_r_config_getboolean (L"IsShowTrayPopup", TRUE))
				{
					if (!_r_wnd_isvisible (hwnd, TRUE) && _app_popupallowed ())
					{
						_r_tray_popup (hwnd, &GUID_TrayIcon, NIIF_INFO, _r_app_getname (), _r_locale_getstring (IDS_STATUS_NEWMESSAGE));

						_r_config_setlong64 (L"PopupTimestamp", _r_unixtime_now ());

						config.timestamp = _r_config_getlong64 (L"PopupTimestamp", 0);
					}
				}

				_r_obj_dereference (message);
			}
		}
	}

	return STATUS_SUCCESS;
}

VOID _app_displayinfoapp_callback (
	_In_ PITEM_DATA ptr_item,
	_Inout_ LPNMLVDISPINFOW lpnmlv
)
{
	// set text
	if (lpnmlv->item.mask & LVIF_TEXT)
	{
		switch (lpnmlv->item.iSubItem)
		{
			case 0:
			{
				_r_str_printf (lpnmlv->item.pszText, lpnmlv->item.cchTextMax, L"%d", ptr_item->index);
				break;
			}

			case 1:
			{
				_r_str_copy (lpnmlv->item.pszText, lpnmlv->item.cchTextMax, ptr_item->timestamp->buffer);
				break;
			}

			case 2:
			{
				_r_str_copy (lpnmlv->item.pszText, lpnmlv->item.cchTextMax, _r_path_getbasename (&ptr_item->path->sr));
				break;
			}

			case 3:
			{
				_r_str_printf (lpnmlv->item.pszText, lpnmlv->item.cchTextMax, L"%d", ptr_item->pid);
				break;
			}

			case 4:
			{

				_r_str_copy (lpnmlv->item.pszText, lpnmlv->item.cchTextMax, ptr_item->message->buffer);
				break;
			}
		}
	}

	// set image
	if (lpnmlv->item.mask & LVIF_IMAGE)
	{
		lpnmlv->item.iImage = ptr_item->icon_id;
	}
}

INT CALLBACK _app_listviewcompare_callback (
	_In_ LPARAM lparam1,
	_In_ LPARAM lparam2,
	_In_ LPARAM lparam
)
{
	WCHAR config_name[128];
	PR_STRING item_text_1;
	PR_STRING item_text_2;
	HWND hlistview;
	HWND hwnd;
	INT listview_id;
	INT column_id;
	INT result = 0;
	INT item1;
	INT item2;
	BOOLEAN is_descend;

	item1 = (INT)(INT_PTR)lparam1;
	item2 = (INT)(INT_PTR)lparam2;

	if (item1 == -1 || item2 == -1)
		return 0;

	hlistview = (HWND)lparam;

	hwnd = GetParent (hlistview);
	listview_id = GetDlgCtrlID (hlistview);

	_r_str_printf (config_name, RTL_NUMBER_OF (config_name), L"listview\\%04" TEXT (PRIX32), listview_id);

	column_id = _r_config_getlong_ex (L"SortColumn", 0, config_name);

	item_text_1 = _r_listview_getitemtext (hwnd, listview_id, item1, column_id);
	item_text_2 = _r_listview_getitemtext (hwnd, listview_id, item2, column_id);

	is_descend = _r_config_getboolean_ex (L"SortIsDescending", FALSE, config_name);

	if (item_text_1 && item_text_2)
	{
		if (!result)
			result = _r_str_compare_logical (item_text_1->buffer, item_text_2->buffer);
	}

	if (item_text_1)
		_r_obj_dereference (item_text_1);

	if (item_text_2)
		_r_obj_dereference (item_text_2);

	return is_descend ? -result : result;
}

VOID _app_listviewsort (
	_In_ HWND hwnd,
	_In_ INT listview_id,
	_In_ INT column_id,
	_In_ BOOLEAN is_notifycode
)
{
	WCHAR config_name[128];
	INT column_count;
	BOOLEAN is_descend;

	column_count = _r_listview_getcolumncount (hwnd, listview_id);

	if (!column_count)
		return;

	_r_str_printf (config_name, RTL_NUMBER_OF (config_name), L"listview\\%04" TEXT (PRIX32), listview_id);

	is_descend = _r_config_getboolean_ex (L"SortIsDescending", FALSE, config_name);

	if (is_notifycode)
		is_descend = !is_descend;

	if (column_id == -1)
		column_id = _r_config_getlong_ex (L"SortColumn", 0, config_name);

	column_id = _r_calc_clamp (column_id, 0, column_count - 1); // set range

	if (is_notifycode)
	{
		_r_config_setboolean_ex (L"SortIsDescending", is_descend, config_name);
		_r_config_setlong_ex (L"SortColumn", column_id, config_name);
	}

	for (INT i = 0; i < column_count; i++)
		_r_listview_setcolumnsortindex (hwnd, listview_id, i, 0);

	_r_listview_setcolumnsortindex (hwnd, listview_id, column_id, is_descend ? -1 : 1);

	_r_wnd_sendmessage (hwnd, listview_id, LVM_SORTITEMSEX, (WPARAM)GetDlgItem (hwnd, listview_id), (LPARAM)&_app_listviewcompare_callback);
}

INT_PTR CALLBACK SettingsProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	switch (msg)
	{
		case RM_INITIALIZE:
		{
			INT dialog_id;

			dialog_id = (INT)wparam;

			switch (dialog_id)
			{
				case IDD_SETTINGS_EXCLUDE:
				{
					R_STRINGREF remaining_part;
					R_STRINGREF value_part;
					PR_STRING string;
					PR_STRING string2;
					INT item_count;

					_r_listview_addcolumn (hwnd, IDC_EXCLUDE, 0, L"", -100, LVCFMT_LEFT);

					string = _r_config_getstring (L"Exclude", NULL);

					if (string)
					{
						_r_obj_initializestringref2 (&remaining_part, &string->sr);

						while (remaining_part.length != 0)
						{
							if (!_r_str_splitatchar (&remaining_part, L';', &value_part, &remaining_part))
								break;

							string2 = _r_obj_createstring2 (&value_part);

							item_count = _r_listview_getitemcount (hwnd, IDC_EXCLUDE);

							_r_listview_additem (hwnd, IDC_EXCLUDE, item_count, string2->buffer);

							_r_obj_dereference (string2);
						}

						_r_obj_dereference (string);
					}

					break;
				}
			}

			break;
		}

		case RM_LOCALIZE:
		{
			INT dialog_id;

			dialog_id = (INT)wparam;

			switch (dialog_id)
			{
				case IDD_SETTINGS_EXCLUDE:
				{
					_r_listview_setcolumn (hwnd, IDC_EXCLUDE, 0, _r_locale_getstring (IDS_EXCLUDE), -100);
					break;
				}
			}

			break;
		}

		case RM_CONFIG_SAVE:
		{
			INT dialog_id;

			dialog_id = (INT)wparam;

			switch (dialog_id)
			{
				case IDD_SETTINGS_EXCLUDE:
				{
					R_STRINGBUILDER sb;
					R_STRINGREF remaining_part;
					R_STRINGREF value_part;
					PR_STRING string;
					HWND hmain;
					INT item_count;

					item_count = _r_listview_getitemcount (hwnd, IDC_EXCLUDE);

					_r_obj_initializestringbuilder (&sb, 256);

					for (INT i = 0; i < item_count; i++)
					{
						string = _r_listview_getitemtext (hwnd, IDC_EXCLUDE, i, 0);

						if (!string)
							continue;

						_r_obj_appendstringbuilder2 (&sb, &string->sr);
						_r_obj_appendstringbuilder (&sb, L";");

						_r_obj_dereference (string);
					}

					string = _r_obj_finalstringbuilder (&sb);

					_r_config_setstring (L"Exclude", _r_obj_getstring (string));

					_r_obj_clearhashtable (exclude_table);

					if (string)
					{
						_r_obj_initializestringref2 (&remaining_part, &string->sr);

						while (remaining_part.length != 0)
						{
							if (_r_str_splitatchar (&remaining_part, L';', &value_part, &remaining_part))
								_r_obj_addhashtableitem (exclude_table, _r_str_gethash2 (&value_part, TRUE), NULL);
						}
					}

					_r_obj_deletestringbuilder (&sb);

					hmain = _r_app_gethwnd ();

					if (!hmain)
						break;

					_app_refreshstatus (hmain, FALSE);

					break;
				}
			}

			break;
		}

		case WM_CONTEXTMENU:
		{
			HMENU hmenu;
			HMENU hsubmenu;

			if (GetDlgCtrlID ((HWND)wparam) != IDC_EXCLUDE)
				break;

			// localize
			hmenu = LoadMenuW (NULL, MAKEINTRESOURCEW (IDM_CONFIG));

			if (!hmenu)
				break;

			hsubmenu = GetSubMenu (hmenu, 0);

			if (hsubmenu)
			{
				_r_menu_setitemtext (hsubmenu, IDM_ADD, FALSE, _r_locale_getstring (IDS_ADD));
				_r_menu_setitemtext (hsubmenu, IDM_DELETE, FALSE, _r_locale_getstring (IDS_DELETE));

				if (!_r_listview_getselectedcount (hwnd, IDC_EXCLUDE))
					_r_menu_enableitem (hsubmenu, IDM_DELETE, MF_BYCOMMAND, FALSE);

				_r_menu_popup (hsubmenu, hwnd, NULL, TRUE);
			}

			DestroyMenu (hmenu);

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR lphdr = (LPNMHDR)lparam;

			switch (lphdr->code)
			{
				case LVN_COLUMNCLICK:
				{
					LPNMLISTVIEW lpnmlv;
					INT ctrl_id;

					lpnmlv = (LPNMLISTVIEW)lparam;
					ctrl_id = (INT)(INT_PTR)lpnmlv->hdr.idFrom;

					if (ctrl_id != IDC_EXCLUDE)
						break;

					_app_listviewsort (hwnd, ctrl_id, lpnmlv->iSubItem, TRUE);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);

			switch (ctrl_id)
			{
				case IDM_ADD:
				{
					static COMDLG_FILTERSPEC filters[] = {
						L"Exe files (*.exe)", L"*.exe",
						L"All files (*.*)", L"*.*",
					};

					R_FILE_DIALOG file_dialog;
					PR_STRING path;
					INT item_count;
					HRESULT status;

					status = _r_filedialog_initialize (&file_dialog, PR_FILEDIALOG_OPENFILE);

					if (SUCCEEDED (status))
					{
						_r_filedialog_setfilter (&file_dialog, filters, RTL_NUMBER_OF (filters));

						status = _r_filedialog_show (hwnd, &file_dialog);

						if (SUCCEEDED (status))
						{
							status = _r_filedialog_getpath (&file_dialog, &path);

							if (SUCCEEDED (status))
							{
								item_count = _r_listview_getitemcount (hwnd, IDC_EXCLUDE);

								_r_listview_additem_ex (hwnd, IDC_EXCLUDE, item_count, _r_path_getbasename (&path->sr), I_IMAGENONE, I_GROUPIDNONE, (LPARAM)path);
							}
						}

						_r_filedialog_destroy (&file_dialog);
					}

					break;
				}

				case IDM_DELETE:
				{
					INT item_count;

					if (!_r_listview_getselectedcount (hwnd, IDC_EXCLUDE))
						break;

					if (!_r_show_confirmmessage (hwnd, NULL, _r_locale_getstring (IDS_QUESTION_DELETE), NULL))
						break;

					item_count = _r_listview_getitemcount (hwnd, IDC_EXCLUDE);

					for (INT i = item_count - 1; i != -1; i--)
					{
						if (!_r_listview_isitemselected (hwnd, IDC_EXCLUDE, i))
							continue;

						_r_listview_deleteitem (hwnd, IDC_EXCLUDE, i);
					}

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

NTSTATUS _app_createevent (
	_In_ HWND hwnd
)
{
	OBJECT_ATTRIBUTES oa = {0};
	WCHAR buffer[128];
	UNICODE_STRING us;
	NTSTATUS status;

	// event #1
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"\\Sessions\\%u\\BaseNamedObjects\\Local\\DBWIN_BUFFER_READY", NtCurrentPeb ()->SessionId);

	_r_obj_initializeunicodestring (&us, buffer);

	InitializeObjectAttributes (&oa, &us, OBJ_INHERIT | OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, NULL);

	status = NtCreateEvent (&config.hdebug_local_evt, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, TRUE);

	if (status == STATUS_OBJECT_NAME_EXISTS)
	{
		_r_show_errormessage (hwnd, NULL, status, _r_locale_getstring (IDS_ERROR_DUPLICATE), ET_NONE);

		RtlExitUserProcess (status);

		return status;
	}

	if (status != STATUS_SUCCESS)
	{
		_r_show_errormessage (hwnd, NULL, status, L"NtCreateEvent (Local\\DBWIN_BUFFER_READY)", ET_NATIVE);

		return status;
	}

	// event #2
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"\\Sessions\\%u\\BaseNamedObjects\\Local\\DBWIN_DATA_READY", NtCurrentPeb ()->SessionId);

	_r_obj_initializeunicodestring (&us, buffer);

	InitializeObjectAttributes (&oa, &us, OBJ_INHERIT | OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, NULL);

	status = NtCreateEvent (&config.hready_local_evt, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, TRUE);

	if (status != STATUS_SUCCESS)
	{
		_r_show_errormessage (hwnd, NULL, status, L"NtCreateEvent (Local\\DBWIN_DATA_READY)", ET_NATIVE);

		return status;
	}

	// event #3
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"\\Sessions\\%u\\BaseNamedObjects\\Global\\DBWIN_BUFFER_READY", NtCurrentPeb ()->SessionId);

	_r_obj_initializeunicodestring (&us, buffer);

	InitializeObjectAttributes (&oa, &us, OBJ_INHERIT | OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, NULL);

	status = NtCreateEvent (&config.hdebug_global_evt, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, TRUE);

	if (status != STATUS_SUCCESS)
	{
		_r_show_errormessage (hwnd, NULL, status, L"NtCreateEvent (Global\\DBWIN_BUFFER_READY)", ET_NATIVE);

		return status;
	}

	// event #4
	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"\\Sessions\\%u\\BaseNamedObjects\\Global\\DBWIN_DATA_READY", NtCurrentPeb ()->SessionId);

	_r_obj_initializeunicodestring (&us, buffer);

	InitializeObjectAttributes (&oa, &us, OBJ_INHERIT | OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, NULL);

	status = NtCreateEvent (&config.hready_global_evt, EVENT_ALL_ACCESS, &oa, SynchronizationEvent, TRUE);

	if (status != STATUS_SUCCESS)
		_r_show_errormessage (hwnd, NULL, status, L"NtCreateEvent (Global\\DBWIN_DATA_READY)", ET_NATIVE);

	return status;
}

VOID _app_initialize (
	_In_ HWND hwnd
)
{
	ULONG privileges[] = {
		SE_DEBUG_PRIVILEGE,
		SE_CREATE_GLOBAL_PRIVILEGE,
	};

	OBJECT_ATTRIBUTES oa = {0};
	LARGE_INTEGER offset = {0};
	LARGE_INTEGER li = {0};
	UNICODE_STRING us;
	WCHAR buffer[128];
	ULONG_PTR base_size = sizeof (DEBUG_BUFFER);
	NTSTATUS status;

	status = _r_sys_setprocessprivilege (NtCurrentProcess (), privileges, RTL_NUMBER_OF (privileges), TRUE);

	if (!NT_SUCCESS (status))
		_r_show_errormessage (hwnd, NULL, status, L"_r_sys_setprocessprivilege", ET_NATIVE);

	status = _app_createevent (hwnd);

	if (status != STATUS_SUCCESS)
		return;

	_r_str_printf (buffer, RTL_NUMBER_OF (buffer), L"\\Sessions\\%u\\BaseNamedObjects\\DBWIN_BUFFER", NtCurrentPeb ()->SessionId);

	_r_obj_initializeunicodestring (&us, buffer);

	InitializeObjectAttributes (&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, NULL);

	li.QuadPart = PAGE_SIZE;

	status = NtCreateSection (&config.hsection, STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE, &oa, &li, PAGE_READWRITE, SEC_COMMIT, NULL);

	if (NT_SUCCESS (status))
	{
		SetSecurityInfo (config.hsection, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
		SetSecurityInfo (config.hdebug_global_evt, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
		SetSecurityInfo (config.hready_global_evt, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
		SetSecurityInfo (config.hdebug_local_evt, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);
		SetSecurityInfo (config.hready_local_evt, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL);

		status = NtMapViewOfSection (config.hsection, NtCurrentProcess (), &config.base_address, 0, 0, &offset, &base_size, ViewShare, 0, PAGE_READONLY);

		if (NT_SUCCESS (status))
		{
			NtSetEvent (config.hdebug_global_evt, NULL);
			NtSetEvent (config.hdebug_local_evt, NULL);

			_r_sys_createthread (NULL, NtCurrentProcess (), &_app_readerthread, hwnd, NULL, L"Monitor");
		}
		else
		{
			_r_show_errormessage (hwnd, NULL, status, L"NtMapViewOfSection", ET_NATIVE);
		}
	}
	else
	{
		_r_show_errormessage (hwnd, NULL, status, L"NtCreateSection", ET_NATIVE);
	}
}

VOID _app_findanddeleteitem (
	_In_ HWND hwnd,
	_In_ ULONG_PTR hash_code
)
{
	PITEM_DATA ptr_item;
	INT item_count;

	item_count = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);

	for (INT i = item_count - 1; i != -1; i--)
	{
		ptr_item = (PITEM_DATA)_r_listview_getitemlparam (hwnd, IDC_LISTVIEW, i);

		if (!ptr_item)
			continue;

		if (ptr_item->hash_code == hash_code)
			_r_listview_deleteitem (hwnd, IDC_LISTVIEW, i);
	}
}

INT_PTR CALLBACK DlgProc (
	_In_ HWND hwnd,
	_In_ UINT msg,
	_In_ WPARAM wparam,
	_In_ LPARAM lparam
)
{
	static R_LAYOUT_MANAGER layout_manager = {0};

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			R_STRINGREF remaining_part;
			R_STRINGREF value_part;
			HIMAGELIST himg = NULL;
			PR_STRING string;
			HRESULT status;

			_r_layout_initializemanager (&layout_manager, hwnd);

			_r_app_sethwnd (hwnd); // HACK!!!

			status = SHGetImageList (SHIL_SMALL, &IID_IImageList2, &himg);

			if (SUCCEEDED (status))
				_r_listview_setimagelist (hwnd, IDC_LISTVIEW, himg);

			exclude_table = _r_obj_createhashtable (sizeof (BOOLEAN), NULL);

			config.timestamp = _r_config_getlong64 (L"PopupTimestamp", 0);

			string = _r_config_getstring (L"Exclude", NULL);

			if (string)
			{
				_r_obj_initializestringref2 (&remaining_part, &string->sr);

				while (remaining_part.length != 0)
				{
					if (_r_str_splitatchar (&remaining_part, L';', &value_part, &remaining_part))
						_r_obj_addhashtableitem (exclude_table, _r_str_gethash2 (&value_part, TRUE), NULL);
				}

				_r_obj_dereference (string);
			}

			_app_initialize (hwnd);

			// configure listview
			_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP, FALSE);

			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 1, NULL, 10, LVCFMT_LEFT);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 2, NULL, 10, LVCFMT_LEFT);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 3, NULL, 10, LVCFMT_LEFT);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 4, NULL, 10, LVCFMT_LEFT);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, 5, NULL, 10, LVCFMT_LEFT);

			// settings
			_r_settings_addpage (IDD_SETTINGS_EXCLUDE, IDS_SETTINGS_EXCLUDE);

			_app_resizecolumns (hwnd);

			_app_refreshstatus (hwnd, FALSE);

			break;
		}

		case WM_DESTROY:
		{
			if (config.base_address)
				NtUnmapViewOfSection (NtCurrentProcess (), config.base_address);

			_r_tray_destroy (hwnd, &GUID_TrayIcon);

			PostQuitMessage (0);

			break;
		}

		case RM_INITIALIZE:
		{
			HMENU hmenu;

			_app_refreshstatus (hwnd, TRUE);

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_checkitem (hmenu, IDM_ALWAYSONTOP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"AlwaysOnTop", FALSE));
				_r_menu_checkitem (hmenu, IDM_STARTMINIMIZED_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"IsStartMinimized", FALSE));
				_r_menu_checkitem (hmenu, IDM_USEDARKTHEME_CHK, 0, MF_BYCOMMAND, _r_theme_isenabled ());
				_r_menu_checkitem (hmenu, IDM_SHOWTRAYPOPUP_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"IsShowTrayPopup", TRUE));
				_r_menu_checkitem (hmenu, IDM_ENABLEHIGHLIGHTING_CHK, 0, MF_BYCOMMAND, _r_config_getboolean (L"IsEnableHighlighting", TRUE));
				_r_menu_checkitem (hmenu, IDM_CHECKUPDATES_CHK, 0, MF_BYCOMMAND, _r_update_isenabled (FALSE));
			}

			break;
		}

		case RM_TASKBARCREATED:
		{
			_app_refreshstatus (hwnd, TRUE);
			break;
		}

		case RM_LOCALIZE:
		{
			// localize menu
			HMENU hmenu;

			hmenu = GetMenu (hwnd);

			if (hmenu)
			{
				_r_menu_setitemtext (hmenu, 0, TRUE, _r_locale_getstring (IDS_FILE));
				_r_menu_setitemtext (hmenu, 1, TRUE, _r_locale_getstring (IDS_VIEW));
				_r_menu_setitemtext (hmenu, 2, TRUE, _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, 3, TRUE, _r_locale_getstring (IDS_HELP));

				_r_menu_setitemtextformat (hmenu, IDM_SETTINGS, FALSE, L"%s...\tF2", _r_locale_getstring (IDS_SETTINGS));
				_r_menu_setitemtext (hmenu, IDM_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));
				_r_menu_setitemtextformat (hmenu, IDM_CLEAR, FALSE, L"%s...\tCtrl+X", _r_locale_getstring (IDS_CLEAR));
				_r_menu_setitemtext (hmenu, IDM_ALWAYSONTOP_CHK, FALSE, _r_locale_getstring (IDS_ALWAYSONTOP_CHK));
				_r_menu_setitemtext (hmenu, IDM_USEDARKTHEME_CHK, FALSE, _r_locale_getstring (IDS_USEDARKTHEME_CHK));
				_r_menu_setitemtext (hmenu, IDM_SHOWTRAYPOPUP_CHK, FALSE, _r_locale_getstring (IDS_SHOWTRAYPOPUP_CHK));
				_r_menu_setitemtext (hmenu, IDM_ENABLEHIGHLIGHTING_CHK, FALSE, _r_locale_getstring (IDS_ENABLEHIGHLIGHTING_CHK));
				_r_menu_setitemtext (hmenu, IDM_STARTMINIMIZED_CHK, FALSE, _r_locale_getstring (IDS_STARTMINIMIZED_CHK));
				_r_menu_setitemtext (hmenu, IDM_CHECKUPDATES_CHK, FALSE, _r_locale_getstring (IDS_CHECKUPDATES_CHK));
				_r_menu_setitemtextformat (GetSubMenu (hmenu, 2), LANG_MENU, TRUE, L"%s (Language)", _r_locale_getstring (IDS_LANGUAGE));
				_r_menu_setitemtext (hmenu, IDM_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
				_r_menu_setitemtext (hmenu, IDM_CHECKUPDATES, FALSE, _r_locale_getstring (IDS_CHECKUPDATES));
				_r_menu_setitemtextformat (hmenu, IDM_ABOUT, FALSE, L"%s\tF1", _r_locale_getstring (IDS_ABOUT));
			}

			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 0, _r_locale_getstring (IDS_INDEX), 0);
			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 1, _r_locale_getstring (IDS_TIME), 0);
			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 2, _r_locale_getstring (IDS_PROCESS), 0);
			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 3, _r_locale_getstring (IDS_PID), 0);
			_r_listview_setcolumn (hwnd, IDC_LISTVIEW, 4, _r_locale_getstring (IDS_MESSAGE), 0);

			_app_refreshstatus (hwnd, FALSE);

			// enum localizations
			if (hmenu)
				_r_locale_enum (GetSubMenu (hmenu, 2), LANG_MENU, IDX_LANGUAGE);

			break;
		}

		case WM_SIZE:
		{
			if (!_r_layout_resize (&layout_manager, wparam))
				break;

			// resize columns
			_app_resizecolumns (hwnd);

			break;
		}

		case WM_GETMINMAXINFO:
		{
			_r_layout_resizeminimumsize (&layout_manager, lparam);
			break;
		}

		case WM_DPICHANGED:
		{
			_app_resizecolumns (hwnd);
			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp;

			nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_RCLICK:
				{
					LPNMITEMACTIVATE lpnmlv;
					HMENU hmenu;
					HMENU hsubmenu;
					INT command_id;

					lpnmlv = (LPNMITEMACTIVATE)lparam;

					if (lpnmlv->hdr.idFrom != IDC_LISTVIEW || lpnmlv->iItem == -1)
						break;

					// localize
					hmenu = LoadMenuW (NULL, MAKEINTRESOURCEW (IDM_LISTVIEW));

					if (!hmenu)
						break;

					hsubmenu = GetSubMenu (hmenu, 0);

					if (hsubmenu)
					{
						_r_menu_setitemtextformat (hsubmenu, IDM_EXPLORE, FALSE, L"%s\tCtrl+E", _r_locale_getstring (IDS_EXPLORE));
						_r_menu_setitemtext (hsubmenu, IDM_EXCLUDE, FALSE, _r_locale_getstring (IDS_EXCLUDE));
						_r_menu_setitemtextformat (hsubmenu, IDM_CLEAR, FALSE, L"%s\tCtrl+X", _r_locale_getstring (IDS_CLEAR));
						_r_menu_setitemtextformat (hsubmenu, IDM_COPY, FALSE, L"%s\tCtrl+C", _r_locale_getstring (IDS_COPY));
						_r_menu_setitemtext (hsubmenu, IDM_COPY_VALUE, FALSE, _r_locale_getstring (IDS_COPY_VALUE));

						command_id = _r_menu_popup (hsubmenu, hwnd, NULL, FALSE);

						if (command_id)
							_r_wnd_sendmessage (hwnd, 0, WM_COMMAND, MAKEWPARAM (command_id, 0), (LPARAM)lpnmlv->iSubItem);
					}

					DestroyMenu (hmenu);

					break;
				}

				case NM_CUSTOMDRAW:
				{
					LPNMLVCUSTOMDRAW lpnmlv;
					LONG_PTR result = CDRF_DODEFAULT;

					if (nmlp->idFrom != IDC_LISTVIEW)
						break;

					lpnmlv = (LPNMLVCUSTOMDRAW)lparam;

					switch (lpnmlv->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						{
							result = CDRF_NOTIFYITEMDRAW;
							break;
						}

						case CDDS_ITEMPREPAINT:
						{
							PITEM_DATA ptr_item;
							COLORREF new_clr;

							if (lpnmlv->dwItemType != LVCDI_ITEM)
								break;

							if (!_r_config_getboolean (L"IsEnableHighlighting", TRUE))
								break;

							ptr_item = (PITEM_DATA)lpnmlv->nmcd.lItemlParam;

							if (!ptr_item)
								break;

							new_clr = ptr_item->clr;

							lpnmlv->clrTextBk = new_clr;
							lpnmlv->clrText = _r_theme_isenabled () ? WND_TEXT_CLR : _r_dc_getcolorbrightness (new_clr);

							result = CDRF_NEWFONT;

							break;
						}
					}

					SetWindowLongPtrW (hwnd, DWLP_MSGRESULT, result);

					return result;
				}

				case NM_DBLCLK:
				{
					LPNMITEMACTIVATE lpnmlv;

					if (nmlp->idFrom != IDC_LISTVIEW)
						break;

					lpnmlv = (LPNMITEMACTIVATE)lparam;

					if (lpnmlv->iItem == -1)
						break;

					_r_wnd_sendmessage (hwnd, 0, WM_COMMAND, MAKEWPARAM (IDM_EXPLORE, 0), 0);

					break;
				}

				case LVN_COLUMNCLICK:
				{
					LPNMLISTVIEW lpnmlv;
					INT ctrl_id;

					lpnmlv = (LPNMLISTVIEW)lparam;
					ctrl_id = (INT)(INT_PTR)lpnmlv->hdr.idFrom;

					if (ctrl_id != IDC_LISTVIEW)
						break;

					_app_listviewsort (hwnd, ctrl_id, lpnmlv->iSubItem, TRUE);

					break;
				}

				case LVN_DELETEITEM:
				{
					LPNMLISTVIEW lpnmlv;

					lpnmlv = (LPNMLISTVIEW)lparam;

					if (lpnmlv->lParam)
						_r_obj_dereference ((PVOID)lpnmlv->lParam);

					_app_resizecolumns (hwnd);
					_app_refreshstatus (hwnd, FALSE);

					break;
				}

				case LVN_GETDISPINFO:
				{
					LPNMLVDISPINFOW lpnmlv;
					INT listview_id;

					lpnmlv = (LPNMLVDISPINFOW)lparam;
					listview_id = (INT)(INT_PTR)lpnmlv->hdr.idFrom;

					if (!lpnmlv->item.lParam)
						break;

					_app_displayinfoapp_callback ((PITEM_DATA)lpnmlv->item.lParam, lpnmlv);

					break;
				}
			}

			break;
		}

		case RM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case NIN_KEYSELECT:
				{
					if (GetForegroundWindow () != hwnd)
						_r_wnd_toggle (hwnd, TRUE);

					break;
				}

				case NIN_BALLOONUSERCLICK:
				{
					_r_wnd_toggle (hwnd, TRUE);
					break;
				}

				case WM_LBUTTONUP:
				case WM_MBUTTONUP:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case WM_CONTEXTMENU:
				{
					HMENU hsubmenu;
					HMENU hmenu;

					SetForegroundWindow (hwnd); // don't touch

					hmenu = LoadMenuW (NULL, MAKEINTRESOURCEW (IDM_TRAY));

					if (!hmenu)
						break;

					hsubmenu = GetSubMenu (hmenu, 0);

					if (!hsubmenu)
						break;

					// localize
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_SHOW, FALSE, _r_locale_getstring (IDS_TRAY_SHOW));
					_r_menu_setitemtextformat (hsubmenu, IDM_TRAY_SETTINGS, FALSE, L"%s...", _r_locale_getstring (IDS_SETTINGS));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_WEBSITE, FALSE, _r_locale_getstring (IDS_WEBSITE));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_ABOUT, FALSE, _r_locale_getstring (IDS_ABOUT));
					_r_menu_setitemtext (hsubmenu, IDM_TRAY_EXIT, FALSE, _r_locale_getstring (IDS_EXIT));

					_r_menu_popup (hsubmenu, hwnd, NULL, TRUE);

					DestroyMenu (hmenu);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);
			INT notify_code = HIWORD (wparam);

			if (notify_code == 0 && ctrl_id >= IDX_LANGUAGE && ctrl_id <= IDX_LANGUAGE + (INT)(INT_PTR)_r_locale_getcount () + 1)
			{
				HMENU hmenu;
				HMENU hsubmenu;

				hmenu = GetMenu (hwnd);

				if (hmenu)
				{
					hsubmenu = GetSubMenu (GetSubMenu (hmenu, 2), LANG_MENU);

					if (hsubmenu)
						_r_locale_apply (hsubmenu, ctrl_id, IDX_LANGUAGE);
				}

				return FALSE;
			}

			switch (ctrl_id)
			{
				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDM_TRAY_EXIT:
				case IDM_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDM_CLEAR:
				{
					if (!_r_show_confirmmessage (hwnd, NULL, _r_locale_getstring (IDS_QUESTION_CLEAN), L"IsWantConfirm"))
						break;

					_r_listview_deleteallitems (hwnd, IDC_LISTVIEW);

					_app_refreshstatus (hwnd, FALSE);

					break;
				}

				case IDM_ALWAYSONTOP_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"AlwaysOnTop", FALSE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"AlwaysOnTop", new_val);

					_r_wnd_top (hwnd, new_val);

					break;
				}

				case IDM_STARTMINIMIZED_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"IsStartMinimized", FALSE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"IsStartMinimized", new_val);

					break;
				}

				case IDM_USEDARKTHEME_CHK:
				{
					BOOLEAN is_enabled = !_r_theme_isenabled ();

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, is_enabled);
					_r_theme_enable (hwnd, is_enabled);

					break;
				}

				case IDM_SHOWTRAYPOPUP_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"IsShowTrayPopup", TRUE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"IsShowTrayPopup", new_val);

					break;
				}

				case IDM_ENABLEHIGHLIGHTING_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_config_getboolean (L"IsEnableHighlighting", TRUE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_config_setboolean (L"IsEnableHighlighting", new_val);

					_r_listview_redraw (hwnd, IDC_LISTVIEW);

					break;
				}

				case IDM_CHECKUPDATES_CHK:
				{
					BOOLEAN new_val;

					new_val = !_r_update_isenabled (FALSE);

					_r_menu_checkitem (GetMenu (hwnd), ctrl_id, 0, MF_BYCOMMAND, new_val);
					_r_update_enable (new_val);

					break;
				}

				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				{
					_r_settings_createwindow (hwnd, &SettingsProc, 0);
					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					_r_shell_opendefault (_r_app_getwebsite_url ());
					break;
				}

				case IDM_CHECKUPDATES:
				{
					_r_update_check (hwnd);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					_r_show_aboutmessage (hwnd);
					break;
				}

				case IDM_EXPLORE:
				{
					PITEM_DATA ptr_item;
					INT item_id = -1;

					while ((item_id = _r_listview_getnextselected (hwnd, IDC_LISTVIEW, item_id)) != -1)
					{
						ptr_item = (PITEM_DATA)_r_listview_getitemlparam (hwnd, IDC_LISTVIEW, item_id);

						if (ptr_item)
						{
							_r_shell_showfile (ptr_item->path->buffer);
						}
					}

					break;
				}

				case IDM_EXCLUDE:
				{
					PITEM_DATA ptr_item;
					R_STRINGBUILDER sb;
					PR_STRING string;
					ULONG_PTR hash_code;
					ULONG_PTR enum_key = 0;
					INT item_id = -1;

					_r_obj_initializestringbuilder (&sb, 256);

					string = _r_config_getstring (L"Exclude", NULL);

					if (string)
					{
						_r_str_trimstring2 (&string->sr, L";", 0);

						_r_obj_appendstringbuilder2 (&sb, &string->sr);
						_r_obj_appendstringbuilder (&sb, L";");

						_r_obj_dereference (string);
					}

					while ((item_id = _r_listview_getnextselected (hwnd, IDC_LISTVIEW, item_id)) != -1)
					{
						ptr_item = (PITEM_DATA)_r_listview_getitemlparam (hwnd, IDC_LISTVIEW, item_id);

						if (ptr_item)
						{
							_r_obj_appendstringbuilder (&sb, _r_path_getbasename (&ptr_item->path->sr));
							_r_obj_appendstringbuilder (&sb, L";");

							hash_code = _r_str_gethash (_r_path_getbasename (&ptr_item->path->sr), TRUE);

							_r_obj_addhashtableitem (exclude_table, hash_code, NULL);
						}
					}

					while (_r_obj_enumhashtable (exclude_table, NULL, &hash_code, &enum_key))
					{
						_app_findanddeleteitem (hwnd, hash_code);
					}

					_r_config_setstring (L"Exclude", _r_obj_finalstringbuilder (&sb)->buffer);

					_r_obj_deletestringbuilder (&sb);

					_app_refreshstatus (hwnd, FALSE);

					break;
				}

				case IDM_COPY:
				{
					R_STRINGBUILDER sb;
					PR_STRING string;
					INT column_count;
					INT item_id = -1;

					_r_obj_initializestringbuilder (&sb, 256);

					column_count = _r_listview_getcolumncount (hwnd, IDC_LISTVIEW);

					while ((item_id = _r_listview_getnextselected (hwnd, IDC_LISTVIEW, item_id)) != -1)
					{
						for (INT i = 0; i < column_count; i++)
						{
							string = _r_listview_getitemtext (hwnd, IDC_LISTVIEW, item_id, i);

							if (string)
							{
								_r_obj_appendstringbuilder2 (&sb, &string->sr);

								if ((i + 1) != column_count)
									_r_obj_appendstringbuilder (&sb, L", ");

								_r_obj_dereference (string);
							}
						}

						_r_obj_appendstringbuilder (&sb, L"\r\n");
					}

					string = _r_obj_finalstringbuilder (&sb);

					_r_str_trimstring2 (&string->sr, L"\r\n ", 0);

					_r_clipboard_set (hwnd, &string->sr);

					_r_obj_dereference (string);

					break;
				}

				case IDM_COPY_VALUE:
				{
					R_STRINGBUILDER sb;
					PR_STRING string;
					INT column_id;
					INT item_id = -1;

					column_id = (INT)lparam;

					_r_obj_initializestringbuilder (&sb, 256);

					while ((item_id = _r_listview_getnextselected (hwnd, IDC_LISTVIEW, item_id)) != -1)
					{
						string = _r_listview_getitemtext (hwnd, IDC_LISTVIEW, item_id, column_id);

						if (string)
						{
							_r_obj_appendstringbuilder2 (&sb, &string->sr);

							_r_obj_dereference (string);
						}

						_r_obj_appendstringbuilder (&sb, L"\r\n");
					}

					string = _r_obj_finalstringbuilder (&sb);

					_r_str_trimstring2 (&string->sr, L"\r\n ", 0);

					_r_clipboard_set (hwnd, &string->sr);

					_r_obj_dereference (string);

					break;
				}

				case IDM_SELECT_ALL:
				{
					if (GetFocus () != GetDlgItem (hwnd, IDC_LISTVIEW))
						break;

					_r_listview_setitemstate (hwnd, IDC_LISTVIEW, -1, LVIS_SELECTED, LVIS_SELECTED);

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (
	_In_ HINSTANCE hinst,
	_In_opt_ HINSTANCE prev_hinst,
	_In_ LPWSTR cmdline,
	_In_ INT show_cmd
)
{
	HWND hwnd;

	if (!_r_app_initialize (NULL))
		return ERROR_APP_INIT_FAILURE;

	hwnd = _r_app_createwindow (hinst, MAKEINTRESOURCEW (IDD_MAIN), MAKEINTRESOURCEW (IDI_MAIN), &DlgProc);

	if (!hwnd)
		return ERROR_APP_INIT_FAILURE;

	return _r_wnd_message_callback (hwnd, MAKEINTRESOURCEW (IDA_MAIN));
}
