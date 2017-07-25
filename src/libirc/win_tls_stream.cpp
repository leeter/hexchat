/* libirc
* Copyright (C) 2017 Leetsoftwerx.
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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef WIN32
#error this file relies on win32 specific functionality
#endif
#pragma comment(lib, "runtimeobject")
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <experimental/resumable>
#include <experimental/generator>
#include <string>
#include <locale>
#include <codecvt>
#include <optional>
#include <string_view>

#include "tcp_connection.hpp"

namespace wns = winrt::Windows::Networking::Sockets;
namespace wn = winrt::Windows::Networking;
namespace wss = winrt::Windows::Storage::Streams;
namespace wsc = winrt::Windows::Security::Cryptography;
namespace wf = winrt::Windows::Foundation;

namespace {
	enum class connect_error {
		success,
		retry,
		next
	};
}

namespace winrt::ABI::Windows::Foundation {
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0111")) __declspec(novtable) AsyncActionProgressHandler<connect_error> : impl_AsyncActionProgressHandler<connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0112")) __declspec(novtable) AsyncActionWithProgressCompletedHandler<connect_error> : impl_AsyncActionWithProgressCompletedHandler<connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0113")) __declspec(novtable) AsyncOperationProgressHandler<connect_error, connect_error> : impl_AsyncOperationProgressHandler<connect_error, connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0114")) __declspec(novtable) AsyncOperationWithProgressCompletedHandler<connect_error, connect_error> : impl_AsyncOperationWithProgressCompletedHandler<connect_error, connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0115")) __declspec(novtable) AsyncOperationCompletedHandler<connect_error> : impl_AsyncOperationCompletedHandler<connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0116")) __declspec(novtable) IAsyncActionWithProgress<connect_error> : impl_IAsyncActionWithProgress<connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0117")) __declspec(novtable) IAsyncOperation<connect_error> : impl_IAsyncOperation<connect_error> {};
	template <> struct __declspec(uuid("3a14233f-a037-4ac0-a0ad-c4bb0bbf0118")) __declspec(novtable) IAsyncOperationWithProgress<connect_error, connect_error> : impl_IAsyncOperationWithProgress<connect_error, connect_error> {};
}
namespace {

	std::string narrow(const std::wstring & to_narrow)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
		return converter.to_bytes(to_narrow);
	}

	std::wstring widen(const std::string & to_widen)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
		return converter.from_bytes(to_widen);
	}

	struct win_connection : public io::tcp::connection {
		wns::StreamSocket m_socket;
		io::tcp::connection_security m_security;

		win_connection(io::tcp::connection_security security)
			:m_security(security){}

		~win_connection()
		{
			this->on_error.disconnect_all_slots();
			try {
				if (m_socket) {
					m_socket.Close();
				}
			}
			catch (winrt::hresult_error &) {
			}
		}

		winrt::Windows::Foundation::IAsyncAction resolveAndConnect(const std::string_view & host, std::uint16_t port) try
		{
			const auto endpoints = co_await wns::StreamSocket::GetEndpointPairsAsync(
				wn::HostName(widen(static_cast<std::string>(host))),
				std::to_wstring(port));

			for (const auto & endpoint : endpoints) {
				connect_error err = connect_error::retry;
				do {
					err = co_await this->connectAsync(endpoint);
				}while(err == connect_error::retry);

				if (err == connect_error::success) {
					co_return;
				}
			}
		}
		catch (const winrt::hresult_error & hr)
		{
			this->on_error(boost::system::error_code(hr.code(), boost::system::get_system_category()));
		}

		

		winrt::Windows::Foundation::IAsyncOperation<connect_error> connectAsync(const wn::EndpointPair & target)
		try
		{
			const auto security_level = m_security != io::tcp::connection_security::none ? wns::SocketProtectionLevel::Tls12 : wns::SocketProtectionLevel::PlainSocket;
			co_await m_socket.ConnectAsync(target, security_level);
			this->on_valid_connection(narrow(target.RemoteHostName().ToString()));
			this->on_connect({});
			this->read();
			co_return connect_error::success;
		}
		catch (const winrt::hresult_error & hr) {
			auto socketInfo = this->m_socket.Information();
			auto certificateErrors = socketInfo.ServerCertificateErrors();
			if (m_security == io::tcp::connection_security::no_verify
				&& certificateErrors.Size() != 0
				&& socketInfo.ServerCertificateErrorSeverity() == wns::SocketSslErrorSeverity::Ignorable) {
	
				const auto ignorableErrors = m_socket.Control().IgnorableServerCertificateErrors();
				for (const auto & err : certificateErrors) {
					ignorableErrors.Append(err);
				}
				co_return connect_error::retry;
			}

			const auto socketError = wns::SocketError::GetStatus(hr.code());
			switch (socketError) {
			case wns::SocketErrorStatus::ConnectionRefused:
			case wns::SocketErrorStatus::ConnectionTimedOut:
				co_return connect_error::next;
			default:
				throw;
			}
		}

		void connect(const std::string_view & host, std::uint16_t port) override final {
			auto control = m_socket.Control();
			control.KeepAlive(true);
			// disable nagel
			control.NoDelay(true);
			control.SerializeConnectionAttempts(true);
			resolveAndConnect(host, port);
		}

		void enqueue_message(const std::string & message) override final {
			this->write(message);
		}

		winrt::Windows::Foundation::IAsyncAction write(const std::string_view& message)
		try
		{
			std::vector<std::uint8_t> temp{ message.cbegin(), message.cend() };
			wss::DataWriter writer(m_socket.OutputStream());
			writer.WriteBytes(temp);

			co_await writer.StoreAsync();
			co_await m_socket.OutputStream().FlushAsync();
			writer.DetachStream();
		}
		catch (const winrt::hresult_error& hr) {
			this->on_error(boost::system::error_code(hr.code(), boost::system::get_system_category()));
		}

		bool connected() const override final {
			return static_cast<bool>(this->m_socket);
		}

		winrt::Windows::Foundation::IAsyncAction read()
		try
		{
			wss::Buffer buffer(512u);
			auto resultBuffer = co_await this->m_socket.InputStream().ReadAsync(buffer, 512u, wss::InputStreamOptions::Partial);
			auto reader = wss::DataReader::FromBuffer(resultBuffer);
			reader.InputStreamOptions(wss::InputStreamOptions::Partial);
			const auto to_read = std::min(reader.UnconsumedBufferLength(), 512u);
			if (to_read == 0) {
				co_return;
			}
			std::vector<std::uint8_t> temp(to_read);
			reader.ReadBytes(temp);
			this->on_message({ temp.cbegin(), temp.cend() }, temp.size());
			this->read();
		}
		catch (const winrt::hresult_error & hr) {
			this->on_error(boost::system::error_code(hr.code(), boost::system::get_system_category()));
		}

		void poll() override final {
			m_socket.OutputStream().FlushAsync().get();
		}
	};
}

namespace io::tcp {
	std::unique_ptr<connection>
		connection::create_connection(connection_security security) {
		return std::make_unique<win_connection>(security);
	}
}

