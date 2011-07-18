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
#include <libgen.h>
#include <glib.h>
#include <getopt.h>
#include <sys/time.h>

#include <curl/curl.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "scan-sparql.h"

extern int sr_parse(const char *filename, const char *format);

typedef struct query_bits_struct {
    char *format;
    char *ep;
    CURL *curl;
    FILE *file;
    char filename[20];
    int verbose;
    int xml_filter;
    int parse;  /* true if we want to parse results */
    int time; /* print execution time */
    const char *operation;
    int auto_prefix; /* true if we want to add PREFIXes */
} query_bits;

/* must not be longer than 20 bytes, see above */
static const char *tmp_filename = "/tmp/sparql-XXXXXXX";

static int execute_operation(const char *query, query_bits *bits);
static void sparql_curl_init(query_bits *bits);

static void interactive(query_bits *bits);

static const char *op_query = "query";
static const char *op_update = "update";

int main(int argc, char *argv[])
{
    query_bits bits = { .format = NULL, .ep = NULL, .verbose = 0, .xml_filter = 0, .parse = 1, .time = 0, .operation = op_query};

    static char *optstring = "f:vnthp";
    char *query = NULL;
    int help = 0;
    int pipe = 0;
    int c, opt_index = 0;

    static struct option long_options[] = {
        { "format", 1, 0, 'f' },
        { "verbose", 0, 0, 'v' },
        { "noparse", 0, 0, 'n' },
        { "time", 0, 0, 't' },
        { "help", 0, 0, 'h' },
        { "pipe", 0, 0, 'p' },
        { "auto", 0, 0, 'a' },
        { 0, 0, 0, 0 }
    };

    char *cmd = basename(argv[0]);
    if (!strcmp(cmd, "sparql-update")) {
        bits.operation = op_update;
        bits.format = "text/plain";
    }

    while ((c = getopt_long (argc, argv, optstring, long_options, &opt_index)) != -1) {
        if (c == 'f') {
            bits.format = optarg;
        } else if (c == 'v') {
            bits.verbose++;
        } else if (c == 'n') {
            bits.parse = 0;
        } else if (c == 't') {
            bits.time = 1;
        } else if (c == 'p') {
            pipe = 1;
        } else if (c == 'a') {
            bits.auto_prefix = 1;
        } else {
            help = 1;
        }
    }

    for (int k = optind; k < argc; ++k) {
        if (!bits.ep) {
            bits.ep = argv[k];
        } else if (!query) {
            query = argv[k];
        } else {
            help = 1;
        }
    }

    if (help || !bits.ep) {
        char *example;
        if (bits.operation == op_update) {
            example = "INSERT DATA { <s> <p> <o> }";
        } else {
            example = "SELECT * WHERE { ?s ?p ?o } LIMIT 10";
        }
        fprintf(stderr, "%s revision %s\n", argv[0], GIT_REV);
        fprintf(stderr, "Usage: %s [-v] [-n] [-t] [-p] [-f MIME type] <ep> [<%s>] e.g.\n", cmd, bits.operation);
        fprintf(stderr, " %s http://example.net/sparql '%s'\n", cmd, example);
        fprintf(stderr, " -n, --noparse  don't parse SPARQL XML results\n");
        fprintf(stderr, " -t, --time     print execution time for each %s\n", bits.operation);
        fprintf(stderr, " -p, --pipe     read %s from standard input and execute immediately\n", bits.operation);
        fprintf(stderr, " -a, --auto     automatically add PREFIXes if missing\n");
        fprintf(stderr, " <ep> is a SPARQL HTTP endpoint\n");
        fprintf(stderr, " <%s> is a SPARQL %s to execute immediately in non-interactive mode\n", bits.operation, bits.operation);
        fprintf(stderr, "remember to use shell quoting if necessary\n");
        return 1;
    }

    atexit(scan_fini);

    if (!bits.format) {
        bits.format = "application/sparql-results+xml";
    }

    if (!query && pipe) {
	ssize_t obtained = 0;
	size_t querylen = 0;
	char block[1024];
	while ((obtained = read(STDIN_FILENO, block, 1024)) != 0) {
	    if (obtained < 0) {
		perror("failed to read from standard input");
		return 1;
	    }
	    query = (char *)realloc(query, querylen + obtained + 1);
	    memcpy(query + querylen, block, obtained);
	    querylen += obtained;
	}
	if (querylen == 0) return 0;
	query[querylen] = '\0';
    }

    if (query) {
        sparql_curl_init(&bits);
        if (bits.auto_prefix) {
            scan_init();
        }
        CURLcode error = execute_operation(query, &bits);

        return error;
    } else {
        interactive(&bits);
        printf("\n");
    }

    return 0;
}

static double double_time()
{
    struct timeval now;

    /* TODO probably this should use clock_gettime(CLOCK_MONOTONIC) where supported */
    if (gettimeofday(&now, 0) == -1) {
        return 0.0;
    }

    return (double)now.tv_sec + (now.tv_usec * 0.000001);
}

static char *history_filename(query_bits *bits)
{
    char *dotfile;
    if (bits->operation == op_query) {
        dotfile = g_strconcat(g_get_home_dir(), "/.sparql_history", NULL);
    } else {
        dotfile = g_strconcat(g_get_home_dir(), "/.sparql_uhistory", NULL);
    }

    return dotfile;
}

static void load_history_dotfile(query_bits *bits)
{
    char *dotfile = history_filename(bits);
    read_history(dotfile);
    g_free(dotfile);
}

static void save_history_dotfile(query_bits *bits)
{
    char *dotfile = history_filename(bits);
    stifle_history(100); /* arbitrarily restrict history file to 100 entries */
    write_history(dotfile);
    g_free(dotfile);
}

static void sparql_curl_init(query_bits *bits)
{
    bits->curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    char *accept;
    if (bits->parse) {
        accept = g_strdup_printf("Accept: %s, application/sparql-results+xml", bits->format);
    } else {
        accept = g_strdup_printf("Accept: %s", bits->format);
    }
    headers = curl_slist_append(headers, accept);
    g_free(accept);

    curl_easy_setopt(bits->curl, CURLOPT_VERBOSE, bits->verbose);
    curl_easy_setopt(bits->curl, CURLOPT_HTTPHEADER, headers);
}

/* CURLOPT_WRITEFUNCTION 
CURLOPT_WRITEDATA */

static size_t my_header_fn(void *ptr, size_t size, size_t nmemb, void *stream)
{
    query_bits *bits = (query_bits *) stream;

    const char content_type[] = "Content-Type:";
    const char *sparql = "application/sparql-results+xml";

    if (size * nmemb > sizeof(content_type) && !strncasecmp((char *) ptr, content_type, sizeof(content_type) - 1)) {
        /* content type */
        char *type = (char *) ptr + sizeof(content_type);
        size_t len = (size * nmemb) - sizeof(content_type);
        if (bits->parse == 1 && len > strlen(sparql) && !strncmp(type, sparql, strlen(sparql))) {
            bits->xml_filter = 1;
            strcpy(bits->filename, tmp_filename);
            int fd = mkstemp(bits->filename);
            bits->file = fdopen(fd, "a");
            curl_easy_setopt(bits->curl, CURLOPT_WRITEDATA, bits->file);
        }
    }

    return size * nmemb;
}

static int execute_operation(const char *query, query_bits *bits)
{
    char my_curl_error[CURL_ERROR_SIZE];
    char *query_url;

    char *executed_query = NULL;

    if (bits->auto_prefix) {
        char *suggestions = NULL;
        scan_sparql(query, &suggestions);
        if (strlen(suggestions)) {
            executed_query = g_strjoin("", suggestions, query, NULL);
            printf("Missing PREFIXes, adding:\n%s", suggestions);
        } else {
            executed_query = g_strdup(query);
        }
        g_free(suggestions);
    } else {
        executed_query = g_strdup(query);
    }

    if (bits->operation == op_query) {
        char *encoded = curl_easy_escape (bits->curl, executed_query, 0);
        if (strchr(bits->ep, '?')) {
            query_url = g_strdup_printf("%s&query=%s", bits->ep, encoded);
        } else {
            query_url = g_strdup_printf("%s?query=%s", bits->ep, encoded);
        }
        curl_free(encoded);
    } else if (bits->operation == op_update) {
        query_url = g_strdup(bits->ep);
    } else {
        printf("Unknown operation %s\n", bits->operation);
        g_free(executed_query);

        return 1;
    }

    /* default to not filtering */
    bits->xml_filter = 0;
    curl_easy_setopt(bits->curl, CURLOPT_WRITEDATA, stdout);
    curl_easy_setopt(bits->curl, CURLOPT_ERRORBUFFER, my_curl_error);
    curl_easy_setopt(bits->curl, CURLOPT_URL, query_url);
    curl_easy_setopt(bits->curl, CURLOPT_HEADERFUNCTION, my_header_fn);
    curl_easy_setopt(bits->curl, CURLOPT_HEADERDATA, bits);
    double then = 0.0, now = 0.0;
    if (bits->time) then = double_time();
    if (bits->operation == op_update) {
        curl_easy_setopt(bits->curl, CURLOPT_POST, 1);
        char *encoded = curl_easy_escape (bits->curl, executed_query, 0);
        char *field = g_strdup_printf("update=%s", encoded);
        curl_free(encoded);
        curl_easy_setopt(bits->curl, CURLOPT_POSTFIELDS, field);
    }
    CURLcode code = curl_easy_perform(bits->curl);

    if (code) {
        fprintf(stderr, "CURL: %s\n", my_curl_error);
    }
    if (bits->time) now = double_time();
    if (bits->xml_filter) {
        fclose(bits->file);
        sr_parse(bits->filename, bits->format);
        unlink(bits->filename);
        bits->xml_filter = 0;
    }
    if (bits->time) {
        fprintf(stderr, "Execution time: %.1fms\n", (now-then)*1000.0);
    }
    g_free(executed_query);

    return code;
}

static int check_endpoint(query_bits *bits)
{
    char my_curl_error[CURL_ERROR_SIZE];
    CURLcode code;

    code = curl_easy_setopt(bits->curl, CURLOPT_ERRORBUFFER, my_curl_error);
    if (!code) code = curl_easy_setopt(bits->curl, CURLOPT_URL, bits->ep);
    if (!code) code = curl_easy_setopt(bits->curl, CURLOPT_NOBODY, 1);
    if (!code) code = curl_easy_perform(bits->curl);

    /* put everything back regardless */
    curl_easy_setopt(bits->curl, CURLOPT_NOBODY, 0);
    /* (some versions of?) curl forces HTTP requests to HEAD when NOBODY is set, but doesn't put it back... */
    curl_easy_setopt(bits->curl, CURLOPT_HTTPGET, 1);
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

static void interactive(query_bits *bits)
{
    const char *prompt =   "sparql$ ";
    const char *reprompt = "      $ ";

    scan_init();

    if (!isatty(0)) {
        /* no terminal input so disable TAB completion */
        rl_bind_key ('\t', rl_insert);
        reprompt = prompt = "";
        bits->parse = 0;
    } else {
        using_history();
        bits->auto_prefix = 1;
    }

    /* fill out readline functions */
    load_history_dotfile(bits);

    sparql_curl_init(bits);
    if (check_endpoint(bits)) {
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
            execute_operation(query, bits);
        }
    } while (query);

    save_history_dotfile(bits);

    return;
}

/* vi:set expandtab sts=4 sw=4: */
