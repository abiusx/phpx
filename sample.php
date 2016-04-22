<?php
$x=2;
$a=2;
$array=[$a,&$a];
var_dump(zval_id($a));
var_dump(zval_id($a));
var_dump(zval_id($x));
$b=5.5;
$c=5.5;
var_dump(zval_id($array[0]));
var_dump(zval_id($array[1]));
echo PHP_EOL;
var_dump(zval_id($b));
var_dump(zval_id($c));
$d=&$c;
var_dump(zval_id($d));
unset($d);

$x=new stdClass;
$x2=$x;
$x3=&$x;
$x4=&$x3;
var_dump(zval_id($x));
var_dump(zval_id($x2));
var_dump(zval_id($x3));
var_dump(zval_id($x4));


echo str_repeat("-",80),PHP_EOL;
var_dump($array);
var_dump(is_ref($array[0]));
var_dump(is_ref($array[1]));
$b=&$array;
unset($b);
$b=2;
$c=$b;
$d=$b;
$ref=&$b;
var_dump(is_ref($b));
die();
ini_set("memory_limit","-1");
echo floor(memory_get_peak_usage()/1024),"KB",PHP_EOL;
$t=microtime(true);


coverage(1);
$a="123";
$multiline="1234
56789"
;
class something{
	public $x="123";

	private $y=
	"234";
	function __construct($a="123")
	{
		return $a;
	}
}
$x=new something();
$x->x;
$n=0;
for ($i=0;$i<1000000;++$i)
{
	$n=$n+1;
	$n--;
}
coverage(0);



print_r(get_coverage_files());
print_r(count(get_coverage_lines()));
echo PHP_EOL,str_repeat("_",80),PHP_EOL;
echo PHP_EOL;
echo floor(memory_get_peak_usage()/1024),"KB",PHP_EOL;
echo floor(memory_get_usage()/1024),"KB",PHP_EOL;
printf("%.5f seconds.\n",microtime(true)-$t);

die();
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


