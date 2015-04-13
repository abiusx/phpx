#include "php_sample.h"
#include "zend_extensions.h"

//TODO: code coverage from xdebug
//TODO: hook classes, like PDO

PHP_FUNCTION(substring_distance)
{
    char *str1,*str2;
    long strlen1,strlen2;
    long limit;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"ssl",&str1,&strlen1,&str2,&strlen2,&limit)
        ==FAILURE)
        RETURN_NULL();
    PHPWRITE(str1,strlen1);
    PHPWRITE(str2,strlen2);
    RETURN_LONG(limit);
}

PHP_FUNCTION(sample_hello_world)
{
    long range;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"l",&range)==FAILURE)
        RETURN_NULL();

    long sum=0;
    for (long i=0;i<range;++i)
        sum+=i%3;
    RETURN_LONG(sum);
} 
PHP_FUNCTION(sample_long)
{
    ZVAL_LONG(return_value,400);
}

PHP_FUNCTION(sample_array_range)
{
    if (return_value_used) {
        int i;
        /* Return an array from 0 - 999 */
        array_init(return_value);
        for(i = 0; i < 1000; i++) {
            add_next_index_long(return_value, i);
        }
        return;
    } else {
        /* Save yourself the effort */
        php_error_docref(NULL TSRMLS_CC, E_NOTICE,
               "Static return-only function called without processing output");
        RETURN_NULL();
    }
}
PHP_FUNCTION(is_associative)
{

    zval *arrht;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"a",&arrht)==FAILURE)
        RETURN_NULL();
    for(zend_hash_internal_pointer_reset(Z_ARRVAL_P(arrht));
        zend_hash_has_more_elements(Z_ARRVAL_P(arrht)) == SUCCESS;
        zend_hash_move_forward(Z_ARRVAL_P(arrht))) 
    {
        char *key;
        uint keylen;
        ulong idx;
        if (zend_hash_get_current_key_type(Z_ARRVAL_P(arrht))
            !=HASH_KEY_IS_STRING)
            RETURN_BOOL(0);
    }
    RETURN_BOOL(1);

}
PHP_MINIT_FUNCTION(sample)
{
    ALLOC_HASHTABLE(FUNCTION_BACKUPS);
    zend_hash_init(FUNCTION_BACKUPS,256,0,0,1);
    ALLOC_HASHTABLE(FUNCTION_HOOKS);
    zend_hash_init(FUNCTION_HOOKS,256,0,0,1);
}
PHP_FUNCTION(hook_callback)
{
    //get the called function name
    zend_execute_data *ptr=EG(current_execute_data);
    const char * function_name=ptr->function_state.function->common.function_name;
    // php_printf("fname: %s\n",function_name);
    char *hooked_func=function_name;
    

    zval *user_function;
    if (zend_hash_find(FUNCTION_HOOKS,hooked_func,strlen(hooked_func)+1,&user_function)==FAILURE)
    {
        php_printf("No callback assigned to '%s'.\n",hooked_func);
        RETURN_FALSE;
    }
    // php_printf("Calling '%s' instead of '%s'\n",Z_STRVAL(*user_function),hooked_func);
    
    //obtain parameters to forward
    int argc = ZEND_NUM_ARGS();
    zval ***argv = safe_emalloc(sizeof(zval**), argc, 0);
    if (argc == 0 ||
        zend_get_parameters_array_ex(argc, argv) == FAILURE) {
        efree(argv);
        WRONG_PARAM_COUNT;
    }
    // php_printf("arg count: %d\n",argc);
    if (call_user_function(EG(function_table),0,user_function,return_value,argc,*argv TSRMLS_CC)==SUCCESS)
    {
        efree(argv);
    }
    else
    {
        efree(argv);
        RETURN_FALSE;
    }
}
PHP_FUNCTION(unhook)
{
    char *core_function,*user_function;
    long len;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&core_function,&len)==FAILURE)
        RETURN_NULL();
    zend_function *orig;
    if (zend_hash_find(EG(function_table), core_function, len+1, (void **)&orig)==FAILURE) 
        php_error(1,"The function '%s' does not exist in PHP.\n",core_function);//function not found

    // void (*handler)(INTERNAL_FUNCTION_PARAMETERS)=safe_emalloc(sizeof(void *),1,0);
    void **handler=malloc(sizeof(void*));
    if (zend_hash_find(FUNCTION_BACKUPS,core_function,len+1,&handler)==FAILURE)
        RETURN_FALSE; //not hooked
    // php_printf("unhook:%p\n",*handler);
    orig->internal_function.handler= *handler;
    zend_hash_del(FUNCTION_BACKUPS,core_function,len+1);
    zend_hash_del(FUNCTION_HOOKS, core_function,len+1);

    RETURN_TRUE;
}
PHP_FUNCTION(hook)
{
    char *core_function,*user_function;
    long len1,len2;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"ss",&core_function,&len1,&user_function,&len2)==FAILURE)
        RETURN_NULL();
    zend_function *orig,*temp;
    if (zend_hash_find(EG(function_table), core_function, len1+1, (void **)&orig)==FAILURE) 
        php_error(1,"The function '%s' does not exist in PHP to override.\n",core_function);
        // RETURN_FALSE; //function not found
    if (zend_hash_exists(FUNCTION_BACKUPS,core_function,len1+1))
        RETURN_FALSE; //already hooked this one.
    zval uf;
    INIT_ZVAL(uf);
    ZVAL_STRING(&uf,user_function,1);
    zend_hash_update(FUNCTION_HOOKS,core_function,len1+1,&uf,sizeof(uf),0);
    
    void (*handler)(INTERNAL_FUNCTION_PARAMETERS);
    handler=orig->internal_function.handler;
    zend_hash_update(FUNCTION_BACKUPS,core_function,len1+1,&handler,sizeof(handler),0);
    orig->internal_function.handler = ZEND_FN(hook_callback)  ;
    
    // php_printf("hooked function name: %s\n",orig->internal_function.function_name);
    RETURN_TRUE;


}
PHP_RINIT_FUNCTION(sample)
{
    return SUCCESS;
}
PHP_RSHUTDOWN_FUNCTION(sample)
{
    return SUCCESS;
}
//functions added to userspace
static zend_function_entry php_sample_functions[] = {
    PHP_FE(substring_distance,  NULL)
    PHP_FE(is_associative, NULL)
    PHP_FE(hook,              NULL)
    PHP_FE(unhook,              NULL)

    { NULL,NULL,NULL}
};



//module entry points
zend_module_entry sample_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
     STANDARD_MODULE_HEADER,
#endif
    PHP_SAMPLE_EXTNAME,
    php_sample_functions, /* Functions */
    PHP_MINIT(sample), /* MINIT */
    NULL, /* MSHUTDOWN */
    PHP_RINIT(sample), /* RINIT */
    PHP_RSHUTDOWN(sample), /* RSHUTDOWN */
    NULL, /* MINFO */
#if ZEND_MODULE_API_NO >= 20010901
    PHP_SAMPLE_EXTVER,
#endif
       // NO_MODULE_GLOBALS,
    // ZEND_MODULE_POST_ZEND_DEACTIVATE_N(sample),

    STANDARD_MODULE_PROPERTIES_EX
};

#ifdef COMPILE_DL_SAMPLE
ZEND_GET_MODULE(sample)
#endif

ZEND_DLEXPORT void sample_statement_call(zend_op_array *op_array)
{
    return;
}
ZEND_DLEXPORT int sample_zend_startup(zend_extension *extension)
{


    return zend_startup_module(&sample_module_entry);
}

ZEND_DLEXPORT void sample_zend_shutdown(zend_extension *extension)
{
}

ZEND_DLEXPORT void sample_init_oparray(zend_op_array *op_array)
{
    // TSRMLS_FETCH();
}

#ifndef ZEND_EXT_API
#define ZEND_EXT_API    ZEND_DLEXPORT
#endif
ZEND_EXTENSION();
#define XDEBUG_NAME       "Sample"
#define XDEBUG_VERSION    "2.3.2"
#define XDEBUG_AUTHOR     "Derick Rethans"
#define XDEBUG_COPYRIGHT  "Copyright (c) 2002-2015 by Derick Rethans"
#define XDEBUG_COPYRIGHT_SHORT "Copyright (c) 2002-2015"
#define XDEBUG_URL        "http://sample.org"
#define XDEBUG_URL_FAQ    "http://sample.org/docs/faq#api"

ZEND_DLEXPORT zend_extension zend_extension_entry = {
    XDEBUG_NAME,
    XDEBUG_VERSION,
    XDEBUG_AUTHOR,
    XDEBUG_URL_FAQ,
    XDEBUG_COPYRIGHT_SHORT,
    sample_zend_startup,
    sample_zend_shutdown,
    NULL,           /* activate_func_t */
    NULL,           /* deactivate_func_t */
    NULL,           /* message_handler_func_t */
    NULL,           /* op_array_handler_func_t */
    sample_statement_call, /* statement_handler_func_t */
    NULL,           /* fcall_begin_handler_func_t */
    NULL,           /* fcall_end_handler_func_t */
    sample_init_oparray,   /* op_array_ctor_func_t */
    NULL,           /* op_array_dtor_func_t */
    STANDARD_ZEND_EXTENSION_PROPERTIES
};

