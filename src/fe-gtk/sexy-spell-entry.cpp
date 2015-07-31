/*
 * @file libsexy/sexy-icon-entry.c Entry widget
 *
 * @Copyright (C) 2004-2006 Christian Hammond.
 * Some of this code is from gtkspell, Copyright (C) 2002 Evan Martin.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <gtk/gtk.h>
#include "sexy-spell-entry.hpp"
#include <fcntl.h>
#include <glib/gi18n.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "sexy-iso-codes.hpp"
#include "../common/marshal.h"

#ifdef WIN32
#include "../common/typedef.h"
#include <io.h>
#else
#include <unistd.h>
#endif

#include "../common/cfgfiles.hpp"
#include "../common/hexchatc.hpp"
#include "palette.hpp"
#include "xtext.hpp"

/*
 * Bunch of poop to make enchant into a runtime dependency rather than a
 * compile-time dependency.  This makes it so I don't have to hear the
 * complaints from people with binary distributions who don't get spell
 * checking because they didn't check their configure output.
 */
struct EnchantDict;
struct EnchantBroker;

using EnchantDictDescribeFn = void(*) (const char * const lang_tag,
									   const char * const provider_name,
									   const char * const provider_desc,
									   const char * const provider_file,
									   void * user_data);

static struct EnchantBroker * (*enchant_broker_init) (void);
static void (*enchant_broker_free) (struct EnchantBroker * broker);
static void (*enchant_broker_free_dict) (struct EnchantBroker * broker, struct EnchantDict * dict);
static void (*enchant_broker_list_dicts) (struct EnchantBroker * broker, EnchantDictDescribeFn fn, void * user_data);
static struct EnchantDict * (*enchant_broker_request_dict) (struct EnchantBroker * broker, const char *const tag);

static void (*enchant_dict_add_to_personal) (struct EnchantDict * dict, const char *const word, ssize_t len);
static void (*enchant_dict_add_to_session) (struct EnchantDict * dict, const char *const word, ssize_t len);
static int (*enchant_dict_check) (struct EnchantDict * dict, const char *const word, ssize_t len);
static void (*enchant_dict_describe) (struct EnchantDict * dict, EnchantDictDescribeFn fn, void * user_data);
static void (*enchant_dict_free_suggestions) (struct EnchantDict * dict, char **suggestions);
static void (*enchant_dict_store_replacement) (struct EnchantDict * dict, const char *const mis, ssize_t mis_len, const char *const cor, ssize_t cor_len);
static char ** (*enchant_dict_suggest) (struct EnchantDict * dict, const char *const word, ssize_t len, size_t * out_n_suggs);
static bool have_enchant = false;

struct SexySpellEntryPrivate
{
	struct EnchantBroker *broker;
	PangoAttrList        *attr_list;
	gint                  mark_character;
	GHashTable           *dict_hash;
	GSList               *dict_list;
	gchar               **words;
	gint                 *word_starts;
	gint                 *word_ends;
	gboolean              checked;
	gboolean              parseattr;
	gint                  preedit_length;

#if GTK_MAJOR_VERSION >= 3
	GdkRGBA *underline_color;
#endif
};

static void sexy_spell_entry_class_init(SexySpellEntryClass *klass);
static void sexy_spell_entry_editable_init (GtkEditableClass *iface);
static void sexy_spell_entry_init(SexySpellEntry *entry);
static void sexy_spell_entry_finalize(GObject *obj);
static void sexy_spell_entry_destroy(GObject *obj);
static gint sexy_spell_entry_draw(GtkWidget *widget, cairo_t *cr);
static gint sexy_spell_entry_expose(GtkWidget *widget, GdkEventExpose *event);
static gint sexy_spell_entry_button_press(GtkWidget *widget, GdkEventButton *event);
static void sexy_spell_entry_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec);
static void sexy_spell_entry_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec);
static void sexy_spell_entry_style_updated(GtkWidget *widget);

/* GtkEditable handlers */
static void sexy_spell_entry_changed(GtkEditable *editable, gpointer data);

/* Other handlers */
static gboolean sexy_spell_entry_popup_menu(GtkWidget *widget, SexySpellEntry *entry);

/* Internal utility functions */
//static gint       gtk_entry_find_position                     (GtkEntry             *entry,
//															   gint                  x);
static gboolean   word_misspelled                             (SexySpellEntry       *entry,
															   int                   start,
															   int                   end);
static gboolean   default_word_check                          (SexySpellEntry       *entry,
															   const gchar          *word);
static bool   sexy_spell_entry_activate_language_internal (SexySpellEntry       *entry,
															   const std::string     &lang,
															   GError              **error);
static gchar     *get_lang_from_dict                          (struct EnchantDict   *dict);
static void       sexy_spell_entry_recheck_all                (SexySpellEntry       *entry);
static void       entry_strsplit_utf8                         (GtkEntry             *entry,
															   gchar              ***set,
															   gint                **starts,
															   gint                **ends);

static GtkEntryClass *parent_class = NULL;

#ifdef HAVE_ISO_CODES
static int codetable_ref = 0;
#endif

#ifndef G_ADD_PRIVATE

#define G_ADD_PRIVATE(TypeName) { \
  //TypeName##_private_offset = \
    g_type_add_instance_private (g_define_type_id, sizeof (TypeName##Private)); \
}

static inline gpointer \
sexy_spell_entry_get_instance_private(SexySpellEntry *self) \
{ \
return (G_STRUCT_MEMBER_P (self, sexy_spell_entry_private_offset)); \
} 
#endif
//G_DEFINE_TYPE_EXTENDED(SexySpellEntry, sexy_spell_entry, GTK_TYPE_ENTRY, 0, G_IMPLEMENT_INTERFACE(GTK_TYPE_EDITABLE, sexy_spell_entry_editable_init)G_ADD_PRIVATE(SexySpellEntry))

G_DEFINE_TYPE_WITH_CODE(SexySpellEntry, sexy_spell_entry, GTK_TYPE_ENTRY,
	G_IMPLEMENT_INTERFACE(GTK_TYPE_EDITABLE, sexy_spell_entry_editable_init)
	G_ADD_PRIVATE(SexySpellEntry))

#define SEXY_SPELL_ENTRY_GET_PRIVATE(obj) \
		(G_TYPE_INSTANCE_GET_PRIVATE ((obj), SEXY_TYPE_SPELL_ENTRY, SexySpellEntryPriv))


static void
free_words(SexySpellEntryPrivate *priv)
{
	if (priv->words)
		g_strfreev(priv->words);
	if (priv->word_starts)
		g_free(priv->word_starts);
	if (priv->word_ends)
		g_free(priv->word_ends);
	priv->words = nullptr;
	priv->word_starts = nullptr;
	priv->word_ends = nullptr;
}


enum
{
	WORD_CHECK,
	LAST_SIGNAL
};
enum
{
	PROP_0,
	PROP_CHECKED,
	N_PROPERTIES
};
static guint signals[LAST_SIGNAL] = {0};

static gboolean
spell_accumulator(GSignalInvocationHint *hint, GValue *return_accu, const GValue *handler_return, gpointer data)
{
	gboolean ret = g_value_get_boolean(handler_return);
	/* Handlers return TRUE if the word is misspelled.  In this
	 * case, it means that we want to stop if the word is checked
	 * as correct */
	g_value_set_boolean (return_accu, ret);
	return ret;
}

template<class T>
static T module_symbol(GModule* enchant, const gchar* name)
{
	gpointer funcptr = nullptr;
	g_module_symbol(enchant, name, &funcptr);
	return reinterpret_cast<T>(funcptr);
}

static void
initialize_enchant ()
{
	GModule *enchant = g_module_open("libenchant." G_MODULE_SUFFIX, GModuleFlags());
	if (enchant == NULL)
	{
#ifndef WIN32
		enchant = g_module_open("libenchant.so.1", GModuleFlags());
		if (enchant == NULL)
		{
#ifdef __APPLE__
			enchant = g_module_open("libenchant.dylib", GModuleFlags());
			if (enchant == NULL)
#endif
				return;
		}
#else
		return;
#endif
	}

	have_enchant = true;

#define MODULE_SYMBOL(name, func) \
	func = module_symbol<decltype(func)>(enchant, (name)); \

	MODULE_SYMBOL("enchant_broker_init", enchant_broker_init)
	MODULE_SYMBOL("enchant_broker_free", enchant_broker_free)
	MODULE_SYMBOL("enchant_broker_free_dict", enchant_broker_free_dict)
	MODULE_SYMBOL("enchant_broker_list_dicts", enchant_broker_list_dicts)
	MODULE_SYMBOL("enchant_broker_request_dict", enchant_broker_request_dict)

	MODULE_SYMBOL("enchant_dict_add_to_personal", enchant_dict_add_to_personal)
	MODULE_SYMBOL("enchant_dict_add_to_session", enchant_dict_add_to_session)
	MODULE_SYMBOL("enchant_dict_check", enchant_dict_check)
	MODULE_SYMBOL("enchant_dict_describe", enchant_dict_describe)
	MODULE_SYMBOL("enchant_dict_free_suggestions",
				  enchant_dict_free_suggestions)
	MODULE_SYMBOL("enchant_dict_store_replacement",
				  enchant_dict_store_replacement)
	MODULE_SYMBOL("enchant_dict_suggest", enchant_dict_suggest)

#undef MODULE_SYMBOL
}

static void
sexy_spell_entry_class_init(SexySpellEntryClass *klass)
{
	initialize_enchant();

#ifndef G_ADD_PRIVATE
	g_type_class_add_private(klass, sizeof(SexySpellEntryPrivate));
#endif
	parent_class = static_cast<GtkEntryClass*>(g_type_class_peek_parent(klass));

	auto gobject_class = G_OBJECT_CLASS(klass);
	auto object_class  = G_OBJECT_CLASS(klass);
	auto widget_class  = GTK_WIDGET_CLASS(klass);

	if (have_enchant)
		klass->word_check = default_word_check;

	gobject_class->set_property = sexy_spell_entry_set_property;
	gobject_class->get_property = sexy_spell_entry_get_property;
	gobject_class->finalize = sexy_spell_entry_finalize;

	object_class->dispose = sexy_spell_entry_destroy;

#if GTK_CHECK_VERSION(3, 0, 0)
	widget_class->draw = sexy_spell_entry_draw;
#else
	widget_class->expose_event = sexy_spell_entry_expose;
#endif
	widget_class->button_press_event = sexy_spell_entry_button_press;

	/**
	 * SexySpellEntry::word-check:
	 * @entry: The entry on which the signal is emitted.
	 * @word: The word to check.
	 *
	 * The ::word-check signal is emitted whenever the entry has to check
	 * a word.  This allows the application to mark words as correct even
	 * if none of the active dictionaries contain it, such as nicknames in
	 * a chat client.
	 *
	 * Returns: %FALSE to indicate that the word should be marked as
	 * correct.
	 */
	signals[WORD_CHECK] = g_signal_new("word_check",
					   G_TYPE_FROM_CLASS(object_class),
					   G_SIGNAL_RUN_LAST,
					   G_STRUCT_OFFSET(SexySpellEntryClass, word_check),
					   (GSignalAccumulator) spell_accumulator, NULL,
					   _hexchat_marshal_BOOLEAN__STRING,
					   G_TYPE_BOOLEAN,
					   1, G_TYPE_STRING);
	/**
	* SexySpellEntry:checked:
	*
	* If checking of spelling is enabled.
	*
	* Since: 1.0
	*/
	g_object_class_install_property(object_class,static_cast<int>(PROP_CHECKED),
		g_param_spec_boolean("checked", "Checked",
		"If checking spelling is enabled",
		TRUE, static_cast<GParamFlags>(G_PARAM_READWRITE)));

#if GTK_MAJOR_VERSION >= 3
	/**
	* SexySpellEntry:underline-color:
	*
	* The underline color of misspelled words.
	* Defaults to red.
	*
	* Since: 1.0
	*/
	gtk_widget_class_install_style_property(widget_class,
		g_param_spec_boxed("underline-color", "Underline Color",
		"Underline color of misspelled words.",
		GDK_TYPE_RGBA, G_PARAM_READABLE)
#endif
}

static void
sexy_spell_entry_editable_init (GtkEditableClass *)
{
}

static void
sexy_spell_entry_set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY(obj);

	switch (prop_id)
	{
	case PROP_CHECKED:
		sexy_spell_entry_set_checked(entry, g_value_get_boolean(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
sexy_spell_entry_get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY(obj);

	switch (prop_id)
	{
	case PROP_CHECKED:
		g_value_set_boolean(value, sexy_spell_entry_get_checked(entry));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}
static gint
sexy_spell_entry_find_position(SexySpellEntry *entry, gint x)
{
	
	PangoLayout *layout;
	PangoLayoutLine *line;
	const gchar *text;
	gint cursor_index;
	gint index;
	gint pos;
	gboolean trailing;

	x = x + GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "scroll-offset"));

	layout = gtk_entry_get_layout(GTK_ENTRY(entry));
	text = pango_layout_get_text(layout);
	cursor_index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "cursor-position"));

	line = pango_layout_get_line_readonly(layout, 0);
	pango_layout_line_x_to_index(line, x * PANGO_SCALE, &index, &trailing);
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (index >= cursor_index && priv->preedit_length)
	{
		if (index >= cursor_index + priv->preedit_length)
		{
			index -= priv->preedit_length;
		}
		else
		{
			index = cursor_index;
			trailing = FALSE;
		}
	}

	pos = g_utf8_pointer_to_offset(text, text + index);
	pos += trailing;

	return pos;
}

static void
sexy_spell_entry_preedit_changed(SexySpellEntry *entry, gchar *preedit, gpointer userdata)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	priv->preedit_length = strlen(preedit);
}
//static gint
//gtk_entry_find_position (GtkEntry *entry, gint x)
//{
//	gint scroll_offset = 0;
//	gint cursor_position = 0;
//	g_object_get(entry, "scroll-offset", &scroll_offset, "cursor-position", &cursor_position, nullptr);
//	x = x + scroll_offset;
//
//	auto layout = gtk_entry_get_layout(entry);
//	auto text = pango_layout_get_text(layout);
//	auto cursor_index = g_utf8_offset_to_pointer(text, cursor_position) - text;
//
//	auto line = static_cast<PangoLayoutLine *>(pango_layout_get_lines(layout)->data);
//	int index;
//	int trailing;
//	pango_layout_line_x_to_index(line, x * PANGO_SCALE, &index, &trailing);
//	if (index >= cursor_index && entry->preedit_length) {
//		if (index >= cursor_index + entry->preedit_length) {
//			index -= entry->preedit_length;
//		} else {
//			index = cursor_index;
//			trailing = 0;
//		}
//	}
//
//	auto pos = g_utf8_pointer_to_offset (text, text + index);
//	pos += trailing;
//	if (pos > std::numeric_limits<gint>::max())
//		throw std::overflow_error("position is greater than a gint can hold and would overflow");
//
//	return static_cast<gint>(pos);
//}

static void
insert_hiddenchar (SexySpellEntry *entry, guint start, guint end)
{
	/* FIXME: Pango does not properly reflect the new widths after a char
	 * is 'hidden' */
#if 0
	PangoAttribute *hattr;
	PangoRectangle *rect = g_malloc (sizeof (PangoRectangle));

	rect->x = 0;
	rect->y = 0;
	rect->width = 0;
	rect->height = 0;

	hattr = pango_attr_shape_new (rect, rect);
	hattr->start_index = start;
	hattr->end_index = end;
	pango_attr_list_insert (entry->priv->attr_list, hattr);

	g_free (rect);
#endif
}

static void
insert_underline_error (SexySpellEntry *entry, guint start, guint end)
{
	auto ucolor = pango_attr_underline_color_new (colors[COL_SPELL].red, colors[COL_SPELL].green, colors[COL_SPELL].blue);
	auto unline = pango_attr_underline_new (PANGO_UNDERLINE_ERROR);

	ucolor->start_index = start;
	unline->start_index = start;

	ucolor->end_index = end;
	unline->end_index = end;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	pango_attr_list_insert (priv->attr_list, ucolor);
	pango_attr_list_insert (priv->attr_list, unline);
}

static void
insert_underline (SexySpellEntry *entry, guint start, bool toggle)
{
	PangoAttribute *uattr = pango_attr_underline_new (toggle ? PANGO_UNDERLINE_NONE : PANGO_UNDERLINE_SINGLE);
	uattr->start_index = start;
	uattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	pango_attr_list_change (priv->attr_list, uattr);
}

static void
insert_bold (SexySpellEntry *entry, guint start, bool toggle)
{
	PangoAttribute *battr = pango_attr_weight_new (toggle ? PANGO_WEIGHT_NORMAL : PANGO_WEIGHT_BOLD);
	battr->start_index = start;
	battr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	pango_attr_list_change (priv->attr_list, battr);
}

static void
insert_italic (SexySpellEntry *entry, guint start, bool toggle)
{
	PangoAttribute *iattr  = pango_attr_style_new (toggle ? PANGO_STYLE_NORMAL : PANGO_STYLE_ITALIC); 
	iattr->start_index = start;
	iattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	pango_attr_list_change (priv->attr_list, iattr);
}

static void
insert_color (SexySpellEntry *entry, guint start, int fgcolor, int bgcolor)
{
	PangoAttribute *fgattr;
	PangoAttribute *ulattr;
	PangoAttribute *bgattr;

	if (fgcolor < 0 || fgcolor > MAX_COL)
	{
		fgattr = pango_attr_foreground_new (colors[COL_FG].red, colors[COL_FG].green, colors[COL_FG].blue);
		ulattr = pango_attr_underline_color_new (colors[COL_FG].red, colors[COL_FG].green, colors[COL_FG].blue);
	}
	else
	{
		fgattr = pango_attr_foreground_new (colors[fgcolor].red, colors[fgcolor].green, colors[fgcolor].blue);
		ulattr = pango_attr_underline_color_new (colors[fgcolor].red, colors[fgcolor].green, colors[fgcolor].blue);
	}

	if (bgcolor < 0 || bgcolor > MAX_COL)
		bgattr = pango_attr_background_new (colors[COL_BG].red, colors[COL_BG].green, colors[COL_BG].blue);
	else
		bgattr = pango_attr_background_new (colors[bgcolor].red, colors[bgcolor].green, colors[bgcolor].blue);
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	fgattr->start_index = start;
	fgattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (priv->attr_list, fgattr);
	ulattr->start_index = start;
	ulattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (priv->attr_list, ulattr);
	bgattr->start_index = start;
	bgattr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_change (priv->attr_list, bgattr);
}

static void
insert_reset (SexySpellEntry *entry, guint start)
{
	insert_bold (entry, start, true);
	insert_underline (entry, start, true);
	insert_italic (entry, start, true);
	insert_color (entry, start, -1, -1);
}

static void
get_word_extents_from_position(SexySpellEntry *entry, gint *start, gint *end, guint position)
{
	*start = -1;
	*end = -1;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (priv->words == NULL)
		return;

	auto text = gtk_entry_get_text(GTK_ENTRY(entry));
	gint bytes_pos = (gint) (g_utf8_offset_to_pointer(text, position) - text);

	for (int i = 0; priv->words[i]; i++) {
		if (bytes_pos >= priv->word_starts[i] &&
			bytes_pos <= priv->word_ends[i]) {
			*start = priv->word_starts[i];
			*end   = priv->word_ends[i];
			return;
		}
	}
}

static void
add_to_dictionary(GtkWidget *menuitem, SexySpellEntry *entry)
{
	if (!have_enchant)
		return;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	gint start, end;
	get_word_extents_from_position(entry, &start, &end, priv->mark_character);
	glib_string word (gtk_editable_get_chars(GTK_EDITABLE(entry), start, end));

	auto dict = static_cast<EnchantDict*>(g_object_get_data(G_OBJECT(menuitem), "enchant-dict"));
	if (dict)
		enchant_dict_add_to_personal(dict, word.get(), -1);

	if (priv->words) {
		g_strfreev(priv->words);
		g_free(priv->word_starts);
		g_free(priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
	sexy_spell_entry_recheck_all (entry);
}

static void
ignore_all(GtkWidget *menuitem, SexySpellEntry *entry)
{
	if (!have_enchant)
		return;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	gint start, end;
	get_word_extents_from_position(entry, &start, &end, priv->mark_character);
	glib_string word(gtk_editable_get_chars(GTK_EDITABLE(entry), start, end));

	for (auto li = priv->dict_list; li; li = g_slist_next (li)) {
		auto dict = static_cast<EnchantDict *>(li->data);
		enchant_dict_add_to_session(dict, word.get(), -1);
	}

	if (priv->words) {
		g_strfreev(priv->words);
		g_free(priv->word_starts);
		g_free(priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
}

static void
replace_word(GtkWidget *menuitem, SexySpellEntry *entry)
{
	if (!have_enchant)
		return;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	gint start, end;
	get_word_extents_from_position(entry, &start, &end, priv->mark_character);
	glib_string oldword(gtk_editable_get_chars(GTK_EDITABLE(entry), start, end));
	auto newword = gtk_label_get_text(GTK_LABEL(gtk_bin_get_child (GTK_BIN(menuitem))));

	auto cursor = gtk_editable_get_position(GTK_EDITABLE(entry));
	/* is the cursor at the end? If so, restore it there */
	if (g_utf8_strlen(gtk_entry_get_text(GTK_ENTRY(entry)), -1) == cursor)
		cursor = -1;
	else if(cursor > start && cursor <= end)
		cursor = start;

	gtk_editable_delete_text(GTK_EDITABLE(entry), start, end);
	gtk_editable_set_position(GTK_EDITABLE(entry), start);
	gtk_editable_insert_text(GTK_EDITABLE(entry), newword, strlen(newword),
							 &start);
	gtk_editable_set_position(GTK_EDITABLE(entry), cursor);

	auto dict = static_cast<EnchantDict *>(g_object_get_data(G_OBJECT(menuitem), "enchant-dict"));

	if (dict)
	{
		enchant_dict_store_replacement(dict,
			oldword.get(), -1,
			newword, -1);
	}
}

namespace {
	class suggestion_deleter
	{
		EnchantDict* _dict;
	public:
		suggestion_deleter(EnchantDict* dict) NOEXCEPT
			:_dict(dict)
		{}
		suggestion_deleter(suggestion_deleter && other) NOEXCEPT
		{
			this->operator=(std::forward<suggestion_deleter&&>(other));
		}
		suggestion_deleter & operator=(suggestion_deleter&& other) NOEXCEPT
		{
			if (this != &other)
			{
				std::swap(this->_dict, other._dict);
			}
			return *this;
		}
		void operator()(gchar** suggestions) NOEXCEPT
		{
			enchant_dict_free_suggestions(_dict, suggestions);
		}
	};
}

static void
build_suggestion_menu(SexySpellEntry *entry, GtkWidget *menu, struct EnchantDict *dict, const gchar *word)
{
	if (!have_enchant)
		return;
	size_t n_suggestions;
	std::unique_ptr<gchar*[], suggestion_deleter> suggestions(enchant_dict_suggest(dict, word, -1, &n_suggestions), suggestion_deleter(dict));
	GtkWidget *mi;
	if (!suggestions || n_suggestions == 0) {
		/* no suggestions.  put something in the menu anyway... */
		GtkWidget *label = gtk_label_new("");
		gtk_label_set_markup(GTK_LABEL(label), _("<i>(no suggestions)</i>"));

		mi = gtk_separator_menu_item_new();
		gtk_container_add(GTK_CONTAINER(mi), label);
		gtk_widget_show_all(mi);
		gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);
	} else {
		/* build a set of menus with suggestions */
		for (size_t i = 0; i < n_suggestions; i++) {
			if ((i != 0) && (i % 10 == 0)) {
				mi = gtk_separator_menu_item_new();
				gtk_widget_show(mi);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);

				mi = gtk_menu_item_new_with_label(_("More..."));
				gtk_widget_show(mi);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);

				menu = gtk_menu_new();
				gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), menu);
			}

			mi = gtk_menu_item_new_with_label(suggestions[i]);
			g_object_set_data(G_OBJECT(mi), "enchant-dict", dict);
			g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(replace_word), entry);
			gtk_widget_show(mi);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
		}
	}
}

static GtkWidget *
build_spelling_menu(SexySpellEntry *entry, const gchar *word)
{
	if (!have_enchant)
		return nullptr;

	auto topmenu = gtk_menu_new();
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (!priv->dict_list)
		return topmenu;
	GtkWidget *mi;
	/* Suggestions */
	if (g_slist_length(priv->dict_list) == 1) {
		auto dict = static_cast<EnchantDict *>(priv->dict_list->data);
		build_suggestion_menu(entry, topmenu, dict, word);
	} else {
		for (auto li = priv->dict_list; li; li = g_slist_next (li)) {
			auto dict = static_cast<EnchantDict *>(li->data);
			glib_string lang(get_lang_from_dict(dict));
			glib_string lang_name(sexy_spell_entry_get_language_name (entry, lang.get()));
			if (lang_name)
			{
				mi = gtk_menu_item_new_with_label(lang_name.get());
			}
			else
			{
				mi = gtk_menu_item_new_with_label(lang.get());
			}

			gtk_widget_show(mi);
			gtk_menu_shell_append(GTK_MENU_SHELL(topmenu), mi);
			auto menu = gtk_menu_new();
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), menu);
			build_suggestion_menu(entry, menu, dict, word);
		}
	}

	/* Separator */
	mi = gtk_separator_menu_item_new ();
	gtk_widget_show(mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(topmenu), mi);

	/* + Add to Dictionary */
	glib_string label(g_strdup_printf(_("Add \"%s\" to Dictionary"), word));
	mi = gtk_image_menu_item_new_with_label(label.get());

	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), gtk_image_new_from_stock(GTK_STOCK_ADD, GTK_ICON_SIZE_MENU));

	if (g_slist_length(priv->dict_list) == 1) {
		auto dict = static_cast<EnchantDict *>(priv->dict_list->data);
		g_object_set_data(G_OBJECT(mi), "enchant-dict", dict);
		g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(add_to_dictionary), entry);
	} else {
		auto menu = gtk_menu_new();
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), menu);

		for (auto li = priv->dict_list; li; li = g_slist_next(li)) {
			auto dict = static_cast<EnchantDict *>(li->data);
			glib_string lang(get_lang_from_dict(dict));
			glib_string lang_name(sexy_spell_entry_get_language_name (entry, lang.get()));
			GtkWidget *submi;
			if (lang_name)
			{
				submi = gtk_menu_item_new_with_label(lang_name.get());
			}
			else 
			{
				submi = gtk_menu_item_new_with_label(lang.get());
			}
			g_object_set_data(G_OBJECT(submi), "enchant-dict", dict);

			g_signal_connect(G_OBJECT(submi), "activate", G_CALLBACK(add_to_dictionary), entry);

			gtk_widget_show(submi);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), submi);
		}
	}

	gtk_widget_show_all(mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(topmenu), mi);

	/* - Ignore All */
	mi = gtk_image_menu_item_new_with_label(_("Ignore All"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), gtk_image_new_from_stock(GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU));
	g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(ignore_all), entry);
	gtk_widget_show_all(mi);
	gtk_menu_shell_append(GTK_MENU_SHELL(topmenu), mi);

	return topmenu;
}

static void
sexy_spell_entry_populate_popup(SexySpellEntry *entry, GtkMenu *menu, gpointer data)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (!have_enchant || (priv->checked == FALSE))
		return;

	if (g_slist_length(priv->dict_list) == 0)
		return;
	gint start, end;
	get_word_extents_from_position(entry, &start, &end, priv->mark_character);
	if (start == end)
		return;
	if (!word_misspelled(entry, start, end))
		return;

	/* separator */
	auto mi = gtk_separator_menu_item_new();
	gtk_widget_show(mi);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);

	/* Above the separator, show the suggestions menu */
	auto icon = gtk_image_new_from_stock(GTK_STOCK_SPELL_CHECK, GTK_ICON_SIZE_MENU);
	mi = gtk_image_menu_item_new_with_label(_("Spelling Suggestions"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), icon);

	glib_string word(gtk_editable_get_chars(GTK_EDITABLE(entry), start, end));
	g_assert(word != NULL);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), build_spelling_menu(entry, word.get()));

	gtk_widget_show_all(mi);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);
}

static void
sexy_spell_entry_init(SexySpellEntry *entry)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	priv->dict_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	if (have_enchant)
	{
#ifdef HAVE_ISO_CODES
		if (codetable_ref == 0)
			codetable_init ();
		codetable_ref++;
#endif
		sexy_spell_entry_activate_default_languages(entry);
	}

	priv->attr_list = pango_attr_list_new();

	priv->checked = TRUE;
	priv->parseattr = TRUE;

	g_signal_connect(G_OBJECT(entry), "popup-menu", G_CALLBACK(sexy_spell_entry_popup_menu), entry);
	g_signal_connect(G_OBJECT(entry), "populate-popup", G_CALLBACK(sexy_spell_entry_populate_popup), NULL);
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(sexy_spell_entry_changed), NULL);
	g_signal_connect(G_OBJECT(entry), "preedit-changed",
		G_CALLBACK(sexy_spell_entry_preedit_changed), NULL);
}

static void
sexy_spell_entry_finalize(GObject *obj)
{
	SexySpellEntry *entry;

	g_return_if_fail(obj != NULL);
	g_return_if_fail(SEXY_IS_SPELL_ENTRY(obj));

	entry = SEXY_SPELL_ENTRY(obj);
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (priv->attr_list)
		pango_attr_list_unref(priv->attr_list);
	if (priv->dict_hash)
		g_hash_table_destroy(priv->dict_hash);
	free_words(priv);

	if (have_enchant) {
		if (priv->broker) {
			for (auto li = priv->dict_list; li; li = g_slist_next(li)) {
				struct EnchantDict *dict = (struct EnchantDict*) li->data;
				enchant_broker_free_dict (priv->broker, dict);
			}
			g_slist_free (priv->dict_list);

			enchant_broker_free(priv->broker);
		}
	}

#ifdef HAVE_ISO_CODES
	codetable_ref--;
	if (codetable_ref == 0)
		codetable_free ();
#endif

	if (G_OBJECT_CLASS(parent_class)->finalize)
		G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void
sexy_spell_entry_destroy(GObject *obj)
{
	g_return_if_fail(SEXY_IS_SPELL_ENTRY(obj));
	if (G_OBJECT_CLASS(parent_class)->dispose)
		G_OBJECT_CLASS(parent_class)->dispose(obj);
}

/**
 * sexy_spell_entry_new
 *
 * Creates a new SexySpellEntry widget.
 *
 * Returns: a new #SexySpellEntry.
 */
GtkWidget *
sexy_spell_entry_new(void)
{
	return GTK_WIDGET(g_object_new(SEXY_TYPE_SPELL_ENTRY, nullptr));
}

GQuark
sexy_spell_error_quark(void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string("sexy-spell-error-quark");
	return q;
}

static gboolean
default_word_check(SexySpellEntry *entry, const gchar *word)
{
	gboolean result = TRUE;

	if (!have_enchant)
		return result;

	if (g_unichar_isalpha(*word) == FALSE) {
		/* We only want to check words */
		return FALSE;
	}
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	for (auto li = priv->dict_list; li; li = g_slist_next (li)) {
		auto dict = static_cast<EnchantDict *>(li->data);
		if (enchant_dict_check(dict, word, strlen(word)) == 0) {
			result = FALSE;
			break;
		}
	}
	return result;
}

static gboolean
word_misspelled(SexySpellEntry *entry, int start, int end)
{
	if (start == end)
		return FALSE;
	auto text = gtk_entry_get_text(GTK_ENTRY(entry));
	glib_string word(g_new0(gchar, end - start + 2));

	g_strlcpy(word.get(), text + start, end - start + 1);
	gboolean ret = FALSE;
	g_signal_emit(entry, signals[WORD_CHECK], 0, word.get(), &ret);
	return ret;
}

static void
check_word(SexySpellEntry *entry, int start, int end)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	/* Check to see if we've got any attributes at this position.
	 * If so, free them, since we'll readd it if the word is misspelled */
	std::unique_ptr<PangoAttrIterator, decltype(&pango_attr_iterator_destroy)> it(pango_attr_list_get_iterator(priv->attr_list), pango_attr_iterator_destroy);
	if (!it)
		return;
	do {
		gint s, e;
		pango_attr_iterator_range(it.get(), &s, &e);
		if (s == start) {
			GSList *attrs = pango_attr_iterator_get_attrs(it.get());
			g_slist_foreach(attrs, (GFunc) pango_attribute_destroy, nullptr);
			g_slist_free(attrs);
		}
	} while (pango_attr_iterator_next(it.get()));

	if (word_misspelled(entry, start, end))
		insert_underline_error(entry, start, end);
}

static void
check_attributes (SexySpellEntry *entry, const char *text, int len)
{
	bool bold = false;
	bool italic = false;
	bool underline = false;
	int parsing_color = 0;
	char fg_color[3] = { 0 };
	char bg_color[3] = { 0 };
	int offset = 0;

	for (int i = 0; i < len; i++)
	{
		switch (text[i])
		{
		case ATTR_BOLD:
			insert_hiddenchar (entry, i, i + 1);
			insert_bold (entry, i, bold);
			bold = !bold;
			goto check_color;

		case ATTR_ITALICS:
			insert_hiddenchar (entry, i, i + 1);
			insert_italic (entry, i, italic);
			italic = !italic;
			goto check_color;

		case ATTR_UNDERLINE:
			insert_hiddenchar (entry, i, i + 1);
			insert_underline (entry, i, underline);
			underline = !underline;
			goto check_color;

		case ATTR_RESET:
			insert_hiddenchar (entry, i, i + 1);
			insert_reset (entry, i);
			bold = false;
			italic = false;
			underline = false;
			goto check_color;

		case ATTR_HIDDEN:
			insert_hiddenchar (entry, i, i + 1);
			goto check_color;

		case ATTR_REVERSE:
			insert_hiddenchar (entry, i, i + 1);
			insert_color (entry, i, COL_BG, COL_FG);
			goto check_color;

		case '\n':
			insert_reset (entry, i);
			parsing_color = 0;
			break;

		case ATTR_COLOR:
			parsing_color = 1;
			offset = 1;
			break;

		default:
check_color:
			if (!parsing_color)
				continue;

			if (!g_unichar_isdigit (text[i]))
			{
				if (text[i] == ',' && parsing_color <= 3)
				{
					parsing_color = 3;
					offset++;
					continue;
				}
				else
					parsing_color = 5;
			}

			/* don't parse background color without a comma */
			else if (parsing_color == 3 && text[i - 1] != ',')
				parsing_color = 5;

			switch (parsing_color)
			{
			case 1:
				fg_color[0] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 2:
				fg_color[1] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 3:
				bg_color[0] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 4:
				bg_color[1] = text[i];
				parsing_color++;
				offset++;
				continue;
			case 5:
				if (bg_color[0] != 0)
				{
					insert_hiddenchar (entry, i - offset, i);
					insert_color (entry, i, atoi (fg_color), atoi (bg_color));
				}
				else if (fg_color[0] != 0)
				{
					insert_hiddenchar (entry, i - offset, i);
					insert_color (entry, i, atoi (fg_color), -1);
				}
				else
				{
					/* No colors but some commas may have been added */
					insert_hiddenchar (entry, i - offset, i - offset + 1);
					insert_color (entry, i, -1, -1);
				}
				std::fill(std::begin(bg_color), std::end(bg_color), 0);
				std::fill(std::begin(fg_color), std::end(fg_color), 0);
				parsing_color = 0;
				offset = 0;
				continue;
			}
		}
	}
}

static void
sexy_spell_entry_recheck_all(SexySpellEntry *entry)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	/* Remove all existing pango attributes.  These will get readded as we check */
	pango_attr_list_unref(priv->attr_list);
	priv->attr_list = pango_attr_list_new();

	if (priv->parseattr)
	{
		/* Check for attributes */
		auto text = gtk_entry_get_text (GTK_ENTRY (entry));
		int text_len = gtk_entry_get_text_length (GTK_ENTRY (entry));
		check_attributes (entry, text, text_len);
	}

	if (have_enchant && priv->checked
		&& g_slist_length (priv->dict_list) != 0)
	{
		/* Loop through words */
		for (int i = 0; priv->words[i]; i++)
		{
			auto length = strlen (priv->words[i]);
			if (length == 0)
				continue;
			check_word (entry, priv->word_starts[i], priv->word_ends[i]);
		}
	}

	auto layout = gtk_entry_get_layout(GTK_ENTRY(entry));
	pango_layout_set_attributes(layout, priv->attr_list);

	if (gtk_widget_get_realized (GTK_WIDGET(entry)))
	{
		GtkAllocation allocation;
		gtk_widget_get_allocation (GTK_WIDGET(entry), &allocation);
		GdkRectangle rect;
		rect.x = 0; rect.y = 0;
		rect.width  = allocation.width;
		rect.height = allocation.height;
		auto widget = GTK_WIDGET(entry);
		gdk_window_invalidate_rect(gtk_widget_get_window (widget), &rect, TRUE);
	}
}

static void sexy_spell_entry_draw_real(GtkWidget *widget)
{
	g_return_if_fail(SEXY_IS_SPELL_ENTRY(widget));
	SexySpellEntry *entry = SEXY_SPELL_ENTRY(widget);
	auto priv = static_cast<SexySpellEntryPrivate*>(
		sexy_spell_entry_get_instance_private(entry));

	if (priv->checked)
	{
		auto layout = gtk_entry_get_layout(GTK_ENTRY(widget));
		pango_layout_set_attributes(layout, priv->attr_list);
	}
}

#if GTK_CHECK_VERSION(3, 0, 0)
static gint
sexy_spell_entry_draw(GtkWidget *widget, cairo_t *cr)
{
	sexy_spell_entry_draw_real(widget);
	return GTK_WIDGET_CLASS(sexy_spell_entry_parent_class)->draw(widget, cr);
}

#else
static gint
sexy_spell_entry_expose(GtkWidget *widget, GdkEventExpose *event)
{
	sexy_spell_entry_draw_real(widget);
	return GTK_WIDGET_CLASS(parent_class)->expose_event (widget, event);
}
#endif

static gint
sexy_spell_entry_button_press(GtkWidget *widget, GdkEventButton *event)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY(widget);
	GtkEntry *gtk_entry = GTK_ENTRY(widget);
	auto pos = sexy_spell_entry_find_position(entry, event->x);
	auto priv = static_cast<SexySpellEntryPrivate*>(
		sexy_spell_entry_get_instance_private(entry));
	priv->mark_character = pos;

	return GTK_WIDGET_CLASS(parent_class)->button_press_event (widget, event);
}

static gboolean
sexy_spell_entry_popup_menu(GtkWidget *widget, SexySpellEntry *entry)
{
	/* Menu popped up from a keybinding (menu key or <shift>+F10). Use
	 * the cursor position as the mark position */
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	priv->mark_character = gtk_editable_get_position (GTK_EDITABLE (entry));
	return FALSE;
}

static void
entry_strsplit_utf8(GtkEntry *entry, gchar ***set, gint **starts, gint **ends)
{
	auto layout = gtk_entry_get_layout(GTK_ENTRY(entry));
	auto text = gtk_entry_get_text(GTK_ENTRY(entry));
	gint n_attrs;
	auto log_attrs = pango_layout_get_log_attrs_readonly (layout, &n_attrs);
	gint n_strings = 0;
	/* Find how many words we have */
	for (gint i = 0; i < n_attrs; i++)
	{
		auto a = log_attrs[i];
		if (a.is_word_start && a.is_word_boundary)
			n_strings++;
	}

	*set    = g_new0(gchar *, n_strings + 1);
	*starts = g_new0(gint, n_strings);
	*ends   = g_new0(gint, n_strings);

	/* Copy out strings */
	for (gint i = 0, j = 0; i < n_attrs; i++)
	{
		auto a = log_attrs[i];
		if (a.is_word_start && a.is_word_boundary)
		{
			gint cend;
			/* Find the end of this string */
			for (cend = i; cend < n_attrs; cend++)
			{
				a = log_attrs[cend];
				if (a.is_word_end && a.is_word_boundary)
					break;
			}

			/* Copy sub-string */
			auto start = g_utf8_offset_to_pointer(text, i);
			auto bytes = (g_utf8_offset_to_pointer(text, cend) - start);
			(*set)[j]    = g_new0(gchar, bytes + 1);
			(*starts)[j] = (gint) (start - text);
			(*ends)[j]   = (gint) (start - text + bytes);
			g_utf8_strncpy((*set)[j], start, cend - i);

			/* Move on to the next word */
			j++;
		}
	}
}

static void
sexy_spell_entry_changed(GtkEditable *editable, gpointer data)
{
	SexySpellEntry *entry = SEXY_SPELL_ENTRY(editable);
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (priv->words)
	{
		g_strfreev(priv->words);
		g_free(priv->word_starts);
		g_free(priv->word_ends);
	}
	entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
}

static bool
enchant_has_lang(const std::string & lang, GSList *langs) {
	for (auto i = langs; i; i = g_slist_next(i))
	{
		if (lang == static_cast<const char*>(i->data))
		{
			return true;
		}
	}
	return false;
}

/**
 * sexy_spell_entry_activate_default_languages:
 * @entry: A #SexySpellEntry.
 *
 * Activate spell checking for languages specified in the 
 * text_spell_langs setting. These languages are
 * activated by default, so this function need only be called
 * if they were previously deactivated.
 */
void
sexy_spell_entry_activate_default_languages(SexySpellEntry *entry)
{
	GSList *enchant_langs;

	if (!have_enchant)
		return;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (!priv->broker)
		priv->broker = enchant_broker_init();

	enchant_langs = sexy_spell_entry_get_languages(entry);

	std::istringstream langs(prefs.hex_text_spell_langs);
	for (std::string lang; std::getline(langs, lang, ',');)
	{
		if (enchant_has_lang (lang, enchant_langs))
		{
			sexy_spell_entry_activate_language_internal (entry, lang, nullptr);
		}
	}

	g_slist_foreach(enchant_langs, (GFunc) g_free, NULL);
	g_slist_free(enchant_langs);

	/* If we don't have any languages activated, use "en" */
	if (priv->dict_list == NULL)
		sexy_spell_entry_activate_language_internal(entry, "en", NULL);

	sexy_spell_entry_recheck_all (entry);
}

static void
get_lang_from_dict_cb(const char * const lang_tag,
			  const char * const provider_name,
			  const char * const provider_desc,
			  const char * const provider_file,
			  void * user_data) {
	gchar **lang = (gchar **)user_data;
	*lang = g_strdup(lang_tag);
}

static gchar *
get_lang_from_dict(struct EnchantDict *dict)
{
	gchar *lang;

	if (!have_enchant)
		return NULL;

	enchant_dict_describe(dict, get_lang_from_dict_cb, &lang);
	return lang;
}

static bool
sexy_spell_entry_activate_language_internal(SexySpellEntry *entry, const std::string & lang, GError **error)
{
	struct EnchantDict *dict;

	if (!have_enchant)
		return false;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (!priv->broker)
		priv->broker = enchant_broker_init();

	if (g_hash_table_lookup(priv->dict_hash, lang.c_str()))
		return true;

	dict = enchant_broker_request_dict(priv->broker, lang.c_str());

	if (!dict) {
		g_set_error(error, SEXY_SPELL_ERROR, SEXY_SPELL_ERROR_BACKEND, _("enchant error for language: %s"), lang.c_str());
		return false;
	}

	enchant_dict_add_to_session (dict, "HexChat", strlen("HexChat"));
	priv->dict_list = g_slist_append(priv->dict_list, (gpointer) dict);
	g_hash_table_insert(priv->dict_hash, get_lang_from_dict(dict), (gpointer) dict);

	return true;
}

static void
dict_describe_cb(const char * const lang_tag,
		 const char * const provider_name,
		 const char * const provider_desc,
		 const char * const provider_file,
		 void * user_data)
{
	GSList **langs = (GSList **)user_data;

	*langs = g_slist_append(*langs, (gpointer)g_strdup(lang_tag));
}

/**
 * sexy_spell_entry_get_languages:
 * @entry: A #SexySpellEntry.
 *
 * Retrieve a list of language codes for which dictionaries are available.
 *
 * Returns: a new #GList object, or %NULL on error.
 */
GSList *
sexy_spell_entry_get_languages(const SexySpellEntry *entry)
{
	GSList *langs = NULL;

	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), NULL);

	if (enchant_broker_list_dicts == NULL)
		return NULL;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(const_cast<SexySpellEntry*>(entry)));
	if (!priv->broker)
		return NULL;

	enchant_broker_list_dicts(priv->broker, dict_describe_cb, &langs);

	return langs;
}

/**
 * sexy_spell_entry_get_language_name:
 * @entry: A #SexySpellEntry.
 * @lang: The language code to lookup a friendly name for.
 *
 * Get a friendly name for a given locale.
 *
 * Returns: The name of the locale. Should be freed with g_free()
 */
gchar *
sexy_spell_entry_get_language_name(const SexySpellEntry *entry,
								   const gchar *lang)
{
#ifdef HAVE_ISO_CODES
	const gchar *lang_name = "";
	const gchar *country_name = "";

	g_return_val_if_fail (have_enchant, NULL);

	if (codetable_ref == 0)
		codetable_init ();
		
	codetable_lookup (lang, &lang_name, &country_name);

	if (codetable_ref == 0)
		codetable_free ();

	if (strlen (country_name) != 0)
		return g_strdup_printf ("%s (%s)", lang_name, country_name);
	else
		return g_strdup_printf ("%s", lang_name);
#else
	return g_strdup (lang);
#endif
}

/**
 * sexy_spell_entry_language_is_active:
 * @entry: A #SexySpellEntry.
 * @lang: The language to use, in a form enchant understands.
 *
 * Determine if a given language is currently active.
 *
 * Returns: TRUE if the language is active.
 */
gboolean
sexy_spell_entry_language_is_active(const SexySpellEntry *entry,
									const gchar *lang)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(const_cast<SexySpellEntry*>(entry)));
	return (g_hash_table_lookup(priv->dict_hash, lang) != NULL);
}

/**
 * sexy_spell_entry_activate_language:
 * @entry: A #SexySpellEntry
 * @lang: The language to use in a form Enchant understands. Typically either
 *        a two letter language code or a locale code in the form xx_XX.
 * @error: Return location for error.
 *
 * Activate spell checking for the language specifed.
 *
 * Returns: FALSE if there was an error.
 */
gboolean
sexy_spell_entry_activate_language(SexySpellEntry *entry, const gchar *lang, GError **error)
{
	gboolean ret;

	g_return_val_if_fail(entry != NULL, FALSE);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), FALSE);
	g_return_val_if_fail(lang != NULL && *lang != '\0', FALSE);

	if (!have_enchant)
		return FALSE;

	if (error)
		g_return_val_if_fail(*error == NULL, FALSE);

	ret = sexy_spell_entry_activate_language_internal(entry, lang, error);

	if (ret) {
		auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
		if (priv->words) {
			g_strfreev(priv->words);
			g_free(priv->word_starts);
			g_free(priv->word_ends);
		}
		entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
		sexy_spell_entry_recheck_all(entry);
	}

	return ret;
}

/**
 * sexy_spell_entry_deactivate_language:
 * @entry: A #SexySpellEntry.
 * @lang: The language in a form Enchant understands. Typically either
 *        a two letter language code or a locale code in the form xx_XX.
 *
 * Deactivate spell checking for the language specifed.
 */
void
sexy_spell_entry_deactivate_language(SexySpellEntry *entry, const gchar *lang)
{
	g_return_if_fail(entry != NULL);
	g_return_if_fail(SEXY_IS_SPELL_ENTRY(entry));

	if (!have_enchant)
		return;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (!priv->dict_list)
		return;

	if (lang) {
		struct EnchantDict *dict;

		dict = static_cast<EnchantDict*>(g_hash_table_lookup(priv->dict_hash, lang));
		if (!dict)
			return;
		enchant_broker_free_dict(priv->broker, dict);
		priv->dict_list = g_slist_remove(priv->dict_list, dict);
		g_hash_table_remove (priv->dict_hash, lang);
	} else {
		/* deactivate all */
		GSList *li;
		struct EnchantDict *dict;

		for (li = priv->dict_list; li; li = g_slist_next(li)) {
			dict = (struct EnchantDict *)li->data;
			enchant_broker_free_dict(priv->broker, dict);
		}

		g_slist_free (priv->dict_list);
		g_hash_table_destroy (priv->dict_hash);
		priv->dict_hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
		priv->dict_list = nullptr;
	}

	free_words(priv);
	entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
}

/**
 * sexy_spell_entry_set_active_languages:
 * @entry: A #SexySpellEntry
 * @langs: A list of language codes to activate, in a form Enchant understands.
 *         Typically either a two letter language code or a locale code in the
 *         form xx_XX.
 * @error: Return location for error.
 *
 * Activate spell checking for only the languages specified.
 *
 * Returns: FALSE if there was an error.
 */
gboolean
sexy_spell_entry_set_active_languages(SexySpellEntry *entry, GSList *langs, GError **error)
{
	GSList *li;

	g_return_val_if_fail(entry != NULL, FALSE);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), FALSE);
	g_return_val_if_fail(langs != NULL, FALSE);

	if (!have_enchant)
		return FALSE;

	/* deactivate all languages first */
	sexy_spell_entry_deactivate_language(entry, NULL);

	for (li = langs; li; li = g_slist_next(li)) {
		if (sexy_spell_entry_activate_language_internal(entry,
			(const gchar *) li->data, error) == FALSE)
			return FALSE;
	}
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	free_words(priv);
	entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
	sexy_spell_entry_recheck_all(entry);
	return TRUE;
}

/**
 * sexy_spell_entry_get_active_languages:
 * @entry: A #SexySpellEntry
 *
 * Retrieve a list of the currently active languages.
 *
 * Returns: A GSList of char* values with language codes (en, fr, etc).  Both
 *          the data and the list must be freed by the user.
 */
GSList *
sexy_spell_entry_get_active_languages(SexySpellEntry *entry)
{
	GSList *ret = NULL, *li;
	struct EnchantDict *dict;
	gchar *lang;

	g_return_val_if_fail(entry != NULL, NULL);
	g_return_val_if_fail(SEXY_IS_SPELL_ENTRY(entry), NULL);

	if (!have_enchant)
		return NULL;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	for (li = priv->dict_list; li; li = g_slist_next(li)) {
		dict = (struct EnchantDict *) li->data;
		lang = get_lang_from_dict(dict);
		ret = g_slist_append(ret, lang);
	}
	return ret;
}

/**
* sexy_spell_entry_get_checked:
* @entry: A #SexySpellEntry.
*
* Queries a #SexySpellEntry and returns whether the entry has spell-checking enabled.
*
* Returns: %TRUE if the entry has spell-checking enabled.
*/
gboolean
sexy_spell_entry_get_checked(SexySpellEntry *entry)
{
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	return priv->checked;
}
/**
 * sexy_spell_entry_is_checked:
 * @entry: A #SexySpellEntry.
 *
 * Queries a #SexySpellEntry and returns whether the entry has spell-checking enabled.
 *
 * Returns: TRUE if the entry has spell-checking enabled.
 */
gboolean
sexy_spell_entry_is_checked(SexySpellEntry *entry)
{
	return sexy_spell_entry_get_checked(entry);
}

/**
 * sexy_spell_entry_set_checked:
 * @entry: A #SexySpellEntry.
 * @checked: Whether to enable spell-checking
 *
 * Sets whether the entry has spell-checking enabled.
 */
void
sexy_spell_entry_set_checked(SexySpellEntry *entry, gboolean checked)
{
	GtkWidget *widget;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (priv->checked == checked)
		return;

	priv->checked = checked;
	widget = GTK_WIDGET(entry);

	if (checked == FALSE && gtk_widget_get_realized (widget))
	{
		/* This will unmark any existing */
		sexy_spell_entry_recheck_all (entry);
	}
	else
	{
		free_words(priv);
		entry_strsplit_utf8(GTK_ENTRY(entry), &priv->words, &priv->word_starts, &priv->word_ends);
		sexy_spell_entry_recheck_all(entry);
	}
}

/**
* sexy_spell_entry_set_parse_attributes:
* @entry: A #SexySpellEntry.
* @parse: Whether to enable showing attributes
*
* Sets whether to enable showing attributes is enabled.
*/
void
sexy_spell_entry_set_parse_attributes (SexySpellEntry *entry, gboolean parse)
{
	GtkWidget *widget;
	auto priv = static_cast<SexySpellEntryPrivate*>(sexy_spell_entry_get_instance_private(entry));
	if (priv->parseattr == parse)
		return;

	priv->parseattr = parse;
	widget = GTK_WIDGET (entry);

	if (parse == FALSE && gtk_widget_get_realized (widget))
	{
		/* This will remove current attrs */
		sexy_spell_entry_recheck_all (entry);
	}
	else
	{
		free_words(priv);
		entry_strsplit_utf8 (GTK_ENTRY (entry), &priv->words, &priv->word_starts, &priv->word_ends);
		sexy_spell_entry_recheck_all (entry);
	}
}
