// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include "x3daudio-hook/x3daudio_hook.h"
#include "xaudio2-hook/creation_hook.h"

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		try
		{
			init_x3daudio_hook();
			install_com_hooks();
		}
		catch (...)
		{
			// An exception leaving DllMain would terminate the game. Without the hooks the game just runs without HRTF.
			return FALSE;
		}
		break;
	case DLL_PROCESS_DETACH:
		try
		{
			uninstall_com_hooks();
		}
		catch (...)
		{
		}
		destroy_x3daudio_hook();
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}
