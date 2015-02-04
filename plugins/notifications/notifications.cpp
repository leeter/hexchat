/* HexChat
* Copyright (c) 2014-2015 Leetsoftwerx
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
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <locale>
#include <codecvt>

#include <Windows.h>
#include <VersionHelpers.h>

#include <roapi.h>
#include <windows.ui.notifications.h>

#include "hexchat-plugin.h"

namespace
{
	hexchat_plugin * ph;
	const char name[] = "Windows Toast Notifications";
	const char desc[] = "Displays Toast notifications";
	const char version[] = "1.0";
	const char helptext[] = "Notifies the user using Toast notifications";
	const wchar_t AppId[] = L"Hexchat.Desktop.Notify";

	enum class WinStatus
	{
		WS_FOCUSED,
		WS_NORMAL,
		WS_HIDDEN
	};

	WinStatus get_window_status()
	{
		const char * st = hexchat_get_info(ph, "win_status");

		if (!st)
			return WinStatus::WS_HIDDEN;

		if (!std::strcmp(st, "active"))
			return WinStatus::WS_FOCUSED;

		if (!std::strcmp(st, "hidden"))
			return WinStatus::WS_HIDDEN;

		return WinStatus::WS_NORMAL;
	}

	std::wstring widen(const std::string & to_widen)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
		return converter.from_bytes(to_widen);
	}

	std::string narrow(const std::wstring & to_narrow)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
		return converter.to_bytes(to_narrow);
	}

	static ::Windows::UI::Notifications::ToastNotifier ^ notifier = nullptr;
	void show_notification(const std::string & title, const std::string & subtitle, const std::string & message)
	{
		try
		{
			namespace wun = Windows::UI::Notifications;
			auto toastTemplate =
				wun::ToastNotificationManager::GetTemplateContent(
				wun::ToastTemplateType::ToastText04);
			auto node_list = toastTemplate->GetElementsByTagName(Platform::StringReference{ L"text", 4 });

			// Title first
			auto wide_title = widen(title);
			node_list->GetAt(0)->AppendChild(
				toastTemplate->CreateTextNode(Platform::StringReference{ wide_title.c_str(), wide_title.size() }));

			// Subtitle
			auto wide_subtitle = widen(subtitle);
			node_list->GetAt(1)->AppendChild(
				toastTemplate->CreateTextNode(
				Platform::StringReference{ wide_subtitle.c_str(), wide_subtitle.size() }));

			// then the message
			auto widen_str = widen(message);
			auto node2 = node_list->GetAt(2);
			node2->AppendChild(
				toastTemplate->CreateTextNode(
				Platform::StringReference{ widen_str.c_str(), widen_str.size() }));

			notifier->Show(ref new wun::ToastNotification{ toastTemplate });
		}
		catch (Platform::Exception ^ ex)
		{
			auto what = ex->ToString();

			hexchat_printf(ph, "An Error Occurred Printing a Notification HRESULT: %#X : %s", static_cast<unsigned long>(ex->HResult), narrow(what->Data()).c_str());
		}
	}
	
	static int handle_incoming(const char *const word[], const char *const word_eol[], void*) throw()
	{
		// is this the active channel or is the window focused?
		if (hexchat_get_context(ph) == hexchat_find_context(ph, nullptr, nullptr) &&
			get_window_status() == WinStatus::WS_FOCUSED)
		{
			return HEXCHAT_EAT_NONE;
		}

		const std::string my_nick{ hexchat_get_info(ph, "nick") };
		// this should be the nick
		auto nick = std::string(word[1]);
		auto bang_loc = nick.find_first_of('!');
		if (bang_loc != std::wstring::npos)
		{
			nick.erase(bang_loc);
		}

		nick.erase(0, 1);
		const std::string channel{ hexchat_get_info(ph, "channel") };
		if (my_nick != word[3] && std::string(word_eol[4]).find(my_nick) == std::string::npos)
			return HEXCHAT_EAT_NONE;

		const std::string server_name{ hexchat_get_info(ph, "server") };

		auto sanitizer_del = std::bind(hexchat_free, ph, std::placeholders::_1);

		std::unique_ptr<char, decltype(sanitizer_del)> sanitized(
			hexchat_strip(ph, word_eol[4], static_cast<int>(std::strlen(word_eol[4])), 7 /*STRIP_ALL*/),
			sanitizer_del);

		auto sanitized_str = std::string{ sanitized.get() };
		sanitized_str.erase(0, 1);
		// remove characters that would break the toast
		sanitized_str.erase(std::remove(sanitized_str.begin(), sanitized_str.end(), '\x1'), sanitized_str.end());
		show_notification(server_name + " - " + channel, nick, sanitized_str);
		return HEXCHAT_EAT_NONE;
	}
}

int
hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name, char **plugin_desc, char **plugin_version, char *arg) throw()
{
	if (!IsWindows8Point1OrGreater())
		return FALSE;
	HRESULT hr = Windows::Foundation::Initialize(RO_INIT_SINGLETHREADED);
	if (FAILED(hr))
		return FALSE;
	ph = plugin_handle;

	*plugin_name = const_cast<char*>(name);
	*plugin_desc = const_cast<char*>(desc);
	*plugin_version = const_cast<char*>(version);
	
	hexchat_hook_server(ph, "PRIVMSG", HEXCHAT_PRI_NORM, handle_incoming, NULL);
	
	hexchat_printf(ph, "%s plugin loaded\n", name);
	namespace wun = Windows::UI::Notifications;
	if (!notifier)
		notifier = wun::ToastNotificationManager::CreateToastNotifier(Platform::StringReference(AppId));
	return TRUE;       /* return 1 for success */
}


int
hexchat_plugin_deinit(void) throw()
{
	// ensure release before we deinit the runtime
	notifier = nullptr;
	Windows::Foundation::Uninitialize();
	hexchat_command(ph, "MENU DEL \"Window/Set up WinRT Notifications\"");
	hexchat_printf(ph, "%s plugin unloaded\n", name);
	return TRUE;
}
