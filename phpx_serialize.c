#include "phpx_serialize.h"
#include "zend_smart_str_public.h"
#include "ext/standard/basic_functions.h"
#include "zend_smart_str.h"
#include "ext/standard/php_incomplete_class.h"

zend_ulong zval_id(zval *var)
{
    return (zend_ulong) 
    (zval_get_type(var)*10000000000000)  //type
    +(zend_uintptr_t) Z_COUNTED_P(var);
}
#define PHPX_DEBUG
zval * deep_copy_intern(zval *var,HashTable *pool,int depth)
{
    #ifdef PHPX_DEBUG
    printf("Depth %d, Size %d, Type %d\n",depth,zend_hash_num_elements(pool),zval_get_type(var));
    fflush(stdout);
    #endif
    // if (depth>16)
    //     return 0;
    zend_ulong id=zval_id(var);
    #ifdef PHPX_DEBUG
    printf("\tGoing to check pool for this... ");
    fflush(stdout);
    #endif
    zval *copy;
    if ((copy=zend_hash_index_find(pool,id)))
    { 
        #ifdef PHPX_DEBUG
        printf("\tAlready available with id (%llu) and type (%d), returning.\n",id,zval_get_type(copy));   
        fflush(stdout);
        #endif
        if (Z_ISREF_P(copy))
            Z_ADDREF_P(copy);
        return copy;
    }
    #ifdef PHPX_DEBUG
    printf("\tid:%llu not in the pool.\n",id);
    fflush(stdout);
    #endif
    if (Z_TYPE_P(var)==IS_ARRAY)
    {
        #ifdef PHPX_DEBUG
        printf("\tArray, iterating. ");
        fflush(stdout);
        #endif
        zval *arr=emalloc(sizeof(zval));
        array_init(arr); //this is the culprit
        HashTable *myht = Z_ARRVAL_P(var);
        int i=zend_array_count(myht);
        #ifdef PHPX_DEBUG
        printf("size: %d\n",i);
        fflush(stdout);
        #endif
        if (i > 0) 
        {
            zend_string *key;
            zval *data;
            zend_ulong index;
            zend_hash_index_add_new(pool,id,arr); //assign reference to pool
            Z_ADDREF_P(arr);
            //FIXME:i think z_refval_p is not correct above
            ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, data) 
            {
                #ifdef PHPX_DEBUG
                printf("\tRecursing next array element, ");
                if (key)
                    printf("key:%s\n",key->val);
                else
                    printf("index:%llu\n",index);
                fflush(stdout);
                #endif
                zval *tmp=deep_copy_intern(data,pool,depth+1);
                if (!key)
                    add_index_zval(arr,index,tmp);
                else
                    add_assoc_zval(arr,key->val,tmp);

            } ZEND_HASH_FOREACH_END();        
        }
        return arr; 

    }
    else if (Z_TYPE_P(var)==IS_OBJECT)
    {
        php_error_docref("phpx",E_ERROR,"Attempting to deep copy an object.");
    }
    else
    {
        if (Z_TYPE_P(var)==IS_RESOURCE)
            php_error_docref("phpx",E_WARNING,"Attempting to deep copy a resource.");
            // printf("WARNING: resource copy.\n");
        
        #ifdef PHPX_DEBUG
        printf("\tNormal zval");
        fflush(stdout);
        printf(", first encounter, copying and adding to pool.\n");
        fflush(stdout);
        #endif
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
    ///unwrap one level of referencing, because reference is sent to this function
    out=deep_copy_intern(Z_REFVAL_P(var),&ht,0);

    RETURN_ZVAL(out,1,0 );
    return_value=out;
}
