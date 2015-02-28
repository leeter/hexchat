/* HexChat
* Copyright (C) 2015 Leetsoftwerx.
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

#ifndef HEXCHAT_GLIST_ITERATORS_HPP
#define HEXCHAT_GLIST_ITERATORS_HPP

#ifdef _MSC_VER
#pragma once
#endif

#include <iterator>

#include <glib.h>

namespace glib_helper
{

	template<typename T, typename L = GSList>
	class glist_iterator : public std::iterator<std::forward_iterator_tag, T>
	{
		L * list;
	public:
		using reference = typename std::iterator < std::forward_iterator_tag, T >::reference ;
		using pointer = typename std::iterator<std::forward_iterator_tag, T>::pointer;
		using const_reference = const reference;
		using const_pointer = const pointer;
		explicit glist_iterator(L * list = nullptr)
			:list(list){}

		auto operator*() -> reference;
		auto operator->() -> pointer;
		auto operator*() const->const_reference;
		auto operator->() const->const_pointer;
		auto operator++()->glist_iterator&;
		auto operator++(int)->glist_iterator&;

		bool equal(glist_iterator const& rhs) const
		{
			return this->list == rhs.list;
		}
	};

	template<typename T, typename L>
	auto glist_iterator<T, L>::operator*() -> reference
	{
		return *static_cast<T*>(list->data);
	}

	template<typename T, typename L>
	auto glist_iterator<T, L>::operator*() const -> const_reference
	{
		return *static_cast<const T*>(list->data);
	}

	template<typename T, typename L>
	auto glist_iterator<T, L>::operator->() -> pointer
	{
		return static_cast<T*>(list->data);
	}

	template<typename T, typename L>
	auto glist_iterator<T, L>::operator->() const -> const_pointer
	{
		return static_cast<const T*>(list->data);
	}


	template<typename T, typename L>
	auto glist_iterator<T, L>::operator++()->glist_iterator&
	{
		list = list ? list->next : nullptr;
		return *this;
	}

	template<typename T, typename L>
	auto glist_iterator<T, L>::operator++(int) -> glist_iterator&
	{
		glist_iterator temp = *this;
		list = list ? list->next : nullptr;
		return temp;
	}

	template<typename T, typename L>
	inline bool operator==(glist_iterator<T, L> const& lhs, glist_iterator<T, L> const& rhs)
	{
		return lhs.equal(rhs);
	}

	template<typename T, typename L>
	inline bool operator!=(glist_iterator<T, L> const& lhs, glist_iterator<T, L> const& rhs)
	{
		return !lhs.equal(rhs);
	}
} // namespace glib_helper


#endif