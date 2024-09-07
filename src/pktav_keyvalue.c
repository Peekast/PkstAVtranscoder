#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pktav_keyvalue.h"
#include "pktav_strings.h"
#include "pktav_error.h"
// Function to split string

size_t count_char(const char* str, char ch) {
    size_t count = 0;
    while(*str) {
        if(*str++ == ch)
            ++count;
    }
    return count;
}


char *get_value(char *ptr, const char delim) {
    for (; *ptr != '\0' && *ptr != delim; ptr++);
    if (*ptr == delim) {
        *ptr = '\0';
        ptr++;
    }
    return ptr;
}


KeyValueList* kv_list_fromstring(const char *kv_str, char pair_delim, char kv_delim) {
    char delim[2];
    char *pair_str, *key_str, *value_str;
    char *tmp_kv_str = pkst_strdup(kv_str);
    KeyValueList* kv_list;
    int i = 0;

    sprintf(delim, "%c", pair_delim);

    if (!kv_str)
        return NULL;

    kv_list = calloc(1,sizeof(KeyValueList));
    if (!kv_list) {
        free(tmp_kv_str);
        return NULL;
    }

    kv_list->count = count_char(kv_str, pair_delim) + 1;
    kv_list->items = calloc(1,kv_list->count * sizeof(KeyValue));

    if (!kv_list->items) {
        free(kv_list);
        free(tmp_kv_str);
        return NULL;
    }

    pair_str = strtok(tmp_kv_str, delim);
    while (pair_str) {
        key_str = pair_str;
        value_str = get_value(key_str, kv_delim);
        kv_list->items[i].key = pkst_strdup(key_str);
        kv_list->items[i].value = pkst_strdup(value_str);
        pair_str = strtok(NULL, delim);
        i++;
    }

    free(tmp_kv_str);

    return kv_list;
}

int add_to_kv_list(KeyValueList **kv_list, const char *key, const char *value) {
    KeyValue* new_items;
    if (!(*kv_list)) {
        *kv_list = calloc(1,sizeof(KeyValueList));
        if (!(kv_list))
            return -1;

        (*kv_list)->items = NULL;
        (*kv_list)->count = 0;
    }

    // Reasigna memoria para un elemento adicional
    new_items = realloc((*kv_list)->items, ((*kv_list)->count + 1) * sizeof(KeyValue));
    if (new_items == NULL) {
        return -OS_ERROR;  // Error al reasignar memoria
    }
    (*kv_list)->items = new_items;

    // Añade el nuevo par clave-valor en el último espacio
    (*kv_list)->items[(*kv_list)->count].key = pkst_strdup(key);
    (*kv_list)->items[(*kv_list)->count].value = pkst_strdup(value);

    // Incrementa la cuenta de elementos en la lista
    (*kv_list)->count++;

    return 0;  // Éxito
}

char* kv_list_tostring(const KeyValueList* kv_list, char pair_delim, char kv_delim) {
    int i;
    size_t buffer_size = 0;

    // Calcula el tamaño total necesario para la cadena.
    for (i = 0; i < kv_list->count; i++) {
        buffer_size += strlen(kv_list->items[i].key) + strlen(kv_list->items[i].value) + 2; // +2 para el '=' y el ';'
    }

    // Reserva memoria para la cadena.
    char* buffer = calloc(1,buffer_size);
    if (buffer == NULL) {
        return NULL; // Error al reservar memoria
    }

    char* ptr = buffer;

    // Llena la cadena con los pares clave-valor.
    for (i = 0; i < kv_list->count; i++) {
        ptr += sprintf(ptr, "%s%c%s%c", kv_list->items[i].key, kv_delim, kv_list->items[i].value, pair_delim);
    }

    *(ptr-1) = '\0'; // Reemplaza el último ';' con '\0'

    return buffer;
}

const char* get_value_from_kv_list(KeyValueList *kv_list, const char *key) {
    for (int i = 0; i < kv_list->count; i++) {
        if (strcmp(kv_list->items[i].key, key) == 0) {
            return kv_list->items[i].value;
        }
    }
    pktav_errno = PK_ERROR_KEYNOTFOUND;
    return NULL; // No se encontró la clave
}


// Function to free KeyValueList
void free_kv_list(KeyValueList* kv_list) {
    for (int i = 0; i < kv_list->count; i++) {
        free(kv_list->items[i].key);
        free(kv_list->items[i].value);
    }
    free(kv_list->items);
    kv_list->items = NULL;
    free(kv_list);
}

void dump_kv_list(const KeyValueList *kv_list) {
    if (!kv_list) {
        printf("KeyValueList is NULL.\n");
        return;
    }

    printf("KeyValueList with %d items:\n", kv_list->count);
    for (int i = 0; i < kv_list->count; i++) {
        printf("Item %d: Key = '%s', Value = '%s'\n", i, kv_list->items[i].key, kv_list->items[i].value);
    }
}