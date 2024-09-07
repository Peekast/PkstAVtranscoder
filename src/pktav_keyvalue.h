#ifndef _KEYVALUE_H
#define _KEYVALUE_H 1


typedef struct  {
    char *key;
    char *value;
} KeyValue;

typedef struct  {
    KeyValue *items;
    int count;
} KeyValueList;


extern void free_kv_list(KeyValueList* kv_list);
extern void dump_kv_list(const KeyValueList *kv_list);
extern char* kv_list_tostring(const KeyValueList* kv_list, char pair_delim, char kv_delim);
extern KeyValueList* kv_list_fromstring(const char *kv_str, char pair_delim, char kv_delim);
extern int add_to_kv_list(KeyValueList **kv_list, const char *key, const char *value);
extern const char* get_value_from_kv_list(KeyValueList *kv_list, const char *key);

#endif