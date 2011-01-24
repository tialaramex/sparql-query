#ifndef SCAN_SPARQL_H
#define SCAN_SPARQL_H

int scan_init();
void scan_fini();

/* fill prefixes out with suggested text for prepending to the query to satisfy
 * any undeclared qname prefixes */
int scan_sparql(const char *str, char **prefixes);

#endif
