/* HexChat
* Copyright (c) 2014 Leetsoftwerx
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

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
    HRESULT hr = Windows::Foundation::Initialize();
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
    Windows::Foundation::Uninitialize();
    hexchat_command(ph, "MENU DEL \"Set up WinRT Notifications\"");
    hexchat_printf(ph, "%s plugin unloaded\n", name);
    return TRUE;
}
