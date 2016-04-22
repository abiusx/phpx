#include "phpx.h"
//ZEND_BEGIN_ARG_INFO(phpx_byref, pass_rest_by_reference)
ZEND_BEGIN_ARG_INFO_EX(phpx_byref_arginfo, 
    1 /*pass_rest_by_reference*/, 
    0/*return_reference*/,
    1/*required_num_args*/)
ZEND_ARG_PASS_INFO(1/*by_ref*/)
ZEND_END_ARG_INFO()

PHP_FUNCTION(zval_id)
{
    //computes the address of first zval sent to it, 
    //and the remainder are ided after it.
    zval *z;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&z)==FAILURE)
        return;
    // printf("%u\n",z->value);
    // printf("%u\n",Z_VAL_P(z));
     long id=*(( long*)(&z->value)); 
    ///return_value will become the address

    static long base=0;
    if (base==0) base=id-1; 
    ///use any constant php variable above (e.g z), just to make addresses simpler
    id=(id^base)>>3; //zval is at least 16 bytes
    // printf ("%u\n",id);
    RETURN_LONG(id);
}
PHP_FUNCTION(is_ref)
{
    zval *z;
    int res;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&z)==FAILURE)
        return;
    // int x=Z_INTVAL_P(z);

    // RETURN_BOOL(Z_ISREF_P(z));
    RETURN_BOOL(Z_REFCOUNT_P(z) > 2); 
    //1 is the reference to this function, the other is the actual var. 
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
