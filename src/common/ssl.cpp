/*
 * ssl.c v0.0.3
 * Copyright (C) 2000  --  DaP <profeta@freemail.c3.hu>
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

#ifdef __APPLE__
#define __AVAILABILITYMACROS__
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif

#include "inet.hpp"				  /* make it first to avoid macro redefinitions */
#include <openssl/ssl.h>		  /* SSL_() */
#include <openssl/err.h>		  /* ERR_() */
#include "../../config.h"
#include <string>
#include <iterator>
#include <iostream>
#include <sstream>
#include <memory>
#include <algorithm>
#include <ctime>				  /* asctime() */
#include <cstring>				  /* strncpy() */
#include "ssl.hpp"				  /* struct cert_info */
#include "util.hpp"

/* +++++ Internal functions +++++ */
namespace {

static std::string
ASN1_TIME_to_string(ASN1_TIME * tm)
{
	char *expires = nullptr;
	std::unique_ptr<BIO, decltype(&::BIO_free)> inMem(BIO_new(BIO_s_mem()), &BIO_free);

	ASN1_TIME_print(inMem.get(), tm);
	BIO_get_mem_data(inMem.get(), &expires);
	std::string buf;
	if (expires)
	{
		buf.append(expires, 24);
	}
	return buf;
}


static void
broke_oneline(char *oneline, char *parray[])
{
	char *pt, *ppt;
	int i;


	i = 0;
	ppt = pt = oneline + 1;
	while ((pt = std::strchr(pt, '/')))
	{
		*pt = 0;
		parray[i++] = ppt;
		ppt = ++pt;
	}
	parray[i++] = ppt;
	parray[i] = NULL;
}

}


/* +++++ SSL functions +++++ */

namespace io{
	namespace ssl{

/*
    FIXME: Master-Key, Extensions, CA bits
	    (openssl x509 -text -in servcert.pem)
*/
int
get_cert_info (cert_info &cert_info, const SSL * ssl)
{
	X509 *peer_cert;
	EVP_PKEY *peer_pkey;
	/* EVP_PKEY *ca_pkey; */
	/* EVP_PKEY *tmp_pkey; */
	int alg;
	int sign_alg;


	if (!(peer_cert = SSL_get_peer_certificate (ssl)))
		return (1);				  /* FATAL? */

	X509_NAME_oneline (X509_get_subject_name (peer_cert), cert_info.subject,
							 sizeof (cert_info.subject));
	X509_NAME_oneline (X509_get_issuer_name (peer_cert), cert_info.issuer,
							 sizeof (cert_info.issuer));
	broke_oneline (cert_info.subject, cert_info.subject_word);
	broke_oneline (cert_info.issuer, cert_info.issuer_word);

	alg = OBJ_obj2nid (peer_cert->cert_info->key->algor->algorithm);
	sign_alg = OBJ_obj2nid (peer_cert->sig_alg->algorithm);
	auto notBefore = ASN1_TIME_to_string (X509_get_notBefore (peer_cert));
	auto notAfter = ASN1_TIME_to_string (X509_get_notAfter (peer_cert));

	peer_pkey = X509_get_pubkey (peer_cert);

	safe_strcpy (cert_info.algorithm,
				(alg == NID_undef) ? "Unknown" : OBJ_nid2ln (alg));
	cert_info.algorithm_bits = EVP_PKEY_bits (peer_pkey);
	safe_strcpy (cert_info.sign_algorithm,
				(sign_alg == NID_undef) ? "Unknown" : OBJ_nid2ln (sign_alg));
	/* EVP_PKEY_bits(ca_pkey)); */
	cert_info.sign_algorithm_bits = 0;
	std::copy(notBefore.cbegin(), notBefore.cend(), std::begin(cert_info.notbefore));
	std::copy(notAfter.cbegin(), notAfter.cend(), std::begin(cert_info.notafter));

	EVP_PKEY_free (peer_pkey);

	/* SSL_SESSION_print_fp(stdout, SSL_get_session(ssl)); */
/*
	if (ssl->session->sess_cert->peer_rsa_tmp) {
		tmp_pkey = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(tmp_pkey, ssl->session->sess_cert->peer_rsa_tmp);
		cert_info->rsa_tmp_bits = EVP_PKEY_bits (tmp_pkey);
		EVP_PKEY_free(tmp_pkey);
	} else
		fprintf(stderr, "REMOTE SIDE DOESN'T PROVIDES ->peer_rsa_tmp\n");
*/
	cert_info.rsa_tmp_bits = 0;

	X509_free (peer_cert);

	return (0);
}


::io::ssl::cipher_info
get_cipher_info (const SSL * ssl)
{
	const SSL_CIPHER *c = SSL_get_current_cipher(ssl);
	::io::ssl::cipher_info info;
	info.version = SSL_CIPHER_get_version(c);
	info.cipher = SSL_CIPHER_get_name(c);
	SSL_CIPHER_get_bits (c, &info.cipher_bits);

	return info;
}

	}

}
