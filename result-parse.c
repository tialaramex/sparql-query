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

#include <string.h>
#include <libxml/parser.h>
#include <glib.h>

enum xmlstate {
    STATE_START,
    STATE_SPARQL_WANT_HEAD,
    STATE_SPARQL_WANT_RESULTS,
    STATE_SPARQL_WANT_BOOLEAN,
    STATE_HEAD,

    STATE_HEAD_ONLY,
    STATE_HEAD_LINK_ONLY,
    STATE_LINK,

    STATE_BINDING,
    STATE_BINDING_DONE,
    STATE_VARIABLE,

    STATE_RESULTS,
    STATE_RESULTS_DONE,
    STATE_RESULT,
    STATE_BOOLEAN,
    STATE_URI,
    STATE_LITERAL,
    STATE_BNODE,

    STATE_DONE,
};

typedef struct {
    enum xmlstate state;
    gchar *text;
} xmlctxt;

static void xml_start_document(void *user_data)
{
    xmlctxt *ctxt = (xmlctxt *) user_data;
    ctxt->state = STATE_START;
}

static void xml_end_document(void *user_data)
{
    xmlctxt *ctxt = (xmlctxt *) user_data;
    if (ctxt->state != STATE_DONE) {
        fprintf(stderr, "SPARQL results end abruptly\n");
    }
}

static void xml_start_element(void *user_data, const xmlChar *xml_name, const xmlChar **attrs)
{
    const char *name = (const char *) xml_name;

    xmlctxt *ctxt = (xmlctxt *) user_data;
    switch  (ctxt->state) {
        case STATE_START:
            if (strcmp(name, "sparql")) {
                fprintf(stderr, "results not in valid SPARQL results format (missing sparql)\n");
                /* stop parsing */
            } else {
                ctxt->state = STATE_SPARQL_WANT_HEAD;
            }
            break;

        case STATE_SPARQL_WANT_HEAD:
            if (!strcmp(name, "head")) {
                ctxt->state = STATE_HEAD;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing head)\n");
                /* stop parsing */
            }
            break;

        case STATE_HEAD:
            if (!strcmp(name, "variable")) {
                ctxt->state = STATE_VARIABLE;
                while (attrs && *attrs) {
                    const char *key = (const char *) *(attrs++);
                    const char *value = (const char *) *(attrs++);
                    if (!strcmp(key, "name")) {
                        printf("\t?%s\t", value);
                        /* do something with g_strdup(value); */
                    }
                }
                break;
            }
                         
       /* fall through */
        case STATE_HEAD_LINK_ONLY:
            if (!strcmp(name, "link")) {
                ctxt->state = STATE_LINK;
                /* handle link */
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (wrong element in head)\n");
                /* stop parsing */
            }
            break;

        case STATE_SPARQL_WANT_RESULTS:
            if (!strcmp(name, "results")) {
                ctxt->state = STATE_RESULTS;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing results)\n");
                /* stop parsing */
            }
            break;

        case STATE_RESULTS:
            if (!strcmp(name, "result")) {
                ctxt->state = STATE_RESULT;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing result)\n");
                /* stop parsing */
            }
            break;

        case STATE_RESULT:
            if (!strcmp(name, "binding")) {
                ctxt->state = STATE_BINDING;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing binding)\n");
                /* stop parsing */
            }
            break;

        case STATE_BINDING:
            if (!strcmp(name, "uri")) {
                ctxt->state = STATE_URI;
            } else if (!strcmp(name, "bnode")) {
                ctxt->state = STATE_BNODE;
            } else if (!strcmp(name, "literal")) {
                /* FIXME we don't worry about lang tags and type URIs here yet */
                ctxt->state = STATE_LITERAL;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (wrong element in binding)\n");
                /* stop parsing */
            }
            break;

        case STATE_SPARQL_WANT_BOOLEAN:
            if (!strcmp(name, "boolean")) {
                ctxt->state = STATE_BOOLEAN;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing boolean)\n");
                /* stop parsing */
            }
            break;

        default:
            fprintf(stderr, "results not in valid SPARQL results format, unexpected <%s>\n", name);
            /* stop parsing */
    }
    g_free(ctxt->text);
    ctxt->text = NULL;
}

static void xml_end_element(void *user_data, const xmlChar *xml_name)
{
    const char *name = (const char *) xml_name;

    xmlctxt *ctxt = (xmlctxt *) user_data;

    switch  (ctxt->state) {
        case STATE_HEAD:
            if (!strcmp(name, "head")) {
                /* FIXME if one or more variables then expect results, otherwise boolean */
                printf("\n");
                ctxt->state = STATE_SPARQL_WANT_RESULTS;
            }
            break;

        case STATE_VARIABLE:
            ctxt->state = STATE_HEAD;
            break;

        case STATE_BOOLEAN:
            printf("boolean '%s'\n", ctxt->text);
            /* by now ctxt->text should be 'true' or 'false' */
            /* expect sparql close next */
            break;

        case STATE_URI:
            if (!strcmp(name, "uri")) {
                printf("<%s>  ", ctxt->text);
                ctxt->state = STATE_BINDING_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after URI\n", name);
            }
            break;

        case STATE_LITERAL:
            if (!strcmp(name, "literal")) {
                printf("%s  ", ctxt->text);
                ctxt->state = STATE_BINDING_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after literal\n", name);
            }
            break;

        case STATE_BINDING_DONE:
            if (!strcmp(name, "binding")) {
                ctxt->state = STATE_RESULT;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after binding\n", name);
            }
            break;

        case STATE_RESULT:
            if (!strcmp(name, "result")) {
                printf("\n");
                ctxt->state = STATE_RESULTS;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after result\n", name);
            }
            break;

        case STATE_RESULTS:
            if (!strcmp(name, "results")) {
                ctxt->state = STATE_RESULTS_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after results\n", name);
            }
            break;

        case STATE_RESULTS_DONE:
            if (!strcmp(name, "sparql")) {
                ctxt->state = STATE_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s>\n", name);
            }
            break;

        default:
            fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s>\n", name);
            /* stop parsing */
    }
}

static void xml_characters(void *user_data, const xmlChar *ch, int len)
{
    const char *chars = (const char *) ch;
    xmlctxt *ctxt = (xmlctxt *) user_data;

    if (ctxt->text) {
        char *tmp1 = ctxt->text;
        char *tmp2 = g_strndup(chars, len);
        ctxt->text = g_strconcat(tmp1, tmp2, NULL);
        g_free(tmp1);
        g_free(tmp2);
    } else {
        ctxt->text = g_strndup(chars, len);
    }
}

static xmlSAXHandler sax = {
    .startDocument = xml_start_document,
    .endDocument = xml_end_document,
    .startElement = xml_start_element,
    .endElement = xml_end_element,
    .characters = xml_characters,
};

int sr_parse(const char *filename)
{
    xmlctxt *ctxt = g_new0(xmlctxt, 1);
    xmlSAXUserParseFile(&sax, (void *) ctxt, filename);
    g_free(ctxt);

    return 0;
}

/* vi:set expandtab sts=4 sw=4: */
