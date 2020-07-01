// IMPORTANT : when making changes in language detection logic and per-language
// rules here, be sure to also bump FORMATTING_VERSION_ID in src/lvtinydom.cpp

#include "../include/textlang.h"
#include "../include/hyphman.h"
#include "../include/lvtinydom.h"
#include "../include/fb2def.h"
#include "../include/crlog.h"

// Uncomment to see which lang_tags are seen and lang_cfg created
// #define DEBUG_LANG_USAGE

// Some macros to expand: LANG_STARTS_WITH(("fr") ("es"))   (no comma!)
// to: lang_tag.startsWith("fr") || lang_tag.startsWith("es") || false
// (from https://stackoverflow.com/questions/19680962/translate-sequence-in-macro-parameters-to-separate-macros )
#define PRIMITIVE_SEQ_ITERATE(...) __VA_ARGS__ ## _END
#define SEQ_ITERATE(...) PRIMITIVE_SEQ_ITERATE(__VA_ARGS__)
#define LANG_STARTS_WITH(seq) SEQ_ITERATE(LANG_STARTS_WITH_EACH_1 seq)
#define LANG_STARTS_WITH_EACH_1(...) lang_tag.startsWith(__VA_ARGS__) || LANG_STARTS_WITH_EACH_2
#define LANG_STARTS_WITH_EACH_2(...) lang_tag.startsWith(__VA_ARGS__) || LANG_STARTS_WITH_EACH_1
#define LANG_STARTS_WITH_EACH_1_END false
#define LANG_STARTS_WITH_EACH_2_END false

// (hyph_filename_prefix added because CoolReader may still have both
// current "Italian.pattern" and old "Italian_hyphen_(Alan).pdb".)
static struct {
    const char * lang_tag;
    const char * hyph_filename_prefix;
    const char * hyph_filename;
    int left_hyphen_min;
    int right_hyphen_min;
} _hyph_dict_table[] = {
    { "bg",    "Bulgarian",     "Bulgarian.pattern",     2, 2 },
    { "ca",    "Catalan",       "Catalan.pattern",       2, 2 },
    { "cs",    "Czech",         "Czech.pattern",         2, 2 },
    { "da",    "Danish",        "Danish.pattern",        2, 2 },
    { "nl",    "Dutch",         "Dutch.pattern",         2, 2 },
    { "en-GB", "English_GB",    "English_GB.pattern",    2, 2 },
    { "en",    "English_US",    "English_US.pattern",    2, 2 },
    { "fi",    "Finnish",       "Finnish.pattern",       2, 2 },
    { "fr",    "French",        "French.pattern",        2, 1 },
    { "gl",    "Galician",      "Galician.pattern",      2, 2 },
    { "de",    "German",        "German.pattern",        2, 2 },
    { "el",    "Greek",         "Greek.pattern",         2, 2 },
    { "hu",    "Hungarian",     "Hungarian.pattern",     2, 2 },
    { "is",    "Icelandic",     "Icelandic.pattern",     2, 2 },
    { "ga",    "Irish",         "Irish.pattern",         2, 2 },
    { "it",    "Italian",       "Italian.pattern",       2, 2 },
    { "no",    "Norwegian",     "Norwegian.pattern",     2, 2 },
    { "pl",    "Polish",        "Polish.pattern",        2, 2 },
    { "pt",    "Portuguese",    "Portuguese.pattern",    2, 2 },
    { "ro",    "Roman",         "Roman.pattern",         2, 2 },
    { "ru-GB", "Russian_EnGB",  "Russian_EnGB.pattern",  2, 2 },
    { "ru-US", "Russian_EnUS",  "Russian_EnUS.pattern",  2, 2 },
    { "ru",    "Russian",       "Russian.pattern",       2, 2 },
    { "sk",    "Slovak",        "Slovak.pattern",        2, 2 },
    { "sl",    "Slovenian",     "Slovenian.pattern",     2, 2 },
    { "es",    "Spanish",       "Spanish.pattern",       2, 2 },
    { "sv",    "Swedish",       "Swedish.pattern",       2, 2 },
    { "tr",    "Turkish",       "Turkish.pattern",       2, 2 },
    { "uk",    "Ukrain",        "Ukrain.pattern",        2, 2 },
    // No-lang hyph methods, for legacy HyphMan methods: other lang properties will be from English
    { "en#@none",        "@none",        "@none",        2, 2 },
    { "en#@softhyphens", "@softhyphens", "@softhyphens", 2, 2 },
    { "en#@algorithm",   "@algorithm",   "@algorithm",   2, 2 },
    { "en#@dictionary",  "@dictionary",  "@dictionary",  2, 2 }, // single instance of a dict created from
                                                                 // stream (by CoolReader on Android)
    { NULL, NULL, NULL, 0, 0 }
};

// Init global TextLangMan members
lString16 TextLangMan::_main_lang = TEXTLANG_DEFAULT_MAIN_LANG_16;
bool TextLangMan::_embedded_langs_enabled = TEXTLANG_DEFAULT_EMBEDDED_LANGS_ENABLED;
LVPtrVector<TextLangCfg> TextLangMan::_lang_cfg_list;

bool TextLangMan::_hyphenation_enabled = TEXTLANG_DEFAULT_HYPHENATION_ENABLED;
bool TextLangMan::_hyphenation_soft_hyphens_only = TEXTLANG_DEFAULT_HYPH_SOFT_HYPHENS_ONLY;
bool TextLangMan::_hyphenation_force_algorithmic = TEXTLANG_DEFAULT_HYPH_FORCE_ALGORITHMIC;
bool TextLangMan::_overridden_hyph_method =   !TEXTLANG_DEFAULT_HYPHENATION_ENABLED
                                            || TEXTLANG_DEFAULT_HYPH_SOFT_HYPHENS_ONLY
                                            || TEXTLANG_DEFAULT_HYPH_FORCE_ALGORITHMIC ;
// These will be set when we can
HyphMethod * TextLangMan::_no_hyph_method = NULL;
HyphMethod * TextLangMan::_algo_hyph_method = NULL;
HyphMethod * TextLangMan::_soft_hyphens_method = NULL;

TextLangMan::TextLangMan() {
}

TextLangMan::~TextLangMan() {
}

lUInt32 TextLangMan::getHash() {
    lUInt32 hash = _main_lang.getHash();
    hash = hash << 4;
    hash = hash + (_embedded_langs_enabled << 3);
    hash = hash + (_hyphenation_soft_hyphens_only << 2);
    hash = hash + (_hyphenation_force_algorithmic << 1);
    hash = hash + _hyphenation_enabled;
    // printf("TextLangMan::getHash %x\n", hash);
    return hash;
}

// No need to explicitely call this in frontend code.
// Calling HyphMan::uninit() will have this one called.
void TextLangMan::uninit() {
    _lang_cfg_list.clear();
}

// For HyphMan legacy methods
void TextLangMan::setMainLangFromHyphDict( lString16 id ) {
    // When setting up TextlangMan thru HyphMan legacy methods,
    // disable embedded langs, for a consistent hyphenation.
    TextLangMan::setEmbeddedLangsEnabled( false );
    // Update flags if asked for @none, @softhyphens or @algorithm
    TextLangMan::setHyphenationEnabled( id != HYPH_DICT_ID_NONE );
    TextLangMan::setHyphenationSoftHyphensOnly( id == HYPH_DICT_ID_SOFTHYPHENS );
    TextLangMan::setHyphenationForceAlgorithmic( id == HYPH_DICT_ID_ALGORITHM );

    for (int i=0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
        if ( id.startsWith( _hyph_dict_table[i].hyph_filename_prefix ) ) {
            TextLangMan::setMainLang( lString16(_hyph_dict_table[i].lang_tag) );
            #ifdef DEBUG_LANG_USAGE
            printf("TextLangMan::setMainLangFromHyphDict %s => %s\n",
                UnicodeToLocal(id).c_str(), UnicodeToLocal(TextLangMan::getMainLang()).c_str());
            #endif
            return;
        }
    }
    printf("CRE WARNING: lang not found for hyphenation dict: %s\n", UnicodeToLocal(id).c_str());
}

// Used only by TextLangCfg
HyphMethod * TextLangMan::getHyphMethodForLang( lString16 lang_tag ) {
    // Look for full lang_tag
#if 1
    // CoolReader use dynamically loaded hyphenation dictionaries (at startup)
    HyphDictionaryList* dictList = HyphMan::getDictList();
    HyphDictionary* dict;
    lang_tag.lowercase();
    for (int i = 0; i < dictList->length(); i++) {
        dict = dictList->get(i);
        if (dict) {
            if (lang_tag == TextLangMan::getLangTag(dict->getTitle()).lowercase())
                return HyphMan::getHyphMethodForDictionary( dict->getId(),
                            _hyph_dict_table[i].left_hyphen_min, _hyph_dict_table[i].right_hyphen_min);
        }
    }
    // Look for lang_tag initial subpart
    int m_pos = lang_tag.pos("-");
    if ( m_pos > 0 ) {
        lString16 lang_tag2 = lang_tag.substr(0, m_pos);
        lang_tag2.lowercase();
        for (int i = 0; i < dictList->length(); i++) {
            dict = dictList->get(i);
            if (dict) {
                if (lang_tag2 == TextLangMan::getLangTag(dict->getTitle()).lowercase())
                    return HyphMan::getHyphMethodForDictionary( dict->getId(),
                                _hyph_dict_table[i].left_hyphen_min, _hyph_dict_table[i].right_hyphen_min);
            }
        }
    }
#else
    // koreader use hardcoded hyphenation dictionary table
    for (int i=0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
        if ( lang_tag == lString16(_hyph_dict_table[i].lang_tag).lowercase() ) {
            return HyphMan::getHyphMethodForDictionary( lString16(_hyph_dict_table[i].hyph_filename),
                        _hyph_dict_table[i].left_hyphen_min, _hyph_dict_table[i].right_hyphen_min);
        }
    }
    // Look for lang_tag initial subpart
    int m_pos = lang_tag.pos("-");
    if ( m_pos > 0 ) {
        lString16 lang_tag2 = lang_tag.substr(0, m_pos);
        for (int i=0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
            if ( lang_tag2 == lString16(_hyph_dict_table[i].lang_tag).lowercase() ) {
                return HyphMan::getHyphMethodForDictionary( lString16(_hyph_dict_table[i].hyph_filename),
                            _hyph_dict_table[i].left_hyphen_min, _hyph_dict_table[i].right_hyphen_min);
            }
        }
    }
#endif
    // Fallback to English_US, as other languages are more likely to get mixed
    // with english text (it feels better than using @algorithm)
    return HyphMan::getHyphMethodForDictionary(TEXTLANG_FALLBACK_HYPH_DICT_ID);

}

// Return the (single and cached) TextLangCfg for the provided lang_tag
TextLangCfg * TextLangMan::getTextLangCfg( lString16 lang_tag ) {
    if ( !_embedded_langs_enabled ) {
        // Drop provided lang_tag: always return main lang TextLangCfg
        lang_tag = _main_lang;
    }
    // Not sure if we can lowercase lang_tag and avoid duplicate (Harfbuzz might
    // need the proper lang tag with some parts starting with some uppercase letter)
    for ( int i=0; i<_lang_cfg_list.length(); i++ ) {
        if ( _lang_cfg_list[i]->_lang_tag == lang_tag ) {
            // printf("TextLangCfg %s reused\n", UnicodeToLocal(lang_tag).c_str());
            // There should rarely be more than 3 lang in a document, so move
            // any requested far down in the list at top to shorten next loops.
            if ( i > 2 ) {
                _lang_cfg_list.move(0, i);
                return _lang_cfg_list[0];
            }
            return _lang_cfg_list[i];
        }
    }
    // Not found in cache: create it
    TextLangCfg * lang_cfg = new TextLangCfg( lang_tag );
    _lang_cfg_list.add( lang_cfg ); // and cache it
    return lang_cfg;
}

TextLangCfg * TextLangMan::getTextLangCfg() {
    // No lang_tag specified: return main lang one
    return TextLangMan::getTextLangCfg( _main_lang );
}

TextLangCfg * TextLangMan::getTextLangCfg( ldomNode * node ) {
    if ( !_embedded_langs_enabled || !node ) {
        // No need to look at nodes: return main lang one
        return TextLangMan::getTextLangCfg( _main_lang );
    }
    if ( node->isText() )
        node = node->getParentNode();
    // We are usually called from renderFinalBlock() with a node that
    // we know has a lang= attribute.
    // But we may be called in other contexts (e.g. writeNodeEx) with
    // any node: so, look at this node parents for that lang= attribute.
    for ( ; !node->isRoot(); node = node->getParentNode() ) {
        if ( node->hasAttribute( attr_lang ) ) {
            lString16 lang_tag = node->getAttributeValue( attr_lang );
            if ( !lang_tag.empty() )
                return TextLangMan::getTextLangCfg( lang_tag );
        }
    }
    // No parent with lang= attribute: return main lang one
    return TextLangMan::getTextLangCfg( _main_lang );
}

int TextLangMan::getLangNodeIndex( ldomNode * node ) {
    if ( !_embedded_langs_enabled || !node ) {
        // No need to look up if !_embedded_langs_enabled
        return 0;
    }
    if ( node->isText() )
        node = node->getParentNode();
    for ( ; !node->isRoot(); node = node->getParentNode() ) {
        if ( node->hasAttribute( attr_lang ) ) {
            if ( !node->getAttributeValue( attr_lang ).empty() ) {
                return node->getDataIndex();
            }
        }
    }
    return 0;
}

// For HyphMan::hyphenate()
HyphMethod * TextLangMan::getMainLangHyphMethod() {
    return getTextLangCfg()->getHyphMethod();
}

lString16 TextLangMan::getLangTag(const lString16& dictTitle)
{
    for (int i = 0; _hyph_dict_table[i].lang_tag!=NULL; i++) {
        if (dictTitle == lString16(_hyph_dict_table[i].hyph_filename_prefix)) {
            return lString16(_hyph_dict_table[i].lang_tag);
        }
    }
    return lString16();
}


// TextLangCfg object: per language holder of language specificities

#if USE_LIBUNIBREAK==1
lChar16 lb_char_sub_func_polish(const lChar16 * text, int pos, int next_usable) {
    // https://github.com/koreader/koreader/issues/5645#issuecomment-559193057
    // Letters aiouwzAIOUWS are prepositions that should not be left at the
    // end of a line.
    // Make them behave (for libunibreak) just like a opening paren (which
    // being LBC_OP, will prevent a line break after it, even if followed
    // by a space).
    if ( pos >= 1 && text[pos-1] == ' ' ) {
        switch ( text[pos] ) {
            case 'A':
            case 'I':
            case 'O':
            case 'U':
            case 'W':
            case 'Z': // Meaning in english:
            case 'a': // and
            case 'i': // and
            case 'o': // about
            case 'u': // at
            case 'w': // in
            case 'z': // with
                return '(';
                break;
            default:
                break;
        }
    }
    return text[pos];
}

lChar16 lb_char_sub_func_czech_slovak(const lChar16 * text, int pos, int next_usable) {
    // Same for Czech and Slovak : AIiVvOoUuSsZzKk
    // https://tex.stackexchange.com/questions/27780/one-letter-word-at-the-end-of-line
    // https://github.com/michal-h21/luavlna
    if ( pos >= 1 && text[pos-1] == ' ' ) {
        switch ( text[pos] ) {
            case 'A':
            case 'I':
            case 'K':
            case 'O':
            case 'S':
            case 'U':
            case 'V':
            case 'Z':
            case 'i':
            case 'k':
            case 'o':
            case 's':
            case 'u':
            case 'v':
            case 'z':
                return '(';
                break;
            default:
                break;
        }
    }
    return text[pos];
}
#endif

TextLangCfg::~TextLangCfg() {
}

// Instantiate a new TextLangCfg with properties adequate to the provided lang_tag
TextLangCfg::TextLangCfg( lString16 lang_tag ) {
    if ( TextLangMan::_no_hyph_method == NULL ) {
        // We need to init static TextLangMan::_no_hyph_method and friends after
        // HyphMan is set up. Do that here, even if unrelated, as TextLangCfg
        // creation is called less often that every other methods around here.
        TextLangMan::_no_hyph_method = HyphMan::getHyphMethodForDictionary(HYPH_DICT_ID_NONE);
        TextLangMan::_soft_hyphens_method = HyphMan::getHyphMethodForDictionary(HYPH_DICT_ID_SOFTHYPHENS);
        TextLangMan::_algo_hyph_method = HyphMan::getHyphMethodForDictionary(HYPH_DICT_ID_ALGORITHM);
    }

    // Keep as our id the provided and non-lowercase'd lang_tag (with possibly bogus #@algorithm)
    _lang_tag = lang_tag;
    // Harfbuzz may know more than us about exotic/complex lang tags,
    // so let it deal the the provided one as-is.
    lString16 hb_lang_tag = lang_tag;
    // Lowercase it for our tests
    lang_tag.lowercase(); // (used by LANG_STARTS_WITH() macros)

    // Get hyph method/dictionary from _hyph_dict_table
    _hyph_method = TextLangMan::getHyphMethodForLang(lang_tag);

    // Cleanup if we got "en#@something" from legacy HyphMan methods
    int h_pos = lang_tag.pos("#");
    if ( h_pos > 0 ) {
        lang_tag = lang_tag.substr(0, h_pos);
        hb_lang_tag = hb_lang_tag.substr(0, h_pos); // Also clean the one for HB
    }
    #ifdef DEBUG_LANG_USAGE
    printf("TextLangCfg %s created (%s %s)\n", UnicodeToLocal(_lang_tag).c_str(),
                    UnicodeToLocal(lang_tag).c_str(), UnicodeToLocal(_hyph_method->getId()).c_str());
    #endif

    // https://drafts.csswg.org/css-text-3/#script-tagging
    // We might need to check for the script subpart (optional 2nd
    // subpart) Lant, Hant, Hrkt... and make some non latin language
    // with a Lant script behave more like latin languages...

    // Note that Harfbuzz seems to do the right same thing with
    // either "zh-TW" and "zh-Hant".

    // See for more clever/complex handling of lang tags:
    // https://android.googlesource.com/platform/frameworks/minikin/+/refs/heads/master/libs/minikin/Locale.cpp

    // We thought about adding a 2nd fallback font per-language, but it feels
    // a bit wrong to limit this feature to documents with lang tags.
    // Better to implement a generic font fallback chain independant of language.

    // https://unicode.org/reports/tr14/#Hyphen : in Polish and Portuguese,
    // a real hyphen at end of line must be duplicated at start of next line.
    _duplicate_real_hyphen_on_next_line = false;

#if USE_HARFBUZZ==1
    _hb_language = hb_language_from_string(UnicodeToLocal(hb_lang_tag).c_str(), -1);
#endif

#if USE_LIBUNIBREAK==1
    // libunibreak per-language LineBreakProperties extensions
    //
    // Rules extracted from libunibreak/src/linebreakdef.c, so we can adapt
    // them and build LineBreakProperties adequately for more languages.
    // See https://en.wikipedia.org/wiki/Quotation_mark
    // These are mostly need only for languages that may add a space between
    // the quote and its content - otherwise, the quote will be part of the
    // word it sticks to, and break will be allowed on the other side which
    // probably is a space.
    // When a language allows the use of unpaired quotes (same quote on both
    // sides), it seems best to not specify anything.
    bool has_left_single_quotation_mark_opening = false;   // U+2018 ‘
    bool has_left_single_quotation_mark_closing = false;
    bool has_right_single_quotation_mark_opening = false;  // U+2019 ’
    bool has_right_single_quotation_mark_closing = false;
    bool has_right_single_quotation_mark_glue = false;
    bool has_left_double_quotation_mark_opening = false;   // U+201C “
    bool has_left_double_quotation_mark_closing = false;
    bool has_right_double_quotation_mark_opening = false;  // U+201D ”
    bool has_right_double_quotation_mark_closing = false;
    bool has_left_single_angle_quotation_mark_opening = false;   // U+2039 ‹
    bool has_left_single_angle_quotation_mark_closing = false;
    bool has_right_single_angle_quotation_mark_opening = false;  // U+203A ›
    bool has_right_single_angle_quotation_mark_closing = false;
    bool has_left_double_angle_quotation_mark_opening = false;   // U+00AB «
    bool has_left_double_angle_quotation_mark_closing = false;
    bool has_right_double_angle_quotation_mark_opening = false;  // U+00BB »
    bool has_right_double_angle_quotation_mark_closing = false;

    // Note: these macros use 'lang_tag'.
    if ( LANG_STARTS_WITH(("en")) ) { // English
        has_left_single_quotation_mark_opening = true; // no right..closing in linebreakdef.c
        has_left_double_quotation_mark_opening = true;
        has_right_double_quotation_mark_closing = true;
    }
    else if ( LANG_STARTS_WITH(("fr") ("es")) ) { // French, Spanish
        has_left_single_quotation_mark_opening = true; // no right..closing in linebreakdef.c
        has_left_double_quotation_mark_opening = true;
        has_right_double_quotation_mark_closing = true;
        has_left_single_angle_quotation_mark_opening = true;
        has_right_single_angle_quotation_mark_closing = true;
        has_left_double_angle_quotation_mark_opening = true;
        has_right_double_angle_quotation_mark_closing = true;
    }
    else if ( LANG_STARTS_WITH(("de")) ) { // German
        has_left_single_quotation_mark_closing = true;
        has_right_single_quotation_mark_glue = true;
        has_left_double_quotation_mark_closing = true;
        has_left_single_angle_quotation_mark_closing = true;
        has_right_single_angle_quotation_mark_opening = true;
        has_left_double_angle_quotation_mark_closing = true;
        has_right_double_angle_quotation_mark_opening = true;
    }
    else if ( LANG_STARTS_WITH(("ru")) ) { // Russian
        has_left_double_quotation_mark_closing = true;
        has_left_double_angle_quotation_mark_opening = true;
        has_right_double_angle_quotation_mark_closing = true;
    }
    else if ( LANG_STARTS_WITH(("zh")) ) { // Chinese
        has_left_single_quotation_mark_opening = true;
        has_right_single_quotation_mark_closing = true;
        has_left_double_quotation_mark_opening = true;
        has_right_double_quotation_mark_closing = true;
    }
    // Add languages rules here, or reuse previous one with other languages if needed.

    // Set up _lb_props.
    // Important: the unicode indices must be in strict ascending order (or libunibreak
    // might abort checking them all)
    int n = 0;
    if ( has_left_double_angle_quotation_mark_opening )  _lb_props[n++] = { 0x00AB, 0x00AB, LBP_OP };
    if ( has_left_double_angle_quotation_mark_closing )  _lb_props[n++] = { 0x00AB, 0x00AB, LBP_CL };
    // Soft-Hyphens are handled by Hyphman hyphenate(), have them handled as Zero-Width-Joiner by
    // libunibreak so they don't allow any break and don't prevent hyphenate() to handle them correctly.
    _lb_props[n++] = { 0x00AD, 0x00AD, LBP_ZWJ };
    if ( has_right_double_angle_quotation_mark_opening ) _lb_props[n++] = { 0x00BB, 0x00BB, LBP_OP };
    if ( has_right_double_angle_quotation_mark_closing ) _lb_props[n++] = { 0x00BB, 0x00BB, LBP_CL };
    if ( has_left_single_quotation_mark_opening )        _lb_props[n++] = { 0x2018, 0x2018, LBP_OP };
    if ( has_left_single_quotation_mark_closing )        _lb_props[n++] = { 0x2018, 0x2018, LBP_CL };
    if ( has_right_single_quotation_mark_opening )       _lb_props[n++] = { 0x2019, 0x2019, LBP_OP };
    if ( has_right_single_quotation_mark_closing )       _lb_props[n++] = { 0x2019, 0x2019, LBP_CL };
    if ( has_right_single_quotation_mark_glue )          _lb_props[n++] = { 0x2019, 0x2019, LBP_GL };
    if ( has_left_double_quotation_mark_opening )        _lb_props[n++] = { 0x201C, 0x201C, LBP_OP };
    if ( has_left_double_quotation_mark_closing )        _lb_props[n++] = { 0x201C, 0x201C, LBP_CL };
    if ( has_right_double_quotation_mark_opening )       _lb_props[n++] = { 0x201D, 0x201D, LBP_OP };
    if ( has_right_double_quotation_mark_closing )       _lb_props[n++] = { 0x201D, 0x201D, LBP_CL };
    if ( has_left_single_angle_quotation_mark_opening )  _lb_props[n++] = { 0x2039, 0x2039, LBP_OP };
    if ( has_left_single_angle_quotation_mark_closing )  _lb_props[n++] = { 0x2039, 0x2039, LBP_CL };
    if ( has_right_single_angle_quotation_mark_opening ) _lb_props[n++] = { 0x203A, 0x203A, LBP_OP };
    if ( has_right_single_angle_quotation_mark_closing ) _lb_props[n++] = { 0x203A, 0x203A, LBP_CL };
    // End of list
    _lb_props[n++] = { 0, 0, LBP_Undefined };
    // Done with libunibreak per-language LineBreakProperties extensions

    // Other line breaking and text layout tweaks
    _lb_char_sub_func = NULL;
    if ( LANG_STARTS_WITH(("pl")) ) { // Polish
        _lb_char_sub_func = &lb_char_sub_func_polish;
        _duplicate_real_hyphen_on_next_line = true;
    }
    if ( LANG_STARTS_WITH(("cs") ("sk")) ) { // Czech, Slovak
        _lb_char_sub_func = &lb_char_sub_func_czech_slovak;
    }
    if ( LANG_STARTS_WITH(("pt")) ) { // Portuguese
        _duplicate_real_hyphen_on_next_line = true;
    }
#endif
}
