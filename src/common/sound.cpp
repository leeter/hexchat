/* X-Chat
* Copyright (C) 1998 Peter Zelezny.
* Copyright (C) 2014-2016 leetsoftwerx
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#ifdef WIN32
#include <Windows.h>
#include <mmsystem.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif
#include <array>
#include <sstream>
#include <string>

#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/utility/string_ref.hpp>

#include "cfgfiles.hpp"
#include "fe.hpp"
#include "filesystem.hpp"
#include "hexchatc.hpp"
#include "sessfwd.hpp"
#include "sound.hpp"
#include "text.hpp"

#ifdef USE_LIBCANBERRA
#include <canberra.h>
#endif

#ifdef USE_LIBCANBERRA
static ca_context *ca_con;
#endif
/* =========================== */
/* ========== SOUND ========== */
/* =========================== */

extern const std::array<text_event, NUM_XP> te;

namespace {
	std::array<std::string, NUM_XP> sound_files;

	bool play_sound(const boost::filesystem::path & path) {
#ifdef WIN32
		return PlaySoundW(path.c_str(), nullptr, SND_NODEFAULT | SND_FILENAME | SND_ASYNC);
#else
#ifdef USE_LIBCANBERRA
		if (ca_con == nullptr)
		{
			ca_context_create(&ca_con);
			ca_context_change_props(ca_con,
				CA_PROP_APPLICATION_ID, "hexchat",
				CA_PROP_APPLICATION_NAME, "HexChat",
				CA_PROP_APPLICATION_ICON_NAME, "hexchat", nullptr);
		}

		if (ca_context_play(ca_con, 0, CA_PROP_MEDIA_FILENAME, wavfile.c_str(), nullptr) != 0)
#endif
		{
			glib_string cmd(g_find_program_in_path("play"));

			if (cmd)
			{
				glib_string buf(g_strdup_printf("%s \"%s\"", cmd.get(), wavfile.c_str()));
				hexchat_exec(buf.get());
			}
			return true;
		}
		return false;
#endif
	}
}

namespace sound {
	void beep(session &sess)
	{
		if (!prefs.hex_gui_focus_omitalerts || !fe_gui_info(sess, 0) == 1)
		{
			if (!sound_files[XP_TE_BEEP].empty())
				/* user defined beep _file_ */
				::sound::play_event(XP_TE_BEEP);
			else
				/* system beep */
				fe_beep();
		}
	}

	void play(const boost::string_ref & file, announce quiet)
	{
		namespace bfs = boost::filesystem;

		/* the pevents GUI editor triggers this after removing a soundfile */
		if (file.empty())
		{
			return;
		}
		bfs::path wavfile;
#ifdef WIN32
		/* check for fullpath */
		if (file[0] == '\\' || (((file[0] >= 'A' && file[0] <= 'Z') || (file[0] >= 'a' && file[0] <= 'z')) && file[1] == ':'))
#else
		if (file[0] == '/')
#endif
		{
			wavfile = file.to_string();
		}
		else
		{
			wavfile = bfs::path(config::config_dir()) / HEXCHAT_SOUND_DIR / file.to_string();
		}

		if (!play_sound(wavfile))
		{
			if (quiet == announce::print)
			{
				std::ostringstream buf;
				buf << boost::format(_("Cannot read sound file:\n%s")) % wavfile;
				fe_message(buf.str(), FE_MSG_ERROR);
			}
		}
	}

	void play_event(int i)
	{
		play(sound_files[i], announce::print);
	}

	// file is intended to be an R-Value
	static void sound_load_event(const std::string & evt, std::string file)
	{
		int i = 0;

		if (!file.empty() && pevent_find(evt, i) != -1)
		{
			sound_files[i] = std::move(file);
		}
	}

	void load()
	{
		namespace bfs = boost::filesystem;
		auto path = bfs::path(config::config_dir()) / "sound.conf";
		bfs::ifstream instream(path, std::ios::in | std::ios::binary);
		std::string evt;
		for (std::string line; std::getline(instream, line, '\n');)
		{
			if (boost::starts_with(line, "event="))
			{
				evt = line.substr(6);
			}
			else if (boost::starts_with(line, "sound="))
			{
				if (!evt.empty())
				{
					sound_load_event(evt, line.substr(6));
				}
			}
		}
	}

	void save()
	{
		int fd = hexchat_open_file("sound.conf", O_CREAT | O_TRUNC | O_WRONLY, 0x180,
			io::fs::XOF_DOMODE);
		if (fd == -1)
			return;

		for (int i = 0; i < NUM_XP; i++)
		{
			if (!sound_files[i].empty())
			{
				char buf[512];
				const auto name_string = gsl::to_string(te[i].name);
				write(fd, buf, snprintf(buf, sizeof(buf),
					"event=%s\n", name_string.c_str()));
				write(fd, buf, snprintf(buf, sizeof(buf),
					"sound=%s\n\n", sound_files[i].c_str()));
			}
		}

		close(fd);
	}
	gsl::span<std::string> files() noexcept
	{
		return sound_files;
	}
}