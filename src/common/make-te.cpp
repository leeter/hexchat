/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

/* Process textevents.in with make-te < textevents.in > textevents.h 2> textenums.h
 *
 * textevents.in notes:
 *
 *  - The number in the ending lines indicates the total number of arguments
 *    a text event supports. So don't touch them unless you actually modify the
 *    EMIT_SIGNAL commands, too.
 *
 *  - The "n" prefix means the event text does not have to be translated thus
 *    the N_() gettext encapsulation will be omitted.
 *
 *  - EMIT_SIGNAL is just a macro for text_emit() which can take a total amount
 *    of 4 event arguments, so events have a hard limit of 4 arguments.
 *
 *  - $t means the xtext tab, i.e. the vertical separator line for indented nicks.
 *    That means $t forces a new line for that event.
 *
 *  - Text events are emitted in ctcp.c, dcc.c, hexchat.c, ignore.c, inbound.c,
 *    modes.c, notify.c, outbound.c, proto-irc.c, server.c and text.c.
 */

#include <string>
#include <vector>
#include <iostream>
#include <limits>

int main()
{	
    std::vector<std::string> defines;
    int i = 0, max;
    std::cout.sync_with_stdio(false);
    std::cout << "/* this file is auto generated, edit textevents.in instead! */\n#ifdef __cplusplus\n#define EXPORT extern \"C\"\n#else\n#define EXPORT\n#endif\n\nEXPORT const struct text_event te[] = {\n";
    for (std::string name; std::getline(std::cin, name);)
    {
        std::string num, help, def, args;
        std::getline(std::cin, num);
        std::getline(std::cin, help);
        std::getline(std::cin, def);
        std::getline(std::cin, args);
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::cout << "\n{\"" << name << "\", " << help << ", ";
        if (args[0] == 'n')
        {
            args.erase(args.begin());
            std::cout << (std::stoi(args) | 128) << ", \n\"" << def << "\"},\n";
        }
        else
            std::cout << std::stoi(args) << ", \nN_(\"" << def << "\")},\n";
        defines.push_back(num);// = strdup(num.c_str());
        i++;
    }
    
    std::cout << "};\n";
    std::cout.flush();

    std::clog.sync_with_stdio(false);    
    std::clog << "/* this file is auto generated, edit textevents.in instead! */\n\nenum\n{\n";
    max = i;
    i = 0;
    while (i < max)
    {
        if (i + 1 < max)
        {
            std::clog << "\t" << defines[i] << ",\t\t" << defines[i + 1] << ",\n";
            i++;
        }
        else
            std::clog << '\t' << defines[i] << ",\n";
        i++;
    }
    std::clog << "\tNUM_XP\n};\n";
    std::clog.flush();
    return 0;
}
