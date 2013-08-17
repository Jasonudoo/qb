<?php

class QBArrayRandomHandler extends QBHandler {

	public function getInputOperandCount() {
		return 2;
	}

	public function getOperandType($i) {
		switch($i) {
			case 1: return $this->operandType;
			case 2: return "U32";
			case 3: return "U32";
		}
	}
	
	public function getOperandAddressMode($i) {
		switch($i) {
			case 1: return "ARR";
			case 2: return $this->addressMode;
			case 3: return "ARR";
		}
	}
		
	public function getResultSizePossibilities() {
		return "op2";
	}

	public function getActionOnUnitData() {
		$lines = array();
		$lines[] = "uint32_t num_key = 0, num_req = op2, num_avail = op1_count;";
		$lines[] = "while(num_req > 0) {";
		$lines[] = 		"double randval = php_rand(TSRMLS_C);";
		$lines[] = 		"if ((double) (randval / (PHP_RAND_MAX + 1.0)) < (double) num_req / (double) num_avail) {";
		$lines[] = 			"*res_ptr = num_key;";
		$lines[] = 			"res_ptr++;";
		$lines[] = 			"num_req--;";
		$lines[] = 		"}";
		$lines[] = 		"num_avail--;";
		$lines[] = 		"if(num_key < op1_count - 1) {";
		$lines[] = 			"num_key++;";
		$lines[] = 		"}";
		$lines[] = "}";
		return $lines;
	}
}

?>