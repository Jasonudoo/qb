<?php

class Terminate extends Handler {
	
	use ScalarAddressMode, ExitVM;
	
	public function getInputOperandCount() {
		return 1;
	}
	
	public function needsInterpreterContext() {
		return true;
	}
	
	public function getAction() {
		$lines = array();
		$lines[] = "cxt->exit_status_code = op1;"; 
		$lines[] = "cxt->exit_type = QB_VM_FORK;";
		$lines[] = "return;";
		return $lines;
	}
}

?>