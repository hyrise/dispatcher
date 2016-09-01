#include "dict.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>



void testDictErrorHandling() {
    struct dict *d = NULL;
    int e = dict_set(d, "hallo", "1");
    assert(e == -1);

    e = dict_del(d, "hallo");
    assert(e == -1);

    e = dict_has(d, "halo");
    assert(e == 0);

    const char *item = dict_get(d, "hallo");
    assert(item == NULL);


    d = dict_create();
    dict_set(d, "hallo", "1");
    dict_set(d, "hallo2", "1");
    dict_set(d, "hallo3", "2");

    e = dict_del(d, "halo");
    assert(e == -1);

    item = dict_get(d, "halo");
    assert(item == NULL);

    dict_free(d);
}


void testBasicDict() {
    struct dict *d = dict_create();

    dict_set(d, "name", "drmaa2");
    dict_del(d, "name");
    dict_set(d, "name", "drmaa2");
    assert(dict_has(d, "name") != 0);
    assert(dict_has(d, "language") == 0);

    const char *v = dict_get(d, "name");
    assert(strcmp(v, "drmaa2") == 0);

    dict_set(d, "language", "c");
    assert(dict_has(d, "language") != 0);

    dict_del(d, "name");
    assert(dict_has(d, "name") == 0);
    v = dict_get(d, "language");
    assert(strcmp(v, "c") == 0);

    dict_free(d);
}

void testDict() {
    struct dict *d = dict_create(NULL);
    dict_set(d, "name", "drmaa2");
    dict_set(d, "language", "c");
    dict_set(d, "version", "2");
    dict_set(d, "age", "2 weeks");

    assert(dict_has(d, "language") != 0);

    dict_del(d, "age");
    dict_del(d, "language");

    assert(dict_has(d, "version") != 0);

    dict_del(d, "name");
    dict_set(d, "version", "3");
    const char *v = dict_get(d, "version");
    assert(strcmp(v, "3") == 0);

    dict_del(d, "version");
    dict_set(d, "new_version", "4");
    assert(dict_has(d, "version") == 0);
    assert(dict_has(d, "new_version") != 0);

    dict_free(d);
}


int main() {
    return 0;
	struct dict *d = dict_create();
	assert(d != NULL);
	assert(d->head == NULL);

	testDictErrorHandling();
	testBasicDict();
	testDict();

	return 0;
}