
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef WIN32
#error this file relies on win32 specific functionality
#endif
#define SECURITY_WIN32
//#pragma comment(lib, "windowsapp")
#pragma comment(lib, "runtimeobject")
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <roapi.h>
#include <experimental/resumable>

#include <array>
#include <string>
#include <locale>
#include <codecvt>
#include <memory>
#include <mutex>
#include <optional>

#include "tcp_connection.hpp"


namespace wns = winrt::Windows::Networking::Sockets;
namespace wn = winrt::Windows::Networking;
namespace wss = winrt::Windows::Storage::Streams;
namespace wsc = winrt::Windows::Security::Cryptography;



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
		std::vector<winrt::hstring> m_outbound_queue;
		std::mutex m_queuemutex;
		//std::optional<winrt::Windows::Foundation::IAsyncOperationWithProgress<winrt::Windows::Storage::Streams::IBuffer, uint32_t>> m_pending_read;
		io::tcp::connection_security m_security;

		win_connection(io::tcp::connection_security security)
			:m_security(security){}

		~win_connection()
		{
			this->on_error.disconnect_all_slots();
			try {
				/*if (m_pending_read) {
					m_pending_read->Cancel();
				}*/
				if (m_socket) {
					m_socket.Close();
				}
			}
			catch (winrt::hresult_error &) {
			}
		}

		winrt::Windows::Foundation::IAsyncAction connectAsync(boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
		try
		{
			if (endpoint_iterator == boost::asio::ip::tcp::resolver::iterator()) {
				return;
			}
			const auto security_level = m_security != io::tcp::connection_security::none ? wns::SocketProtectionLevel::Tls12 : wns::SocketProtectionLevel::PlainSocket;
			co_await m_socket.ConnectAsync(wn::HostName(widen(endpoint_iterator->host_name())), widen(endpoint_iterator->service_name()), security_level);
			this->on_valid_connection(endpoint_iterator->host_name());
			this->on_connect({});
			this->read();
		}
		catch (const winrt::hresult_error & hr) {
			auto socketInfo = this->m_socket.Information();
			auto certificateErrors = socketInfo.ServerCertificateErrors();
			if (m_security == io::tcp::connection_security::no_verify
				&& certificateErrors.Size() != 0
				&& socketInfo.ServerCertificateErrorSeverity() == wns::SocketSslErrorSeverity::Ignorable) {
				std::vector<winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult> tempResults(this->m_socket.Information().ServerCertificateErrors().Size());
				certificateErrors.GetMany(0, tempResults);
				auto ignorableErrors = m_socket.Control().IgnorableServerCertificateErrors();
				for (const auto & err : tempResults) {
					ignorableErrors.Append(err);
				}
				this->connectAsync(endpoint_iterator);
				return;
			}

			const auto socketError = wns::SocketError::GetStatus(hr.code());
			switch (socketError) {
			case wns::SocketErrorStatus::ConnectionRefused:
			case wns::SocketErrorStatus::ConnectionTimedOut:
				++endpoint_iterator;
				this->connectAsync(endpoint_iterator);
				break;
			default:
				this->on_error(boost::system::error_code(hr.code(), boost::system::get_system_category()));
				break;
			}
		}

		void connect(boost::asio::ip::tcp::resolver::iterator endpoint_iterator) override final {
			auto control = m_socket.Control();
			control.KeepAlive(true);
			// disable nagel
			control.NoDelay(true);
			connectAsync(endpoint_iterator);
		}

		void enqueue_message(const std::string & message) override final {
			this->m_outbound_queue.push_back(widen(message));
			// return if we have a pending write
			if (this->m_outbound_queue.size() > 1) {
				return;
			}

			this->write();
		}

		winrt::Windows::Foundation::IAsyncAction write()
		try
		{
			wss::DataWriter writer(m_socket.OutputStream());
			writer.UnicodeEncoding(wss::UnicodeEncoding::Utf8);
			for (const auto & message : m_outbound_queue) {
				writer.WriteString(message);
			}
			m_outbound_queue.clear();

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
			reader.UnicodeEncoding(wss::UnicodeEncoding::Utf8);
			const auto to_read = std::min(reader.UnconsumedBufferLength(), 512u);
			if (to_read == 0) {
				return;
			}
			const auto result = narrow(reader.ReadString(to_read));
			this->on_message(result, result.size());
			this->read();
		}
		catch (const winrt::hresult_error & hr) {
			this->on_error(boost::system::error_code(hr.code(), boost::system::get_system_category()));
		}

		// glib calls MsgWaitForMultipleObjectsEx which should ensure callbacks happen no need to poll
		void poll() override final {}
	};
}

namespace io::tcp {
	std::unique_ptr<connection>
		connection::create_connection(connection_security security, boost::asio::io_service& io_service) {
		return std::make_unique<win_connection>(security);
	}
}

