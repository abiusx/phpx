#include "phpx.h"
//ZEND_BEGIN_ARG_INFO(phpx_byref, pass_rest_by_reference)
ZEND_BEGIN_ARG_INFO_EX(phpx_byref_arginfo, 
    1 /*pass_rest_by_reference*/, 
    0/*return_reference*/,
    1/*required_num_args*/)
ZEND_ARG_PASS_INFO(1/*by_ref*/)
ZEND_END_ARG_INFO();

zend_ulong zval_id(zval *var)
{
    return (zend_ulong) 
    (zval_get_type(var)*10000000000000)  //type
    +(zend_uintptr_t) Z_COUNTED_P(var);
}
PHP_FUNCTION(zval_id)
{
    zval *z;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&z)==FAILURE)
        return;
    RETURN_LONG(zval_id(z));
}
PHP_FUNCTION(is_ref)  
{
    zval *z;
    int res;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&z)==FAILURE)
        return;
    RETURN_BOOL(Z_REFCOUNT_P(z) > 2); 
    //1 is the reference sent to this function, the other is the actual var. 
    //if more, reference!
}
static int isCloneable(zval *obj)
{
    // zend_object *zobj=obj->value.obj;
    // if (zobj->ce->clone) {
    if (Z_OBJCE_P(obj)->clone) {
        // return zobj->ce->clone->common.fn_flags & ZEND_ACC_PUBLIC;
        return Z_OBJCE_P(obj)->clone->common.fn_flags & ZEND_ACC_PUBLIC;
    } else {
        return Z_OBJ_HANDLER_P(obj, clone_obj) != NULL;
    }
}

#define PHPX_DEBUG 
#undef PHPX_DEBUG
zval * deep_copy_intern(zval *var,HashTable *pool,HashTable *object_pool,int depth)
{
    //TODO: don't really need object_pool, apparently all same objects are same zval (and have the same id)
    #ifdef PHPX_DEBUG
    for (int i=0;i<depth;++i) printf("\t");
    printf("Depth %d, Size %d, Type %d\n",depth,zend_hash_num_elements(pool),zval_get_type(var));
    fflush(stdout);
    #endif
    // if (depth>16)
    //     return 0;
    zend_ulong id=zval_id(var);
    #ifdef PHPX_DEBUG
    for (int i=0;i<depth;++i) printf("\t");
    printf("\tGoing to check pool for id %llu... ",id);
    fflush(stdout);
    #endif
    zval *copy;
    if ((copy=zend_hash_index_find(pool,id)))
    { 
        #ifdef PHPX_DEBUG
        for (int i=0;i<depth;++i) printf("\t");
        printf("\tAlready available with id (%llu) and type (%d), returning.\n",id,zval_get_type(copy));   
        fflush(stdout);
        #endif
        if (Z_ISREF_P(copy))
        {

            #ifdef PHPX_DEBUG
            for (int i=0;i<depth;++i) printf("\t");
            printf("\tIt is a reference! Increase count.\n");   
            fflush(stdout);
            #endif
            Z_ADDREF_P(copy);
        }
        return copy;
    }
    #ifdef PHPX_DEBUG
    for (int i=0;i<depth;++i) printf("\t");
    printf("\tid:%llu not in the pool.\n",id);
    fflush(stdout);
    #endif
    if (Z_TYPE_P(var)==IS_ARRAY)
    {
        #ifdef PHPX_DEBUG
        for (int i=0;i<depth;++i) printf("\t");
        printf("\tArray, iterating. ");
        fflush(stdout);
        #endif
        zval *arr=emalloc(sizeof(zval));
        array_init(arr); 
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
            Z_ADDREF_P(arr); //FIXME: is it needed?
            ZEND_HASH_FOREACH_KEY_VAL_IND(myht, index, key, data) 
            {
                #ifdef PHPX_DEBUG
                for (int i=0;i<depth;++i) printf("\t");
                printf("\tRecursing next array element, ");
                if (key)
                    printf("key:%s\n",key->val);
                else
                    printf("index:%llu\n",index);
                fflush(stdout);
                #endif
                zval *tmp=deep_copy_intern(data,pool,object_pool,depth+1);
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
        zval *cloned_obj,*obj=var;
        zend_ulong handle=Z_OBJ_HANDLE_P(obj);
        #ifdef PHPX_DEBUG
        for (int i=0;i<depth;++i) printf("\t");
        printf("\tDeep copying object #%llu: ",handle);
        fflush(stdout);
        #endif
        if ((cloned_obj=zend_hash_index_find(object_pool,handle)))
        {
            #ifdef PHPX_DEBUG
            printf("already cloned, returning from object pool.\n");
            fflush(stdout);
            #endif
        }
        else //new object
        {
            #ifdef PHPX_DEBUG
            printf("first object encounter. Cloning.\n");
            fflush(stdout);
            #endif
            if (!isCloneable(obj))
            {
                php_error_docref("phpx",E_WARNING,"Attempting to deep copy an uncloneable object.");
                cloned_obj=obj;
            }
            else
            {
                // cloned_obj=zend_objects_clone_obj(obj);
                cloned_obj=emalloc(sizeof(zval));
                ZVAL_OBJ(cloned_obj,zend_objects_clone_obj(obj));
            }
            zend_hash_index_add_new(object_pool,handle,cloned_obj);
        }
        zend_hash_index_add_new(pool,id,cloned_obj);
        return cloned_obj;
    }
    else if (Z_TYPE_P(var)==IS_REFERENCE)
    {
        #ifdef PHPX_DEBUG
        for (int i=0;i<depth;++i) printf("\t");
        printf("\tReference of type %d encountered.\n",zval_get_type(Z_REFVAL_P(var)));
        fflush(stdout);
        #endif

        zval *temp;
        temp=deep_copy_intern(Z_REFVAL_P(var),pool,object_pool,depth+1);
        zval *copy=emalloc(sizeof(zval));
        ZVAL_NEW_REF(copy,temp); //make it a reference again

        #ifdef PHPX_DEBUG
        for (int i=0;i<depth;++i) printf("\t");
        printf("\tWrapping back into reference...\n");
        fflush(stdout);
        #endif
        
        zend_hash_index_add_new(pool,id,copy); //refer to the copy
        return copy;
    }
    else
    {
        if (Z_TYPE_P(var)==IS_RESOURCE)
            php_error_docref("phpx",E_WARNING,"Attempting to deep copy a resource.");
        
        #ifdef PHPX_DEBUG
        for (int i=0;i<depth;++i) printf("\t");
        printf("\tNormal zval");
        fflush(stdout);
        printf(", first encounter, copying and adding to pool.\n");
        fflush(stdout);
        #endif
        zval *copy=emalloc(sizeof(zval));
        ZVAL_COPY_VALUE(copy, var);
        zend_hash_index_add_new(pool,id,copy); //refer to the copy
        return copy;
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
    
    HashTable object_pool;
    zend_hash_init(&object_pool, 16, NULL, ZVAL_PTR_DTOR, 0);

    ///unwrap one level of referencing, because reference is sent to this function
    out=deep_copy_intern(Z_REFVAL_P(var),&ht,&object_pool,0);

    RETURN_ZVAL(out,1,0 );
    return_value=out;
}

PHP_MSHUTDOWN_FUNCTION(phpx)
{
        return SUCCESS;

}
PHP_MINIT_FUNCTION(phpx)
{
        return SUCCESS;

}
PHP_RINIT_FUNCTION(phpx)
{
    return SUCCESS;
}
PHP_RSHUTDOWN_FUNCTION(phpx)
{
    return SUCCESS;
}
static zend_function_entry phpx_functions[] = {
    PHP_FE(is_ref,              phpx_byref_arginfo)
    PHP_FE(zval_id,              phpx_byref_arginfo)
    PHP_FE(deep_copy,              phpx_byref_arginfo)

    { NULL,NULL,NULL}
};
               

zend_module_entry phpx_module_entry = {
     STANDARD_MODULE_HEADER,
    PHPX_EXTNAME, //name
    phpx_functions, // Functions 
    // NULL,NULL,NULL,NULL,
    PHP_MINIT(phpx), // MINIT 
    PHP_MSHUTDOWN(phpx), // MSHUTDOWN 
    PHP_RINIT(phpx), // RINIT 
    PHP_RSHUTDOWN(phpx), // RSHUTDOWN 
    NULL, // MINFO 
    PHPX_EXTVER,
    NO_MODULE_GLOBALS,
    NULL,//ZEND_MODULE_POST_ZEND_DEACTIVATE_N(phpx),

    STANDARD_MODULE_PROPERTIES_EX
};
// #ifdef COMPILE_DL_MYLIB
ZEND_GET_MODULE(phpx)
// #endif
