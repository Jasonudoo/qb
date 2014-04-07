<?php

class Interrupt extends Handler {

	use NoOperands, AlwaysReturn;
	
	public function getHandlerFunctionType() {
		return null;
	}
	
	public function getAction() {
		$lines = array();
		$lines[] = "cxt->instruction_pointer = ip;";
		$lines[] = "cxt->exit_type = QB_VM_YIELD;";
		$lines[] = "return;";
		return $lines;
	}
}

?>
