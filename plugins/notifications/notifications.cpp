// notifications.cpp : Defines the exported functions for the DLL application.
//
#include <SDKDDKVer.h>
#include <roapi.h>
#include <windows.ui.notifications.h>

#include "hexchat-plugin.h"

namespace
{
    hexchat_plugin * ph;
    const char name[] = "Windows notifications";
    const char desc[] = "Displays WinRT notifications";
    const char version[] = "1.0";
    const char helptext[] = "Notifies the user using WinRT notifications";


    static int
        cmd_cb(const char * const word[], const char * const word_eol[], void *user_data)
    {
        return HEXCHAT_EAT_ALL;
    }
}

int
hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name, char **plugin_desc, char **plugin_version, char *arg)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return FALSE;
    ph = plugin_handle;

    *plugin_name = const_cast<char*>(name);
    *plugin_desc = const_cast<char*>(desc);
    *plugin_version = const_cast<char*>(version);

    hexchat_hook_command(ph, "RTNOTIFIY", HEXCHAT_PRI_NORM, cmd_cb, helptext, nullptr);
    hexchat_command(ph, "MENU -ishare\\system.png ADD \"Set up WinRT Notifications\" \"RTNOTIFY\"");

    hexchat_printf(ph, "%s plugin loaded\n", name);

    return TRUE;       /* return 1 for success */
}


int
hexchat_plugin_deinit(void)
{
    RoUninitialize();
    hexchat_command(ph, "MENU DEL \"Set up WinRT Notifications\"");
    hexchat_printf(ph, "%s plugin unloaded\n", name);
    return TRUE;
}
