<?php

class QBRSqrtHandler extends QBHandler {

	protected function getScalarExpression() {
		$type = $this->getOperandType($this->srcCount + 1);
		$cType = $this->getOperandCType($this->srcCount + 1);
		$f = ($type == 'F32') ? 'f' : '';
		return "res = rsqrt$f(op1);";
	}
}

?>