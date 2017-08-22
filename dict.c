#include "dict.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


struct dict *dict_create() {
    struct dict *d = (struct dict*)malloc(sizeof(struct dict));
    d->head = NULL;
    return d;
}


void dict_free(struct dict *d) {
    if (d == NULL)
        return;

    struct dict_item *head = d->head;
    struct dict_item *tmp;
    while (head != NULL) {
        tmp = head;
        head = head->next;
        free(tmp->key);
        free(tmp->value);
        free(tmp);
    }
    free(d);
} 


int dict_has(const struct dict *d, const char *key) {
    if (d == NULL)
        return 0;

    struct dict_item *current_item = d->head;
    while (current_item != NULL) {
        if (!strcmp(current_item->key, key))
            return 1;     // found
        current_item = current_item->next;
    }
    return 0;
}


char *dict_get(const struct dict *d, const char *key) {
    if (d == NULL)
        return NULL;

    struct dict_item *current_item = d->head;
    while (current_item != NULL) {
        if (!strcmp(current_item->key, key))
            return current_item->value;     // found -> return
        current_item = current_item->next;
    }
    return NULL;
}


int dict_del(struct dict *d, const char * key) {
    if (d == NULL)
        return -1;

    struct dict_item *current_item = d->head;
    struct dict_item *prev = NULL;

    while (current_item != NULL) {
        if (!strcmp(current_item->key, key)) {
            // found -> delete
            if (prev)
                prev->next = current_item->next;
            else
                d->head = current_item->next;
            free(current_item->key);
            free(current_item->next);
            free(current_item);
            return 0;
        }
        prev = current_item;
        current_item = current_item->next;
    }
    return -1;
}

int dict_set(struct dict *d, char *key, char *val) {
    if (d == NULL)
        return -1;

    if (d->head == NULL) {
        //empty dict
        struct dict_item *new = (struct dict_item *)malloc(sizeof(struct dict_item));
        new->key = key;
        new->value = val;
        new->next = NULL;
        d->head = new;
        return 0;
    }
    struct dict_item *current_item = d->head;
    struct dict_item *prev = NULL;
    while (current_item != NULL) {
        if (!strcmp(current_item->key, key)) {
            // found -> replace
            current_item->value = val;
            return 0;
        }
        prev = current_item;
        current_item = current_item->next;
    }
    struct dict_item *new = (struct dict_item *)malloc(sizeof(struct dict_item));
    new->key = key;
    new->value = val;
    new->next = NULL;
    prev->next = new;
    return 0;
}
