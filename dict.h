#ifndef DICT_H_
#define DICT_H_


struct dict_item {
    const char *key;
    const char *value;
    struct dict_item *next;
};


struct dict {
    struct dict_item *head;
};


struct dict *dict_create();
void dict_free(struct dict *d);
int dict_has(const struct dict *d, const char *key);
const char *dict_get(const struct dict *d, const char *key);
int dict_del(struct dict *d, const char * key);
int dict_set(struct dict *d, const char *key, const char *val);


#endif
