<?php
//requires php 5.4+
function md5_($x)
{
	return "x".md5_original($x);
}
var_dump(function_override("md5","md5_"));
var_dump(function_override("md5","md5_"));
var_dump(md5(123));
var_dump(function_restore("md5"));
var_dump(function_restore("md5"));
var_dump(md5(123));
var_dump(function_exists("md5_original"));
var_dump(function_exists("md5_"));
var_dump(function_remove("md5_"));
var_dump(function_exists("md5_"));
echo PHP_EOL;

class_rename("PDO","PDO_");
class PDO extends PDO_
{
	 function Something()
	{
		echo "did something";
		return true;
	}
}
$x=new PDO("sqlite:memory");
foreach ($x->query("SELECT 1,2,3,4,0") as $data)
	var_dump($data);
$x->Something();


