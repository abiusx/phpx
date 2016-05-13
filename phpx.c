#include "phpx.h"
//ZEND_BEGIN_ARG_INFO(phpx_byref, pass_rest_by_reference)
ZEND_BEGIN_ARG_INFO_EX(phpx_byref_arginfo, 
    1 /*pass_rest_by_reference*/, 
    0/*return_reference*/,
    1/*required_num_args*/)
ZEND_ARG_PASS_INFO(1/*by_ref*/)
ZEND_END_ARG_INFO();
#include "phpx_serialize.h"

PHP_FUNCTION(zval_id)
{
    //BUG: produces same ids for some values
    //computes the address of first zval sent to us, 
    //and the rest receive id relative to that.
    zval *z;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&z)==FAILURE)
        return;
    zend_bool is_ref = Z_ISREF_P(z);
    // if (is_ref)
    //     z=Z_REFVAL_P(z);
    long id=*(( long*)(&z->value)); //return_value will become the address
    // long id=(zend_uintptr_t) Z_COUNTED_P(z);; //same as above
    // printf("===%u\n",id);
    static long base=0;
    if (base==0) base=id-1; 
    id=(id^base)>>3; //zval is at least 16 bytes
    RETURN_LONG(id);
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
    PHP_FE(xserialize,              phpx_byref_arginfo)
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
