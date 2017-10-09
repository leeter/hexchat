/* HexChat
* Copyright (C) 2017 Leetsoftwerx
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


#pragma once

#include <atomic>
#include <exception>
#include <system_error>
#include <type_traits>
#include <winerror.h>
#include <minwindef.h>

namespace comhelp {

	class HResultException : public std::exception {
		HRESULT m_hr;

	public:
		HResultException(HRESULT hr)
			:m_hr(hr) {}

		HRESULT getHResult() const noexcept {
			return m_hr;
		}
	};

	void CheckResult(HRESULT hr) {
		if (FAILED(hr)) {
			throw HResultException(hr);
		}
	}

	HRESULT translate_exception() noexcept {
		try {
			throw;
		}
		catch (const HResultException& ex)
		{
			return ex.getHResult();
		}
		catch (const std::bad_alloc&)
		{
			return E_OUTOFMEMORY;
		}
		catch (const std::system_error& ex)
		{
			const auto& code = ex.code();
			if (code.category() == std::system_category()) {
				return HRESULT_FROM_WIN32(static_cast<unsigned long>(code.value()));
			}
			return E_UNEXPECTED;
		}
		catch (...)
		{
			return E_UNEXPECTED;
		}
	};

	template<typename T>
	std::enable_if_t<std::is_invocable_v<typename T()>, HRESULT>
		NoExceptBoundary(T&& fn) noexcept {
		try {
			fn();
			return S_OK;
		}
		catch (...) {
			return translate_exception();
		}
	}

	template<REFIID classID, typename PrimaryInterface, typename ... Interfaces>
	class DECLSPEC_NOVTABLE ComBase : public PrimaryInterface
	{
		std::atomic<ULONG> ref_count = 0ul;
		
		template<typename Iface>
		typename std::enable_if<std::is_abstract_v<typename Iface>, bool>::type isValid(REFIID riid) noexcept
		{
			return riid == __uuidof(typename Iface);
		}
		template<typename Iface, typename ... Interfaces>
		typename std::enable_if<sizeof...(Interfaces) != 0, bool>::type
			isValid(REFIID riid) noexcept
		{
			return riid == __uuidof(typename Iface) || isValid<typename Interfaces...>(riid);
		}
	public:

		virtual ~ComBase() = default;
		STDMETHODIMP QueryInterface( /* [in] */ REFIID riid,
			/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) override
		{
			if (riid == __uuidof(IUnknown)
				|| riid == classID
				|| riid == __uuidof(typename PrimaryInterface)
				|| isValid<typename Interfaces...>(riid)) {
				this->AddRef();
				*ppvObject = reinterpret_cast<void*>(this);
				return S_OK;
			}
			*ppvObject = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++ref_count;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const auto adjusted_count = --ref_count;
			if (adjusted_count == 0) {
				delete this;
			}
			return adjusted_count;
		}
	};
}