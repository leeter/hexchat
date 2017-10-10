/* HexChat
* Copyright (c) 2014-2017 Leetsoftwerx
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

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT_TYPED_ITEMIDS
#include <codecvt>
#include <cstdint>
#include <locale>
#include <memory>
#include <string>

#include <Windows.h>
#include <VersionHelpers.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Data.Xml.Dom.h>

#include "hexchat-plugin.h"


namespace
{
hexchat_plugin *ph;
const char name[] = "Windows Toast Notifications";
const char desc[] = "Displays Toast notifications";
const char version[] = "1.0";
const char helptext[] = "Notifies the user using Toast notifications";
const wchar_t AppId[] = L"Hexchat.Desktop.Notify";

static bool notify_priv = true;
static bool notify_chan = true;
static bool notify_chan_action = true;
static bool notify_priv_action = true;
hexchat_hook *command_hook = nullptr;

enum class WinStatus
{
	WS_FOCUSED,
	WS_NORMAL,
	WS_HIDDEN
};

enum class hilight_source : std::uintptr_t
{
	channel = 0,
	action,
	priv,
	priv_action
};

WinStatus get_window_status() noexcept
{
	using namespace std::string_view_literals;
	const char *st = hexchat_get_info(ph, "win_status");

	if (!st)
		return WinStatus::WS_HIDDEN;

	if (u8"active"sv == st)
		return WinStatus::WS_FOCUSED;

	if (u8"hidden"sv == st)
		return WinStatus::WS_HIDDEN;

	return WinStatus::WS_NORMAL;
}

using utf8converter = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>;

std::wstring widen(const std::string &to_widen)
{
	return utf8converter{}.from_bytes(to_widen);
}

std::string narrow(const std::wstring &to_narrow)
{
	return utf8converter{}.to_bytes(to_narrow);
}

static void save_prefs() noexcept
{
	hexchat_pluginpref_set_int(ph, "chan", notify_chan);
	hexchat_pluginpref_set_int(ph, "priv", notify_priv);
	hexchat_pluginpref_set_int(ph, "chan_action", notify_chan_action);
	hexchat_pluginpref_set_int(ph, "priv_action", notify_priv_action);
}

static void setup_prefs() noexcept
{
	char preflist[4096] = {};
	hexchat_pluginpref_list(ph, preflist);
	if (preflist[0])
	{
		notify_chan = !!hexchat_pluginpref_get_int(ph, "chan");
		notify_priv = !!hexchat_pluginpref_get_int(ph, "priv");
		notify_chan_action =
		    !!hexchat_pluginpref_get_int(ph, "chan_action");
		notify_priv_action =
		    !!hexchat_pluginpref_get_int(ph, "priv_action");
		return;
	}
	else
	{
		save_prefs();
	}
}

static void show_notification(const std::string &title,
			      const std::string &subtitle,
			      const std::string &message) 
try
{
	namespace awun = winrt::Windows::UI::Notifications;
	namespace xml = winrt::Windows::Data::Xml::Dom;
	using namespace std::string_view_literals;
	const auto notification_xml = 
		awun::ToastNotificationManager::GetTemplateContent(awun::ToastTemplateType::ToastText04);

	const auto node_list = notification_xml.GetElementsByTagName(L"text"sv);
	
	// Title first
	{
		const auto wide_title = widen(title);
		const auto title_text = notification_xml.CreateTextNode(wide_title);
		node_list.GetAt(0).AppendChild(title_text);
	}
	

	// Subtitle
	{
		const auto wide_subtitle = widen(subtitle);
		const auto subtitle_text = notification_xml.CreateTextNode(wide_subtitle);
		node_list.GetAt(1).AppendChild(subtitle_text);
	}

	// then the message
	{
		const auto wide_message = widen(message);
		const auto message_text = notification_xml.CreateTextNode(wide_message);
		node_list.GetAt(2).AppendChild(message_text);
	}

	const awun::ToastNotification notification(notification_xml);
	const auto notifier = awun::ToastNotificationManager::CreateToastNotifier(AppId);
	
	notifier.Show(notification);
}
catch (const winrt::hresult_error& ex)
{
	const auto description = ex.message();
	const auto what = narrow(std::wstring(description));

	hexchat_printf(ph, u8"An Error Occurred Printing a Notification "
			   "HRESULT: %#X : %s",
		       static_cast<unsigned long>(ex.code()),
		       what.c_str());
}
catch (const std::range_error &) 
{
	hexchat_print(ph, u8"Invalid conversion from UTF8");
}

static bool should_handle(void *source) noexcept
{
	hilight_source msg_source = static_cast<hilight_source>(
	    reinterpret_cast<std::uintptr_t>(source));
	switch (msg_source)
	{
	case hilight_source::priv:
		return notify_priv;
	case hilight_source::action:
		return notify_chan_action;
	case hilight_source::priv_action:
		return notify_priv_action;
	default: // hilight_source::channel:
		return notify_chan;
	}
}

static int handle_hilight(const char *const word[], void *source) noexcept
{
	using namespace std::string_view_literals;
	if (!should_handle(source) ||
	    (hexchat_get_context(ph) ==
		 hexchat_find_context(ph, nullptr, nullptr) &&
	     get_window_status() == WinStatus::WS_FOCUSED))
	{
		return HEXCHAT_EAT_NONE;
	}

	const std::string channel{hexchat_get_info(ph, "channel")};
	const std::string server_name{hexchat_get_info(ph, "server")};

	auto sanitizer_del = [](void *ptr) noexcept
	{
		hexchat_free(ph, ptr);
	};

	std::unique_ptr<char, decltype(sanitizer_del)> sanitized(
	    hexchat_strip(ph, word[2], static_cast<int>(std::strlen(word[2])),
			  7 /*STRIP_ALL*/),
	    sanitizer_del);
	const auto sanitized_str = std::string{sanitized.get()};
	show_notification(server_name + u8" - " + channel, word[1],
			  sanitized_str);

	return HEXCHAT_EAT_NONE;
}

static bool parse_set(const char val[])
{
	return !(!val || !val[0] || stricmp(val, "off") == 0 ||
		 !std::atoi(val));
}

static constexpr const char *get_bool_str(bool val) noexcept { return val ? "ON" : "OFF"; }

int notifications_cmd_cb(const char *const word[], const char *const[],
			 void *) noexcept
{
	if (strcmpi(word[2], "set") == 0)
	{
		if (stricmp(word[3], "chan") == 0)
		{
			notify_chan = parse_set(word[4]);
		}
		else if (stricmp(word[3], "priv") == 0)
		{
			notify_priv = parse_set(word[4]);
		}
		if (stricmp(word[3], "priv_action") == 0)
		{
			notify_priv_action = parse_set(word[4]);
		}
		if (stricmp(word[3], "chan_action") == 0)
		{
			notify_chan_action = parse_set(word[4]);
		}
		save_prefs();
	}
	else
	{
		hexchat_print(ph, "Usage: /notifications set <option> "
				  "1|0|ON|OFF Where <option> is one of: chan, "
				  "priv, priv_action, chan_action");
		hexchat_printf(ph, "chan: %s", get_bool_str(notify_chan));
		hexchat_printf(ph, "priv: %s", get_bool_str(notify_priv));
		hexchat_printf(ph, "priv_action: %s",
			       get_bool_str(notify_priv_action));
		hexchat_printf(ph, "chan_action: %s",
			       get_bool_str(notify_chan_action));
	}
	return HEXCHAT_EAT_ALL;
}
} // anonymous namespace

int hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name,
			char **plugin_desc, char **plugin_version,
			char *) noexcept
try{
	if (!IsWindows8Point1OrGreater())
	{
		return false;
	}

	winrt::init_apartment(winrt::apartment_type::single_threaded);

	ph = plugin_handle;

	*plugin_name = const_cast<char *>(name);
	*plugin_desc = const_cast<char *>(desc);
	*plugin_version = const_cast<char *>(version);
	setup_prefs();

	hexchat_hook_print(ph, u8"Channel Msg Hilight", HEXCHAT_PRI_NORM,
			   handle_hilight,
			   static_cast<void *>(static_cast<std::uintptr_t>(hilight_source::channel)));
	hexchat_hook_print(ph, u8"Channel Action Hilight", HEXCHAT_PRI_NORM,
			   handle_hilight,
			   reinterpret_cast<void *>(static_cast<std::uintptr_t>(hilight_source::action)));
	hexchat_hook_print(
	    ph, u8"Private Action to Dialog", HEXCHAT_PRI_NORM, handle_hilight,
	    reinterpret_cast<void *>(hilight_source::priv_action));
	hexchat_hook_print(ph, u8"Private Message to Dialog", HEXCHAT_PRI_NORM,
			   handle_hilight,
			   reinterpret_cast<void *>(static_cast<std::uintptr_t>(hilight_source::priv)));

	command_hook = hexchat_hook_command(
	    ph, u8"NOTIFICATIONS", HEXCHAT_PRI_NORM, notifications_cmd_cb,
	    u8"sets or unsets the prefs for notifications", nullptr);

	hexchat_printf(ph, u8"%s plugin loaded\n", name);

	return true; /* return 1 for success */
}
catch (winrt::hresult_error &) {
	return false;
}

int hexchat_plugin_deinit() noexcept
{
	save_prefs();
	// ensure release before we deinit the runtime
	winrt::uninit_apartment();
	hexchat_command(ph, u8"MENU DEL \"Window/Set up WinRT Notifications\"");
	hexchat_unhook(ph, command_hook);
	hexchat_printf(ph, u8"%s plugin unloaded\n", name);
	return true;
}
