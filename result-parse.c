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
#include <locale.h>
#include <langinfo.h>
#include <libxml/parser.h>
#include <glib.h>

/* protect us from Linux's lame strlen */
#define safe_strlen(_s) ((_s) ? strlen(_s) : 0)

/* number of columns names to record widths for in 1st pass */
#define TMP_COLS 32

static const char *nullstr = "";

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

struct aa_chars {
    char *H;
    char *V;
    char *TL;
    char *TC;
    char *TR;
    char *CL;
    char *CC;
    char *CR;
    char *BL;
    char *BC;
    char *BR;
};

typedef struct _xmlctxt {
    enum xmlstate state;
    int pass;
    int col;
    int cols;
    int *widths;
    char **names;
    char **row;
    int tmp_widths[TMP_COLS];
    gchar *text;
    struct aa_chars aa;
    GSList *name_list;
    int current_col;
} xmlctxt;

static int name_to_col(xmlctxt *ctxt, const char *name)
{
    for (int i=0; i<ctxt->cols; i++) {
        if (!strcmp(name, ctxt->names[i])) {
            return i;
        }
    }

    fprintf(stderr, "unknown column name ‘%s’ in results\n", name);

    return 0;
}

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
                if (ctxt->pass == 0) {
                    /* do nothing */
                } else {
                    for (int i=0; i<MAX(ctxt->cols, 1); i++) {
                        if (i == 0) {
                            printf("%s%s%s", ctxt->aa.TL, ctxt->aa.H, ctxt->aa.H);
                        } else {
                            printf("%s%s%s", ctxt->aa.TC, ctxt->aa.H, ctxt->aa.H);
                        }
                        for (int j=0; j<ctxt->widths[i]; j++) {
                            printf(ctxt->aa.H);
                        }
                    }
                    printf("%s\n", ctxt->aa.TR);
                }
                ctxt->state = STATE_HEAD;
                ctxt->col = 0;
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
                    if (ctxt->pass == 0) {
                        if (ctxt->cols < TMP_COLS) {
                            ctxt->tmp_widths[ctxt->cols] = strlen(value) + 1;
                        }
                        ctxt->name_list = g_slist_append(ctxt->name_list, g_strdup(value));
                        (ctxt->cols)++;
                    } else {
                        if (!strcmp(key, "name")) {
                            printf("%s ?%*s ", ctxt->aa.V, -ctxt->widths[ctxt->col] + 1, value);
                            (ctxt->col)++;
                        }
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
                if (ctxt->pass == 0) {
                    /* do nothing */
                } else {
                    for (int i=0; i<ctxt->cols; i++) {
                        if (i == 0) {
                            printf("%s%s%s", ctxt->aa.CL, ctxt->aa.H, ctxt->aa.H);
                        } else {
                            printf("%s%s%s", ctxt->aa.CC, ctxt->aa.H, ctxt->aa.H);
                        }
                        for (int j=0; j<ctxt->widths[i]; j++) {
                            printf(ctxt->aa.H);
                        }
                    }
                    printf("%s\n", ctxt->aa.CR);
                }
                ctxt->state = STATE_RESULTS;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing results)\n");
                /* stop parsing */
            }
            break;

        case STATE_RESULTS:
            if (!strcmp(name, "result")) {
                ctxt->state = STATE_RESULT;
                ctxt->col = 0;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format (missing result)\n");
                /* stop parsing */
            }
            break;

        case STATE_RESULT:
            if (!strcmp(name, "binding")) {
                while (attrs && *attrs) {
                    const char *key = (const char *) *(attrs++);
                    const char *value = (const char *) *(attrs++);
                    ctxt->current_col = -1;
                    if (!strcmp(key, "name")) {
                        ctxt->current_col = name_to_col(ctxt, value);
                    }
                    if (ctxt->current_col == -1) {
                        fprintf(stderr, "no column name found in results\n");
                        ctxt->current_col = 0;
                    }
                }
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
                if (ctxt->pass == 0) {
                    ctxt->widths = g_new0(int, MAX(ctxt->cols, 1));
                    ctxt->names = g_new0(char *, MAX(ctxt->cols, 1));
                    ctxt->row = g_new0(char *, MAX(ctxt->cols, 1));
                    GSList *nlist = ctxt->name_list;
                    for (int k = 0; k < ctxt->cols; ++k) {
                        if (!nlist) {
                            fprintf(stderr, "name list error\n");
                            exit(1);
                        }
                        ctxt->names[k] = nlist->data;
                        nlist = nlist->next;
                        ctxt->row[k] = (char *)nullstr;
                        if (k < TMP_COLS) {
                            ctxt->widths[k] = ctxt->tmp_widths[k];
                        } else {
                            ctxt->widths[k] = 2;
                        }
                    }
                } else {
                    if (ctxt->cols > 0) {
                        printf("%s\n", ctxt->aa.V);
                    }
                }
                if (ctxt->cols > 0) {
                    ctxt->state = STATE_SPARQL_WANT_RESULTS;
                } else {
                    ctxt->state = STATE_SPARQL_WANT_BOOLEAN;
                }
            }
            break;

        case STATE_VARIABLE:
            ctxt->state = STATE_HEAD;
            break;

        case STATE_BOOLEAN:
            if (ctxt->pass == 0) {
                ctxt->widths[ctxt->col] = MAX(safe_strlen(ctxt->text), ctxt->widths[ctxt->col]);
                ctxt->state = STATE_RESULTS_DONE;
            } else {
                printf("%s %*s %s\n", ctxt->aa.V, -ctxt->widths[ctxt->col], ctxt->text, ctxt->aa.V);
                printf(ctxt->aa.BL);
                for (int i=0; i<ctxt->widths[ctxt->col] + 2; i++) {
                    printf(ctxt->aa.H);
                }
                printf("%s\n", ctxt->aa.BR);
                /* by now ctxt->text should be 'true' or 'false' */
                /* expect sparql close next */
                ctxt->state = STATE_RESULTS_DONE;
            }
            break;

        case STATE_URI:
            if (!strcmp(name, "uri")) {
                if (ctxt->pass == 0) {
                    if (ctxt->widths) {
                        ctxt->widths[ctxt->current_col] = MAX(safe_strlen(ctxt->text) + 2, ctxt->widths[ctxt->current_col]);
                    }
                } else {
                    ctxt->row[ctxt->current_col] = g_strdup_printf("<%s>", ctxt->text);
                }
                ctxt->state = STATE_BINDING_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after URI\n", name);
            }
            break;

        case STATE_LITERAL:
            if (!strcmp(name, "literal")) {
                if (ctxt->pass == 0) {
                    ctxt->widths[ctxt->current_col] = MAX(safe_strlen(ctxt->text), ctxt->widths[ctxt->current_col]);
                } else {
                    ctxt->row[ctxt->current_col] = g_strdup_printf("%s", ctxt->text ? ctxt->text : "");
                }
                ctxt->state = STATE_BINDING_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after literal\n", name);
            }
            break;

        case STATE_BNODE:
            if (!strcmp(name, "bnode")) {
                if (ctxt->pass == 0) {
                    ctxt->widths[ctxt->current_col] = MAX(safe_strlen(ctxt->text) + 2, ctxt->widths[ctxt->current_col]);
                } else {
                    ctxt->row[ctxt->current_col] = g_strdup_printf("_:%s", ctxt->text ? ctxt->text : "");
                }
                ctxt->state = STATE_BINDING_DONE;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after bnode\n", name);
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
                if (ctxt->pass == 0) {
                    /* do nothing */
                } else {
                    /* print the reuslt row */
                    for (int i=0; i<ctxt->cols; i++) {
                        printf("%s %s%*s ", ctxt->aa.V, ctxt->row[i], -ctxt->widths[i] + (int)safe_strlen(ctxt->row[i]), "");
                        if (ctxt->row[i] != nullstr) {
                            g_free(ctxt->row[i]);
                            ctxt->row[i] = (char *)nullstr;
                        }
                    }
                    printf("%s\n", ctxt->aa.V);
                }
                ctxt->state = STATE_RESULTS;
            } else {
                fprintf(stderr, "results not in valid SPARQL results format, unexpected </%s> after result\n", name);
            }
            break;

        case STATE_RESULTS:
            if (!strcmp(name, "results")) {
                ctxt->state = STATE_RESULTS_DONE;
                if (ctxt->pass == 0) {
                    /* do nothing */
                } else {
                    for (int i=0; i<ctxt->cols; i++) {
                        if (i == 0) {
                            printf("%s%s%s", ctxt->aa.BL, ctxt->aa.H, ctxt->aa.H);
                        } else {
                            printf("%s%s%s", ctxt->aa.BC, ctxt->aa.H, ctxt->aa.H);
                        }
                        for (int j=0; j<ctxt->widths[i]; j++) {
                            printf(ctxt->aa.H);
                        }
                    }
                    printf("%s\n", ctxt->aa.BR);
                }
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
    setlocale(LC_ALL, "");
    int utf8_mode = (strcmp(nl_langinfo(CODESET), "UTF-8") == 0);
    if (utf8_mode) {
        ctxt->aa.H = "─";
        ctxt->aa.V = "│";
        ctxt->aa.TL = "┌";
        ctxt->aa.TC = "┬";
        ctxt->aa.TR = "┐";
        ctxt->aa.CL = "├";
        ctxt->aa.CC = "┼";
        ctxt->aa.CR = "┤";
        ctxt->aa.BL = "└";
        ctxt->aa.BC = "┴";
        ctxt->aa.BR = "┘";
    } else {
        ctxt->aa.H = "-";
        ctxt->aa.V = "|";
        ctxt->aa.TL = ".";
        ctxt->aa.TC = "-";
        ctxt->aa.TR = ".";
        ctxt->aa.CL = "|";
        ctxt->aa.CC = "+";
        ctxt->aa.CR = "|";
        ctxt->aa.BL = "'";
        ctxt->aa.BC = "-";
        ctxt->aa.BR = "'";
    }
    ctxt->pass = 0;
    xmlSAXUserParseFile(&sax, (void *) ctxt, filename);
    ctxt->pass = 1;
    xmlSAXUserParseFile(&sax, (void *) ctxt, filename);
    if (ctxt->widths) {
        g_free(ctxt->widths);
    }
    g_free(ctxt);

    return 0;
}

/* vi:set expandtab sts=4 sw=4: */
