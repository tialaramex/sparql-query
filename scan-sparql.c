/*
 *  Copyright (C) 2011 Steve Harris
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  $Id: scan-sparql.c $
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <curl/curl.h>

#include "scan-sparql.h"

#define S_UNKNOWN "[unknown]"

#define S_CONFIG_GROUP "prefixes"

static GRegex *re_prefix = NULL, *re_qname = NULL;

static GHashTable *lookup = NULL;

static GKeyFile *keyfile = NULL;
static char *keyfile_filename = NULL;

int scan_init()
{
    GError *err = NULL;

    re_prefix = g_regex_new("PREFIX\\s+([a-z](?:[a-z.-]*[a-z-])?)?:\\s+<([^<>\"{}|^`\\\\\\s>]*)>", 
            G_REGEX_CASELESS | G_REGEX_MULTILINE | G_REGEX_OPTIMIZE, 0,
            &err);
    if (err) {
        fprintf(stderr, "Regex compilation error: %s\n", err->message);
        g_error_free(err);

        return 1;
    }

    re_qname = g_regex_new("(?:\\s|^)([a-z](?:[a-z.-]*[a-z-])?)?:([a-z0-9]([a-z0-9.-]*[a-z0-9-])?)?", 
            G_REGEX_CASELESS | G_REGEX_MULTILINE | G_REGEX_OPTIMIZE, 0,
            &err);
    if (err) {
        fprintf(stderr, "Regex compilation error: %s\n", err->message);
        g_error_free(err);

        return 1;
    }

    lookup = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    if (!keyfile) {
        keyfile = g_key_file_new();
        keyfile_filename = g_strconcat(g_get_home_dir(), "/.sparql", NULL);
    }
    if (!g_key_file_load_from_file(keyfile, keyfile_filename, G_KEY_FILE_KEEP_COMMENTS, &err)) {
        if (err->code != G_FILE_ERROR_NOENT &&
            err->code != G_FILE_ERROR_EXIST &&
            err->code != G_FILE_ERROR_ISDIR) {
            g_error("%s(%d) reading %s", err->message, err->code, keyfile_filename);

            return 1;
        }
        g_error_free(err);
        err = NULL;
    } else {
        char **keys = NULL;
        keys = g_key_file_get_keys(keyfile, S_CONFIG_GROUP, NULL, &err);
        if (keys) {
            for (int i=0; keys[i]; i++) {
                char *prefix = g_key_file_get_string(keyfile, S_CONFIG_GROUP, keys[i], &err);
                if (prefix) {
                    g_hash_table_insert(lookup, g_strdup(keys[i]), g_strdup(prefix));
                }
            }
            g_free(keys);
        }
    }

    return 0;
}

void scan_fini()
{
    if (re_prefix) {
        g_regex_unref(re_prefix);
        re_prefix = NULL;
    }
    if (re_qname) {
        g_regex_unref(re_qname);
        re_qname = NULL;
    }
    if (lookup) {
        g_hash_table_unref(lookup);
        lookup = NULL;
    }

    if (keyfile) {
        GError *err = NULL;
        gsize length = 0;
        char *key_data = g_key_file_to_data(keyfile, &length, &err);
        g_file_set_contents(keyfile_filename, key_data, length, &err);
        g_key_file_free(keyfile);
        keyfile = NULL;
        g_free(key_data);
        g_free(keyfile_filename);
        keyfile_filename = NULL;
    }
}

/* scan str, looking for PREFIX x: <y>, and x:y 
 *
 * prefixes will be filled out for every occurance of PREFIX x: <y>
 * and .sname will be filled out, but not .prefix for x:y, with no matching
 * PREFIX 
 *
 * Return value must be freed with g_free()
 */
int scan_sparql(const char *str, char **prefixes)
{
    GMatchInfo *match_info;
    GSList *defined = NULL;
    *prefixes = g_strdup("");

    g_regex_match(re_prefix, str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *sname = g_match_info_fetch(match_info, 1);
        gchar *prefix = g_match_info_fetch(match_info, 2);

        defined = g_slist_prepend(defined, g_strdup(sname));
        char *lprefix = g_hash_table_lookup(lookup, sname);
        if (lprefix) {
            if (strcmp(prefix, lprefix)) {
                g_hash_table_replace(lookup, sname, g_strdup(prefix));
                g_key_file_set_string(keyfile, S_CONFIG_GROUP, sname, prefix);
            }
        } else {
            g_hash_table_insert(lookup, g_strdup(sname), g_strdup(prefix));
            g_key_file_set_string(keyfile, S_CONFIG_GROUP, sname, prefix);
        }
        g_match_info_next(match_info, NULL);
    }
    g_match_info_free(match_info);

    char *where = strchr(str, '{');
    if (!where) {
        return 0;
    }

    g_regex_match(re_qname, where, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *sname = g_match_info_fetch(match_info, 1);
        int found = 0;
        for (GSList *ptr = defined; ptr; ptr = ptr->next) {
            if (!strcmp(ptr->data, sname)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            char *prefix = NULL;
            prefix = g_hash_table_lookup(lookup, sname);
            if (prefix && !strcmp(prefix, S_UNKNOWN)) {
                /* we can't find a value for it */
            } else if (prefix) {
                char *old_prefixes = *prefixes;
                *prefixes = g_strdup_printf("%sPREFIX %s: <%s>\n", old_prefixes, sname, prefix);
                g_free(old_prefixes);
                defined = g_slist_prepend(defined, g_strdup(sname));
            } else {
                CURL *curl = curl_easy_init();
                FILE *tfile = tmpfile();
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, tfile);
                char *url = g_strdup_printf("http://prefix.cc/%s.file.txt", sname);
                curl_easy_setopt(curl, CURLOPT_URL, url);
                CURLcode code = curl_easy_perform(curl);
                long status = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                g_free(url);
                if (code == 0 && status == 200) {
                    fseek(tfile, 0, SEEK_SET);
                    char q[256];
                    q[255] = '\0';
                    if (fscanf(tfile, "%*s\t%255s", q) == 1) {
                        char *old_prefixes = *prefixes;
                        *prefixes = g_strdup_printf("%sPREFIX %s: <%s>\n", old_prefixes, sname, q);
                        g_free(old_prefixes);
                        defined = g_slist_prepend(defined, g_strdup(sname));
                        g_hash_table_insert(lookup, g_strdup(sname), g_strdup(q));
                        g_key_file_set_string(keyfile, S_CONFIG_GROUP, sname, q);
                    } else {
                        g_hash_table_insert(lookup, g_strdup(sname), g_strdup(S_UNKNOWN));
                    }
                }
                curl_easy_cleanup(curl);
                fclose(tfile);
            }
        }

        g_free(sname);
        g_match_info_next(match_info, NULL);
    }
    g_match_info_free(match_info);

    for (GSList *ptr = defined; ptr; ptr = ptr->next) {
        g_free(ptr->data);
    }
    g_slist_free(defined);

    return 0;
}

/* vi:set expandtab sts=4 sw=4: */
