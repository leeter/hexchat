/* HexChat
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

#include <chrono>
#include <queue>
#include <boost/algorithm/string.hpp>
#include "throttled_queue.hpp"
#include "tcp_connection.hpp"
#include "sutter.hpp"

namespace io
{
	namespace irc
	{
		class throttled_queue::p_impl
		{
			std::priority_queue<std::pair<int, std::string> > outbound_queue;
			size_type queue_len_in_bytes;
			std::chrono::system_clock::time_point next_send;
			std::chrono::system_clock::time_point prev_now;
		public:
			p_impl()
				:queue_len_in_bytes(0)
			{}

			~p_impl(){}

			void push(const std::string & inbound)
			{
				int priority = 2;	/* pri 2 for most things */
				/* privmsg and notice get a lower priority */
				if (boost::icontains(inbound, "PRIVMSG") ||
					boost::icontains(inbound, "NOTICE"))
				{
					priority = 1;
				}
				else
				{
					/* WHO/MODE get the lowest priority */
					if (boost::icontains(inbound, "WHO") ||
						/* but only MODE queries, not changes */
						(boost::icontains(inbound, "MODE") &&
						inbound.find_first_of('-') == std::string::npos &&
						inbound.find_first_of('+') == std::string::npos))
						priority = 0;
				}

				this->outbound_queue.emplace(std::make_pair(priority, inbound));
				this->queue_len_in_bytes += inbound.size(); /* tcp_send_queue uses strlen */
			}
			
			void pop()
			{
				this->outbound_queue.pop();
			}

			boost::optional<std::string> front()
			{
				/* try priority 2,1,0 */
				auto now = std::chrono::system_clock::now();

				/* did the server close since the timeout was added? */
				if (this->outbound_queue.empty())
					return boost::none;

				auto & top = this->outbound_queue.top();

				if (this->next_send < now)
					this->next_send = now;
				if (this->next_send - now >= std::chrono::seconds(10))
				{
					/* check for clock skew */
					if (now >= this->prev_now)
						return boost::none;		  /* don't remove the timeout handler */
					/* it is skewed, reset to something sane */
					this->next_send = now;
				}
				std::string::size_type i;
				const char* p;
				for (p = top.second.c_str(), i = top.second.size(); i && *p != ' '; p++, i--);
				this->next_send += std::chrono::seconds(2 + i / 120);
				this->queue_len_in_bytes -= top.second.size();
				this->prev_now = now;
				
				return top.second;
			}

			size_type queue_length() const
			{
				return this->queue_len_in_bytes;
			}

			void clear()
			{
				decltype(this->outbound_queue) empty;
				std::swap(this->outbound_queue, empty);
			}
		};


		throttled_queue::throttled_queue()
			:impl(sutter::make_unique<throttled_queue::p_impl>())
		{}

		throttled_queue::size_type throttled_queue::queue_length() const
		{
			return impl->queue_length();
		}

		void throttled_queue::push(const std::string & inbound)
		{
			impl->push(inbound);
		}

		void throttled_queue::pop()
		{
			impl->pop();
		}

		void throttled_queue::clear()
		{
			impl->clear();
		}
	}
}