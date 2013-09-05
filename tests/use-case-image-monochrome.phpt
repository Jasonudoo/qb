--TEST--
Use case: Monochrome image
--SKIPIF--
<?php 
	if(!function_exists('imagepng')) print 'skip PNG function not available';
?>
--FILE--
<?php

/**
 * A test function
 * 
 * @engine	qb
 * @param	image1	$image
 * 
 * @return	void
 * 
 */
function test_function(&$image) {
}

qb_compile();

$folder = dirname(__FILE__);
$image = imagecreatefrompng("$folder/input/malgorzata_socha.png");
$correct_path = "$folder/output/image-monochrome.correct.png";
$incorrect_path = "$folder/output/image-monochrome.incorrect.png";
test_function($image);

ob_start();
imagesavealpha($image, true);
imagepng($image);
$output_png = ob_get_clean();

/**
 * @engine qb
 *
 * @param image3	$img2;
 * @param image3	$img1;
 * @return float32
 */
function _image_diff($img1, $img2) {
	$img2 -= $img1;
	$img2 *= $img2;
	return sqrt(array_sum($img2));
}

if(file_exists($correct_path)) {
	$correct_md5 = md5_file($correct_path);
	$output_md5 = md5($output_png);
	if($correct_md5 == $output_md5) {
		// exact match
		$match = true;
	} else {
		$correct_output = imagecreatefrompng($correct_path);
		$diff = _image_diff($output, $correct_output);
		// error can be large for this filter
		if($diff < 1) {
			// the output is different ever so slightly
			$match = true;
		} else {
			$match = false;
		}
	}
	if($match) {
		echo "CORRECT\n";
		if(file_exists($incorrect_path)) {
			unlink($incorrect_path);
		}
	} else {
		echo "INCORRECT ($diff)\n";
		file_put_contents($incorrect_path, $output_png);
	}
} else {
	// reference image not yet available--save image and inspect it for correctness by eye
	file_put_contents($correct_path, $output_png);
	echo "CORRECT\n";
}

?>
--EXPECT--
CORRECT
