/* HexChat
* Copyright (C) 1998-2010 Peter Zelezny.
* Copyright (C) 2009-2013 Berke Viktor.
* Copyright (C) 2014 Leetsoftwerx.
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

#include "../../config.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <boost/format.hpp>

#include <glib.h>

#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/rand.h>
#include <openssl/blowfish.h>
#include <openssl/aes.h>

#include <boost/config.hpp>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#ifdef BOOST_NO_NOEXCEPT
#define NOEXCEPT throw()
#else
#define NOEXCEPT noexcept
#endif

#include "sasl.hpp"
#include "base64.hpp"


namespace
{
class dh_setup;

static bool parse_dh(const std::string& str, dh_setup & setup);

class dh_setup
{
	std::vector<unsigned char> _secret;
	std::unique_ptr<BIGNUM, decltype(&BN_free)> _pubkey;
	std::unique_ptr<DH, decltype(&DH_free)> _dh;
	int _key_size;

	explicit dh_setup(
		std::vector<unsigned char> && secret,
		std::unique_ptr<BIGNUM, decltype(&BN_free)> pubkey,
		std::unique_ptr<DH, decltype(&DH_free)> dh,
		int key_size) NOEXCEPT
		:_secret(std::forward<std::vector<unsigned char> && >(secret)),
		_pubkey(std::move(pubkey)),
		_dh(std::move(dh)),
		_key_size(key_size)
	{
	}
	dh_setup(const dh_setup&) = delete;
	dh_setup& operator=(const dh_setup&) = delete;
public:
	dh_setup()
		:_pubkey(nullptr, BN_free), _dh(nullptr, DH_free)
	{}
	dh_setup(dh_setup&& other) NOEXCEPT
		:_pubkey(nullptr, BN_free), _dh(nullptr, DH_free)
	{
		*this = std::move(other);
	}

	dh_setup& operator=(dh_setup&& other) NOEXCEPT
	{
		if (this != &other)
		{
			this->_pubkey = std::move(other._pubkey);
			this->_dh = std::move(other._dh);
			this->_secret = std::move(other._secret);
			this->_key_size = other._key_size;
			other._key_size = 0;
		}
		return *this;
	}

	const DH* dh() const NOEXCEPT
	{
		return _dh.get();
	}

	const unsigned char* secret() const NOEXCEPT
	{
		return _secret.data();
	}

	int key_size() const NOEXCEPT
	{
		return _key_size;
	}
	friend bool parse_dh(const std::string& str, dh_setup & setup);
};
	/* Adapted from ZNC's SASL module */

static bool
parse_dh(const std::string& str, dh_setup & setup)
{
	std::stringstream data_stream;
	if (!util::transforms::decode_base64(str, data_stream))
		return false;

	std::unique_ptr<DH, decltype(&DH_free)> dh(DH_new(), DH_free);
	if (!dh)
		return false;

	auto data_len = data_stream.tellp();
	if (data_len < 2)
		return false;
	data_stream.seekp(0);
	std::uint16_t size16;
	data_stream.read(reinterpret_cast<char*>(&size16), sizeof(size16));
	/* prime number */
	auto size = ntohs(size16);
	data_len -= 2;

	if (size > data_len)
		return false;
	std::vector<unsigned char> data(size);
	std::copy_n(
		std::istream_iterator<unsigned char>(data_stream),
		size,
		data.begin());
	dh->p = BN_bin2bn(data.data(), size, nullptr);

	/* Generator */
	if (data_len < 2)
		return false;

	data_stream.read(reinterpret_cast<char*>(&size16), sizeof(size16));
	size = ntohs(size16);
	data_len -= 2;

	if (size > data_len)
		return false;
	data.resize(size);
	std::copy_n(
		std::istream_iterator<unsigned char>(data_stream),
		size,
		data.begin());
	dh->g = BN_bin2bn(data.data(), size, nullptr);

	/* pub key */
	if (data_len < 2)
		return false;

	data_stream.read(reinterpret_cast<char*>(&size16), sizeof(size16));
	size = ntohs(size16);
	data_len -= 2;

	data.resize(size);
	std::copy_n(
		std::istream_iterator<unsigned char>(data_stream),
		size,
		data.begin());

	std::unique_ptr<BIGNUM, decltype(&BN_free)> pubkey(
		BN_bin2bn(data.data(), size, nullptr),
		BN_free);
	if (!(DH_generate_key(dh.get())))
		return false;

	std::vector<unsigned char> secret(DH_size(dh.get()));
	int key_size = DH_compute_key(&secret[0], pubkey.get(), dh.get());
	if (key_size == -1)
		return false;

	setup = dh_setup(std::move(secret), std::move(pubkey), std::move(dh), key_size);

	return true;
}
} //end anonymous namespace

namespace auth
{
	namespace sasl
	{

std::string encode_sasl_pass_plain(const std::string &user, const std::string &pass)
{
	std::ostringstream response;
	response << boost::format("%s%c%s%c%s") % user % '\0' % user % '\0' % pass;
	return util::transforms::encode_base64(response.str());
}

std::string encode_sasl_pass_blowfish(const std::string & user, const std::string& pass, const std::string & data)
{
	auto pass_len = pass.size() + (8 - (pass.size() % 8));
	//auto user_len = user.size();

	dh_setup setup;
	if (!parse_dh(data, setup))
		return{};

	BF_KEY key;
	BF_set_key(&key, setup.key_size(), setup.secret());

	std::vector<unsigned char> encrypted_pass(pass_len);
	std::fill(encrypted_pass.begin(), encrypted_pass.end(), '\0');
	std::string plain_pass(pass);
	plain_pass.resize(pass_len);

	unsigned char * out_ptr = &encrypted_pass[0];
	char *in_ptr = &plain_pass[0];

	for (auto length = pass_len; length; length -= 8, in_ptr += 8, out_ptr += 8)
		BF_ecb_encrypt(reinterpret_cast<unsigned char*>(in_ptr), out_ptr, &key, BF_ENCRYPT);

	/* Create response */
	//auto length = 2 + BN_num_bytes(setup.dh()->pub_key) + pass_len + user_len + 1;
	std::ostringstream response;

	/* our key */
	std::uint16_t size16 = htons(static_cast<std::uint16_t>(BN_num_bytes(setup.dh()->pub_key)));
	response.write(reinterpret_cast<const char*>(&size16), sizeof(size16));
	std::vector<unsigned char> buffer(BN_num_bytes(setup.dh()->pub_key));
	BN_bn2bin(setup.dh()->pub_key, &buffer[0]);
	std::copy(buffer.cbegin(), buffer.cend(), std::ostream_iterator<unsigned char>(response));

	/* username */
	std::copy(user.cbegin(), user.cend(), std::ostream_iterator<char>(response));
	response.put(0);

	/* pass */
	std::copy(encrypted_pass.cbegin(), encrypted_pass.cend(), std::ostream_iterator<unsigned char>(response));

	return util::transforms::encode_base64(response.str());
}

std::string encode_sasl_pass_aes(const std::string & user, const std::string& pass, const std::string & data)
{
	AES_KEY key;
	//unsigned char *encrypted_userpass, *plain_userpass;
	int length;
	unsigned char iv[16], iv_copy[16];
	auto user_len = user.size() + 1;
	auto pass_len = pass.size() + 1;
	auto len = user_len + pass_len;
	auto padlen = 16 - (len % 16);
	auto userpass_len = len + padlen;

	dh_setup setup;
	if (!parse_dh(data, setup))
		return{};

	std::vector<unsigned char> encrypted_userpass(userpass_len);
	std::vector<unsigned char> plain_userpass(userpass_len);

	/* create message */
	/* format of: <username>\0<password>\0<padding> */
	auto ptr = plain_userpass.begin();
	std::copy(user.cbegin(), user.cend(), ptr);
	ptr += user_len;
	std::copy(pass.cbegin(), pass.cend(), ptr);
	ptr += pass_len;
	if (padlen)
	{
		/* Padding */
		unsigned char randbytes[16];
		if (!RAND_bytes(randbytes, padlen))
			return{};
		std::copy_n(std::begin(randbytes), padlen, ptr);
	}

	if (!RAND_bytes(iv, sizeof(iv)))
		return{};

	std::copy(std::begin(iv), std::end(iv), std::begin(iv_copy));

	/* Encrypt */
	AES_set_encrypt_key(setup.secret(), setup.key_size() * 8, &key);
	AES_cbc_encrypt(plain_userpass.data(), &encrypted_userpass[0], userpass_len, &key, iv_copy, AES_ENCRYPT);

	/* Create response */
	/* format of:  <size pubkey><pubkey><iv (always 16 bytes)><ciphertext> */
	length = 2 + setup.key_size() + sizeof(iv) + userpass_len;
	std::ostringstream response;
	
	/* our key */
	std::uint16_t size16 = htons((std::uint16_t)setup.key_size());
	response.write(reinterpret_cast<const char*>(&size16), sizeof(size16));
	std::vector<unsigned char> buffer(setup.key_size());
	BN_bn2bin(setup.dh()->pub_key, (guchar*)&buffer[0]);
	std::ostream_iterator<unsigned char> out(response);
	std::copy(buffer.cbegin(), buffer.cend(), out);


	/* iv */
	std::copy(std::begin(iv), std::end(iv), out);

	/* userpass */
	std::copy(encrypted_userpass.cbegin(), encrypted_userpass.cend(), out);

	return util::transforms::encode_base64(response.str());
}

	} // namespace sasl
} // namespace auth