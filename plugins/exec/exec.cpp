/* HexChat
 * Copyright (c) 2011-2012 Berke Viktor.
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <codecvt>
#include <cstring>
#include <locale>
#include <type_traits>
#include <memory>
#include <sstream>
#include <string>
#include <boost/algorithm/string.hpp>
#if _MSC_VER >= 1900
#define NOEXCEPT noexcept
#else
#define NOEXCEPT throw()
#endif

#include "hexchat-plugin.h"

namespace {
	static hexchat_plugin *ph;   /* plugin handle */
	static char name[] = "Exec";
	static char desc[] = "Execute commands inside HexChat";
	static char version[] = "1.2";

	std::wstring widen(const std::string & to_widen)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
		return converter.from_bytes(to_widen);
	}

	struct handle_deleter
	{
		using pointer = HANDLE;
		void operator()(HANDLE h) NOEXCEPT
		{
			if (h != INVALID_HANDLE_VALUE)
				CloseHandle(h);
		}
	};

	using handle_ptr = std::unique_ptr < HANDLE, handle_deleter > ;
	static int
		run_command(const char *const word[], const char *const word_eol[], void *userdata) NOEXCEPT
	{
		if (std::strlen(word[2]) > 0)
		{
			char buffer[4096] = { 0 };
			DWORD dwRead = 0;
			DWORD dwLeft = 0;
			DWORD dwAvail = 0;
			bool announce = false;

			handle_ptr readPipe;
			handle_ptr writePipe;
			STARTUPINFO sInfo = { 0 };
			PROCESS_INFORMATION pInfo = { 0 };
			SECURITY_ATTRIBUTES secattr = { 0 };

			secattr.nLength = sizeof(secattr);
			secattr.bInheritHandle = TRUE;
			std::chrono::nanoseconds time_elapsed;
			try
			{
				std::wostringstream commandLine(L"cmd.exe /c ", std::ios::ate);

				if (boost::iequals("-O", word[2]))
				{
					commandLine << widen(word_eol[3]);
					announce = true;
				}
				else
				{
					commandLine << widen(word_eol[2]);
				}

				{
					HANDLE rdPipe;
					HANDLE wtPipe;
					CreatePipe(&rdPipe, &wtPipe, &secattr, 0); /* might be replaced with MyCreatePipeEx */
					readPipe.reset(rdPipe);
					writePipe.reset(wtPipe);
				}

				sInfo.cb = sizeof(STARTUPINFO);
				sInfo.dwFlags = STARTF_USESTDHANDLES;
				sInfo.hStdInput = nullptr;
				sInfo.hStdOutput = writePipe.get();
				sInfo.hStdError = writePipe.get();

				std::wstring process_cmdln(commandLine.str());
				process_cmdln.push_back(L'\0');
				::CreateProcessW(0, &process_cmdln[0], 0, 0, TRUE, NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW, 0, 0, &sInfo, &pInfo);
				writePipe.reset();
				handle_ptr hProcess(pInfo.hProcess);
				handle_ptr hThread(pInfo.hThread);

				auto start = std::chrono::high_resolution_clock::now();
				while (::PeekNamedPipe(readPipe.get(), buffer, 1, &dwRead, &dwAvail, &dwLeft) && time_elapsed < std::chrono::seconds(10))
				{
					if (dwRead)
					{
						if (::ReadFile(readPipe.get(), buffer, sizeof(buffer) - 1, &dwRead, nullptr) && dwRead != 0)
						{
							/* avoid garbage */
							buffer[dwRead] = '\0';

							if (announce)
							{
								std::istringstream inputbuf(buffer);
								/* Say each line seperately, TODO: improve... */
								for (std::string line; std::getline(inputbuf, line, '\n');)
								{
									hexchat_commandf(ph, "SAY %s", line.c_str());
								}
							}
							else
								hexchat_printf(ph, "%s", buffer);
						}
					}
					else
					{
						/* this way we'll more likely get full lines */
						SleepEx(100, TRUE);
					}
					time_elapsed = std::chrono::high_resolution_clock::now() - start;
				}
			}
			catch (const std::exception & ex)
			{
				hexchat_printf(ph, "An error occured during execution: %s", ex.what());
			}

			/* display a newline to separate things */
			if (!announce)
				hexchat_print(ph, "\n");

			if (time_elapsed >= std::chrono::seconds(10))
			{
				hexchat_print(ph, "Command took too much time to run, execution aborted.\n");
			}
		}
		else
		{
			hexchat_command(ph, "help exec");
		}

		return HEXCHAT_EAT_HEXCHAT;
	}
}
int
hexchat_plugin_init(hexchat_plugin *plugin_handle, char **plugin_name, char **plugin_desc, char **plugin_version, char *arg) NOEXCEPT
{
	ph = plugin_handle;

	*plugin_name = name;
	*plugin_desc = desc;
	*plugin_version = version;

	hexchat_hook_command(ph, "EXEC", HEXCHAT_PRI_NORM, run_command, "Usage: /EXEC [-O] - execute commands inside HexChat", 0);
	hexchat_printf(ph, "%s plugin loaded\n", name);

	return 1;       /* return 1 for success */
}

int
hexchat_plugin_deinit(void) NOEXCEPT
{
	hexchat_printf(ph, "%s plugin unloaded\n", name);
	return 1;
}
