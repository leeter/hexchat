/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <sstream>
#include <string>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/system/error_code.hpp>
#include <boost/utility/string_ref.hpp>

#ifndef WIN32
#include <unistd.h>
#endif

#include "hexchat.hpp"
#include "cfgfiles.hpp"
#include "fe.hpp"
#include "filesystem.hpp"
#include "glist_iterators.hpp"
#include "server.hpp"
#include "text.hpp"
#include "util.hpp" /* token_foreach */
#include "hexchatc.hpp"

#include "servlist.hpp"

#include "session.hpp"

static void
free_and_clear(char *str)
{
	if (str)
	{
		volatile char *orig = str;
		while (*orig)
			*orig++ = 0;
		free(str);
	}
}

ircnet::ircnet()
	:nick(),
	nick2(),
	user(),
	real(),
	pass(),
	logintype(),
	comment(),
	encoding(),
	servlist(),
	commandlist(),
	favchanlist(),
	selected(),
	flags()
{}

ircnet::~ircnet()
{
	free(this->nick);
	free(this->nick2);
	free(this->user);
	free(this->real);
	free_and_clear(this->pass);
	if (this->favchanlist)
		g_slist_free_full(this->favchanlist, (GDestroyNotify)servlist_favchan_free);
	if (this->commandlist)
		g_slist_free_full(this->commandlist, (GDestroyNotify)servlist_command_free);
	free(this->comment);
	free(this->encoding);

	/* for safety */
	for(auto & serv : glib_helper::glist_iterable<server>(serv_list))
	{
		if (serv.network == this)
		{
			serv.network = nullptr;
		}
	}
}

struct defaultserver
{
	const char *network;
	const char *host;
	const char *channel;
	const char *charset;
	int loginmode;		/* default authentication type */
	const char *connectcmd;	/* default connect command - should only be used for rare login types, paired with LOGIN_CUSTOM */
};

static const struct defaultserver def[] =
{
	{"2600net",	0, 0, 0, 0, 0},
	{ 0, "irc.2600.net", 0, 0, 0, 0 },

	{"2ch", 0, 0, "iso-2022-jp", 0, 0},
	{ 0, "irc.2ch.sc", 0, 0, 0, 0 },
	{ 0, "irc.nurs.or.jp", 0, 0, 0, 0 },
	{ 0, "irc.juggler.jp", 0, 0, 0, 0 },

	{ "AccessIRC", 0, 0, 0, 0, 0 },
	{ 0, "irc.accessirc.net", 0, 0, 0, 0 },
	{ 0, "eu.accessirc.net", 0, 0, 0, 0 },

	{ "AfterNET", 0, 0, 0, 0, 0 },
	{ 0, "irc.afternet.org", 0, 0, 0, 0 },
	{ 0, "us.afternet.org", 0, 0, 0, 0 },
	{ 0, "eu.afternet.org", 0, 0, 0, 0 },

	{ "Aitvaras", 0, 0, 0, 0, 0 },
#ifdef USE_IPV6
#ifdef USE_OPENSSL
	{ 0, "irc6.ktu.lt/+7668", 0, 0, 0, 0 },
#endif
	{ 0, "irc6.ktu.lt/7666", 0, 0, 0, 0 },
#endif
#ifdef USE_OPENSSL
	{ 0, "irc.data.lt/+6668", 0, 0, 0, 0 },
	{ 0, "irc.omnitel.net/+6668", 0, 0, 0, 0 },
	{ 0, "irc.ktu.lt/+6668", 0, 0, 0, 0 },
	{ 0, "irc.kis.lt/+6668", 0, 0, 0, 0 },
	{ 0, "irc.vub.lt/+6668", 0, 0, 0, 0 },
#endif
	{ 0, "irc.data.lt", 0, 0, 0, 0 },
	{ 0, "irc.omnitel.net", 0, 0, 0, 0 },
	{ 0, "irc.ktu.lt", 0, 0, 0, 0 },
	{ 0, "irc.kis.lt", 0, 0, 0, 0 },
	{ 0, "irc.vub.lt", 0, 0, 0, 0 },

	{"AlphaChat",	0, 0, 0, LOGIN_SASL},
	{ 0, "irc.alphachat.net", 0, 0, 0, 0 },
	{ 0, "na.alphachat.net", 0, 0, 0, 0 },
	{ 0, "eu.alphachat.net", 0, 0, 0, 0 },
	{ 0, "au.alphachat.net", 0, 0, 0, 0 },
	{ 0, "za.alphachat.net", 0, 0, 0, 0 },

	{ "Anthrochat", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.anthrochat.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.anthrochat.net", 0, 0, 0, 0 },

	{ "ARCNet", 0, 0, 0, 0, 0 },
	{ 0, "se1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "us1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "us2.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "us3.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "ca1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "de1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "de3.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "ch1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "be1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "nl3.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "uk1.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "uk2.arcnet.vapor.com", 0, 0, 0, 0 },
	{ 0, "fr1.arcnet.vapor.com", 0, 0, 0, 0 },

	{ "AustNet", 0, 0, 0, 0, 0 },
	{ 0, "au.austnet.org", 0, 0, 0, 0 },
	{ 0, "us.austnet.org", 0, 0, 0, 0 },

	{ "AzzurraNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.azzurra.org", 0, 0, 0, 0 },
	{ 0, "crypto.azzurra.org", 0, 0, 0, 0 },

	{"Canternet", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.canternet.org/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.canternet.org", 0, 0, 0, 0 },

	{ "Chat4all", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.chat4all.org/+7001", 0, 0, 0, 0 },
#endif
	{ 0, "irc.chat4all.org", 0, 0, 0, 0 },

	{ "ChattingAway", 0, 0, 0, 0, 0 },
	{ 0, "irc.chattingaway.com", 0, 0, 0, 0 },

	{ "ChatJunkies", 0, 0, 0, 0, 0 },
	{ 0, "irc.chatjunkies.org", 0, 0, 0, 0 },
	{ 0, "nl.chatjunkies.org", 0, 0, 0, 0 },

	{ "ChatNet", 0, 0, 0, 0, 0 },
	{ 0, "US.ChatNet.Org", 0, 0, 0, 0 },

	{ "ChatSpike", 0, 0, 0, 0, 0 },
	{ 0, "irc.chatspike.net", 0, 0, 0, 0 },

	{ "Criten", 0, 0, 0, 0, 0 },
	{ 0, "irc.criten.net", 0, 0, 0, 0 },
	{ 0, "irc.eu.criten.net", 0, 0, 0, 0 },

	{ "DALnet", 0, 0, 0, 0, 0 },
	{ 0, "irc.dal.net", 0, 0, 0, 0 },
	{ 0, "irc.eu.dal.net", 0, 0, 0, 0 },

	{ "Dark-Tou-Net", 0, 0, 0, 0, 0 },
	{ 0, "irc.d-t-net.de", 0, 0, 0, 0 },
	{ 0, "bw.d-t-net.de", 0, 0, 0, 0 },
	{ 0, "nc.d-t-net.de", 0, 0, 0, 0 },

	{"DarkMyst", 0, 0, 0, LOGIN_SASL, 0},
	{ 0, "irc.darkmyst.org", 0, 0, 0, 0 },

	{ "DeepIRC", 0, 0, 0, 0, 0 },
	{ 0, "irc.deepirc.net", 0, 0, 0, 0 },

	{ "DeltaAnime", 0, 0, 0, 0, 0 },
	{ 0, "irc.deltaanime.net", 0, 0, 0, 0 },

	{ "EFnet", 0, 0, 0, 0, 0 },
	{ 0, "irc.blackened.com", 0, 0, 0, 0 },
	{ 0, "irc.Prison.NET", 0, 0, 0, 0 },
	{ 0, "irc.Qeast.net", 0, 0, 0, 0 },
	{ 0, "irc.efnet.pl", 0, 0, 0, 0 },
	{ 0, "irc.lightning.net", 0, 0, 0, 0 },
	{ 0, "irc.servercentral.net", 0, 0, 0, 0 },

	{ "ElectroCode", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL

	{ 0, "irc.electrocode.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.electrocode.net", 0, 0, 0, 0 },

	{ "EnterTheGame", 0, 0, 0, 0, 0 },
	{ 0, "IRC.EnterTheGame.Com", 0, 0, 0, 0 },

	{"EntropyNet",	0, 0, 0, LOGIN_SASL,0},
#ifdef USE_OPENSSL
	{ 0, "irc.entropynet.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.entropynet.net", 0, 0, 0, 0 },
#ifdef USE_IPV6
#ifdef USE_OPENSSL
	{ 0, "irc6.entropynet.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc6.entropynet.net", 0, 0, 0, 0 },
#endif

	{"EsperNet", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.esper.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.esper.net", 0, 0, 0, 0 },

	{ "EUIrc", 0, 0, 0, 0, 0 },
	{ 0, "irc.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.ham.de.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.ber.de.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.ffm.de.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.bre.de.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.hes.de.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.inn.at.euirc.net", 0, 0, 0, 0 },
	{ 0, "irc.bas.ch.euirc.net", 0, 0, 0, 0 },

	{ "EuropNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.europnet.org", 0, 0, 0, 0 },

	{ "FDFNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.fdfnet.net", 0, 0, 0, 0 },
	{ 0, "irc.eu.fdfnet.net", 0, 0, 0, 0 },

	{"FEFNet", 0, 0, 0, LOGIN_SASL,0},
	{ 0, "irc.fef.net", 0, 0, 0, 0 },

	{"freenode", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "chat.freenode.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "chat.freenode.net", 0, 0, 0, 0 },
	/* irc. points to chat. but many users and urls still reference it */
	{ 0, "irc.freenode.net", 0, 0, 0, 0 },

	{ "Furnet", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.furnet.org/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.furnet.org", 0, 0, 0, 0 },

	{ "GalaxyNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.galaxynet.org", 0, 0, 0, 0 },

	{ "GameSurge", 0, 0, 0, 0, 0 },
	{ 0, "irc.gamesurge.net", 0, 0, 0, 0 },
	
	{"GeeksIRC", 0, 0, 0, LOGIN_SASL,0},
#ifdef USE_OPENSSL
	{ 0, "irc.geeksirc.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.geeksirc.net", 0, 0, 0, 0 },

	{ "GeekShed", 0, 0, 0, 0, 0 },
	{ 0, "irc.geekshed.net", 0, 0, 0, 0 },

	{ "German-Elite", 0, 0, 0, 0, 0 },
	{ 0, "dominion.german-elite.net", 0, 0, 0, 0 },
	{ 0, "komatu.german-elite.net", 0, 0, 0, 0 },

	{ "GIMPNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.gimp.org", 0, 0, 0, 0 },
	{ 0, "irc.gnome.org", 0, 0, 0, 0 },

	{ "Hashmark", 0, 0, 0, 0, 0 },
	{ 0, "irc.hashmark.net", 0, 0, 0, 0 },

	{ "IdleMonkeys", 0, 0, 0, 0, 0 },
	{ 0, "irc.idlemonkeys.net", 0, 0, 0, 0 },

	{ "IndirectIRC", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.indirectirc.com/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.indirectirc.com", 0, 0, 0, 0 },
	
	{"Interlinked", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.interlinked.me/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.interlinked.me", 0, 0, 0, 0 },

	{"IRC4Fun", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.irc4fun.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.irc4fun.net", 0, 0, 0, 0 },

	{ "IRCHighWay", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.irchighway.net/+9999", 0, 0, 0, 0 },
#endif
	{ 0, "irc.irchighway.net", 0, 0, 0, 0 },

	{ "IrcLink", 0, 0, 0, 0, 0 },
	{ 0, "irc.irclink.net", 0, 0, 0, 0 },
	{ 0, "Alesund.no.eu.irclink.net", 0, 0, 0, 0 },
	{ 0, "Oslo.no.eu.irclink.net", 0, 0, 0, 0 },
	{ 0, "frogn.no.eu.irclink.net", 0, 0, 0, 0 },
	{ 0, "tonsberg.no.eu.irclink.net", 0, 0, 0, 0 },

	{ "IRCNet", 0, 0, 0, 0, 0 },
	{ 0, "open.ircnet.net", 0, 0, 0, 0 },
	{ 0, "irc.de.ircnet.net", 0, 0, 0, 0 },
	
	{"IRCNode", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.ircnode.org/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.ircnode.org", 0, 0, 0, 0 },

	{ "Irctoo.net", 0, 0, 0, 0, 0 },
	{ 0, "irc.irctoo.net", 0, 0, 0, 0 },

	{ "iZ-smart.net", 0, 0, 0, 0, 0 },
	{ 0, "irc.iZ-smart.net/6666", 0, 0, 0, 0 },
	{ 0, "irc.iZ-smart.net/6667", 0, 0, 0, 0 },
	{ 0, "irc.iZ-smart.net/6668", 0, 0, 0, 0 },

	{ "Krstarica", 0, 0, 0, 0, 0 },
	{ 0, "irc.krstarica.com", 0, 0, 0, 0 },

#ifdef USE_OPENSSL
	{ "LinkNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.link-net.org/+7000", 0, 0, 0, 0 },
	{ 0, "as.link-net.org/+7000", 0, 0, 0, 0 },
	{ 0, "eu.link-net.org/+7000", 0, 0, 0, 0 },
	{ 0, "us.link-net.org/+7000", 0, 0, 0, 0 },
#ifdef USE_IPV6
	{ 0, "irc6.link-net.org/+7000", 0, 0, 0, 0 },
#endif
#endif

	{ "MindForge", 0, 0, 0, 0, 0 },
	{ 0, "irc.mindforge.org", 0, 0, 0, 0 },

	{ "MIXXnet", 0, 0, 0, 0, 0 },
	{ 0, "irc.mixxnet.net", 0, 0, 0, 0 },

	{ "Moznet", 0, 0, 0, 0, 0 },
	{ 0, "irc.mozilla.org", 0, 0, 0, 0 },
	
	{ "ObsidianIRC", 0, 0, 0, 0, 0 },
	{ 0, "irc.obsidianirc.net", 0, 0, 0, 0 },

	{"Oceanius", 0, 0, 0, LOGIN_SASL, 0},
	{ 0, "irc.oceanius.com", 0, 0, 0, 0 },

	{ "OFTC", 0, 0, 0, 0, 0 },
	{ 0, "irc.oftc.net", 0, 0, 0, 0 },

	{ "OtherNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.othernet.org", 0, 0, 0, 0 },

	{ "OzNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.oz.org", 0, 0, 0, 0 },

	{ "PIRC.PL", 0, 0, 0, 0, 0 },
	{ 0, "irc.pirc.pl", 0, 0, 0, 0 },
	
	{ "PonyChat", 0, 0, 0, LOGIN_SASL, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.ponychat.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.ponychat.net", 0, 0, 0, 0 },

	{ "PTNet.org", 0, 0, 0, 0, 0 },
	{ 0, "irc.PTNet.org", 0, 0, 0, 0 },
	{ 0, "world.PTnet.org", 0, 0, 0, 0 },
	{ 0, "netvisao.PTnet.org", 0, 0, 0, 0 },
	{ 0, "uevora.PTnet.org", 0, 0, 0, 0 },
	{ 0, "vianetworks.PTnet.org", 0, 0, 0, 0 },
	{ 0, "uc.PTnet.org", 0, 0, 0, 0 },
	{ 0, "nfsi.ptnet.org", 0, 0, 0, 0 },
	{ 0, "fctunl.ptnet.org", 0, 0, 0, 0 },

	{"QuakeNet", 0, 0, 0, LOGIN_CHALLENGEAUTH, 0},
	{ 0, "irc.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.se.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.dk.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.no.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.fi.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.be.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.uk.quakenet.org", 0, 0, 0, 0 },
	{ 0, "irc.it.quakenet.org", 0, 0, 0, 0 },

	{ "Rizon", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.rizon.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.rizon.net", 0, 0, 0, 0 },
#ifdef USE_IPV6
#ifdef USE_OPENSSL
	{ 0, "irc6.rizon.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc6.rizon.net", 0, 0, 0, 0 },
#endif

	{"RusNet", 0, 0, "KOI8-R (Cyrillic)", 0, 0},
	{ 0, "irc.tomsk.net", 0, 0, 0, 0 },
	{ 0, "irc.run.net", 0, 0, 0, 0 },
	{ 0, "irc.ru", 0, 0, 0, 0 },
	{ 0, "irc.lucky.net", 0, 0, 0, 0 },

	{ "SceneNet", 0, 0, 0, 0, 0 },
	{ 0, "irc.scene.org", 0, 0, 0, 0 },
	{ 0, "irc.eu.scene.org", 0, 0, 0, 0 },
	{ 0, "irc.us.scene.org", 0, 0, 0, 0 },

	{ "SeilEn.de", 0, 0, 0, 0, 0 },
	{ 0, "irc.seilen.de", 0, 0, 0, 0 },

	{"SeionIRC", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.seion.us/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.seion.us", 0, 0, 0, 0 },

	{ "Serenity-IRC", 0, 0, 0, 0, 0 },
	{ 0, "irc.serenity-irc.net", 0, 0, 0, 0 },
	{ 0, "eu.serenity-irc.net", 0, 0, 0, 0 },
	{ 0, "us.serenity-irc.net", 0, 0, 0, 0 },

	{ "SlashNET", 0, 0, 0, 0, 0 },
	{ 0, "irc.slashnet.org", 0, 0, 0, 0 },
	{ 0, "area51.slashnet.org", 0, 0, 0, 0 },
	{ 0, "moo.slashnet.org", 0, 0, 0, 0 },
	{ 0, "radon.slashnet.org", 0, 0, 0, 0 },

	{"Snoonet", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.snoonet.org/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.snoonet.org/6667", 0, 0, 0, 0 },

	{ "Snyde", 0, 0, 0, 0, 0 },
	{ 0, "irc.snyde.net/6667", 0, 0, 0, 0 },

	{ "Sohbet.Net", 0, 0, 0, 0, 0 },
	{ 0, "irc.sohbet.net", 0, 0, 0, 0 },

	{ "SolidIRC", 0, 0, 0, 0, 0 },
	{ 0, "irc.solidirc.com", 0, 0, 0, 0 },

	{"SorceryNet", 0, 0, 0, LOGIN_SASL, 0},
	{ 0, "irc.sorcery.net/9000", 0, 0, 0, 0 },
	{ 0, "irc.us.sorcery.net/9000", 0, 0, 0, 0 },
	{ 0, "irc.eu.sorcery.net/9000", 0, 0, 0, 0 },
	
	{"SpotChat", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.spotchat.org/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.spotchat.org/6667", 0, 0, 0, 0 },

	{ "StarChat", 0, 0, 0, 0, 0 },
	{ 0, "irc.starchat.net", 0, 0, 0, 0 },
	{ 0, "gainesville.starchat.net", 0, 0, 0, 0 },
	{ 0, "freebsd.starchat.net", 0, 0, 0, 0 },
	{ 0, "sunset.starchat.net", 0, 0, 0, 0 },
	{ 0, "revenge.starchat.net", 0, 0, 0, 0 },
	{ 0, "tahoma.starchat.net", 0, 0, 0, 0 },
	{ 0, "neo.starchat.net", 0, 0, 0, 0 },

	{"StaticBox", 0, 0, 0, LOGIN_SASL, 0},
	{ 0, "irc.staticbox.net", 0, 0, 0, 0 },

	{ "Station51", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.station51.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.station51.net", 0, 0, 0, 0 },

	{"StormBit", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.stormbit.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.stormbit.net", 0, 0, 0, 0 },

	{ "SwiftIRC", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.swiftirc.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.swiftirc.net/6667", 0, 0, 0, 0 },

	{ "synIRC", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.synirc.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.synirc.net/6667", 0, 0, 0, 0 },

	{"Techman's World IRC",	0, 0, 0, LOGIN_SASL,0},
#ifdef USE_OPENSSL
	{ 0, "irc.techmansworld.com/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.techmansworld.com/6667", 0, 0, 0, 0 },

	{"TinyCrab", 0, 0, 0, LOGIN_SASL, 0},
	{ 0, "irc.tinycrab.net", 0, 0, 0, 0 },

	{ "TURLINet", 0, 0, 0, 0, 0 },
	{ 0, "irc.turli.net", 0, 0, 0, 0 },
	{ 0, "irc.servx.ru", 0, 0, 0, 0 },
	{ 0, "irc.gavnos.ru", 0, 0, 0, 0 },

	{"UnderNet", 0, 0, 0, LOGIN_CUSTOM, "MSG x@channels.undernet.org login %u %p"},
	{ 0, "us.undernet.org", 0, 0, 0, 0 },

	{"UniBG", 0, 0, 0, LOGIN_CUSTOM, "MSG NS IDENTIFY %p"},
	{ 0, "irc.lirex.com", 0, 0, 0, 0 },
	{ 0, "irc.naturella.com", 0, 0, 0, 0 },
	{ 0, "irc.techno-link.com", 0, 0, 0, 0 },
	
	{"ValleyNode", 0, 0, 0, LOGIN_SASL,0},
	{ 0, "irc.valleynode.net", 0, 0, 0, 0 },

	{ "Worldnet", 0, 0, 0, 0, 0 },
	{ 0, "irc.worldnet.net", 0, 0, 0, 0 },

	{ "Windfyre", 0, 0, 0, 0, 0 },
#ifdef USE_OPENSSL
	{ 0, "irc.windfyre.net/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.windfyre.net", 0, 0, 0, 0 },

	{"Xertion", 0, 0, 0, LOGIN_SASL, 0},
#ifdef USE_OPENSSL
	{ 0, "irc.xertion.org/+6697", 0, 0, 0, 0 },
#endif
	{ 0, "irc.xertion.org", 0, 0, 0, 0 },

	{ 0, 0, 0, 0, 0, 0 }
};

GSList *network_list = nullptr;

#if !GLIB_CHECK_VERSION(2,34,0)
#define g_slist_copy_deep servlist_slist_copy_deep
/* FIXME copy-paste from gslist.c, should be dumped sometime */
static GSList*
servlist_slist_copy_deep (GSList *list, GCopyFunc func, gpointer user_data)
{
  GSList *new_list = nullptr;

  if (list)
	{
	  GSList *last;

	  new_list = g_slice_new (GSList);
	  if (func)
		new_list->data = func (list->data, user_data);
	  else
		new_list->data = list->data;
	  last = new_list;
	  list = list->next;
	  while (list)
		{
		  last->next = g_slice_new (GSList);
		  last = last->next;
		  if (func)
			last->data = func (list->data, user_data);
		  else
			last->data = list->data;
		  list = list->next;
		}
	  last->next = nullptr;
	}

  return new_list;
}
#endif

favchannel *
servlist_favchan_copy (favchannel *fav)
{
	if (!fav) return nullptr;
	return new favchannel(*fav);
}

void
servlist_connect (session *sess, ircnet &net, bool join)
{
	ircserver *ircserv;
	GSList *list;
	char *port;
	server *serv;

	if (!sess)
		sess = new_ircwindow(nullptr, nullptr, session::SESS_SERVER, true);

	serv = sess->server;

	/* connect to the currently selected Server-row */
	list = g_slist_nth (net.servlist, net.selected);
	if (!list)
		list = net.servlist;
	if (!list)
		return;
	ircserv = static_cast<ircserver*>(list->data);

	/* in case a protocol switch is added to the servlist gui */
	server_fill_her_up (*sess->server);

	if (join)
	{
		sess->willjoinchannel[0] = 0;

		if (net.favchanlist)
		{
			if (serv->favlist)
			{
				g_slist_free_full (serv->favlist, (GDestroyNotify) servlist_favchan_free);
			}
			serv->favlist = g_slist_copy_deep (net.favchanlist, (GCopyFunc) servlist_favchan_copy, nullptr);
		}
	}

	if (net.logintype)
	{
		serv->loginmethod = net.logintype;
	}
	else
	{
		serv->loginmethod = LOGIN_DEFAULT_REAL;
	}

	serv->password[0] = 0;

	if (net.pass)
	{
		safe_strcpy (serv->password, net.pass, sizeof (serv->password));
	}

	if (net.flags & FLAG_USE_GLOBAL)
	{
		strcpy (serv->nick, prefs.hex_irc_nick1);
	}
	else
	{
		if (net.nick)
			safe_strcpy(serv->nick, net.nick);
	}

	serv->dont_use_proxy = (net.flags & FLAG_USE_PROXY) ? false : true;

#ifdef USE_OPENSSL
	serv->use_ssl = (net.flags & FLAG_USE_SSL) ? true : false;
	serv->accept_invalid_cert =
		(net.flags & FLAG_ALLOW_INVALID) ? true : false;
#endif

	serv->network = &net;

	port = strrchr (ircserv->hostname, '/');
	if (port)
	{
		*port = 0;

		/* support "+port" to indicate SSL (like mIRC does) */
		if (port[1] == '+')
		{
#ifdef USE_OPENSSL
			serv->use_ssl = true;
#endif
			serv->connect (ircserv->hostname, atoi (port + 2), false);
		} else
		{
			serv->connect (ircserv->hostname, atoi (port + 1), false);
		}

		*port = '/';
	} else
		serv->connect (ircserv->hostname, -1, false);

	serv->set_encoding (net.encoding);
}

bool servlist_connect_by_netname (session *sess, char *network, bool join)
{
	for(auto & net : glib_helper::glist_iterable<ircnet>(network_list))
	{
		if (g_ascii_strcasecmp (net.name.c_str(), network) == 0)
		{
			servlist_connect (sess, net, join);
			return true;
		}
	}

	return false;
}

bool servlist_have_auto (void)
{
	for(const auto& net : glib_helper::glist_iterable<ircnet>(network_list))
	{
		if (net.flags & FLAG_AUTO_CONNECT)
			return true;
	}

	return false;
}

bool servlist_auto_connect (session *sess)
{
	bool ret = false;

	for (auto & net : glib_helper::glist_iterable<ircnet>(network_list))
	{
		if (net.flags & FLAG_AUTO_CONNECT)
		{
			servlist_connect (sess, net, true);
			ret = true;
		}
	}

	return ret;
}

static gint
servlist_cycle_cb (server *serv)
{
	if (serv->network)
	{
		PrintTextf (serv->server_session,
			_("Cycling to next server in %s...\n"), serv->network->name.c_str());
		servlist_connect(serv->server_session, *serv->network, true);
	}

	return 0;
}

bool servlist_cycle (server *serv)
{
	auto net = serv->network;
	if (!net)
	{
		return false;
	}

	int max = g_slist_length(net->servlist);
	if (max <= 0)
	{
		return false;
	}

	/* try the next server, if that option is on */
	if (net->flags & FLAG_CYCLE)
	{
		net->selected++;
		if (net->selected >= max)
			net->selected = 0;
	}

	int del = prefs.hex_net_reconnect_delay * 1000;
	if (del < 1000)
		del = 500;				  /* so it doesn't block the gui */

	if (del)
		serv->recondelay_tag = fe_timeout_add(del, (GSourceFunc)servlist_cycle_cb, serv);
	else
		servlist_connect(serv->server_session, *net, true);

	return true;
}

ircserver *
servlist_server_find (ircnet *net, const char name[], int *pos)
{
	GSList *list = net->servlist;
	ircserver *serv;
	int i = 0;

	while (list)
	{
		serv = static_cast<ircserver*>(list->data);
		if (strcmp (serv->hostname, name) == 0)
		{
			if (pos)
			{
				*pos = i;
			}
			return serv;
		}
		i++;
		list = list->next;
	}

	return nullptr;
}

favchannel *
servlist_favchan_find (ircnet *net, const std::string& channel, int *pos)
{
	if (net == nullptr)
		return nullptr;

	GSList *list = net->favchanlist;
	int i = 0;

	while (list)
	{
		auto favchan = static_cast<favchannel*>(list->data);
		if (g_ascii_strcasecmp (favchan->name.c_str(), channel.c_str()) == 0)
		{
			if (pos)
			{
				*pos = i;
			}
			return favchan;
		}
		i++;
		list = list->next;
	}

	return nullptr;
}

commandentry *
servlist_command_find (ircnet *net, char *cmd, int *pos)
{
	GSList *list = net->commandlist;
	commandentry *entry;
	int i = 0;

	while (list)
	{
		entry = static_cast<commandentry*>(list->data);
		if (entry->command == cmd)
		{
			if (pos)
			{
				*pos = i;
			}
			return entry;
		}
		i++;
		list = list->next;
	}

	return nullptr;
}

/* find a network (e.g. (ircnet *) to "FreeNode") from a hostname
   (e.g. "irc.eu.freenode.net") */

ircnet *servlist_net_find_from_server (char *server_name)
{
	for(auto & net : glib_helper::glist_iterable<ircnet>(network_list))
	{
		for(const auto & serv : glib_helper::glist_iterable<ircserver>(net.servlist))
		{
			if (g_ascii_strcasecmp (serv.hostname, server_name) == 0)
				return &net;
		}
	}

	return nullptr;
}

ircnet *
servlist_net_find (char *name, int *pos, int (*cmpfunc) (const char *, const char *))
{
	GSList *list = network_list;
	ircnet *net;
	int i = 0;

	while (list)
	{
		net = static_cast<ircnet *>(list->data);
		if (cmpfunc (net->name.c_str(), name) == 0)
		{
			if (pos)
				*pos = i;
			return net;
		}
		i++;
		list = list->next;
	}

	return nullptr;
}

ircserver *servlist_server_add (ircnet *net, const char *name)
{
	auto serv = new ircserver();
	serv->hostname = strdup (name);

	net->servlist = g_slist_append (net->servlist, serv);

	return serv;
}

commandentry *servlist_command_add (ircnet *net, const char *cmd)
{
	commandentry *entry = new commandentry();
	entry->command = cmd;

	net->commandlist = g_slist_append (net->commandlist, entry);

	return entry;
}

GSList *servlist_favchan_listadd (GSList *chanlist, const char *channel, const char *key)
{
	auto chan = new favchannel;
	if (channel)
		chan->name = channel;
	if (key)
		chan->key = key;

	chanlist = g_slist_append (chanlist, chan);

	return chanlist;
}

void
servlist_favchan_add (ircnet *net, const char *channel)
{
	glib_string name;
	glib_string key;
	if (strchr (channel, ',') != nullptr)
	{
		auto pos = (strchr (channel, ',') - channel);
		name.reset(g_strndup (channel, pos));
		key.reset(g_strdup (channel + pos + 1));
	}
	else
	{
		name.reset(g_strdup (channel));
	}

	net->favchanlist = servlist_favchan_listadd (net->favchanlist, name.get(), key.get());
}

void servlist_server_remove (ircnet *net, ircserver *serv)
{
	free (serv->hostname);
	net->servlist = g_slist_remove (net->servlist, serv);
	delete serv;
}

static void
servlist_server_remove_all (ircnet *net)
{
	ircserver *serv;

	while (net->servlist)
	{
		serv = static_cast<ircserver*>(net->servlist->data);
		servlist_server_remove (net, serv);
	}
}

void servlist_command_free (commandentry *entry)
{
	delete entry;
}

void servlist_command_remove (ircnet *net, commandentry *entry)
{
	net->commandlist = g_slist_remove (net->commandlist, entry);
	servlist_command_free(entry);
}

void
servlist_favchan_free (favchannel *channel)
{
	delete channel;
}

void
servlist_favchan_remove (ircnet *net, favchannel *channel)
{
	net->favchanlist = g_slist_remove (net->favchanlist, channel);
	servlist_favchan_free(channel);
}

/* executed on exit: Clear any password strings */
void servlist_cleanup (void)
{
	for (auto & net : glib_helper::glist_iterable<ircnet>(network_list))
	{
		free_and_clear (net.pass);
	}
}

void servlist_net_remove (ircnet *net)
{
	std::unique_ptr<ircnet> net_ptr{ net };
	servlist_server_remove_all (net);
	network_list = g_slist_remove (network_list, net);
}

ircnet *
servlist_net_add (const char *name, const char *comment, int prepend)
{
	ircnet *net = new ircnet();
	net->name = name;
/*	net->comment = strdup (comment);*/
	net->flags = FLAG_CYCLE | FLAG_USE_GLOBAL | FLAG_USE_PROXY;

	if (prepend)
		network_list = g_slist_prepend (network_list, net);
	else
		network_list = g_slist_append (network_list, net);

	return net;
}

static void
servlist_load_defaults (void)
{
	int i = 0, j = 0;
	ircnet *net = nullptr;
	guint def_hash = g_str_hash ("freenode");

	for (;;)
	{
		if (def[i].network)
		{
			net = servlist_net_add (def[i].network, def[i].host, false);
			if (def[i].channel)
			{
				servlist_favchan_add (net, def[i].channel);
			}
			if (def[i].charset)
			{
				net->encoding = g_strdup (def[i].charset);
			}
			else
			{
				net->encoding = g_strdup (IRC_DEFAULT_CHARSET);
			}
			if (def[i].loginmode)
			{
				net->logintype = def[i].loginmode;
			}
			if (def[i].connectcmd)
			{
				servlist_command_add (net, def[i].connectcmd);
			}

			if (g_str_hash (def[i].network) == def_hash)
			{
				prefs.hex_gui_slist_select = j;
			}

			j++;
		}
		else
		{
			servlist_server_add (net, def[i].host);
			if (!def[i+1].host && !def[i+1].network)
			{
				break;
			}
		}
		i++;
	}
}

static bool servlist_load (void)
{
	namespace bfs = boost::filesystem;
	namespace bs = boost::system;

	/* simple migration we will keep for a short while */
	auto oldfile = io::fs::make_config_path("servlist_.conf");
	auto newfile = io::fs::make_config_path("servlist.conf");
	
	bs::error_code ec;
	if (bfs::exists(oldfile, ec) && !bfs::exists(newfile, ec))
	{
		bfs::rename(oldfile, newfile, ec);
	}

	bfs::ifstream infile(io::fs::make_config_path("servlist.conf"), std::ios::in);
	if (!infile)
		return false;

	ircnet *net = nullptr;

	for(std::string buf; std::getline(infile, buf);)
	{
		if (net)
		{
			switch (buf[0])
			{
			case 'I':
				net->nick = strdup (buf.c_str() + 2);
				break;
			case 'i':
				net->nick2 = strdup (buf.c_str() + 2);
				break;
			case 'U':
				net->user = strdup (buf.c_str() + 2);
				break;
			case 'R':
				net->real = strdup (buf.c_str() + 2);
				break;
			case 'P':
				net->pass = strdup (buf.c_str() + 2);
				break;
			case 'L':
				net->logintype = std::atoi (buf.c_str() + 2);
				break;
			case 'E':
				net->encoding = strdup (buf.c_str() + 2);
				break;
			case 'F':
				net->flags = std::atoi (buf.c_str() + 2);
				break;
			case 'S':	/* new server/hostname for this network */
				servlist_server_add (net, buf.c_str() + 2);
				break;
			case 'C':
				servlist_command_add (net, buf.c_str() + 2);
				break;
			case 'J':
				servlist_favchan_add (net, buf.c_str() + 2);
				break;
			case 'D':
				net->selected = std::atoi (buf.c_str() + 2);
				break;
			/* FIXME Migration code. In 2.9.5 the order was:
			 *
			 * P=serverpass, A=saslpass, B=nickservpass
			 *
			 * So if server password was unset, we can safely use SASL
			 * password for our new universal password, or if that's also
			 * unset, use NickServ password.
			 *
			 * Should be removed at some point.
			 */
			case 'A':
				if (!net->pass)
				{
					net->pass = strdup (buf.c_str() + 2);
					if (!net->logintype)
					{
						net->logintype = LOGIN_SASL;
					}
				}
			case 'B':
				if (!net->pass)
				{
					net->pass = strdup (buf.c_str() + 2);
					if (!net->logintype)
					{
						net->logintype = LOGIN_NICKSERV;
					}
				}
			}
		}
		if (!buf.empty() && buf[0] == 'N')
			net = servlist_net_add (buf.c_str() + 2, /* comment */ nullptr, false);
	}

	return true;
}

void
servlist_init (void)
{
	if (!network_list)
		if (!servlist_load ())
			servlist_load_defaults ();
}

/* check if a charset is known by Iconv */
bool servlist_check_encoding(std::string charset)
{
	auto space = charset.find_first_of(' ');
	if (space != std::string::npos)
		charset.resize(space);

	if (!g_ascii_strcasecmp (charset.c_str(), "IRC")) /* special case */
	{
		return true;
	}

	auto gic = g_iconv_open (charset.c_str(), "UTF-8");

	if (gic != (GIConv)-1)
	{
		g_iconv_close (gic);
		return true;
	}

	return false;
}

bool servlist_save (void)
{
	namespace bfs = boost::filesystem;
	namespace bs = boost::system;
	auto outpath = io::fs::make_config_path("servlist.conf");
	
#ifndef WIN32
	bs::error_code ec;
	bool first = !bfs::exists(outpath, ec);
#endif
	bfs::ofstream outfile(outpath, std::ios::out | std::ios::trunc);
	if (!outfile)
	{
		return false;
	}
	
#ifndef WIN32
	if (first)
	{
		bfs::permissions(outpath, bfs::owner_read | bfs::owner_write, ec);
	}
#endif
	outfile << "v=" PACKAGE_VERSION "\n\n";

	for(const auto & net : glib_helper::glist_iterable<ircnet>(network_list))
	{
		outfile << "N=" << net.name << '\n';
		if (net.nick)
			outfile << "I=" << net.nick << '\n';
		if (net.nick2)
			outfile << "i=" << net.nick2 << '\n';
		if (net.user)
			outfile << "U=" << net.user << '\n';
		if (net.real)
			outfile << "R=" << net.real << '\n';
		if (net.pass)
			outfile << "P=" << net.pass << '\n';
		if (net.logintype)
			outfile << "L=" << net.logintype << '\n';
		if (net.encoding && g_ascii_strcasecmp (net.encoding, "System") &&
			 g_ascii_strcasecmp (net.encoding, "System default"))
		{
			outfile << "E=" << net.encoding << '\n';
			if (!servlist_check_encoding (net.encoding))
			{
				std::ostringstream buffer;
				buffer << boost::format(_("Warning: \"%s\" character set is unknown. No conversion will be applied for network %s.")) % net.encoding % net.name;
				fe_message (buffer.str(), FE_MSG_WARN);
			}
		}
		outfile << "F=" << net.flags << "\nD=" << net.selected << '\n';

		for (const auto & serv : glib_helper::glist_iterable<ircserver>(net.servlist))
		{
			outfile << "S=" << serv.hostname << '\n';
		}

		for(const auto & cmd : glib_helper::glist_iterable<commandentry>(net.commandlist))
		{
			outfile << "C=" << cmd.command << '\n';
		}

		for(const auto & favchan : glib_helper::glist_iterable<favchannel>(net.favchanlist))
		{
			outfile << "J=" << favchan.name;
			if (favchan.key)
			{
				outfile << ',' << favchan.key.get();
			}
			outfile << '\n';
		}

		if (!(outfile << '\n'))
		{
			return false;
		}
	}
	return true;
}

static int
joinlist_find_chan (favchannel *curr_item, const char *channel)
{
	if (!g_ascii_strcasecmp (curr_item->name.c_str(), channel))
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

bool joinlist_is_in_list (server *serv,  const char channel[])
{
	if (!serv->network || !serv->network->favchanlist)
	{
		return false;
	}

	return g_slist_find_custom(serv->network->favchanlist, channel, (GCompareFunc)joinlist_find_chan) != nullptr;
}
