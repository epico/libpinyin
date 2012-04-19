/* 
 *  libpinyin
 *  Library to deal with pinyin.
 *  
 *  Copyright (C) 2011 Peng Wu <alexepico@gmail.com>
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include "pinyin.h"
#include <stdio.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include "pinyin_internal.h"

/* a glue layer for input method integration. */

struct _pinyin_context_t{
    pinyin_option_t m_options;

    FullPinyinParser2 * m_full_pinyin_parser;
    DoublePinyinParser2 * m_double_pinyin_parser;
    ChewingParser2 * m_chewing_parser;

    FacadeChewingTable * m_pinyin_table;
    FacadePhraseTable * m_phrase_table;
    FacadePhraseIndex * m_phrase_index;
    Bigram * m_system_bigram;
    Bigram * m_user_bigram;

    PinyinLookup * m_pinyin_lookup;
    PhraseLookup * m_phrase_lookup;

    char * m_system_dir;
    char * m_user_dir;
    bool m_modified;
};


static bool check_format(const char * userdir){
    gchar * filename = g_build_filename
        (userdir, "version", NULL);

    MemoryChunk chunk;
    bool exists = chunk.load(filename);

    if (exists) {
        exists = (0 == memcmp
                  (LIBPINYIN_FORMAT_VERSION, chunk.begin(),
                   strlen(LIBPINYIN_FORMAT_VERSION) + 1));
    }
    g_free(filename);

    if (exists)
        return exists;

    /* clean up files, if version mis-matches. */
    filename = g_build_filename
        (userdir, "gb_char.dbin", NULL);
    g_unlink(filename);
    g_free(filename);

    filename = g_build_filename
        (userdir, "gbk_char.dbin", NULL);
    g_unlink(filename);
    g_free(filename);

    filename = g_build_filename
        (userdir, "user.db", NULL);
    g_unlink(filename);
    g_free(filename);

    return exists;
}

static bool mark_version(const char * userdir){
    gchar * filename = g_build_filename
        (userdir, "version", NULL);
    MemoryChunk chunk;
    chunk.set_content(0, LIBPINYIN_FORMAT_VERSION,
                      strlen(LIBPINYIN_FORMAT_VERSION) + 1);
    bool retval = chunk.save(filename);
    g_free(filename);
    return retval;
}

pinyin_context_t * pinyin_init(const char * systemdir, const char * userdir){
    pinyin_context_t * context = new pinyin_context_t;

    context->m_options = USE_TONE;

    context->m_system_dir = g_strdup(systemdir);
    context->m_user_dir = g_strdup(userdir);
    context->m_modified = false;

    check_format(context->m_user_dir);

    context->m_pinyin_table = new FacadeChewingTable;
    MemoryChunk * chunk = new MemoryChunk;
    gchar * filename = g_build_filename
        (context->m_system_dir, "pinyin_index.bin", NULL);
    if (!chunk->load(filename)) {
        fprintf(stderr, "open %s failed!\n", filename);
        return NULL;
    }
    g_free(filename);

    context->m_pinyin_table->load(context->m_options, chunk, NULL);

    context->m_full_pinyin_parser = new FullPinyinParser2;
    context->m_double_pinyin_parser = new DoublePinyinParser2;
    context->m_chewing_parser = new ChewingParser2;

    context->m_phrase_table = new FacadePhraseTable;
    chunk = new MemoryChunk;
    filename = g_build_filename(context->m_system_dir, "phrase_index.bin", NULL);
    if (!chunk->load(filename)) {
        fprintf(stderr, "open %s failed!\n", filename);
        return NULL;
    }
    g_free(filename);
    context->m_phrase_table->load(chunk, NULL);

    context->m_phrase_index = new FacadePhraseIndex;
    MemoryChunk * log = new MemoryChunk; chunk = new MemoryChunk;
    filename = g_build_filename(context->m_system_dir, "gb_char.bin", NULL);
    if (!chunk->load(filename)) {
        fprintf(stderr, "open %s failed!\n", filename);
        return NULL;
    }
    g_free(filename);
    context->m_phrase_index->load(1, chunk);
    filename = g_build_filename(context->m_user_dir, "gb_char.dbin", NULL);
    log->load(filename);
    g_free(filename);
    context->m_phrase_index->merge(1, log);

    log = new MemoryChunk; chunk = new MemoryChunk;
    filename = g_build_filename(context->m_system_dir, "gbk_char.bin", NULL);
    if (!chunk->load(filename)) {
        fprintf(stderr, "open %s failed!\n", filename);
        return NULL;
    }
    g_free(filename);
    context->m_phrase_index->load(2, chunk);
    filename = g_build_filename(context->m_user_dir, "gbk_char.dbin", NULL);
    log->load(filename);
    g_free(filename);
    context->m_phrase_index->merge(2, log);

    context->m_system_bigram = new Bigram;
    filename = g_build_filename(context->m_system_dir, "bigram.db", NULL);
    context->m_system_bigram->attach(filename, ATTACH_READONLY);
    g_free(filename);

    context->m_user_bigram = new Bigram;
    filename = g_build_filename(context->m_user_dir, "user.db", NULL);
    context->m_user_bigram->load_db(filename);
    g_free(filename);

    context->m_pinyin_lookup = new PinyinLookup
        ( context->m_options, context->m_pinyin_table,
          context->m_phrase_index, context->m_system_bigram,
          context->m_user_bigram);

    context->m_phrase_lookup = new PhraseLookup
        (context->m_phrase_table, context->m_phrase_index,
         context->m_system_bigram, context->m_user_bigram);

    return context;
}

bool pinyin_save(pinyin_context_t * context){
    if (!context->m_user_dir)
        return false;

    if (!context->m_modified)
        return false;

    MemoryChunk * oldchunk = new MemoryChunk;
    MemoryChunk * newlog = new MemoryChunk;

    gchar * filename = g_build_filename(context->m_system_dir,
                                        "gb_char.bin", NULL);
    oldchunk->load(filename);
    g_free(filename);

    context->m_phrase_index->diff(1, oldchunk, newlog);
    gchar * tmpfilename = g_build_filename(context->m_user_dir,
                                           "gb_char.dbin.tmp", NULL);
    filename = g_build_filename(context->m_user_dir,
                                "gb_char.dbin", NULL);
    newlog->save(tmpfilename);
    rename(tmpfilename, filename);
    g_free(tmpfilename);
    g_free(filename);
    delete newlog;

    oldchunk = new MemoryChunk; newlog = new MemoryChunk;
    filename = g_build_filename(context->m_system_dir,
                                "gbk_char.bin", NULL);
    oldchunk->load(filename);
    g_free(filename);

    context->m_phrase_index->diff(2, oldchunk, newlog);
    tmpfilename = g_build_filename(context->m_user_dir,
                                   "gbk_char.dbin.tmp", NULL);
    filename = g_build_filename(context->m_user_dir,
                                "gbk_char.dbin", NULL);
    newlog->save(tmpfilename);
    rename(tmpfilename, filename);
    g_free(tmpfilename);
    g_free(filename);
    delete newlog;

    tmpfilename = g_build_filename(context->m_user_dir,
                                   "user.db.tmp", NULL);
    unlink(tmpfilename);
    filename = g_build_filename(context->m_user_dir, "user.db", NULL);
    context->m_user_bigram->save_db(tmpfilename);
    rename(tmpfilename, filename);
    g_free(tmpfilename);
    g_free(filename);

    mark_version(context->m_user_dir);

    context->m_modified = false;
    return true;
}

bool pinyin_set_double_pinyin_scheme(pinyin_context_t * context,
                                     DoublePinyinScheme scheme){
    context->m_double_pinyin_parser->set_scheme(scheme);
    return true;
}

bool pinyin_set_chewing_scheme(pinyin_context_t * context,
                               ChewingScheme scheme){
    context->m_chewing_parser->set_scheme(scheme);
    return true;
}


void pinyin_fini(pinyin_context_t * context){
    delete context->m_full_pinyin_parser;
    delete context->m_double_pinyin_parser;
    delete context->m_chewing_parser;
    delete context->m_pinyin_table;
    delete context->m_phrase_table;
    delete context->m_phrase_index;
    delete context->m_system_bigram;
    delete context->m_user_bigram;
    delete context->m_pinyin_lookup;
    delete context->m_phrase_lookup;

    g_free(context->m_system_dir);
    g_free(context->m_user_dir);
    context->m_modified = false;

    delete context;
}

/* copy from custom to context->m_custom. */
bool pinyin_set_options(pinyin_context_t * context,
                        pinyin_option_t options){
    context->m_options = options;
    context->m_pinyin_table->set_options(context->m_options);
    context->m_pinyin_lookup->set_options(context->m_options);
    return true;
}


pinyin_instance_t * pinyin_alloc_instance(pinyin_context_t * context){
    pinyin_instance_t * instance = new pinyin_instance_t;
    instance->m_context = context;

    instance->m_raw_full_pinyin = NULL;

    instance->m_prefixes = g_array_new(FALSE, FALSE, sizeof(phrase_token_t));
    instance->m_pinyin_keys = g_array_new(FALSE, FALSE, sizeof(ChewingKey));
    instance->m_pinyin_key_rests =
        g_array_new(FALSE, FALSE, sizeof(ChewingKeyRest));
    instance->m_constraints = g_array_new
        (FALSE, FALSE, sizeof(lookup_constraint_t));
    instance->m_match_results =
        g_array_new(FALSE, FALSE, sizeof(phrase_token_t));

    return instance;
}

void pinyin_free_instance(pinyin_instance_t * instance){
    g_free(instance->m_raw_full_pinyin);
    g_array_free(instance->m_prefixes, TRUE);
    g_array_free(instance->m_pinyin_keys, TRUE);
    g_array_free(instance->m_pinyin_key_rests, TRUE);
    g_array_free(instance->m_constraints, TRUE);
    g_array_free(instance->m_match_results, TRUE);

    delete instance;
}


static bool pinyin_update_constraints(pinyin_instance_t * instance){
    pinyin_context_t * & context = instance->m_context;
    ChewingKeyVector & pinyin_keys = instance->m_pinyin_keys;
    CandidateConstraints & constraints = instance->m_constraints;

    size_t key_len = constraints->len;
    g_array_set_size(constraints, pinyin_keys->len);
    for (size_t i = key_len; i < pinyin_keys->len; ++i ) {
        lookup_constraint_t * constraint =
            &g_array_index(constraints, lookup_constraint_t, i);
        constraint->m_type = NO_CONSTRAINT;
    }

    context->m_pinyin_lookup->validate_constraint
        (constraints, pinyin_keys);

    return true;
}


bool pinyin_guess_sentence(pinyin_instance_t * instance){
    pinyin_context_t * & context = instance->m_context;

    g_array_set_size(instance->m_prefixes, 0);
    g_array_append_val(instance->m_prefixes, sentence_start);

    pinyin_update_constraints(instance);
    bool retval = context->m_pinyin_lookup->get_best_match
        (instance->m_prefixes,
         instance->m_pinyin_keys,
         instance->m_constraints,
         instance->m_match_results);

    return retval;
}

bool pinyin_guess_sentence_with_prefix(pinyin_instance_t * instance,
                                       const char * prefix){
    pinyin_context_t * & context = instance->m_context;

    g_array_set_size(instance->m_prefixes, 0);
    g_array_append_val(instance->m_prefixes, sentence_start);

    glong written = 0;
    ucs4_t * ucs4_str = g_utf8_to_ucs4(prefix, -1, NULL, &written, NULL);

    if (ucs4_str && written) {
        /* add prefixes. */
        for (ssize_t i = 1; i <= written; ++i) {
            if (i > MAX_PHRASE_LENGTH)
                break;

            phrase_token_t token = null_token;
            ucs4_t * start = ucs4_str + written - i;
            int result = context->m_phrase_table->search(i, start, token);
            if (result & SEARCH_OK)
                g_array_append_val(instance->m_prefixes, token);
        }
    }
    g_free(ucs4_str);

    pinyin_update_constraints(instance);
    bool retval = context->m_pinyin_lookup->get_best_match
        (instance->m_prefixes,
         instance->m_pinyin_keys,
         instance->m_constraints,
         instance->m_match_results);

    return retval;
}

bool pinyin_phrase_segment(pinyin_instance_t * instance,
                           const char * sentence){
    pinyin_context_t * & context = instance->m_context;

    const glong num_of_chars = g_utf8_strlen(sentence, -1);
    glong ucs4_len = 0;
    ucs4_t * ucs4_str = g_utf8_to_ucs4(sentence, -1, NULL, &ucs4_len, NULL);

    g_return_val_if_fail(num_of_chars == ucs4_len, FALSE);

    bool retval = context->m_phrase_lookup->get_best_match
        (ucs4_len, ucs4_str, instance->m_match_results);

    g_free(ucs4_str);
    return retval;
}

/* the returned sentence should be freed by g_free(). */
bool pinyin_get_sentence(pinyin_instance_t * instance,
                         char ** sentence){
    pinyin_context_t * & context = instance->m_context;

    bool retval = pinyin::convert_to_utf8
        (context->m_phrase_index, instance->m_match_results,
         NULL, *sentence);

    return retval;
}

bool pinyin_parse_full_pinyin(pinyin_instance_t * instance,
                              const char * onepinyin,
                              ChewingKey * onekey){
    pinyin_context_t * & context = instance->m_context;

    int pinyin_len = strlen(onepinyin);
    int parse_len = context->m_full_pinyin_parser->parse_one_key
        ( context->m_options, *onekey, onepinyin, pinyin_len);
    return pinyin_len == parse_len;
}

size_t pinyin_parse_more_full_pinyins(pinyin_instance_t * instance,
                                      const char * pinyins){
    pinyin_context_t * & context = instance->m_context;

    g_free(instance->m_raw_full_pinyin);
    instance->m_raw_full_pinyin = g_strdup(pinyins);
    int pinyin_len = strlen(pinyins);

    int parse_len = context->m_full_pinyin_parser->parse
        ( context->m_options, instance->m_pinyin_keys,
          instance->m_pinyin_key_rests, pinyins, pinyin_len);

    return parse_len;
}

bool pinyin_parse_double_pinyin(pinyin_instance_t * instance,
                                const char * onepinyin,
                                ChewingKey * onekey){
    pinyin_context_t * & context = instance->m_context;

    int pinyin_len = strlen(onepinyin);
    int parse_len = context->m_double_pinyin_parser->parse_one_key
        ( context->m_options, *onekey, onepinyin, pinyin_len);
    return pinyin_len == parse_len;
}

size_t pinyin_parse_more_double_pinyins(pinyin_instance_t * instance,
                                        const char * pinyins){
    pinyin_context_t * & context = instance->m_context;
    int pinyin_len = strlen(pinyins);

    int parse_len = context->m_double_pinyin_parser->parse
        ( context->m_options, instance->m_pinyin_keys,
          instance->m_pinyin_key_rests, pinyins, pinyin_len);

    return parse_len;
}

bool pinyin_parse_chewing(pinyin_instance_t * instance,
                          const char * onechewing,
                          ChewingKey * onekey){
    pinyin_context_t * & context = instance->m_context;

    int chewing_len = strlen(onechewing);
    int parse_len = context->m_chewing_parser->parse_one_key
        ( context->m_options, *onekey, onechewing, chewing_len );
    return chewing_len == parse_len;
}

size_t pinyin_parse_more_chewings(pinyin_instance_t * instance,
                                  const char * chewings){
    pinyin_context_t * & context = instance->m_context;
    int chewing_len = strlen(chewings);

    int parse_len = context->m_chewing_parser->parse
        ( context->m_options, instance->m_pinyin_keys,
          instance->m_pinyin_key_rests, chewings, chewing_len);

    return parse_len;
}

bool pinyin_in_chewing_keyboard(pinyin_instance_t * instance,
                                const char key, const char ** symbol) {
    pinyin_context_t * & context = instance->m_context;
    return context->m_chewing_parser->in_chewing_scheme
        (context->m_options, key, symbol);
}


static gint compare_item_with_token(gconstpointer lhs,
                                                gconstpointer rhs) {
    lookup_candidate_t * item_lhs = (lookup_candidate_t *)lhs;
    lookup_candidate_t * item_rhs = (lookup_candidate_t *)rhs;

    phrase_token_t token_lhs = item_lhs->m_token;
    phrase_token_t token_rhs = item_rhs->m_token;

    return (token_lhs - token_rhs);
}

static gint compare_item_with_frequency(gconstpointer lhs,
                                                    gconstpointer rhs) {
    lookup_candidate_t * item_lhs = (lookup_candidate_t *)lhs;
    lookup_candidate_t * item_rhs = (lookup_candidate_t *)rhs;

    guint32 freq_lhs = item_lhs->m_freq;
    guint32 freq_rhs = item_rhs->m_freq;

    return -(freq_lhs - freq_rhs); /* in descendant order */
}

static phrase_token_t _get_previous_token(pinyin_instance_t * instance,
                                          size_t offset) {
    phrase_token_t prev_token = null_token;
    ssize_t i;

    if (0 == offset) {
        prev_token = sentence_start;
    } else {
        assert (0 < offset);

        phrase_token_t cur_token = g_array_index
            (instance->m_match_results, phrase_token_t, offset);
        if (null_token != cur_token) {
            for (i = offset - 1; i >= 0; --i) {
                cur_token = g_array_index
                    (instance->m_match_results, phrase_token_t, i);
                if (null_token != cur_token) {
                    prev_token = cur_token;
                    break;
                }
            }
        }
    }

    return prev_token;
}

static void _append_items(pinyin_context_t * context,
                          PhraseIndexRanges ranges,
                          lookup_candidate_t * template_item,
                          CandidateVector items) {
    guint8 min_index, max_index;
    assert( ERROR_OK == context->m_phrase_index->
            get_sub_phrase_range(min_index, max_index));

    /* reduce and append to a single GArray. */
    for (size_t m = min_index; m <= max_index; ++m) {
        if (NULL == ranges[m])
            continue;

        for (size_t n = 0; n < ranges[m]->len; ++n) {
            PhraseIndexRange * range =
                &g_array_index(ranges[m], PhraseIndexRange, n);
            for (size_t k = range->m_range_begin;
                 k < range->m_range_end; ++k) {
                lookup_candidate_t item;
                item.m_candidate_type = template_item->m_candidate_type;
                item.m_token = k;
                item.m_orig_rest = template_item->m_orig_rest;
                item.m_new_pinyins = g_strdup(template_item->m_new_pinyins);
                item.m_freq = template_item->m_freq;
                g_array_append_val(items, item);
            }
        }
    }
}

static void _remove_duplicated_items(CandidateVector items) {
    /* remove the duplicated items. */
    phrase_token_t last_token = null_token;
    for (size_t n = 0; n < items->len; ++n) {
        lookup_candidate_t * item = &g_array_index
            (items, lookup_candidate_t, n);
        if (last_token == item->m_token) {
            g_array_remove_index(items, n);
            n--;
        }
        last_token = item->m_token;
    }
}

static void _compute_frequency_of_items(pinyin_context_t * context,
                                        phrase_token_t prev_token,
                                        SingleGram * merged_gram,
                                        CandidateVector items) {
    pinyin_option_t & options = context->m_options;
    ssize_t i;

    PhraseItem cached_item;
    /* compute all freqs. */
    for (i = 0; i < items->len; ++i) {
        lookup_candidate_t * item = &g_array_index
            (items, lookup_candidate_t, i);
        phrase_token_t & token = item->m_token;

        gfloat bigram_poss = 0; guint32 total_freq = 0;
        if (options & DYNAMIC_ADJUST) {
            if (null_token != prev_token) {
                guint32 bigram_freq = 0;
                merged_gram->get_total_freq(total_freq);
                merged_gram->get_freq(token, bigram_freq);
                if (0 != total_freq)
                    bigram_poss = bigram_freq / (gfloat)total_freq;
            }
        }

        /* compute the m_freq. */
        FacadePhraseIndex * & phrase_index = context->m_phrase_index;
        phrase_index->get_phrase_item(token, cached_item);
        total_freq = phrase_index->get_phrase_index_total_freq();
        assert (0 < total_freq);

        /* Note: possibility value <= 1.0. */
        guint32 freq = (LAMBDA_PARAMETER * bigram_poss +
                        (1 - LAMBDA_PARAMETER) *
                        cached_item.get_unigram_frequency() /
                        (gfloat) total_freq) * 256 * 256 * 256;
        item->m_freq = freq;
    }
}

bool pinyin_get_candidates(pinyin_instance_t * instance,
                           size_t offset,
                           TokenVector candidates) {

    pinyin_context_t * & context = instance->m_context;
    pinyin_option_t & options = context->m_options;
    ChewingKeyVector & pinyin_keys = instance->m_pinyin_keys;
    g_array_set_size(candidates, 0);

    size_t pinyin_len = pinyin_keys->len - offset;
    ssize_t i;

    /* lookup the previous token here. */
    phrase_token_t prev_token = null_token;

    if (options & DYNAMIC_ADJUST) {
        prev_token = _get_previous_token(instance, offset);
    }

    SingleGram merged_gram;
    SingleGram * system_gram = NULL, * user_gram = NULL;

    if (options & DYNAMIC_ADJUST) {
        if (null_token != prev_token) {
            context->m_system_bigram->load(prev_token, system_gram);
            context->m_user_bigram->load(prev_token, user_gram);
            merge_single_gram(&merged_gram, system_gram, user_gram);
        }
    }

    PhraseIndexRanges ranges;
    memset(ranges, 0, sizeof(ranges));

    guint8 min_index, max_index;
    assert( ERROR_OK == context->m_phrase_index->
            get_sub_phrase_range(min_index, max_index));

    for (size_t m = min_index; m <= max_index; ++m) {
        ranges[m] = g_array_new(FALSE, FALSE, sizeof(PhraseIndexRange));
    }

    GArray * items = g_array_new(FALSE, FALSE, sizeof(lookup_candidate_t));

    for (i = pinyin_len; i >= 1; --i) {
        g_array_set_size(items, 0);

        ChewingKey * keys = &g_array_index
            (pinyin_keys, ChewingKey, offset);

        /* do pinyin search. */
        int retval = context->m_pinyin_table->search
            (i, keys, ranges);

        if ( !(retval & SEARCH_OK) )
            continue;

        lookup_candidate_t template_item;
        _append_items(context, ranges, &template_item, items);

        g_array_sort(items, compare_item_with_token);

        _remove_duplicated_items(items);

        _compute_frequency_of_items(context, prev_token, &merged_gram, items);

        /* sort the candidates of the same length by frequency. */
        g_array_sort(items, compare_item_with_frequency);

        /* transfer back items to tokens, and save it into candidates */
        for (i = 0; i < items->len; ++i) {
            lookup_candidate_t * item = &g_array_index
                (items, lookup_candidate_t, i);
            g_array_append_val(candidates, item->m_token);
        }

        if (!(retval & SEARCH_CONTINUED))
            break;
    }

    g_array_free(items, TRUE);

    for (size_t m = min_index; m <= max_index; ++m) {
        if (ranges[m])
            g_array_free(ranges[m], TRUE);
    }

    if (system_gram)
        delete system_gram;
    if (user_gram)
        delete user_gram;
    return true;
}


bool pinyin_get_full_pinyin_candidates(pinyin_instance_t * instance,
                                       size_t offset,
                                       CandidateVector candidates){

    pinyin_context_t * & context = instance->m_context;
    pinyin_option_t & options = context->m_options;
    ChewingKeyVector & pinyin_keys = instance->m_pinyin_keys;
    ChewingKeyRestVector & pinyin_key_rests = instance->m_pinyin_key_rests;
    g_array_set_size(candidates, 0);

    size_t pinyin_len = pinyin_keys->len - offset;
    ssize_t i;

    /* lookup the previous token here. */
    phrase_token_t prev_token = null_token;

    if (options & DYNAMIC_ADJUST) {
        prev_token = _get_previous_token(instance, offset);
    }

    SingleGram merged_gram;
    SingleGram * system_gram = NULL, * user_gram = NULL;

    if (options & DYNAMIC_ADJUST) {
        if (null_token != prev_token) {
            context->m_system_bigram->load(prev_token, system_gram);
            context->m_user_bigram->load(prev_token, user_gram);
            merge_single_gram(&merged_gram, system_gram, user_gram);
        }
    }

    PhraseIndexRanges ranges;
    memset(ranges, 0, sizeof(ranges));

    guint8 min_index, max_index;
    assert( ERROR_OK == context->m_phrase_index->
            get_sub_phrase_range(min_index, max_index));

    for (size_t m = min_index; m <= max_index; ++m) {
        ranges[m] = g_array_new(FALSE, FALSE, sizeof(PhraseIndexRange));
    }

    GArray * items = g_array_new(FALSE, FALSE, sizeof(lookup_candidate_t));

    if (pinyin_len == 1) {
        /* because there is only one pinyin left,
         *  the following for-loop will not produce 2 character candidates.
         * the if-branch will fill the candidate list with
         *  2 character candidates.
         */

        if (options & USE_DIVIDED_TABLE) {
            g_array_set_size(items, 0);
            /* handle "^xian$" -> "xi'an" here */

            ChewingKey * key = &g_array_index(pinyin_keys, ChewingKey, offset);
            ChewingKeyRest * rest = &g_array_index(pinyin_key_rests,
                                                   ChewingKeyRest, offset);
            ChewingKeyRest orig_rest = *rest;
            guint16 tone = CHEWING_ZERO_TONE;

            const divided_table_item_t * item = NULL;

            /* back up tone */
            if (options & USE_TONE) {
                tone = key->m_tone;
                if (CHEWING_ZERO_TONE != tone) {
                    key->m_tone = CHEWING_ZERO_TONE;
                    rest->m_raw_end --;
                }
            }

            item = context->m_full_pinyin_parser->retrieve_divided_item
                (options, offset, pinyin_keys, pinyin_key_rests,
                 instance->m_raw_full_pinyin,
                 strlen(instance->m_raw_full_pinyin));

            if (item) {

            ChewingKey divided_keys[2];
            assert(context->m_full_pinyin_parser->
                   parse_one_key(options, divided_keys[0], item->m_new_keys[0],
                                 strlen(item->m_new_keys[0])));
            assert(context->m_full_pinyin_parser->
                   parse_one_key(options, divided_keys[1], item->m_new_keys[1],
                                 strlen(item->m_new_keys[1])));

            gchar * new_pinyins = g_strdup_printf
                ("%s'%s", item->m_new_keys[0], item->m_new_keys[1]);

            /* propagate the tone */
            if (options & USE_TONE) {
                if (CHEWING_ZERO_TONE != tone) {
                    assert(0 < tone && tone <= 5);
                    divided_keys[1].m_tone = tone;

                    gchar * tmp_str = g_strdup_printf
                        ("%s%d", new_pinyins, tone);
                    g_free(new_pinyins);
                    new_pinyins = tmp_str;
                }
            }

            /* do pinyin search. */
            int retval = context->m_pinyin_table->search
                (2, divided_keys, ranges);

            if (retval & SEARCH_OK) {
                lookup_candidate_t template_item;
                template_item.m_candidate_type = DIVIDED_CANDIDATE;
                template_item.m_orig_rest = orig_rest;
                template_item.m_new_pinyins = new_pinyins;

                _append_items(context, ranges, &template_item, items);

                g_free(new_pinyins);

                g_array_sort(items, compare_item_with_token);

                _remove_duplicated_items(items);

                _compute_frequency_of_items(context, prev_token,
                                            &merged_gram, items);

                /* sort the candidates of the same length by frequency. */
                g_array_sort(items, compare_item_with_frequency);

                /* transfer back items to tokens, and save it into candidates */
                for (i = 0; i < items->len; ++i) {
                    lookup_candidate_t * item = &g_array_index
                        (items, lookup_candidate_t, i);
                    g_array_append_val(candidates, *item);
                }

            }
            }

            /* restore tones */
            if (options & USE_TONE) {
                if (CHEWING_ZERO_TONE != tone) {
                    key->m_tone = tone;
                    rest->m_raw_end ++;
                }
            }
        }
    }

    for (i = pinyin_len; i >= 1; --i) {
        g_array_set_size(items, 0);

        if (2 == i) {
            /* handle fuzzy pinyin segment here. */
            if (options & USE_DIVIDED_TABLE) {
                assert(FALSE);
            }
            if (options & USE_RESPLIT_TABLE) {
                assert(FALSE);
            }
        }

        ChewingKey * keys = &g_array_index
            (pinyin_keys, ChewingKey, offset);

        /* do pinyin search. */
        int retval = context->m_pinyin_table->search
            (i, keys, ranges);

        if ( !(retval & SEARCH_OK) )
            continue;

        lookup_candidate_t template_item;
        _append_items(context, ranges, &template_item, items);

        g_array_sort(items, compare_item_with_token);

        _remove_duplicated_items(items);

        _compute_frequency_of_items(context, prev_token, &merged_gram, items);

        g_array_sort(items, compare_item_with_frequency);

        for (i = 0; i < items->len; ++i) {
            lookup_candidate_t * item = &g_array_index
                (items, lookup_candidate_t, i);
            g_array_append_val(candidates, *item);
        }

        if (!(retval & SEARCH_CONTINUED))
            break;
    }

    g_array_free(items, TRUE);

    for (size_t m = min_index; m <= max_index; ++m) {
        if (ranges[m])
            g_array_free(ranges[m], TRUE);
    }

    if (system_gram)
        delete system_gram;
    if (user_gram)
        delete user_gram;
    return true;
}


int pinyin_choose_candidate(pinyin_instance_t * instance,
                            size_t offset,
                            phrase_token_t token){
    pinyin_context_t * & context = instance->m_context;

    guint8 len = context->m_pinyin_lookup->add_constraint
        (instance->m_constraints, offset, token);

    bool retval = context->m_pinyin_lookup->validate_constraint
        (instance->m_constraints, instance->m_pinyin_keys) && len;

    return offset + len;
}

bool pinyin_clear_constraint(pinyin_instance_t * instance,
                             size_t offset){
    pinyin_context_t * & context = instance->m_context;

    bool retval = context->m_pinyin_lookup->clear_constraint
        (instance->m_constraints, offset);

    return retval;
}

bool pinyin_clear_constraints(pinyin_instance_t * instance){
    pinyin_context_t * & context = instance->m_context;
    bool retval = true;

    for ( size_t i = 0; i < instance->m_constraints->len; ++i ) {
        retval = context->m_pinyin_lookup->clear_constraint
            (instance->m_constraints, i) && retval;
    }

    return retval;
}

/* the returned word should be freed by g_free. */
bool pinyin_translate_token(pinyin_instance_t * instance,
                            phrase_token_t token, char ** word){
    pinyin_context_t * & context = instance->m_context;
    PhraseItem item;
    ucs4_t buffer[MAX_PHRASE_LENGTH];

    int retval = context->m_phrase_index->get_phrase_item(token, item);
    item.get_phrase_string(buffer);
    guint8 length = item.get_phrase_length();
    *word = g_ucs4_to_utf8(buffer, length, NULL, NULL, NULL);
    return ERROR_OK == retval;
}

bool pinyin_train(pinyin_instance_t * instance){
    if (!instance->m_context->m_user_dir)
        return false;

    pinyin_context_t * & context = instance->m_context;
    context->m_modified = true;

    bool retval = context->m_pinyin_lookup->train_result2
        (instance->m_pinyin_keys, instance->m_constraints,
         instance->m_match_results);

    return retval;
}

bool pinyin_reset(pinyin_instance_t * instance){
    g_array_set_size(instance->m_pinyin_keys, 0);
    g_array_set_size(instance->m_pinyin_key_rests, 0);
    g_array_set_size(instance->m_constraints, 0);
    g_array_set_size(instance->m_match_results, 0);

    return true;
}

/**
 *  TODO: to be implemented.
 *    Note: prefix is the text before the pre-edit string.
 *  bool pinyin_get_guessed_sentence_with_prefix(...);
 *  bool pinyin_get_candidates_with_prefix(...);
 *  For context-dependent order of the candidates list.
 */
