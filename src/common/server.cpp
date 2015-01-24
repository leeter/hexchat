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

#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <iterator>
#include <new>
#include <utility>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#include <fcntl.h>
#include <boost/utility/string_ref.hpp>
#include <tcp_connection.hpp>

#define WANTSOCKET
#define WANTARPA
#include "inet.hpp"

#ifdef WIN32
#include <winbase.h>
#include <io.h>
#include <thread>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "hexchat.hpp"
#include "fe.hpp"
#include "cfgfiles.hpp"
#include "network.hpp"
#include "notify.hpp"
#include "hexchatc.hpp"
#include "inbound.hpp"
#include "outbound.hpp"
#include "text.hpp"
#include "util.hpp"
#include "url.hpp"
#include "proto-irc.hpp"
#include "servlist.hpp"
#include "server.hpp"
#include "dcc.hpp"
#include "session.hpp"


namespace dcc = ::hexchat::dcc;

#ifdef USE_OPENSSL
#include <openssl/ssl.h>		  /* SSL_() */
#include <openssl/err.h>		  /* ERR_() */
#include "ssl.hpp"
#endif

#ifdef WIN32
#include "identd.hpp"
#endif

#ifdef USE_LIBPROXY
#include <proxy.h>
#endif

#ifdef USE_OPENSSL
/* local variables */
static struct session *g_sess = nullptr;
#endif

static GSList *away_list = nullptr;
GSList *serv_list = nullptr;

#ifdef USE_LIBPROXY
extern pxProxyFactory *libproxy_factory;
#endif

/* actually send to the socket. This might do a character translation or
   send via SSL. server/dcc both use this function. */

int
tcp_send_real (void *ssl, int sok, const char *encoding, int using_irc, const char *buf, int len, server * serv)
{
	if (!serv->server_connection)
		return 1; // throw?

	int ret = 0;
	glib_string locale;
	gsize loc_len;

	if (encoding == nullptr)	/* system */
	{
		if (!prefs.utf8_locale)
		{
			const gchar *charset;

			g_get_charset(&charset);
			locale.reset(g_convert_with_fallback(buf, len, charset, "UTF-8",
				"?", 0, &loc_len, 0));
		}
	} else
	{
		if (using_irc)	/* using "IRC" encoding (CP1252/UTF-8 hybrid) */
			/* if all chars fit inside CP1252, use that. Otherwise this
			   returns NULL and we send UTF-8. */
			locale.reset(g_convert (buf, len, "CP1252", "UTF-8", 0, &loc_len, 0));
		else
			locale.reset(g_convert_with_fallback (buf, len, encoding, "UTF-8",
														 "?", 0, &loc_len, 0));
	}

	if (locale)
	{
		serv->server_connection->enqueue_message(locale.get());
#if 0
		len = loc_len;
#ifdef USE_OPENSSL
		if (!ssl)
			ret = send (sok, locale, len, 0);
		else
			ret = _SSL_send (static_cast<SSL*>(ssl), locale, len);
#else
		ret = send (sok, locale, len, 0);
#endif
#endif
	} else
	{
		serv->server_connection->enqueue_message(buf);
#if 0
#ifdef USE_OPENSSL
		if (!ssl)
			ret = send (sok, buf, len, 0);
		else
			ret = _SSL_send (static_cast<SSL*>(ssl), buf, len);
#else
		ret = send (sok, buf, len, 0);
#endif
#endif
	}

	return ret;
}

static int
server_send_real (server &serv, const boost::string_ref & buf)
{
	fe_add_rawlog (&serv, buf, true);

	url_check_line(buf.data(), buf.size());

	return tcp_send_real (serv.ssl, serv.sok, serv.encoding ? serv.encoding->c_str() : nullptr, serv.using_irc,
		buf.data(), buf.size(), &serv);
}

/* new throttling system, uses the same method as the Undernet
   ircu2.10 server; under test, a 200-line paste didn't flood
   off the client */

static int
tcp_send_queue (server *serv)
{
	/* did the server close since the timeout was added? */
	if (!is_server (serv))
		return 0;

	const char *p;
	int  i;
	time_t now = time(0);

	/* try priority 2,1,0 */
	while (!serv->outbound_queue.empty())
	{
		auto & top = serv->outbound_queue.top();

		if (serv->next_send < now)
			serv->next_send = now;
		if (serv->next_send - now >= 10)
		{
			/* check for clock skew */
			if (now >= serv->prev_now)
				return 1;		  /* don't remove the timeout handler */
			/* it is skewed, reset to something sane */
			serv->next_send = now;
		}

		for (p = top.second.c_str(), i = top.second.size(); i && *p != ' '; p++, i--){}

		serv->next_send += (2 + i / 120);
		serv->sendq_len -= top.second.size();
		serv->prev_now = now;
		fe_set_throttle (serv);

		server_send_real(*serv, top.second);

		serv->outbound_queue.pop(); // = g_slist_remove (serv->outbound_queue, buf);
	}
	return 0;						  /* remove the timeout handler */
}

int
tcp_send_len (server &serv, const boost::string_ref & buf)
{
	bool noqueue = serv.outbound_queue.empty();

	if (!prefs.hex_net_throttle)
		return server_send_real (serv, buf);

	int priority = 2;	/* pri 2 for most things */

	/* privmsg and notice get a lower priority */
	if (g_ascii_strncasecmp (buf.data() + 1, "PRIVMSG", 7) == 0 ||
		 g_ascii_strncasecmp (buf.data() + 1, "NOTICE", 6) == 0)
	{
		priority = 1;
	}
	else
	{
		/* WHO/MODE get the lowest priority */
		if (g_ascii_strncasecmp(buf.data() + 1, "WHO ", 4) == 0 ||
		/* but only MODE queries, not changes */
		(g_ascii_strncasecmp(buf.data() + 1, "MODE", 4) == 0 &&
			 buf.find_first_of('-') == boost::string_ref::npos &&
			 buf.find_first_of('+') == boost::string_ref::npos))
			priority = 0;
	}

	serv.outbound_queue.emplace(std::make_pair(priority, buf.to_string()));
	serv.sendq_len += buf.size(); /* tcp_send_queue uses strlen */

	if (tcp_send_queue (&serv) && noqueue)
		fe_timeout_add(500, (GSourceFunc)tcp_send_queue, &serv);

	return 1;
}

void
tcp_sendf (server &serv, const char *fmt, ...)
{
	va_list args;
	/* keep this buffer in BSS. Converting UTF-8 to ISO-8859-x might make the
	  string shorter, so allow alot more than 512 for now. */
	static char send_buf[1540];	/* good code hey (no it's not overflowable) */

	va_start (args, fmt);
	auto len = std::vsnprintf (send_buf, sizeof (send_buf) - 1, fmt, args);
	va_end (args);

	send_buf[sizeof (send_buf) - 1] = '\0';
	if (len < 0 || len > (sizeof (send_buf) - 1))
		len = std::strlen (send_buf);

	tcp_send_len(serv, boost::string_ref{ send_buf, static_cast<std::size_t>( len ) });
}

static int
close_socket_cb (gpointer sok)
{
	closesocket (GPOINTER_TO_INT (sok));
	return 0;
}

static void
close_socket (int sok)
{
	/* close the socket in 5 seconds so the QUIT message is not lost */
	fe_timeout_add (5000, close_socket_cb, GINT_TO_POINTER (sok));
}

/* handle 1 line of text received from the server */

static void
server_inline (server *serv, char *line, size_t len)
{
	std::string outline;
	/* Checks whether we're set to use UTF-8 charset */
	if (serv->using_irc ||				/* 1. using CP1252/UTF-8 Hybrid */
		(serv->encoding && prefs.utf8_locale) || /* OR 2. using system default->UTF-8 */
		(!serv->encoding &&				/* OR 3. explicitly set to UTF-8 */
		 (g_ascii_strcasecmp (serv->encoding->c_str(), "UTF8") == 0 ||
		  g_ascii_strcasecmp (serv->encoding->c_str(), "UTF-8") == 0)))
	{
		/* The user has the UTF-8 charset set, either via /charset
		command or from his UTF-8 locale. Thus, we first try the
		UTF-8 charset, and if we fail to convert, we assume
		it to be ISO-8859-1 (see text_validate). */

		outline = text_validate(boost::string_ref{ line, len });

	} else
	{
		/* Since the user has an explicit charset set, either
		via /charset command or from his non-UTF8 locale,
		we don't fallback to ISO-8859-1 and instead try to remove
		errnoeous octets till the string is convertable in the
		said charset. */

		const char *encoding = nullptr;

		if (serv->encoding)
			encoding = serv->encoding->c_str();
		else
			g_get_charset (&encoding);

		if (encoding != nullptr)
		{
			size_t conv_len; /* tells g_convert how much of line to convert */
			gsize utf_len;
			gsize read_len;
			bool retry;

			std::unique_ptr<char[]> conv_line{ new char[len + 1] };
			std::copy_n(line, len, conv_line.get());
			conv_line[len] = 0;
			conv_len = len;

			/* if CP1255, convert it with the NUL terminator.
				Works around SF bug #1122089 */
			if (serv->using_cp1255)
				conv_len++;
			glib_string utf_line_allocated;
			do
			{
				GError *err = nullptr;
				retry = false;
				utf_line_allocated.reset(g_convert_with_fallback (conv_line.get(), conv_len, "UTF-8", encoding, "?", &read_len, &utf_len, &err));
				if (err != nullptr)
				{
					std::unique_ptr<GError, decltype(&g_error_free)> err_ptr(err, g_error_free);
					if (err->code == G_CONVERT_ERROR_ILLEGAL_SEQUENCE && conv_len > (read_len + 1))
					{
						/* Make our best bet by removing the erroneous char.
						   This will work for casual 8-bit strings with non-standard chars. */
						std::memmove (conv_line.get() + read_len, conv_line.get() + read_len + 1, conv_len - read_len -1);
						conv_len--;
						retry = true;
					}
				}
			} while (retry);

			/* If any conversion has occured at all. Conversion might fail
			due to errors other than invalid sequences, e.g. unknown charset. */
			if (utf_line_allocated != nullptr)
			{
				if (serv->using_cp1255 && utf_len > 0)
					utf_len--;
				outline = std::string{ utf_line_allocated.get(), utf_len };
			}
			else
			{
				/* If all fails, treat as UTF-8 with fallback to ISO-8859-1. */
				outline = text_validate(boost::string_ref{ line, len });
			}
		}
	}

	fe_add_rawlog(serv, outline, false);

	/* let proto-irc.c handle it */
	serv->p_inline (outline);
}

/* read data from socket */
static void 
server_read_cb(server * serv, const std::string & message, size_t length)
{
	for (int i = 0; i < length; ++i)
	{
		switch (message[i])
		{
		case '\0':
		case '\r':
			break;

		case '\n':
			serv->linebuf[serv->pos] = 0;
			server_inline(serv, serv->linebuf, serv->pos);
			serv->pos = 0;
			break;

		default:
			serv->linebuf[serv->pos] = message[i];
			if (serv->pos >= (sizeof(serv->linebuf) - 1))
				fprintf(stderr,
				"*** HEXCHAT WARNING: Buffer overflow - shit server!\n");
			else
				serv->pos++;
		}
	}
}

static gboolean
server_read (GIOChannel *source, GIOCondition condition, server *serv)
{
	int sok = serv->sok;
	int error, i, len;
	char lbuf[2050];

	while (1)
	{
#ifdef USE_OPENSSL
		if (!serv->ssl)
#endif
			len = recv (sok, lbuf, sizeof (lbuf) - 2, 0);
#ifdef USE_OPENSSL
		else
			len = io::ssl::_SSL_recv (serv->ssl, lbuf, sizeof (lbuf) - 2);
#endif
		if (len < 1)
		{
			error = 0;
			if (len < 0)
			{
				if (would_block ())
					return TRUE;
				error = sock_error ();
			}
			if (!serv->end_of_motd)
			{
				serv->disconnect (serv->server_session, false, error);
				if (!servlist_cycle (serv))
				{
					if (prefs.hex_net_auto_reconnect)
						serv->auto_reconnect (false, error);
				}
			} else
			{
				if (prefs.hex_net_auto_reconnect)
					serv->auto_reconnect (false, error);
				else
					serv->disconnect (serv->server_session, false, error);
			}
			return TRUE;
		}

		i = 0;

		lbuf[len] = 0;

		while (i < len)
		{
			switch (lbuf[i])
			{
			case '\r':
				break;

			case '\n':
				serv->linebuf[serv->pos] = 0;
				server_inline (serv, serv->linebuf, serv->pos);
				serv->pos = 0;
				break;

			default:
				serv->linebuf[serv->pos] = lbuf[i];
				if (serv->pos >= (sizeof (serv->linebuf) - 1))
					fprintf (stderr,
								"*** HEXCHAT WARNING: Buffer overflow - shit server!\n");
				else
					serv->pos++;
			}
			i++;
		}
	}
}

static void
server_connected1(server * serv, const boost::system::error_code & error)
{
	prefs.wait_on_exit = TRUE;
	serv->ping_recv = boost::chrono::steady_clock::now();
	serv->lag_sent = 0;
	serv->connected = true;
	serv->connecting = false;
	if (!serv->no_login)
	{
		EMIT_SIGNAL(XP_TE_CONNECTED, serv->server_session, nullptr, nullptr, nullptr,
			nullptr, 0);
		if (serv->network)
		{
			ircnet* net = serv->network;
			serv->p_login((!(net->flags & FLAG_USE_GLOBAL) &&
				(net->user)) ?
				(net->user) :
				prefs.hex_irc_user_name,
				(!(net->flags & FLAG_USE_GLOBAL) &&
				(net->real)) ?
				(net->real) :
				prefs.hex_irc_real_name);
		}
		else
		{
			serv->p_login(prefs.hex_irc_user_name, prefs.hex_irc_real_name);
		}
	}
	else
	{
		EMIT_SIGNAL(XP_TE_SERVERCONNECTED, serv->server_session, nullptr, nullptr,
			nullptr, nullptr, 0);
	}

	serv->set_name(serv->servername);
	fe_server_event(serv, fe_serverevents::CONNECT, 0);
}

static void
server_connected (server * serv)
{
	prefs.wait_on_exit = TRUE;
	serv->ping_recv = boost::chrono::steady_clock::now();
	serv->lag_sent = 0;
	serv->connected = true;
	set_nonblocking (serv->sok);
	serv->iotag = fe_input_add(serv->sok, FIA_READ | FIA_EX, (GIOFunc)server_read, serv);
	if (!serv->no_login)
	{
		EMIT_SIGNAL (XP_TE_CONNECTED, serv->server_session, nullptr, nullptr, nullptr,
						 nullptr, 0);
		if (serv->network)
		{
			ircnet* net = serv->network;
			serv->p_login (	(!(net->flags & FLAG_USE_GLOBAL) &&
								 (net->user)) ?
								(net->user) :
								prefs.hex_irc_user_name,
								(!(net->flags & FLAG_USE_GLOBAL) &&
								 (net->real)) ?
								(net->real) :
								prefs.hex_irc_real_name);
		} else
		{
			serv->p_login (prefs.hex_irc_user_name, prefs.hex_irc_real_name);
		}
	} else
	{
		EMIT_SIGNAL (XP_TE_SERVERCONNECTED, serv->server_session, nullptr, nullptr,
						 nullptr, nullptr, 0);
	}

	serv->set_name (serv->servername);
	fe_server_event(serv, fe_serverevents::CONNECT, 0);
}

#ifdef WIN32

static gboolean
server_close_pipe (int *pipefd)	/* see comments below */
{
//	close (pipefd[0]);	/* close WRITE end first to cause an EOF on READ */
//	close (pipefd[1]);	/* in giowin32, and end that thread. */
	free (pipefd);
	return FALSE;
}

#endif

static void
server_stopconnecting (server * serv)
{
	if (serv->iotag)
	{
		fe_input_remove (serv->iotag);
		serv->iotag = 0;
	}

	if (serv->joindelay_tag)
	{
		fe_timeout_remove (serv->joindelay_tag);
		serv->joindelay_tag = 0;
	}

#ifndef WIN32
	/* kill the child process trying to connect */
	kill (serv->childpid, SIGKILL);
	waitpid (serv->childpid, nullptr, 0);

	close (serv->childwrite);
	close (serv->childread);
#else
	//PostThreadMessageW (serv->childpid, WM_QUIT, 0, 0);

	//{
	//	/* if we close the pipe now, giowin32 will crash. */
	//	int *pipefd = static_cast<int*>(malloc (sizeof (int) * 2));
	//	if (!pipefd)
	//		std::terminate();
	//	pipefd[0] = serv->childwrite;
	//	pipefd[1] = serv->childread;
	//	g_idle_add ((GSourceFunc)server_close_pipe, pipefd);
	//}
#endif

#ifdef USE_OPENSSL
	if (serv->ssl_do_connect_tag)
	{
		fe_timeout_remove (serv->ssl_do_connect_tag);
		serv->ssl_do_connect_tag = 0;
	}
#endif

	fe_progressbar_end (serv);

	serv->connecting = false;
	fe_server_event(serv, fe_serverevents::DISCONNECT, 0);
}

#ifdef USE_OPENSSL
#define	SSLTMOUT	90				  /* seconds */
static void
ssl_cb_info (const SSL * s, int where, int ret)
{
/*	char buf[128];*/


	return;							  /* FIXME: make debug level adjustable in serverlist or settings */

/*	snprintf (buf, sizeof (buf), "%s (%d)", SSL_state_string_long (s), where);
	if (g_sess)
		EMIT_SIGNAL (XP_TE_SSLMESSAGE, g_sess, buf, NULL, NULL, NULL, 0);
	else
		fprintf (stderr, "%s\n", buf);*/
}

static void
ssl_print_cert_info(server *serv, const SSL* ctx)
{
	char buf[512];
	io::ssl::cert_info cert_info = { 0 };
	int verify_error;

	if (!io::ssl::get_cert_info(cert_info, ctx))
	{
		snprintf(buf, sizeof(buf), "* Certification info:");
		EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);
		snprintf(buf, sizeof(buf), "  Subject:");
		EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);
		for (int i = 0; cert_info.subject_word[i]; i++)
		{
			snprintf(buf, sizeof(buf), "    %s", cert_info.subject_word[i]);
			EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
				nullptr, 0);
		}
		snprintf(buf, sizeof(buf), "  Issuer:");
		EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);
		for (int i = 0; cert_info.issuer_word[i]; i++)
		{
			snprintf(buf, sizeof(buf), "    %s", cert_info.issuer_word[i]);
			EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
				nullptr, 0);
		}
		snprintf(buf, sizeof(buf), "  Public key algorithm: %s (%d bits)",
			cert_info.algorithm, cert_info.algorithm_bits);
		EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);
		/*if (cert_info.rsa_tmp_bits)
		{
		snprintf (buf, sizeof (buf),
		"  Public key algorithm uses ephemeral key with %d bits",
		cert_info.rsa_tmp_bits);
		EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, NULL, NULL,
		NULL, 0);
		}*/
		snprintf(buf, sizeof(buf), "  Sign algorithm %s",
			cert_info.sign_algorithm/*, cert_info.sign_algorithm_bits*/);
		EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);
		snprintf(buf, sizeof(buf), "  Valid since %s to %s",
			cert_info.notbefore, cert_info.notafter);
		EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);
	}

	auto info = io::ssl::get_cipher_info(ctx);	/* static buffer */
	snprintf(buf, sizeof(buf), "* Cipher info:");
	EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr, nullptr,
		0);
	snprintf(buf, sizeof(buf), "  Version: %s, cipher %s (%u bits)",
		info.version.c_str(), info.cipher.c_str(),
		info.cipher_bits);
	EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr, nullptr,
		0);

	verify_error = SSL_get_verify_result(ctx);
	switch (verify_error)
	{
	case X509_V_OK:
		/* snprintf (buf, sizeof (buf), "* Verify OK (?)"); */
		/* EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, NULL, NULL, NULL, 0); */
		break;
	case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
	case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
	case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
	case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
	case X509_V_ERR_CERT_HAS_EXPIRED:
		if (serv->accept_invalid_cert)
		{
			snprintf(buf, sizeof(buf), "* Verify E: %s.? (%d) -- Ignored",
				X509_verify_cert_error_string(verify_error),
				verify_error);
			EMIT_SIGNAL(XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
				nullptr, 0);
			break;
		}
	default:
		break;
		/*snprintf(buf, sizeof(buf), "%s.? (%d)",
			X509_verify_cert_error_string(verify_error),
			verify_error);
		EMIT_SIGNAL(XP_TE_CONNFAIL, serv->server_session, buf, nullptr, nullptr,
			nullptr, 0);

		serv->cleanup();*/
	}
}

static int
ssl_cb_verify (int ok, X509_STORE_CTX * ctx)
{
	char subject[256];
	char issuer[256];
	char buf[512];


	X509_NAME_oneline (X509_get_subject_name (ctx->current_cert), subject,
							 sizeof (subject));
	X509_NAME_oneline (X509_get_issuer_name (ctx->current_cert), issuer,
							 sizeof (issuer));

	snprintf (buf, sizeof (buf), "* Subject: %s", subject);
	EMIT_SIGNAL (XP_TE_SSLMESSAGE, g_sess, buf, nullptr, nullptr, nullptr, 0);
	snprintf (buf, sizeof (buf), "* Issuer: %s", issuer);
	EMIT_SIGNAL (XP_TE_SSLMESSAGE, g_sess, buf, nullptr, nullptr, nullptr, 0);

	return (TRUE);					  /* always ok */
}

static int
ssl_do_connect (server * serv)
{
	char buf[128];

	g_sess = serv->server_session;
	if (SSL_connect (serv->ssl) <= 0)
	{
		char err_buf[128];
		int err;

		g_sess = nullptr;
		if ((err = ERR_get_error ()) > 0)
		{
			ERR_error_string (err, err_buf);
			snprintf (buf, sizeof (buf), "(%d) %s", err, err_buf);
			EMIT_SIGNAL (XP_TE_CONNFAIL, serv->server_session, buf, nullptr,
							 nullptr, nullptr, 0);

			if (ERR_GET_REASON (err) == SSL_R_WRONG_VERSION_NUMBER)
				PrintText (serv->server_session, _("Are you sure this is a SSL capable server and port?\n"));

			serv->cleanup();

			if (prefs.hex_net_auto_reconnectonfail)
				serv->auto_reconnect (false, -1);

			return (0);				  /* remove it (0) */
		}
	}
	g_sess = nullptr;

	if (SSL_is_init_finished (serv->ssl))
	{
		io::ssl::cert_info cert_info = { 0 };
		int verify_error;

		if (!io::ssl::get_cert_info (cert_info, serv->ssl))
		{
			snprintf (buf, sizeof (buf), "* Certification info:");
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
			snprintf (buf, sizeof (buf), "  Subject:");
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
			for (int i = 0; cert_info.subject_word[i]; i++)
			{
				snprintf (buf, sizeof (buf), "    %s", cert_info.subject_word[i]);
				EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
								 nullptr, 0);
			}
			snprintf (buf, sizeof (buf), "  Issuer:");
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
			for (int i = 0; cert_info.issuer_word[i]; i++)
			{
				snprintf (buf, sizeof (buf), "    %s", cert_info.issuer_word[i]);
				EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
								 nullptr, 0);
			}
			snprintf (buf, sizeof (buf), "  Public key algorithm: %s (%d bits)",
						 cert_info.algorithm, cert_info.algorithm_bits);
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
			/*if (cert_info.rsa_tmp_bits)
			{
				snprintf (buf, sizeof (buf),
							 "  Public key algorithm uses ephemeral key with %d bits",
							 cert_info.rsa_tmp_bits);
				EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, NULL, NULL,
								 NULL, 0);
			}*/
			snprintf (buf, sizeof (buf), "  Sign algorithm %s",
						 cert_info.sign_algorithm/*, cert_info.sign_algorithm_bits*/);
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
			snprintf (buf, sizeof (buf), "  Valid since %s to %s",
						 cert_info.notbefore, cert_info.notafter);
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
		} else
		{
			snprintf (buf, sizeof (buf), " * No Certificate");
			EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);
		}

		auto info = io::ssl::get_cipher_info (serv->ssl);	/* static buffer */
		snprintf (buf, sizeof (buf), "* Cipher info:");
		EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr, nullptr,
						 0);
		snprintf (buf, sizeof (buf), "  Version: %s, cipher %s (%u bits)",
					 info.version.c_str(), info.cipher.c_str(),
					 info.cipher_bits);
		EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr, nullptr,
						 0);

		verify_error = SSL_get_verify_result (serv->ssl);
		switch (verify_error)
		{
		case X509_V_OK:
			/* snprintf (buf, sizeof (buf), "* Verify OK (?)"); */
			/* EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, NULL, NULL, NULL, 0); */
			break;
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
		case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
		case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
		case X509_V_ERR_CERT_HAS_EXPIRED:
			if (serv->accept_invalid_cert)
			{
				snprintf (buf, sizeof (buf), "* Verify E: %s.? (%d) -- Ignored",
							 X509_verify_cert_error_string (verify_error),
							 verify_error);
				EMIT_SIGNAL (XP_TE_SSLMESSAGE, serv->server_session, buf, nullptr, nullptr,
								 nullptr, 0);
				break;
			}
		default:
			snprintf (buf, sizeof (buf), "%s.? (%d)",
						 X509_verify_cert_error_string (verify_error),
						 verify_error);
			EMIT_SIGNAL (XP_TE_CONNFAIL, serv->server_session, buf, nullptr, nullptr,
							 nullptr, 0);

			serv->cleanup ();

			return (0);
		}

		server_stopconnecting (serv);

		/* activate gtk poll */
		server_connected (serv);

		return (0);					  /* remove it (0) */
	} else
	{
		if (serv->ssl->session && serv->ssl->session->time + SSLTMOUT < time (nullptr))
		{
			snprintf (buf, sizeof (buf), "SSL handshake timed out");
			EMIT_SIGNAL (XP_TE_CONNFAIL, serv->server_session, buf, nullptr,
							 nullptr, nullptr, 0);
			serv->cleanup (); /* ->connecting = FALSE */

			if (prefs.hex_net_auto_reconnectonfail)
				serv->auto_reconnect (false, -1);

			return (0);				  /* remove it (0) */
		}

		return (1);					  /* call it more (1) */
	}
}
#endif

static int
timeout_auto_reconnect (server *serv)
{
	if (is_server (serv))  /* make sure it hasnt been closed during the delay */
	{
		serv->recondelay_tag = 0;
		if (!serv->connected && !serv->connecting && serv->server_session)
		{
			serv->connect (serv->hostname, serv->port, false);
		}
	}
	return 0;			  /* returning 0 should remove the timeout handler */
}

void
server::auto_reconnect (bool send_quit, int err)
{
	session *s;
	GSList *list;
	int del;

	if (this->server_session == nullptr)
		return;

	list = sess_list;
	while (list)				  /* make sure auto rejoin can work */
	{
		s = static_cast<session*>(list->data);
		if (s->type == session::SESS_CHANNEL && s->channel[0])
		{
			strcpy (s->waitchannel, s->channel);
			strcpy (s->willjoinchannel, s->channel);
		}
		list = list->next;
	}

	if (this->connected)
		this->disconnect (this->server_session, send_quit, err);

	del = prefs.hex_net_reconnect_delay * 1000;
	if (del < 1000)
		del = 500;				  /* so it doesn't block the gui */

#ifndef WIN32
	if (err == -1 || err == 0 || err == ECONNRESET || err == ETIMEDOUT)
#else
	if (err == -1 || err == 0 || err == WSAECONNRESET || err == WSAETIMEDOUT)
#endif
		this->reconnect_away = this->is_away;

	/* is this server in a reconnect delay? remove it! */
	if (this->recondelay_tag)
	{
		fe_timeout_remove (this->recondelay_tag);
		this->recondelay_tag = 0;
	}

	this->recondelay_tag = fe_timeout_add(del, (GSourceFunc)timeout_auto_reconnect, this);
	fe_server_event(this, fe_serverevents::RECONDELAY, del);
}

void
server::flush_queue ()
{
	decltype(this->outbound_queue) empty;
	std::swap(this->outbound_queue, empty);
	this->sendq_len = 0;
	fe_set_throttle (this);
}

boost::optional<session&> 
server::find_channel(const boost::string_ref &chan)
{
	session *sess;
	GSList *list = sess_list;
	while (list)
	{
		sess = static_cast<session*>(list->data);
		if ((this == sess->server) && sess->type == session::SESS_CHANNEL)
		{
			if (!this->p_cmp(chan.data(), sess->channel))
				return *sess;
		}
		list = list->next;
	}
	return boost::none;
}

/* connect() successed */

//static void
//server_connect_success (server *serv)
//{
//#ifdef USE_OPENSSL
//#define	SSLDOCONNTMOUT	300
//	if (serv->use_ssl)
//	{
//		char *err;
//
//		/* it'll be a memory leak, if connection isn't terminated by
//		   server_cleanup() */
//		serv->ssl = io::ssl::_SSL_socket (ctx, serv->sok);
//		if ((err = io::ssl::_SSL_set_verify (ctx, ssl_cb_verify, nullptr)))
//		{
//			EMIT_SIGNAL (XP_TE_CONNFAIL, serv->server_session, err, nullptr,
//							 nullptr, nullptr, 0);
//			serv->cleanup ();	/* ->connecting = FALSE */
//			return;
//		}
//		/* FIXME: it'll be needed by new servers */
//		/* send(serv->sok, "STLS\r\n", 6, 0); sleep(1); */
//		set_nonblocking (serv->sok);
//		serv->ssl_do_connect_tag = fe_timeout_add (SSLDOCONNTMOUT,
//																 (GSourceFunc)ssl_do_connect, serv);
//		return;
//	}
//
//	serv->ssl = nullptr;
//#endif
//	server_stopconnecting (serv);	/* ->connecting = FALSE */
//	/* activate glib poll */
//	server_connected (serv);
//}

/* receive info from the child-process about connection progress */

//static gboolean
//server_read_child (GIOChannel *source, GIOCondition condition, server *serv)
//{
//	session *sess = serv->server_session;
//	char tbuf[128];
//	char outbuf[512];
//	char host[100];
//	char ip[100];
//#ifdef USE_MSPROXY
//	char *p;
//#endif
//
//	waitline2 (source, tbuf, sizeof tbuf);
//
//	switch (tbuf[0])
//	{
//	case '0':	/* print some text */
//		waitline2 (source, tbuf, sizeof tbuf);
//		PrintText (serv->server_session, tbuf);
//		break;
//	case '1':						  /* unknown host */
//		server_stopconnecting (serv);
//		closesocket (serv->sok4);
//		if (serv->proxy_sok4 != -1)
//			closesocket (serv->proxy_sok4);
//#ifdef USE_IPV6
//		if (serv->sok6 != -1)
//			closesocket (serv->sok6);
//		if (serv->proxy_sok6 != -1)
//			closesocket (serv->proxy_sok6);
//#endif
//		EMIT_SIGNAL (XP_TE_UKNHOST, sess, nullptr, nullptr, nullptr, nullptr, 0);
//		if (!servlist_cycle (serv))
//			if (prefs.hex_net_auto_reconnectonfail)
//				serv->auto_reconnect (false, -1);
//		break;
//	case '2':						  /* connection failed */
//		waitline2 (source, tbuf, sizeof tbuf);
//		server_stopconnecting (serv);
//		closesocket (serv->sok4);
//		if (serv->proxy_sok4 != -1)
//			closesocket (serv->proxy_sok4);
//#ifdef USE_IPV6
//		if (serv->sok6 != -1)
//			closesocket (serv->sok6);
//		if (serv->proxy_sok6 != -1)
//			closesocket (serv->proxy_sok6);
//#endif
//		EMIT_SIGNAL (XP_TE_CONNFAIL, sess, errorstring (atoi (tbuf)), nullptr,
//						 nullptr, nullptr, 0);
//		if (!servlist_cycle (serv))
//			if (prefs.hex_net_auto_reconnectonfail)
//				serv->auto_reconnect (false, -1);
//		break;
//	case '3':						  /* gethostbyname finished */
//		waitline2 (source, host, sizeof host);
//		waitline2 (source, ip, sizeof ip);
//		waitline2 (source, outbuf, sizeof outbuf);
//		EMIT_SIGNAL (XP_TE_CONNECT, sess, host, ip, outbuf, nullptr, 0);
//#ifdef WIN32
//		if (prefs.hex_identd)
//		{
//			if (serv->network && serv->network->user)
//			{
//				identd_start (serv->network->user);
//			}
//			else
//			{
//				identd_start (prefs.hex_irc_user_name);
//			}
//		}
//#else
//		snprintf (outbuf, sizeof (outbuf), "%s/auth/xchat_auth",
//					 g_get_home_dir ());
//		if (access (outbuf, X_OK) == 0)
//		{
//			snprintf (outbuf, sizeof (outbuf), "exec -d %s/auth/xchat_auth %s",
//						 g_get_home_dir (), prefs.hex_irc_user_name);
//			handle_command (serv->server_session, outbuf, FALSE);
//		}
//#endif
//		break;
//	case '4':						  /* success */
//		waitline2 (source, tbuf, sizeof (tbuf));
//		serv->sok = atoi (tbuf);
//#ifdef USE_IPV6
//		/* close the one we didn't end up using */
//		if (serv->sok == serv->sok4)
//			closesocket (serv->sok6);
//		else
//			closesocket (serv->sok4);
//		if (serv->proxy_sok != -1)
//		{
//			if (serv->proxy_sok == serv->proxy_sok4)
//				closesocket (serv->proxy_sok6);
//			else
//				closesocket (serv->proxy_sok4);
//		}
//#endif
//		server_connect_success (serv);
//		break;
//	case '5':						  /* prefs ip discovered */
//		waitline2 (source, tbuf, sizeof tbuf);
//		prefs.local_ip = inet_addr (tbuf);
//		break;
//	case '7':						  /* gethostbyname (prefs.hex_net_bind_host) failed */
//		sprintf (outbuf,
//					_("Cannot resolve hostname %s\nCheck your IP Settings!\n"),
//					prefs.hex_net_bind_host);
//		PrintText (sess, outbuf);
//		break;
//	case '8':
//		PrintText (sess, _("Proxy traversal failed.\n"));
//		serv->disconnect (sess, false, -1);
//		break;
//	case '9':
//		waitline2 (source, tbuf, sizeof tbuf);
//		EMIT_SIGNAL (XP_TE_SERVERLOOKUP, sess, tbuf, nullptr, nullptr, nullptr, 0);
//		break;
//	}
//
//	return TRUE;
//}

/* kill all sockets & iotags of a server. Stop a connection attempt, or
   disconnect if already connected. */

server::cleanup_result
server::cleanup ()
{
	fe_set_lag (this, 0);

	if (this->death_timer)
	{
		fe_timeout_remove(this->death_timer);
		this->death_timer = 0;
	}

	if (this->iotag)
	{
		fe_timeout_remove(this->iotag);
		//fe_input_remove (this->iotag);
		this->iotag = 0;
	}

	if (this->joindelay_tag)
	{
		fe_timeout_remove (this->joindelay_tag);
		this->joindelay_tag = 0;
	}

//#ifdef USE_OPENSSL
//	if (this->ssl)
//	{
//		SSL_shutdown (this->ssl);
//		SSL_free (this->ssl);
//		this->ssl = nullptr;
//	}
//#endif

	if (this->connecting)
	{
		server_stopconnecting (this);
		/*closesocket (this->sok4);
		if (this->proxy_sok4 != -1)
			closesocket (this->proxy_sok4);
		if (this->sok6 != -1)
			closesocket (this->sok6);
		if (this->proxy_sok6 != -1)
			closesocket (this->proxy_sok6);*/
		return cleanup_result::still_connecting;
	}

	if (this->connected)
	{
		/*close_socket (this->sok);
		if (this->proxy_sok)
			close_socket (this->proxy_sok);*/
		this->connected = false;
		this->end_of_motd = false;
		return cleanup_result::connected;
	}

	/* is this server in a reconnect delay? remove it! */
	if (this->recondelay_tag)
	{
		fe_timeout_remove (this->recondelay_tag);
		this->recondelay_tag = 0;
		return cleanup_result::reconnecting;
	}
	if (this->server_connection)
	{
		this->server_connection.reset();
	}

	return cleanup_result::not_connected;
	}

// if the server isn't dead yet, kill it
static gboolean
server_kill(session *sess)
{
	sess->server->disconnect(sess, false, -1);
	return false;
}

void
server::disconnect (session * sess, bool sendquit, int err)
{
	server *serv = sess->server;
	GSList *list;
	char tbuf[64];
	bool shutup = false;

	/* send our QUIT reason */
	if (sendquit && serv->connected)
	{
		server_sendquit (sess);
		serv->death_timer = fe_timeout_add(500, (GSourceFunc)server_kill, sess);
		return;
	}

	fe_server_event(serv, fe_serverevents::DISCONNECT, 0);

	if (this->server_connection)
	{
		// flush any outgoing messages
		this->server_connection->poll();
	}

	/* close all sockets & io tags */
	switch (serv->cleanup ())
	{
	case cleanup_result::not_connected:							  /* it wasn't even connected! */
		notc_msg (sess);
		return;
	case cleanup_result::still_connecting:							  /* it was in the process of connecting */
		sprintf (tbuf, "%d", sess->server->childpid);
		EMIT_SIGNAL (XP_TE_STOPCONNECT, sess, tbuf, nullptr, nullptr, nullptr, 0);
		return;
	case cleanup_result::reconnecting:
		shutup = true;	/* won't print "disconnected" in channels */
	}

	serv->flush_queue ();

	list = sess_list;
	while (list)
	{
		sess = (struct session *) list->data;
		if (sess->server == serv)
		{
			if (!shutup || sess->type == session::SESS_SERVER)
				/* print "Disconnected" to each window using this server */
				EMIT_SIGNAL (XP_TE_DISCON, sess, errorstring (err), nullptr, nullptr, nullptr, 0);

			if (!sess->channel[0] || sess->type == session::SESS_CHANNEL)
				clear_channel (*sess);
		}
		list = list->next;
	}

	serv->pos = 0;
	serv->motd_skipped = false;
	serv->no_login = false;
	serv->servername[0] = 0;
	serv->lag_sent = 0;

	notify_cleanup ();
}

/* send a "print text" command to the parent process - MUST END IN \n! */

static void
proxy_error (int fd, char *msg)
{
	write (fd, "0\n", 2);
	write (fd, msg, strlen (msg));
}

struct sock_connect
{
	char version;
	char type;
	guint16 port;
	guint32 address;
	char username[10];
};

/* traverse_socks() returns:
 *				0 success                *
 *          1 socks traversal failed */

static int
traverse_socks (int print_fd, int sok, char *serverAddr, int port)
{
	unsigned char buf[256];
	struct sock_connect sc = { 0 };
	sc.version = 4;
	sc.type = 1;
	sc.port = htons(port);
	sc.address = inet_addr(serverAddr);

	strncpy (sc.username, prefs.hex_irc_user_name, 9);

	send (sok, (char *) &sc, 8 + strlen (sc.username) + 1, 0);
	buf[1] = 0;
	recv (sok, (char*)buf, 10, 0);
	if (buf[1] == 90)
		return 0;

	snprintf ((gchar*)buf, sizeof (buf), "SOCKS\tServer reported error %d,%d.\n", buf[0], buf[1]);
	proxy_error (print_fd, (char*)buf);
	return 1;
}

struct sock5_connect1
{
	char version;
	char nmethods;
	char method;
};

static int
traverse_socks5 (int print_fd, int sok, const std::string & serverAddr, int port)
{
	struct sock5_connect1 sc1 = { 0	};
	sc1.version = 5;
	sc1.nmethods = 1;
	std::vector<unsigned char> sc2;
	unsigned int packetlen;
	std::array<unsigned char, 260> buf;
	//unsigned char buf[260];
	int auth = prefs.hex_net_proxy_auth && prefs.hex_net_proxy_user[0] && prefs.hex_net_proxy_pass[0];

	if (auth)
		sc1.method = 2;  /* Username/Password Authentication (UPA) */
	else
		sc1.method = 0;  /* NO Authentication */
	send (sok, (char *) &sc1, 3, 0);
	if (recv (sok, (char*)&buf[0], 2, 0) != 2)
		goto read_error;

	if (buf[0] != 5)
	{
		proxy_error (print_fd, "SOCKS\tServer is not socks version 5.\n");
		return 1;
	}

	/* did the server say no auth required? */
	if (buf[1] == 0)
		auth = 0;

	if (auth)
	{
		int len_u=0, len_p=0;

		/* authentication sub-negotiation (RFC1929) */
		if (buf[1] != 2)  /* UPA not supported by server */
		{
			proxy_error (print_fd, "SOCKS\tServer doesn't support UPA authentication.\n");
			return 1;
		}
		buf.fill(0);

		/* form the UPA request */
		len_u = strlen (prefs.hex_net_proxy_user);
		len_p = strlen (prefs.hex_net_proxy_pass);
		buf[0] = 1;
		buf[1] = len_u;
		auto buf_itr = buf.begin();
		std::copy(
			std::begin(prefs.hex_net_proxy_user),
			std::end(prefs.hex_net_proxy_user),
			buf_itr + 2);
		buf[2 + len_u] = len_p;
		std::copy(
			std::begin(prefs.hex_net_proxy_pass), 
			std::end(prefs.hex_net_proxy_pass), 
			buf_itr + 3 + len_u);

		send (sok, (char*)&buf[0], 3 + len_u + len_p, 0);
		if (recv(sok, (char*)&buf[0], 2, 0) != 2)
			goto read_error;
		if ( buf[1] != 0 )
		{
			proxy_error (print_fd, "SOCKS\tAuthentication failed. "
							 "Is username and password correct?\n");
			return 1; /* UPA failed! */
		}
	}
	else
	{
		if (buf[1] != 0)
		{
			proxy_error (print_fd, "SOCKS\tAuthentication required but disabled in settings.\n");
			return 1;
		}
	}

	packetlen = 4 + 1 + serverAddr.length() + 2;
	sc2.resize(packetlen);
	sc2[0] = 5;						  /* version */
	sc2[1] = 1;						  /* command */
	sc2[2] = 0;						  /* reserved */
	sc2[3] = 3;						  /* address type */
	sc2[4] = static_cast<unsigned char>(serverAddr.length());	/* hostname length */
	{
		auto it = sc2.begin() + 5;
		::std::copy(serverAddr.cbegin(), serverAddr.cend(), it);
		it += serverAddr.length();
		*((unsigned short *)&(*it)) = htons(port);
		send(sok, (char*)&sc2[0], packetlen, 0);
	}

	/* consume all of the reply */
	if (recv(sok, (char*)&buf[0], 4, 0) != 4)
		goto read_error;
	if (buf[0] != 5 || buf[1] != 0)
	{
		if (buf[1] == 2)
			snprintf((gchar*)&buf[0], sizeof(buf), "SOCKS\tProxy refused to connect to host (not allowed).\n");
		else
			snprintf((gchar*)&buf[0], sizeof(buf), "SOCKS\tProxy failed to connect to host (error %d).\n", buf[1]);
		proxy_error(print_fd, (char*)&buf[0]);
		return 1;
	}
	if (buf[3] == 1)	/* IPV4 32bit address */
	{
		if (recv(sok, (char*)&buf[0], 6, 0) != 6)
			goto read_error;
	} else if (buf[3] == 4)	/* IPV6 128bit address */
	{
		if (recv(sok, (char*)&buf[0], 18, 0) != 18)
			goto read_error;
	} else if (buf[3] == 3)	/* string, 1st byte is size */
	{
		if (recv(sok, (char*)&buf[0], 1, 0) != 1)	/* read the string size */
			goto read_error;
		packetlen = buf[0] + 2;	/* can't exceed 260 */
		if (recv(sok, (char*)&buf[0], packetlen, 0) != packetlen)
			goto read_error;
	}

	return 0;	/* success */

read_error:
	proxy_error (print_fd, "SOCKS\tRead error from server.\n");
	return 1;
}

static int
traverse_wingate (int print_fd, int sok, char *serverAddr, int port)
{
	char buf[128];

	snprintf (buf, sizeof (buf), "%s %d\r\n", serverAddr, port);
	send (sok, buf, strlen (buf), 0);

	return 0;
}

/* stuff for HTTP auth is here */

static void
three_to_four (const char *from, char *to)
{
	static const char tab64[64]=
	{
		'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
		'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
		'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
		'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
	};

	to[0] = tab64 [ (from[0] >> 2) & 63 ];
	to[1] = tab64 [ ((from[0] << 4) | (from[1] >> 4)) & 63 ];
	to[2] = tab64 [ ((from[1] << 2) | (from[2] >> 6)) & 63 ];
	to[3] = tab64 [ from[2] & 63 ];
}

void
base64_encode (char *to, const char *from, unsigned int len)
{
	while (len >= 3)
	{
		three_to_four (from, to);
		len -= 3;
		from += 3;
		to += 4;
	}
	if (len)
	{
		char three[3] = {0,0,0};
		unsigned int i;
		for (i = 0; i < len; i++)
		{
			three[i] = *from++;
		}
		three_to_four (three, to);
		if (len == 1)
		{
			to[2] = to[3] = '=';
		}
		else if (len == 2)
		{
			to[3] = '=';
		}
		to += 4;
	};
	to[0] = 0;
}

static int
http_read_line (int print_fd, int sok, char *buf, int len)
{
	len = waitline (sok, buf, len, TRUE);
	if (len >= 1)
	{
		/* print the message out (send it to the parent process) */
		write (print_fd, "0\n", 2);

		if (buf[len-1] == '\r')
		{
			buf[len-1] = '\n';
			write (print_fd, buf, len);
		} else
		{
			write (print_fd, buf, len);
			write (print_fd, "\n", 1);
		}
	}

	return len;
}

static int
traverse_http (int print_fd, int sok, char *serverAddr, int port)
{
	char buf[512];
	char auth_data[256];
	char auth_data2[252];
	int n, n2;

	n = snprintf (buf, sizeof (buf), "CONNECT %s:%d HTTP/1.0\r\n",
					  serverAddr, port);
	if (prefs.hex_net_proxy_auth)
	{
		n2 = snprintf (auth_data2, sizeof (auth_data2), "%s:%s",
							prefs.hex_net_proxy_user, prefs.hex_net_proxy_pass);
		base64_encode (auth_data, auth_data2, n2);
		n += snprintf (buf+n, sizeof (buf)-n, "Proxy-Authorization: Basic %s\r\n", auth_data);
	}
	n += snprintf (buf+n, sizeof (buf)-n, "\r\n");
	send (sok, buf, n, 0);

	n = http_read_line (print_fd, sok, buf, sizeof (buf));
	/* "HTTP/1.0 200 OK" */
	if (n < 12)
		return 1;
	if (memcmp (buf, "HTTP/", 5) || memcmp (buf + 9, "200", 3))
		return 1;
	while (1)
	{
		/* read until blank line */
		n = http_read_line (print_fd, sok, buf, sizeof (buf));
		if (n < 1 || (n == 1 && buf[0] == '\n'))
			break;
	}
	return 0;
}

static int
traverse_proxy (int proxy_type, int print_fd, int sok, char *ip, int port)
{
	switch (proxy_type)
	{
	case 1:
		return traverse_wingate (print_fd, sok, ip, port);
	case 2:
		return traverse_socks (print_fd, sok, ip, port);
	case 3:
		return traverse_socks5 (print_fd, sok, ip, port);
	case 4:
		return traverse_http (print_fd, sok, ip, port);
	}

	return 1;
}

/* this is the child process making the connection attempt */
#if 0
static int
server_child (server * serv)
{
	netstore *ns_server;
	netstore *ns_proxy = nullptr;
	netstore *ns_local;
	int port = serv->port;
	int error;
	int sok = -1, psok = -1;
	char *hostname = serv->hostname;
	char *real_hostname = nullptr;
	char *ip;
	char *proxy_ip = nullptr;
	char *local_ip;
	int connect_port;
	char buf[512];
	char bound = 0;
	int proxy_type = 0;
	char *proxy_host = nullptr;
	int proxy_port;

	ns_server = net_store_new ();

	/* is a hostname set? - bind to it */
	if (prefs.hex_net_bind_host[0])
	{
		ns_local = net_store_new ();
		local_ip = net_resolve (ns_local, prefs.hex_net_bind_host, 0, &real_hostname);
		if (local_ip != nullptr)
		{
			snprintf (buf, sizeof (buf), "5\n%s\n", local_ip);
			write (serv->childwrite, buf, strlen (buf));
			net_bind (ns_local, serv->sok4, serv->sok6);
			bound = 1;
		} else
		{
			write (serv->childwrite, "7\n", 2);
		}
		net_store_destroy (ns_local);
	}

	if (!serv->dont_use_proxy) /* blocked in serverlist? */
	{
		if (false)
			;
#ifdef USE_LIBPROXY
		else if (prefs.hex_net_proxy_type == 5)
		{
			char **proxy_list;
			char *url, *proxy;

			url = g_strdup_printf ("irc://%s:%d", hostname, port);
			proxy_list = px_proxy_factory_get_proxies (libproxy_factory, url);

			if (proxy_list) {
				/* can use only one */
				proxy = proxy_list[0];
				if (!strncmp (proxy, "direct", 6))
					proxy_type = 0;
				else if (!strncmp (proxy, "http", 4))
					proxy_type = 4;
				else if (!strncmp (proxy, "socks5", 6))
					proxy_type = 3;
				else if (!strncmp (proxy, "socks", 5))
					proxy_type = 2;
			}

			if (proxy_type) {
				char *c;
				c = strchr (proxy, ':') + 3;
				proxy_host = strdup (c);
				c = strchr (proxy_host, ':');
				*c = '\0';
				proxy_port = atoi (c + 1);
			}

			g_strfreev (proxy_list);
			g_free (url);
		}
#endif
		else if (prefs.hex_net_proxy_host[0] &&
			   prefs.hex_net_proxy_type > 0 &&
			   prefs.hex_net_proxy_use != 2) /* proxy is NOT dcc-only */
		{
			proxy_type = prefs.hex_net_proxy_type;
			proxy_host = strdup (prefs.hex_net_proxy_host);
			proxy_port = prefs.hex_net_proxy_port;
		}
	}

	serv->proxy_type = proxy_type;

	/* first resolve where we want to connect to */
	if (proxy_type > 0)
	{
		snprintf (buf, sizeof (buf), "9\n%s\n", proxy_host);
		write (serv->childwrite, buf, strlen (buf));
		ip = net_resolve (ns_server, proxy_host, proxy_port, &real_hostname);
		free (proxy_host);
		if (!ip)
		{
			write (serv->childwrite, "1\n", 2);
			goto xit;
		}
		connect_port = proxy_port;

		/* if using socks4 or MS Proxy, attempt to resolve ip for irc server */
		if ((proxy_type == 2) || (proxy_type == 5))
		{
			ns_proxy = net_store_new ();
			proxy_ip = net_resolve (ns_proxy, hostname, port, &real_hostname);
			if (!proxy_ip)
			{
				write (serv->childwrite, "1\n", 2);
				goto xit;
			}
		} else						  /* otherwise we can just use the hostname */
			proxy_ip = strdup (hostname);
	} else
	{
		ip = net_resolve (ns_server, hostname, port, &real_hostname);
		if (!ip)
		{
			write (serv->childwrite, "1\n", 2);
			goto xit;
		}
		connect_port = port;
	}

	snprintf (buf, sizeof (buf), "3\n%s\n%s\n%d\n",
				 real_hostname, ip, connect_port);
	write (serv->childwrite, buf, strlen (buf));

	if (!serv->dont_use_proxy && (proxy_type == 5))
		error = net_connect (ns_server, serv->proxy_sok4, serv->proxy_sok6, &psok);
	else
	{
		error = net_connect (ns_server, serv->sok4, serv->sok6, &sok);
		psok = sok;
	}

	if (error != 0)
	{
		snprintf (buf, sizeof (buf), "2\n%d\n", sock_error ());
		write (serv->childwrite, buf, strlen (buf));
	} else
	{
		/* connect succeeded */
		if (proxy_ip)
		{
			switch (traverse_proxy (proxy_type, serv->childwrite, psok, proxy_ip, port))
			{
			case 0:	/* success */
					snprintf (buf, sizeof (buf), "4\n%d\n", sok);	/* success */
				write (serv->childwrite, buf, strlen (buf));
				break;
			case 1:	/* socks traversal failed */
				write (serv->childwrite, "8\n", 2);
				break;
			}
		} else
		{
			snprintf (buf, sizeof (buf), "4\n%d\n", sok);	/* success */
			write (serv->childwrite, buf, strlen (buf));
		}
	}

xit:

#if defined (USE_IPV6) || defined (WIN32)
	/* this is probably not needed */
	net_store_destroy (ns_server);
	if (ns_proxy)
		net_store_destroy (ns_proxy);
#endif

	/* no need to free ip/real_hostname, this process is exiting */
#ifdef WIN32
	/* under win32 we use a thread -> shared memory, must free! */
	if (proxy_ip)
		free (proxy_ip);
	if (ip)
		free (ip);
	if (real_hostname)
		free (real_hostname);
#endif

	return 0;
	/* cppcheck-suppress memleak */
}
#endif

static gboolean
io_poll(io::tcp::connection * connection){
	connection->poll();
	return TRUE;
}

void server_error(server * serv, const boost::system::error_code & error)
{
	PrintText(serv->front_session, error.message());
	PrintText(serv->front_session, std::to_string(error.value()));
	if (!serv->end_of_motd)
	{
		serv->disconnect(serv->server_session, false, error.value());
		if (!servlist_cycle(serv))
		{
			if (prefs.hex_net_auto_reconnect)
				serv->auto_reconnect(false, error.value());
		}
	}
	else
	{
		if (prefs.hex_net_auto_reconnect)
			serv->auto_reconnect(false, error.value());
		else
			serv->disconnect(serv->server_session, false, error.value());
	}
}


void
server::connect (char *hostname, int port, bool no_login)
{
	int read_des[2] = { 0 };
	//unsigned int pid;
	session *sess = this->server_session;
	boost::asio::io_service io_service;
	auto resolved = io::tcp::resolve_endpoints(io_service, hostname, port);
	if (resolved.first){
		server_error(this, resolved.first);
		return;
	}
	this->server_connection = io::tcp::connection::create_connection(this->use_ssl ? io::tcp::connection_security::no_verify : io::tcp::connection_security::none, io_service );
	this->server_connection->on_connect.connect(std::bind(server_connected1, this, std::placeholders::_1));
	this->server_connection->on_valid_connection.connect([this](const std::string & hostname){ safe_strcpy(this->servername, hostname.c_str()); });
	this->server_connection->on_error.connect(std::bind(server_error, this, std::placeholders::_1));
	this->server_connection->on_message.connect(std::bind(server_read_cb, this, std::placeholders::_1, std::placeholders::_2));
	this->server_connection->on_ssl_handshakecomplete.connect(std::bind(ssl_print_cert_info, this, std::placeholders::_1));
	this->server_connection->connect(resolved.second);
	
	this->reset_to_defaults();
	this->connecting = true;
	this->port = port;
	this->no_login = no_login;

	fe_server_event(this, fe_serverevents::CONNECTING, 0);
	fe_set_away (*this);
	this->flush_queue ();
	this->iotag = fe_timeout_add(50, (GSourceFunc)&io_poll, this->server_connection.get());
#if 0
#ifdef USE_OPENSSL
	if (!ctx && this->use_ssl)
	{
		if (!(serv->ctx = _SSL_context_init (ssl_cb_info, FALSE)))
		{
			fprintf (stderr, "_SSL_context_init failed\n");
			exit (1);
		}
	}
#endif

	if (!hostname[0])
		return;

	if (port < 0)
	{
		/* use default port for this server type */
		port = 6667;
#ifdef USE_OPENSSL
		if (this->use_ssl)
			port = 6697;
#endif
	}
	port &= 0xffff;	/* wrap around */

	if (this->connected || this->connecting || this->recondelay_tag)
		this->disconnect (sess, true, -1);

	fe_progressbar_start (sess);

	EMIT_SIGNAL (XP_TE_SERVERLOOKUP, sess, hostname, nullptr, nullptr, nullptr, 0);

	safe_strcpy (this->servername, hostname, sizeof (this->servername));
	/* overlap illegal in strncpy */
	if (hostname != this->hostname)
		safe_strcpy (this->hostname, hostname, sizeof (this->hostname));

#ifdef USE_OPENSSL
	if (this->use_ssl)
	{
		char *cert_file;
		this->have_cert = false;

		/* first try network specific cert/key */
		cert_file = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "certs" G_DIR_SEPARATOR_S "%s.pem",
					 get_xdir (), this->get_network (true));
		if (SSL_CTX_use_certificate_file (ctx, cert_file, SSL_FILETYPE_PEM) == 1)
		{
			if (SSL_CTX_use_PrivateKey_file (ctx, cert_file, SSL_FILETYPE_PEM) == 1)
				this->have_cert = true;
		}
		else
		{
			/* if that doesn't exist, try <config>/certs/client.pem */
			cert_file = g_build_filename (get_xdir (), "certs", "client.pem", nullptr);
			if (SSL_CTX_use_certificate_file (ctx, cert_file, SSL_FILETYPE_PEM) == 1)
			{
				if (SSL_CTX_use_PrivateKey_file(ctx, cert_file, SSL_FILETYPE_PEM) == 1)
					this->have_cert = true;
			}
		}
		g_free (cert_file);
	}
#endif

	this->reset_to_defaults();
	this->connecting = true;
	this->port = port;
	this->no_login = no_login;

	fe_server_event (this, fe_serverevents::CONNECTING, 0);
	fe_set_away (this);
	this->flush_queue ();

#ifdef WIN32
	if (_pipe (read_des, 4096, _O_BINARY) < 0)
#else
	if (pipe (read_des) < 0)
#endif
		return;
#ifdef __EMX__ /* os/2 */
	setmode (read_des[0], O_BINARY);
	setmode (read_des[1], O_BINARY);
#endif
	this->childread = read_des[0];
	this->childwrite = read_des[1];

	/* create both sockets now, drop one later */
	net_sockets (&this->sok4, &this->sok6);
	this->proxy_sok4 = -1;
	this->proxy_sok6 = -1;

#ifdef WIN32
	std::thread server_thread(server_child, this);
	pid = GetThreadId(server_thread.native_handle());
	server_thread.detach();
#else
#ifdef LOOKUPD
	/* CL: net_resolve calls rand() when LOOKUPD is set, so prepare a different
	 * seed for each child. This method gives a bigger variation in seed values
	 * than calling srand(time(0)) in the child itself.
	 */
	rand();
#endif
	switch (pid = fork ())
	{
	case -1:
		return;

	case 0:
		/* this is the child */
		setuid (getuid ());
		server_child (this);
		_exit (0);
	}
#endif
	this->childpid = pid;
#ifdef WIN32
	this->iotag = fe_input_add(this->childread, FIA_READ | FIA_FD, (GIOFunc)server_read_child,
#else
	this->iotag = fe_input_add (this->childread, FIA_READ, (GIOFunc)server_read_child,
#endif
										 this);
#endif
}

void
server_fill_her_up (server &serv)
{
	serv.p_cmp = rfc_casecmp;	/* can be changed by 005 in modes.c */
	serv.imbue(rfc_locale(std::locale()));
}

void server::imbue(const std::locale& other)
{
	this->locale_ = other;
}

int server::compare(const boost::string_ref & lhs, const boost::string_ref &rhs) const
{
	auto& collate = std::use_facet<std::collate<char>>(locale_);
	return collate.compare(lhs.cbegin(), lhs.cend(), rhs.cbegin(), rhs.cend());
}

const std::locale & server::current_locale() const
{
	return locale_;
}

void
server::set_encoding (const char *new_encoding)
{
	if (this->encoding)
	{
		/* can be left as uninitialized to indicate system encoding */
		this->encoding = boost::optional<std::string>();
		this->using_cp1255 = false;
		this->using_irc = false;
	}

	if (new_encoding)
	{
		this->encoding = new_encoding;
		/* the serverlist GUI might have added a space 
			and short description - remove it. */
		auto space = this->encoding->find_first_of(' ');
		if (space != std::string::npos)
			this->encoding->erase(space);

		/* server_inline() uses these flags */
		if (!g_ascii_strcasecmp (this->encoding->c_str(), "CP1255") ||
			!g_ascii_strcasecmp(this->encoding->c_str(), "WINDOWS-1255"))
			this->using_cp1255 = true;
		else if (!g_ascii_strcasecmp(this->encoding->c_str(), "IRC"))
			this->using_irc = true;
	}
}

server::server()
	:death_timer(0),
	p_cmp(),
	port(),
	sok(),					/* is equal to sok4 or sok6 (the one we are using) */
	sok4(),					/* tcp4 socket */
	sok6(),					/* tcp6 socket */
	proxy_type(),
	proxy_sok(),				/* Additional information for MS Proxy beast */
	proxy_sok4(),
	proxy_sok6(),
	id(),				/* unique ID number (for plugin API) */
	ssl(),
#ifdef USE_OPENSSL
	ssl_do_connect_tag(),
#endif
	childread(),
	childwrite(),
	childpid(),
	iotag(),
	recondelay_tag(),				/* reconnect delay timeout */
	joindelay_tag(),				/* waiting before we send JOIN */
	hostname(),				/* real ip number */
	servername(),			/* what the server says is its name */
	password(),
	nick(),
	linebuf(),
	pos(),								/* current position in linebuf */
	nickcount(),
	loginmethod(),
	modes_per_line(),			/* 6 on undernet, 4 on efnet etc... */
	network(),						/* points to entry in servlist.c or NULL! */
	next_send(),						/* cptr->since in ircu */
	prev_now(),					/* previous now-time */
	sendq_len(),						/* queue size */
	lag(),								/* milliseconds */
	front_session(),	/* front-most window/tab */
	server_session(),	/* server window/tab */
	gui(),		  /* initialized by fe_new_server */
	ctcp_counter(),	  /*flood */
	ctcp_last_time(),
	msg_counter(),	  /*counts the msg tab opened in a certain time */
	msg_last_time(),

	/*time_t connect_time;*/				/* when did it connect? */
	lag_sent(),   /* we are still waiting for this ping response*/
	//ping_recv(),					/* when we last got a ping reply */
	away_time(),					/* when we were marked away */
	favlist(),			/* list of channels & keys to join */

	motd_skipped(),
	connected(),
	connecting(),
	no_login(),
	skip_next_userhost(),/* used for "get my ip from server" */
	skip_next_whois(),	/* hide whois output */
	inside_whois(),
	doing_dns(),			/* /dns has been done */
	retry_sasl(),   	/* retrying another sasl mech */
	end_of_motd(),		/* end of motd reached (logged in) */
	sent_quit(),			/* sent a QUIT already? */
	use_listargs(),		/* undernet and dalnet need /list >0,<10000 */
	is_away(),
	reconnect_away(),	/* whether to reconnect in is_away state */
	dont_use_proxy(),	/* to proxy or not to proxy */
	supports_watch(),	/* supports the WATCH command */
	supports_monitor(),	/* supports the MONITOR command */
	bad_prefix(),			/* gave us a bad PREFIX= 005 number */
	have_namesx(),		/* 005 tokens NAMESX and UHNAMES */
	have_awaynotify(),
	have_uhnames(),
	have_whox(),		/* have undernet's WHOX features */
	have_idmsg(),		/* freenode's IDENTIFY-MSG */
	have_accnotify(), /* cap account-notify */
	have_extjoin(),	/* cap extended-join */
	have_server_time(),	/* cap server-time */
	have_sasl(),		/* SASL capability */
	have_except(),	/* ban exemptions +e */
	have_invite(),	/* invite exemptions +I */
	have_cert(),	/* have loaded a cert */
	using_cp1255(), 	/* encoding is CP1255/WINDOWS-1255? */
	using_irc(),		/* encoding is "IRC" (CP1252/UTF-8 hybrid)? */
	use_who(),			/* whether to use WHO command to get dcc_ip */
	sasl_mech(),			/* mechanism for sasl auth */
	sent_saslauth(),	/* have sent AUTHENICATE yet */
	sent_capend()	/* have sent CAP END yet */
#ifdef USE_OPENSSL
	,use_ssl(),
	accept_invalid_cert()
#endif
{}

server::~server(){}

server *
server_new (void)
{
	static int id = 0;
	server *serv;

	serv = new server;
	/* use server.c and proto-irc.c functions */
	server_fill_her_up(*serv);
	serv->id = id++;
	serv->sok = -1;
	strcpy (serv->nick, prefs.hex_irc_nick1);
	serv->reset_to_defaults();

	serv_list = g_slist_prepend (serv_list, serv);

	fe_new_server (serv);

	return serv;
}

bool
is_server (server *serv)
{
	return g_slist_find (serv_list, serv) ? true : false;
}

void
server::reset_to_defaults()
{
	this->chantypes.clear();
	this->chanmodes.clear();
	this->nick_prefixes.clear();
	this->nick_modes.clear();

	this->chantypes = "#&!+";
	this->chanmodes = "beI,k,l";
	this->nick_prefixes = "@%+";
	this->nick_modes = "ohv";

	this->nickcount = 1;
	this->end_of_motd = false;
	this->is_away = false;
	this->supports_watch = false;
	this->supports_monitor = false;
	this->bad_prefix = false;
	this->use_who = true;
	this->have_namesx = false;
	this->have_awaynotify = false;
	this->have_uhnames = false;
	this->have_whox = false;
	this->have_idmsg = false;
	this->have_accnotify = false;
	this->have_extjoin = false;
	this->have_server_time = false;
	this->have_sasl = false;
	this->have_except = false;
	this->have_invite = false;
}

char *
server::get_network (bool fallback) const
{
	/* check the network list */
	if (this->network)
		return &(this->network->name)[0];

	/* check the network name given in 005 NETWORK=... */
	if (this->server_session && *this->server_session->channel)
		return this->server_session->channel;

	if (fallback)
		return const_cast<char*>(this->servername); // DANGER WE NEED TO FIX THE CALLERS

	return nullptr;
}

void
server::set_name (const std::string& name)
{
	GSList *list = sess_list;
	session *sess;
	std::string name_to_set = name;

	if (name.empty())
		name_to_set = this->hostname;

	/* strncpy parameters must NOT overlap */
	if (name != this->servername)
	{
		safe_strcpy (this->servername, name.c_str());
	}

	while (list)
	{
		sess = (session *) list->data;
		if (sess->server == this)
			fe_set_title (*sess);
		list = list->next;
	}

	if (this->server_session->type == session::SESS_SERVER)
	{
		if (this->network)
		{
			safe_strcpy (this->server_session->channel, this->network->name.c_str());
		} else
		{
			safe_strcpy (this->server_session->channel, name.c_str());
		}
		fe_set_channel (this->server_session);
	}
}

boost::optional<const std::pair<bool, std::string>& >
server::get_away_message(const std::string & nick) const NOEXCEPT
		{
	auto res = this->away_map.find(nick);
	if (res == this->away_map.cend())
		return boost::none;
	return boost::make_optional<const std::pair<bool, std::string>&>(res->second);
}

void
server::save_away_message(const std::string& nick, const boost::optional<std::string>& message)
		{
	this->away_map.insert({ nick, std::make_pair(static_cast<bool>(message), message ? message.get() : "") });
}

void
server_free (server *serv)
{
	serv->cleanup ();

	serv_list = g_slist_remove (serv_list, serv);

	dcc::dcc_notify_kill (serv);
	serv->flush_queue ();

	if (serv->favlist)
		g_slist_free_full (serv->favlist, (GDestroyNotify) servlist_favchan_free);

	fe_server_callback (serv);

	delete serv;

	notify_cleanup ();
}
