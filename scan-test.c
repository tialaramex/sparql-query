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
 *  $Id: scan-test.c $
 */

#include <stdio.h>

#include "scan-sparql.h"

int main()
{
    char *q[] = {
        "PREFIX a: <X>",
        "PREFIX : <>",
        "PREFIX q: <qdos>",
        "PREFIX q: <http://qdos.com/schema/>",
        "PREFIX q: <http://qdos.com/§/> Prefix foaf: <http://xmlns.com/foaf/0.1/> WHERE { a:foo a foaf:name . :a: a:b a:c . }",
        "{ dbpedia:a rdf:type dbpedia:c . <> a foaf:Document }",
        "{ madeup:foo fdsgsagdsa:fdsf owl:Class }",
        NULL
    };

    scan_init();

    for (int i=0; 1; i++) {
        if (!q[i]) break;
        printf("%s\n", q[i]);
        char *suggest;
        scan_sparql(q[i], &suggest);
        printf("suggest:\n%s", suggest);
    }

    return 0;
}

/* vi:set expandtab sts=4 sw=4: */
