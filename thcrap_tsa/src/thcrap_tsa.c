/**
  * Touhou Community Reliant Automatic Patcher
  * Team Shanghai Alice support plugin
  *
  * ----
  *
  * Plugin setup.
  */

#include <thcrap.h>
#include <commctrl.h>
#include "thcrap_tsa.h"
#include "layout.h"

tsa_game_t game_id;

// Translate strings to IDs.
tsa_game_t game_id_from_string(const char *game)
{
	if(!strcmp(game, "th06")) {
		return TH06;
	} else if(!strcmp(game, "th07")) {
		return TH07;
	} else if(!strcmp(game, "th08")) {
		return TH08;
	} else if(!strcmp(game, "th09")) {
		return TH09;
	} else if(!strcmp(game, "th095")) {
		return TH095;
	} else if(!strcmp(game, "th10")) {
		return TH10;
	} else if(!strcmp(game, "alcostg")) {
		return ALCOSTG;
	} else if(!strcmp(game, "th11")) {
		return TH11;
	} else if(!strcmp(game, "th12")) {
		return TH12;
	} else if(!strcmp(game, "th125")) {
		return TH125;
	} else if(!strcmp(game, "th128")) {
		return TH128;
	} else if(!strcmp(game, "th13")) {
		return TH13;
	} else if(!strcmp(game, "th14")) {
		return TH14;
	}
	return TH_FUTURE;
}

int __stdcall thcrap_plugin_init()
{
	if(stack_remove_if_unneeded("base_tsa")) {
		return 1;
	}

	const char *game = json_object_get_string(runconfig_get(), "game");
	game_id = game_id_from_string(game);

	// th06_msg
	patchhook_register("msg*.dat", patch_msg_dlg); // th06-08
	patchhook_register("p*.msg", patch_msg_dlg); // th09
	patchhook_register("s*.msg", patch_msg_dlg); // lowest common denominator for th10+
	patchhook_register("msg*.msg", patch_msg_dlg); // th143
	patchhook_register("e*.msg", patch_msg_end); // th10+ endings

	patchhook_register("*.anm", patch_anm);
	return 0;
}

int InitDll(HMODULE hDll)
{
	// custom.exe bugfix
	INITCOMMONCONTROLSEX icce = {
		sizeof(icce), ICC_STANDARD_CLASSES
	};
	InitCommonControlsEx(&icce);
	return 0;
}

// Yes, this _has_ to be included in every project.
// Visual C++ won't use it when imported from a library
// and just defaults to msvcrt's one in this case.
BOOL APIENTRY DllMain(HMODULE hDll, DWORD ulReasonForCall, LPVOID lpReserved)
{
	switch(ulReasonForCall) {
		case DLL_PROCESS_ATTACH:
			InitDll(hDll);
			break;
	}
	return TRUE;
}
