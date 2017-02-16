#ifndef HEXCHAT_PRECOMPILED_HEADER
#define HEXCHAT_PRECOMPILED_HEADER

#pragma once
#define _FILE_OFFSET_BITS 64 /* allow selection of large files */
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define GDK_MULTIHEAD_SAFE

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <type_traits>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string/iter_find.hpp>
#include <boost/algorithm/string/finder.hpp>
#include <boost/system/error_code.hpp>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libintl.h>

#if defined (WIN32) || defined (__APPLE__)
#include <pango/pangocairo.h>
#endif

#endif // HEXCHAT_PRECOMPILED_HEADER