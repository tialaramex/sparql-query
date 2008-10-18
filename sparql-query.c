/*  sparql-query - a SPARQL client with GNU readline support
    Copyright (C) 2006-8 Nick Lamb and Steve Harris for Garlik
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <rasqal.h>
#include <getopt.h>
#include <sys/time.h>

#include <curl/curl.h>

#include <readline/readline.h>
#include <readline/history.h>

static int execute_query(CURL *curl, const char *ep, const char *query);
static CURL *sparql_curl_init(const char *format, int verbose);

static void interactive(const char *ep, const char *format, int verbose);

int main(int argc, char *argv[])
{
    char *format = "text/plain";
    static char *optstring = "f:v";
    char *ep = NULL, *query = NULL;
    int help = 0;
    int verbose = 0;
    int c, opt_index = 0;

    static struct option long_options[] = {
        { "format", 1, 0, 'f' },
        { "verbose", 0, 0, 'v' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long (argc, argv, optstring, long_options, &opt_index)) != -1) {
        if (c == 'f') {
            format = optarg;
        } else if (c == 'v') {
            verbose++;
        } else {
            help = 1;
        }
    }

    for (int k = optind; k < argc; ++k) {
        if (!ep) {
            ep = argv[k];
        } else if (!query) {
            query = argv[k];
        } else {
            help = 1;
        }
    }

    if (help || !ep) {
        fprintf(stderr, "%s revision %s\n", argv[0], GIT_REV);
        fprintf(stderr, "Usage: %s [-v] [-f MIME type] <ep> [<query>] e.g.\n", argv[0]);
        fprintf(stderr, " %s http://example.net/sparql 'SELECT * WHERE { ?s ?p ?o } LIMIT 10'\n", argv[0]);
        fprintf(stderr, " <ep> is a SPARQL HTTP endpoint\n");
        fprintf(stderr, " <query> is a SPARQL query to execute immediately in non-interactive mode\n");
        fprintf(stderr, "remember to use shell quoting if necessary\n");
        return 1;
    }

    if (query) {
        CURL *curl = sparql_curl_init(format, verbose);
        CURLcode error = execute_query(curl, ep, query);
        return error;
    } else {
        interactive(ep, format, verbose);
        printf("\n");
    }

    return 0;
}

static void load_history_dotfile(void)
{
    char *dotfile = g_strconcat(g_get_home_dir(), "/.sparql_history", NULL);
    read_history(dotfile);
    g_free(dotfile);
}

static void save_history_dotfile(void)
{
    char *dotfile = g_strconcat(g_get_home_dir(), "/.sparql_history", NULL);
    stifle_history(100); /* arbitrarily restrict history file to 100 entries */
    write_history(dotfile);
    g_free(dotfile);
}

static CURL *sparql_curl_init(const char *format, int verbose)
{
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    char *accept = g_strdup_printf("Accept: %s", format);
    headers = curl_slist_append(headers, accept);
    g_free(accept);

    curl_easy_setopt(curl, CURLOPT_VERBOSE, verbose);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    return curl;
}

static int execute_query(CURL *curl, const char *ep, const char *query)
{
    char my_curl_error[CURL_ERROR_SIZE];
    char *encoded = curl_easy_escape (curl, query, 0);
    char *query_url = g_strdup_printf("%s?query=%s", ep, encoded);
    curl_free(encoded);

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, my_curl_error);
    curl_easy_setopt(curl, CURLOPT_URL, query_url);
    CURLcode code = curl_easy_perform(curl);

    if (code) {
      fprintf(stderr, "CURL: %s\n", my_curl_error);
    }

    return code;
}

static int check_endpoint(CURL *curl, const char *ep)
{
    char my_curl_error[CURL_ERROR_SIZE];
    CURLcode code;

    code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, my_curl_error);
    if (!code) code = curl_easy_setopt(curl, CURLOPT_URL, ep);
    if (!code) code = curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    if (!code) code = curl_easy_perform(curl);

    /* put everything back regardless */
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0);
    /* (some versions of?) curl forces HTTP requests to HEAD when NOBODY is set, but doesn't put it back... */
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
    /* can't put back ERRORBUFFER so future callers must set it ... */

    switch (code) {
        case CURLE_UNSUPPORTED_PROTOCOL:
        case CURLE_FAILED_INIT:
        case CURLE_URL_MALFORMAT:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
            fprintf(stderr, "CURL: %s\n", my_curl_error);
            return code;

        case CURLE_OK:
        default: /* many errors at this point aren't fatal */
            return 0;
    }
}

static void interactive(const char *ep, const char *format, int verbose)
{
    const char *prompt = "sparql>";
    const char *reprompt = "      >";

    if (!isatty(0)) {
        /* no terminal input so disable TAB completion */
        rl_bind_key ('\t', rl_insert);
        reprompt = prompt = "";
    }
    /* fill out readline functions */
    load_history_dotfile();

    CURL *curl = sparql_curl_init(format, verbose);
    if (check_endpoint(curl, ep)) {
        return;
    }

    char *query = NULL;

    do {
        /* assemble query string */
        char *line = readline(prompt);
        if (!line) break; /* EOF */

        g_free(query);
        query = g_strdup(line);

        if (*line == '\0') {
            free(line);
            continue;
        }

        while (line && !g_str_has_suffix(line, ";")) {
            free(line);
            line = readline(reprompt);
            if (line) {
                char *old = query;
                query = g_strjoin("\n", old, line, NULL);
                g_free(old);
            }
        }
        free(line);
        add_history(query);
        char *old = query;
        query = g_strconcat(old, "\n", NULL);
        g_free(old);

        /* process query string */
        if (g_str_has_suffix(query, ";\n")) {
            query[strlen(query) - 2] = '\0';
        }
        if (query) {
            execute_query(curl, ep, query);
        }
    } while (query);

    save_history_dotfile();
    return;
}

/* vi:set expandtab sts=4 sw=4: */
