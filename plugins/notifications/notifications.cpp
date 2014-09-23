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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT_TYPED_ITEMIDS
#include <memory>
#include <string>
#include <locale>
#include <codecvt>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include <Windows.h>
#include <ShlObj.h>
#include <Shobjidl.h>
#include <Propvarutil.h>
#include <functiondiscoverykeys.h>
#include <VersionHelpers.h>

#include <roapi.h>
#include <wrl\client.h>
#include <wrl\implements.h>
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

	WinStatus
	get_window_status()
	{
		const char * st = hexchat_get_info(ph, "win_status");

		if (!st)
			return WinStatus::WS_HIDDEN;

		if (!strcmp(st, "active"))
			return WinStatus::WS_FOCUSED;

		if (!strcmp(st, "hidden"))
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

	// we have to create an app compatible shortcut to use toast notifications
	HRESULT InstallShortcut(const std::wstring& shortcutPath)
	{
		wchar_t exePath[MAX_PATH];

		DWORD charWritten = GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath));

		HRESULT hr = charWritten > 0 ? S_OK : E_FAIL;

		if (SUCCEEDED(hr))
		{
			Microsoft::WRL::ComPtr<IShellLinkW> shellLink;
			hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shellLink));

			if (SUCCEEDED(hr))
			{
				hr = shellLink->SetPath(exePath);
				if (SUCCEEDED(hr))
				{
					hr = shellLink->SetArguments(L"");
					if (SUCCEEDED(hr))
					{
						Microsoft::WRL::ComPtr<IPropertyStore> propertyStore;

						hr = shellLink.As(&propertyStore);
						if (SUCCEEDED(hr))
						{
							PROPVARIANT appIdPropVar;
							hr = InitPropVariantFromString(AppId, &appIdPropVar);
							if (SUCCEEDED(hr))
							{
								hr = propertyStore->SetValue(PKEY_AppUserModel_ID, appIdPropVar);
								if (SUCCEEDED(hr))
								{
									hr = propertyStore->Commit();
									if (SUCCEEDED(hr))
									{
										Microsoft::WRL::ComPtr<IPersistFile> persistFile;
										hr = shellLink.As(&persistFile);
										if (SUCCEEDED(hr))
										{
											hr = persistFile->Save(shortcutPath.c_str(), TRUE);
										}
									}
								}
								PropVariantClear(&appIdPropVar);
							}
						}
					}
				}
			}
		}
		return hr;
	}

	HRESULT TryInstallAppShortcut()
	{
		wchar_t*roaming_path_wide = nullptr;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming_path_wide);
		if (FAILED(hr))
			return hr;

		std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> roaming_path(roaming_path_wide, &::CoTaskMemFree);
		boost::filesystem::path path(roaming_path_wide);

		path /= L"\\Microsoft\\Windows\\Start Menu\\Programs\\Hexchat.lnk";
		bool fileExists = boost::filesystem::exists(path);

		if (!fileExists)
		{
			hr = InstallShortcut(path.wstring());
		}
		else
		{
			hr = S_FALSE;
		}
		return hr;
	}
	
	static int handle_incoming(const char *const word[], const char *const word_eol[], void*) {

		// is this the active channel or is the window focused?
		if (hexchat_get_context(ph) == hexchat_find_context(ph, nullptr, nullptr) &&
			get_window_status() == WinStatus::WS_FOCUSED)
		{
			return HEXCHAT_EAT_NONE;
		}

		const std::string nick(hexchat_get_info(ph, "nick"));


		if (nick != word[3] && std::string(word_eol[4]).find(nick) == std::string::npos)
			return HEXCHAT_EAT_NONE;

		const std::string channel(hexchat_get_info(ph, "channel"));
		const std::string server_name(hexchat_get_info(ph, "server"));

		try
		{
			auto toastTemplate =
				Windows::UI::Notifications::ToastNotificationManager::GetTemplateContent(
					Windows::UI::Notifications::ToastTemplateType::ToastText04);
			auto node_list = toastTemplate->GetElementsByTagName(L"text");
			UINT node_count = node_list->Length;

			// put the channel name first
			node_list->GetAt(0)->AppendChild(
				toastTemplate->CreateTextNode(
					ref new Platform::String(widen(server_name + " - " + channel).c_str())));

			// this should be the nick
			auto wide_nick = widen(word[1]);
			auto bang_loc = wide_nick.find_first_of(L'!');
			if (bang_loc != std::wstring::npos)
				wide_nick.erase(bang_loc, std::wstring::npos);
			wide_nick.erase(0, 1);
			node_list->GetAt(1)->AppendChild(
				toastTemplate->CreateTextNode(
					ref new Platform::String(wide_nick.c_str())));

			// then the message
			auto sanitizer_del = std::bind(hexchat_free, ph, std::placeholders::_1);
			std::unique_ptr<char, decltype(sanitizer_del)> sanitized(
				hexchat_strip(ph, word_eol[4], static_cast<int>(strlen(word_eol[4])), 7 /*STRIP_ALL*/),
				sanitizer_del);
			auto widen_str = widen(sanitized.get());
			widen_str.erase(0, 1);
			boost::algorithm::erase_all(widen_str, L"\x1");
			auto node2 = node_list->GetAt(2);
			node2->AppendChild(
				toastTemplate->CreateTextNode(
					ref new Platform::String(widen_str.c_str())));

			auto notifier = Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(Platform::StringReference(AppId));
			notifier->Show(ref new Windows::UI::Notifications::ToastNotification(toastTemplate));
		}
		catch (Platform::Exception ^ ex)
		{
			auto what = ex->ToString();

			hexchat_printf(ph, "An Error Occured Printing a Notification HRESULT: %#X : %s", static_cast<unsigned long>(ex->HResult), narrow(what->Data()).c_str());
		}
		return HEXCHAT_EAT_NONE;
	}
}

int
hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name, char **plugin_desc, char **plugin_version, char *arg)
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

	hr = TryInstallAppShortcut();
	if (FAILED(hr))
		return FALSE;
	
   // hexchat_hook_command(ph, "RTNOTIFIY", HEXCHAT_PRI_NORM, cmd_cb, helptext, nullptr);
	hexchat_hook_server(ph, "PRIVMSG", HEXCHAT_PRI_NORM, handle_incoming, NULL);
	//hexchat_command(ph, "MENU -ishare\\system.png ADD \"Window/Set up WinRT Notifications\" \"RTNOTIFY\"");
	
	hexchat_printf(ph, "%s plugin loaded\n", name);

	return TRUE;       /* return 1 for success */
}


int
hexchat_plugin_deinit(void)
{
	Windows::Foundation::Uninitialize();
	hexchat_command(ph, "MENU DEL \"Window/Set up WinRT Notifications\"");
	hexchat_printf(ph, "%s plugin unloaded\n", name);
	return TRUE;
}
