#include "phpx_serialize.h"
#include "zend_smart_str_public.h"
#include "ext/standard/basic_functions.h"
#include "zend_smart_str.h"
#include "ext/standard/php_incomplete_class.h"
#define FF fflush(stdout)
struct php_serialize_data {
    HashTable ht;
    uint32_t n;
};

struct php_unserialize_data {
    void *first;
    void *last;
    void *first_dtor;
    void *last_dtor;
};

typedef struct php_serialize_data *php_serialize_data_t;
typedef struct php_unserialize_data *php_unserialize_data_t;

ZEND_BEGIN_ARG_INFO_EX(phpx_2byref_arginfo, 
    1 /*pass_rest_by_reference*/, 
    0/*return_reference*/,
    1/*required_num_args*/)
ZEND_ARG_PASS_INFO(1/*by_ref*/)
ZEND_END_ARG_INFO();


#define PHP_VAR_SERIALIZE_INIT(d) \
do  { \
    /* fprintf(stderr, "SERIALIZE_INIT      == lock: %u, level: %u\n", BG(serialize_lock), BG(serialize).level); */ \
    if (BG(serialize_lock) || !BG(serialize).level) { \
        (d) = (php_serialize_data_t) emalloc(sizeof(struct php_serialize_data)); \
        zend_hash_init(&(d)->ht, 16, NULL, ZVAL_PTR_DTOR, 0); \
        (d)->n = 0; \
        if (!BG(serialize_lock)) { \
            BG(serialize).data = d; \
            BG(serialize).level = 1; \
        } \
    } else { \
        (d) = BG(serialize).data; \
        ++BG(serialize).level; \
    } \
} while(0)

#define PHP_VAR_SERIALIZE_DESTROY(d) \
do { \
    /* fprintf(stderr, "SERIALIZE_DESTROY   == lock: %u, level: %u\n", BG(serialize_lock), BG(serialize).level); */ \
    if (BG(serialize_lock) || BG(serialize).level == 1) { \
        zend_hash_destroy(&(d)->ht); \
        efree((d)); \
    } \
    if (!BG(serialize_lock) && !--BG(serialize).level) { \
        BG(serialize).data = NULL; \
    } \
} while (0)

static void phpx_var_serialize_intern(smart_str *buf, zval *struc, php_serialize_data_t var_hash);


static inline zend_long phpx_add_var_hash(php_serialize_data_t data, zval *var) /* {{{ */
{
    zval *zv;
    zend_ulong key;
    zend_bool is_ref = Z_ISREF_P(var);

    data->n += 1;

    if (!is_ref && Z_TYPE_P(var) != IS_OBJECT) {
        return 0;
    }

    /* References to objects are treated as if the reference didn't exist */
    if (is_ref && Z_TYPE_P(Z_REFVAL_P(var)) == IS_OBJECT) {
        var = Z_REFVAL_P(var);
    }

    /* Index for the variable is stored using the numeric value of the pointer to
     * the zend_refcounted struct */
    key = (zend_ulong) (zend_uintptr_t) Z_COUNTED_P(var);
    zv = zend_hash_index_find(&data->ht, key);

    if (zv) {
        /* References are only counted once, undo the data->n increment above */
        if (is_ref) {
            data->n -= 1;
        }

        return Z_LVAL_P(zv);
    } else {
        zval zv_n;
        ZVAL_LONG(&zv_n, data->n);
        zend_hash_index_add_new(&data->ht, key, &zv_n);

        /* Additionally to the index, we also store the variable, to ensure that it is
         * not destroyed during serialization and its pointer reused. The variable is
         * stored at the numeric value of the pointer + 1, which cannot be the location
         * of another zend_refcounted structure. */
        zend_hash_index_add_new(&data->ht, key + 1, var);
        Z_ADDREF_P(var);

        return 0;
    }
}
/* }}} */

static inline void phpx_var_serialize_long(smart_str *buf, zend_long val) /* {{{ */
{
    smart_str_appendl(buf, "i:", 2);
    smart_str_append_long(buf, val);
    smart_str_appendc(buf, ';');
}
/* }}} */

static inline void phpx_var_serialize_string(smart_str *buf, char *str, size_t len) /* {{{ */
{
    smart_str_appendl(buf, "s:", 2);
    smart_str_append_unsigned(buf, len);
    smart_str_appendl(buf, ":\"", 2);
    smart_str_appendl(buf, str, len);
    smart_str_appendl(buf, "\";", 2);
}
/* }}} */

static inline zend_bool phpx_var_serialize_class_name(smart_str *buf, zval *struc) /* {{{ */
{
    PHP_CLASS_ATTRIBUTES;

    PHP_SET_CLASS_ATTRIBUTES(struc);
    smart_str_appendl(buf, "O:", 2);
    smart_str_append_unsigned(buf, ZSTR_LEN(class_name));
    smart_str_appendl(buf, ":\"", 2);
    smart_str_append(buf, class_name);
    smart_str_appendl(buf, "\":", 2);
    PHP_CLEANUP_CLASS_ATTRIBUTES();
    return incomplete_class;
}
/* }}} */

static HashTable *phpx_var_serialize_collect_names(HashTable *src, uint32_t count, zend_bool incomplete) /* {{{ */ {
    zval *val;
    HashTable *ht;
    zend_string *key, *name;

    ALLOC_HASHTABLE(ht);
    zend_hash_init(ht, count, NULL, NULL, 0);
    ZEND_HASH_FOREACH_STR_KEY_VAL(src, key, val) {
        if (incomplete && strcmp(ZSTR_VAL(key), MAGIC_MEMBER) == 0) {
            continue;
        }
        if (Z_TYPE_P(val) != IS_STRING) {
            php_error_docref(NULL, E_NOTICE,
                    "__sleep should return an array only containing the names of instance-variables to serialize.");
        }
        name = zval_get_string(val);
        if (zend_hash_exists(ht, name)) {
            php_error_docref(NULL, E_NOTICE,
                    "\"%s\" is returned from __sleep multiple times", ZSTR_VAL(name));
            zend_string_release(name);
            continue;
        }
        zend_hash_add_empty_element(ht, name);
        zend_string_release(name);
    } ZEND_HASH_FOREACH_END();

    return ht;
}
/* }}} */

static void phpx_var_serialize_class(smart_str *buf, zval *struc, zval *retval_ptr, php_serialize_data_t var_hash) /* {{{ */
{
    uint32_t count;
    zend_bool incomplete_class;
    HashTable *ht;

    incomplete_class = phpx_var_serialize_class_name(buf, struc);
    /* count after serializing name, since php_var_serialize_class_name
     * changes the count if the variable is incomplete class */
    if (Z_TYPE_P(retval_ptr) == IS_ARRAY) {
        ht = Z_ARRVAL_P(retval_ptr);
        count = zend_array_count(ht);
    } else if (Z_TYPE_P(retval_ptr) == IS_OBJECT) {
        ht = Z_OBJPROP_P(retval_ptr);
        count = zend_array_count(ht);
        if (incomplete_class) {
            --count;
        }
    } else {
        count = 0;
        ht = NULL;
    }

    if (count > 0) {
        zval *d;
        zval nval, *nvalp;
        zend_string *name;
        HashTable *names, *propers;

        names = phpx_var_serialize_collect_names(ht, count, incomplete_class);

        smart_str_append_unsigned(buf, zend_hash_num_elements(names));
        smart_str_appendl(buf, ":{", 2);

        ZVAL_NULL(&nval);
        nvalp = &nval;
        propers = Z_OBJPROP_P(struc);

        ZEND_HASH_FOREACH_STR_KEY(names, name) {
            if ((d = zend_hash_find(propers, name)) != NULL) {
                if (Z_TYPE_P(d) == IS_INDIRECT) {
                    d = Z_INDIRECT_P(d);
                    if (Z_TYPE_P(d) == IS_UNDEF) {
                        continue;
                    }
                }
                phpx_var_serialize_string(buf, ZSTR_VAL(name), ZSTR_LEN(name));
                phpx_var_serialize_intern(buf, d, var_hash);
            } else {
                zend_class_entry *ce = Z_OBJ_P(struc)->ce;
                if (ce) {
                    zend_string *prot_name, *priv_name;

                    do {
                        priv_name = zend_mangle_property_name(
                                ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), ZSTR_VAL(name), ZSTR_LEN(name), ce->type & ZEND_INTERNAL_CLASS);
                        if ((d = zend_hash_find(propers, priv_name)) != NULL) {
                            if (Z_TYPE_P(d) == IS_INDIRECT) {
                                d = Z_INDIRECT_P(d);
                                if (Z_ISUNDEF_P(d)) {
                                    break;
                                }
                            }
                            phpx_var_serialize_string(buf, ZSTR_VAL(priv_name), ZSTR_LEN(priv_name));
                            zend_string_free(priv_name);
                            phpx_var_serialize_intern(buf, d, var_hash);
                            break;
                        }
                        zend_string_free(priv_name);
                        prot_name = zend_mangle_property_name(
                                "*", 1, ZSTR_VAL(name), ZSTR_LEN(name), ce->type & ZEND_INTERNAL_CLASS);
                        if ((d = zend_hash_find(propers, prot_name)) != NULL) {
                            if (Z_TYPE_P(d) == IS_INDIRECT) {
                                d = Z_INDIRECT_P(d);
                                if (Z_TYPE_P(d) == IS_UNDEF) {
                                    zend_string_free(prot_name);
                                    break;
                                }
                            }
                            phpx_var_serialize_string(buf, ZSTR_VAL(prot_name), ZSTR_LEN(prot_name));
                            zend_string_free(prot_name);
                            phpx_var_serialize_intern(buf, d, var_hash);
                            break;
                        }
                        zend_string_free(prot_name);
                        phpx_var_serialize_string(buf, ZSTR_VAL(name), ZSTR_LEN(name));
                        phpx_var_serialize_intern(buf, nvalp, var_hash);
                        php_error_docref(NULL, E_NOTICE,
                                "\"%s\" returned as member variable from __sleep() but does not exist", ZSTR_VAL(name));
                    } while (0);
                } else {
                    phpx_var_serialize_string(buf, ZSTR_VAL(name), ZSTR_LEN(name));
                    phpx_var_serialize_intern(buf, nvalp, var_hash);
                }
            }
        } ZEND_HASH_FOREACH_END();
        smart_str_appendc(buf, '}');

        zend_hash_destroy(names);
        FREE_HASHTABLE(names);
    } else {
        smart_str_appendl(buf, "0:{}", 4);
    }
}
static void phpx_var_serialize_intern(smart_str *buf,
     zval *struc, php_serialize_data_t var_hash) 
{
    zend_long var_already;
    HashTable *myht;

    if (EG(exception)) {
        return;
    }

    if (var_hash && (var_already = phpx_add_var_hash(var_hash, struc))) {
        if (Z_ISREF_P(struc)) {
            smart_str_appendl(buf, "R:", 2);
            smart_str_append_long(buf, var_already);
            smart_str_appendc(buf, ';');
            return;
        } else if (Z_TYPE_P(struc) == IS_OBJECT) {
            smart_str_appendl(buf, "r:", 2);
            smart_str_append_long(buf, var_already);
            smart_str_appendc(buf, ';');
            return;
        }
    }

again:
    switch (Z_TYPE_P(struc)) {
        case IS_FALSE:
            smart_str_appendl(buf, "b:0;", 4);
            return;

        case IS_TRUE:
            smart_str_appendl(buf, "b:1;", 4);
            return;

        case IS_NULL:
            smart_str_appendl(buf, "N;", 2);
            return;

        case IS_LONG:
            phpx_var_serialize_long(buf, Z_LVAL_P(struc));
            return;

        case IS_DOUBLE: {
                char *s;

                smart_str_appendl(buf, "d:", 2);
                s = (char *) safe_emalloc(PG(serialize_precision), 1, MAX_LENGTH_OF_DOUBLE + 1);
                php_gcvt(Z_DVAL_P(struc), (int)PG(serialize_precision), '.', 'E', s);
                smart_str_appends(buf, s);
                smart_str_appendc(buf, ';');
                efree(s);
                return;
            }

        case IS_STRING:
            phpx_var_serialize_string(buf, Z_STRVAL_P(struc), Z_STRLEN_P(struc));
            return;

        case IS_OBJECT: {
                zval retval;
                zval fname;
                int res;
                zend_class_entry *ce = Z_OBJCE_P(struc);

                if (ce->serialize != NULL) {
                    /* has custom handler */
                    unsigned char *serialized_data = NULL;
                    size_t serialized_length;

                    if (ce->serialize(struc, &serialized_data, &serialized_length, (zend_serialize_data *)var_hash) == SUCCESS) {
                        smart_str_appendl(buf, "C:", 2);
                        smart_str_append_unsigned(buf, ZSTR_LEN(Z_OBJCE_P(struc)->name));
                        smart_str_appendl(buf, ":\"", 2);
                        smart_str_append(buf, Z_OBJCE_P(struc)->name);
                        smart_str_appendl(buf, "\":", 2);

                        smart_str_append_unsigned(buf, serialized_length);
                        smart_str_appendl(buf, ":{", 2);
                        smart_str_appendl(buf, (char *) serialized_data, serialized_length);
                        smart_str_appendc(buf, '}');
                    } else {
                        smart_str_appendl(buf, "N;", 2);
                    }
                    if (serialized_data) {
                        efree(serialized_data);
                    }
                    return;
                }

                if (ce != PHP_IC_ENTRY && zend_hash_str_exists(&ce->function_table, "__sleep", sizeof("__sleep")-1)) {
                    ZVAL_STRINGL(&fname, "__sleep", sizeof("__sleep") - 1);
                    BG(serialize_lock)++;
                    res = call_user_function_ex(CG(function_table), struc, &fname, &retval, 0, 0, 1, NULL);
                    BG(serialize_lock)--;
                    zval_dtor(&fname);

                    if (EG(exception)) {
                        zval_ptr_dtor(&retval);
                        return;
                    }

                    if (res == SUCCESS) {
                        if (Z_TYPE(retval) != IS_UNDEF) {
                            if (HASH_OF(&retval)) {
                                phpx_var_serialize_class(buf, struc, &retval, var_hash);
                            } else {
                                php_error_docref(NULL, E_NOTICE, "__sleep should return an array only containing the names of instance-variables to serialize");
                                /* we should still add element even if it's not OK,
                                 * since we already wrote the length of the array before */
                                smart_str_appendl(buf,"N;", 2);
                            }
                            zval_ptr_dtor(&retval);
                        }
                        return;
                    }
                    zval_ptr_dtor(&retval);
                }

                /* fall-through */
            }
        case IS_ARRAY: {
            uint32_t i;
            zend_bool incomplete_class = 0;
            if (Z_TYPE_P(struc) == IS_ARRAY) {
                smart_str_appendl(buf, "a:", 2);
                myht = Z_ARRVAL_P(struc);
                i = zend_array_count(myht);
            } else {
                incomplete_class = phpx_var_serialize_class_name(buf, struc);
                myht = Z_OBJPROP_P(struc);
                /* count after serializing name, since php_var_serialize_class_name
                 * changes the count if the variable is incomplete class */
                i = zend_array_count(myht);
                if (i > 0 && incomplete_class) {
                    --i;
                }
            }
            smart_str_append_unsigned(buf, i);
            smart_str_appendl(buf, ":{", 2);
            if (i > 0) {
                zend_string *key;
                zval *data;
                zend_ulong index;

                ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, data) {

                    if (incomplete_class && strcmp(ZSTR_VAL(key), MAGIC_MEMBER) == 0) {
                        continue;
                    }

                    if (!key) {
                        phpx_var_serialize_long(buf, index);
                    } else {
                        phpx_var_serialize_string(buf, ZSTR_VAL(key), ZSTR_LEN(key));
                    }

                    /* we should still add element even if it's not OK,
                     * since we already wrote the length of the array before */
                    if ((Z_TYPE_P(data) == IS_ARRAY && Z_TYPE_P(struc) == IS_ARRAY && Z_ARR_P(data) == Z_ARR_P(struc))
                        || (Z_TYPE_P(data) == IS_ARRAY && Z_ARRVAL_P(data)->u.v.nApplyCount > 1)
                    ) {
                        smart_str_appendl(buf, "N;", 2);
                    } else {
                        if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data))) {
                            Z_ARRVAL_P(data)->u.v.nApplyCount++;
                        }
                        phpx_var_serialize_intern(buf, data, var_hash);
                        if (Z_TYPE_P(data) == IS_ARRAY && ZEND_HASH_APPLY_PROTECTION(Z_ARRVAL_P(data))) {
                            Z_ARRVAL_P(data)->u.v.nApplyCount--;
                        }
                    }
                } ZEND_HASH_FOREACH_END();
            }
            smart_str_appendc(buf, '}');
            return;
        }
        case IS_REFERENCE:
            struc = Z_REFVAL_P(struc);
            goto again;
        default:
            // smart_str_appendl(buf, "i:0;", 4);
            smart_str_appendl(buf, "X:", 2);
            // smart_str_append_long(buf,Z_RES_P(struc)->handle);
            // smart_str_append_long(buf, var_already);
            smart_str_append_long(buf, var_hash->n);
            smart_str_appendc(buf, ';');
            return;
    }
}

static void phpx_var_serialize(smart_str *buf, zval *struc, 
    php_serialize_data_t *data) 
{
    phpx_var_serialize_intern(buf, struc, *data);
    smart_str_0(buf);
}

PHP_FUNCTION(xserialize)
{
    zval *struc;
    zval *out;
    php_serialize_data_t var_hash;
    smart_str buf = {0};

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &struc) == FAILURE) {
        return;
    }
    // zend_hash_init(&var_hash->ht, 16, NULL, ZVAL_PTR_DTOR, 0);
    // var_hash->n=0;
    PHP_VAR_SERIALIZE_INIT(var_hash);
    phpx_var_serialize(&buf, struc, &var_hash);
    PHP_VAR_SERIALIZE_DESTROY(var_hash);
    // zend_hash_destroy(&var_hash->ht);
    // efree(var_hash); 
    if (EG(exception)) {
        smart_str_free(&buf);
        RETURN_FALSE;
    }

    if (buf.s) {
        RETURN_NEW_STR(buf.s);
    } else {
        RETURN_NULL();
    }
}
zend_long deep_copy_add_to_pool(zval *var,HashTable *pool,zend_ulong *id)
{
    zval *zv;
    zend_ulong key;
    zend_bool is_ref = Z_ISREF_P(var);

    /* References to objects are treated as if the reference didn't exist */
    if (is_ref && Z_TYPE_P(Z_REFVAL_P(var)) == IS_OBJECT) {
        var = Z_REFVAL_P(var);
    }

    /* Index for the variable is stored using the numeric value of the pointer to
     * the zend_refcounted struct */
    key = (zend_ulong) (zend_uintptr_t) Z_COUNTED_P(var);
    zv = zend_hash_index_find(pool, key);
    *id=key;
    if (zv) 
        return true;// Z_LVAL(key);
    else 
    {
        /* Additionally to the index, we also store the variable, to ensure that it is
         * not destroyed during serialization and its pointer reused. The variable is
         * stored at the numeric value of the pointer + 1, which cannot be the location
         * of another zend_refcounted structure. */
        zend_hash_index_add_new(pool, key, var);
        Z_ADDREF_P(var);

        return false;
    }
}
zend_ulong zval_id(zval *var)
{
    return (zend_ulong) (zend_uintptr_t) Z_COUNTED_P(var);
}

zval * deep_copy_intern(zval *var,HashTable *pool,int depth)
{
    printf("Depth %d, Size %d, Type %d\n",depth,zend_hash_num_elements(pool),zval_get_type(var));
    fflush(stdout);
    if (depth>16)
        return 0;
    zend_ulong id=zval_id(var);
    printf("\tGoing to check pool for this... ");
    fflush(stdout);
    zval *copy;
    if ((copy=zend_hash_index_find(pool,id)))
    {
        printf("\tAlready available with id (%d) and type (%d), returning.\n",id,zval_get_type(copy));   
        fflush(stdout);
        if (Z_ISREF_P(copy))
            Z_ADDREF_P(copy);
        return copy;
    }
    printf("\tid:%d not in the pool.\n",id);
    fflush(stdout);
    if (Z_TYPE_P(var)==IS_ARRAY)
    {
        printf("\tArray, iterating. ");
        fflush(stdout);
        zval *arr=emalloc(sizeof(zval));
        array_init(arr); //this is the culprit
        HashTable *myht = Z_ARRVAL_P(var);
        int i=zend_array_count(myht);
        printf("size: %d\n",i);
        fflush(stdout);
        if (i > 0) 
        {
            zend_string *key;
            zval *data;
            zend_ulong index;
            // zend_hash_index_add_new(pool,id,Z_REFVAL_P(arr)); //assign reference to pool
            zend_hash_index_add_new(pool,id,arr); //assign reference to pool
            Z_ADDREF_P(arr);
            //FIXME:i think z_refval_p is not correct above
            ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, data) 
            {
                printf("\tRecursing next array element, ");
                if (key)
                    printf("key:%s\n",key);
                else
                    printf("index:%d\n",index);
                fflush(stdout);
                zval *tmp=deep_copy_intern(data,pool,depth+1);
            // printf("\n=sofar so good=\n\n");fflush(stdout);
                // if (!Z_ISREF_P(data)) //copy if not isref
                // {
                //     printf("\tCopying, because array element was not byref...\n");
                //     fflush(stdout);
                //     zval *tmp2=tmp;
                //     tmp=emalloc(sizeof(zval));
                //     // ZVAL_ARR(tmp,tmp2);
                //     ZVAL_COPY_VALUE(tmp,tmp2);
                // }
                if (!key)
                    // zend_hash_index_add_new(arr,index,tmp);
                    add_index_zval(arr,index,tmp);
                else
                    add_assoc_zval(arr,key->val,tmp);
                    // zend_hash_key_add_new(arr,key,strlen(key),tmp);

            } ZEND_HASH_FOREACH_END();        
        }
        return arr; 

    }
    else if (Z_TYPE_P(var)==IS_OBJECT)
    {
        printf("\tObject. Not yet supported.\n");
    }
    else
    {
        if (Z_TYPE_P(var)==IS_RESOURCE)
            printf("WARNING: resource copy.\n");
        printf("\tNormal zval");
        fflush(stdout);
    
        printf(", first encounter, copying and adding to pool.\n");
        fflush(stdout);
        zval *tmp_var=emalloc(sizeof(zval));
        int isref=Z_ISREF_P(var);
        if (isref)
            var=Z_REFVAL_P(var); //inherent deref
        ZVAL_COPY_VALUE(tmp_var, var);
        if (isref)
            ZVAL_NEW_REF(tmp_var,tmp_var); //make it a reference to the old tmp_var
        zend_hash_index_add_new(pool,id,tmp_var); //refer to the copy
        return tmp_var;
    } 
    return 0;

}
PHP_FUNCTION(deep_copy)
{

    zval *var;
    zval *out;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z", &var) == FAILURE) {
        return;
    }
    HashTable ht;
    zend_hash_init(&ht, 16, NULL, ZVAL_PTR_DTOR, 0);
    // if (Z_ISREF_P(var))
        // var=Z_REFVAL_P(var);
    ///unwrap one level of referencing, because reference is sent to this function
    out=deep_copy_intern(Z_REFVAL_P(var),&ht,0);

    // zval *params = { struc };
    // zend_ulong param_count = 1;
    // zval *retval_ptr;

    // zval function_name;
    // ZVAL_STRING(&function_name, "var_dump");

    // if (call_user_function(
    //     CG(function_table), NULL /* no object */, &function_name,
    //     retval_ptr, param_count, params TSRMLS_CC)
    //     ==SUCCESS)
    //     ;
    // else
    //     printf("Fail!");
    RETURN_ZVAL(out,1,0 );
    return_value=out;
}
