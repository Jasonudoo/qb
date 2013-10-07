/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2012 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Chung Leong <cleong@cal.berkeley.edu>                        |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

static void qb_validate_operands_array_element(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *container = &operands[0];
	qb_operand *index = &operands[1];

	if(container->type != QB_OPERAND_ADDRESS) {
		qb_abort("cannot retrieve element from non-array");
	}

	if((index->type == QB_OPERAND_ADDRESS && !SCALAR(index->address)) || (index->type == QB_OPERAND_ARRAY_INITIALIZER)) {
		qb_abort("an array cannot be used as an array index");
	} else if(index->type == QB_OPERAND_ZVAL) {
		if(index->constant->type == IS_STRING) {
			qb_abort("no support for associative array");
		} else if(index->constant->type != IS_BOOL && index->constant->type != IS_LONG && index->constant->type != IS_DOUBLE) {
			qb_abort("invalid index type");
		}
	} else if(index->type == QB_OPERAND_NONE) {
		// an append operation
		if(!RESIZABLE(container->address)) {
			qb_abort("cannot append element to a fixed-length array");
		}
	}
}

static void qb_validate_operands_object_property(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *container = &operands[0];
	qb_operand *name = &operands[1];

	if(name->type != QB_OPERAND_ZVAL) {
		qb_abort("no support for variable variable-names");
	}
	if(container->type == QB_OPERAND_NONE) {
		if(!qb_get_instance_variable(cxt, name->constant)) {
			qb_abort("no property by the name of '%s'", Z_STRVAL_P(name->constant));
		}
	} else if(container->type == QB_OPERAND_ADDRESS) {
		int32_t index;
		if(SCALAR(container->address)) {
			qb_abort("illegal operation: not an array");
		}
		if(!container->address->index_alias_schemes || !container->address->index_alias_schemes[0]) {
			qb_abort("array elements are not named");
		}
		index = qb_find_index_alias(cxt, container->address->index_alias_schemes[0], name->constant);
		if(index == -1) {
			qb_abort("no element by the name of '%s'", Z_STRVAL_P(name->constant));
		}
	} else {
		qb_abort("cannot fetch property of objects other than $this");
	}
}

static void qb_validate_operands_matching_type(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	uint32_t i;
	for(i = 1; i < operand_count; i++) {
		if(operands[0].address->type != operands[i].address->type) {
			qb_abort("type mismatch");
		}
	}
}

static void qb_validate_operands_return(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	if(!cxt->return_variable->address) {
		qb_operand *value = &operands[0];
		if(value->type != QB_OPERAND_NONE && !(value->type == QB_OPERAND_ZVAL && value->constant->type == IS_NULL)) {
			qb_abort("returning a value in a function with return type declared to be void");
		}
	}
}
static void qb_validate_operands_rand(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	if(operand_count != 0 && operand_count != 2) {
		qb_abort("%s() expects either 0 or 2 arguments", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_minmax(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *container = &operands[0];

	if(operand_count == 1) {
		if(SCALAR(container->address)) {
			qb_abort("%s() expects an array as parameter when only one parameter is given", cxt->intrinsic_function->name);
		}
	}
}

static void qb_validate_array_append(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *value = &operands[0], *index = &operands[1];

	if(index->type == QB_OPERAND_ZVAL) {
		switch(Z_TYPE_P(index->constant)) {
			case IS_BOOL:
			case IS_LONG:
			case IS_DOUBLE: {
			}	break;
			case IS_STRING: {
			}	break;
		}
	}
}

static void qb_validate_array_init(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	if(operand_count > 0) {
		qb_validate_array_append(cxt, f, operands, operand_count);
	}
}

static zval * qb_get_special_constant(qb_compiler_context *cxt, const char *name, uint32_t length) {
	static zval type_constants[QB_TYPE_COUNT];
	static zval qb_indicator;

	if(strcmp(name, "__QB__") == 0) {
		Z_TYPE(qb_indicator) = IS_LONG;
		Z_LVAL(qb_indicator) = 1;
		return &qb_indicator;
	} else {
		uint32_t i;
		for(i = 0; i < QB_TYPE_COUNT; i++) {
			const char *type = type_names[i];
			if(strcmp(name, type) == 0) {
				Z_TYPE(type_constants[i]) = IS_LONG;
				Z_LVAL(type_constants[i]) = i;
				return &type_constants[i];
			}
		}
	}
	return NULL;
}

static void qb_validate_operands_fetch_class_self(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	zend_class_entry *ce = cxt->zend_function->common.scope;
	if(!ce) {
		qb_abort("Cannot access self:: when no class scope is active");
	}
}

static void qb_validate_operands_fetch_class_parent(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	zend_class_entry *ce = cxt->zend_function->common.scope;
	if(!ce) {
		qb_abort("Cannot access parent:: when no class scope is active");
	}
	if(!ce->parent) {
		qb_abort("Cannot access parent:: when current class scope has no parent");
	}
}

static void qb_validate_operands_fetch_class_static(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	zend_class_entry *ce = cxt->zend_function->common.scope;
	if(!ce) {
		qb_abort("Cannot access static:: when no class scope is active");
	}
}

static void qb_validate_operands_fetch_constant(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	USE_TSRM
	qb_operand *scope = &operands[0], *name = &operands[1];
	if(scope->type == QB_OPERAND_ZEND_STATIC_CLASS) {
	} else {
		zend_class_entry *ce = NULL;
		if(scope->type == QB_OPERAND_ZEND_CLASS) {
			ce = scope->zend_class;
		} else if(scope->type == QB_OPERAND_ZVAL) {
			ce = zend_fetch_class_by_name(Z_STRVAL_P(scope->constant), Z_STRLEN_P(scope->constant), NULL, 0 TSRMLS_CC);
		}
		if(ce) {
			if(!zend_hash_exists(&ce->constants_table, Z_STRVAL_P(name->constant), Z_STRLEN_P(name->constant) + 1)) {
				qb_abort("Undefined class constant '%s'", Z_STRVAL_P(name->constant));
			}
		} else {
			if(!zend_hash_exists(EG(zend_constants), Z_STRVAL_P(name->constant), Z_STRLEN_P(name->constant) + 1)) {
				if(!qb_get_special_constant(cxt, Z_STRVAL_P(name->constant), Z_STRLEN_P(name->constant))) {
					qb_abort("Undefined constant '%s'", Z_STRVAL_P(name->constant));
				}
			}
		}
	}
}

static void qb_validate_operands_assign_ref(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *variable = &operands[0], *value = &operands[1];
	// global and static assign ref to bring variables into the local scope
	if(variable->address != value->address) {
		qb_abort("No support for reference");
	}
}

static void qb_validate_operands_one_array(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand = &operands[0];

	if(SCALAR(operand->address)) {
		qb_abort("%s() expects an array as parameter", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_referenceable(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand = &operands[0];

	if(operand->type != QB_OPERAND_ADDRESS) {
		qb_abort("%s() can only operate on variables", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_one_array_variable(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand = &operands[0];

	qb_validate_operands_referenceable(cxt, f, operands, operand_count);
	qb_validate_operands_one_array(cxt, f, operands, operand_count);
}

static void qb_validate_operands_two_arrays(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0], *operand2 = &operands[1];

	if(SCALAR(operand1->address)) {
		qb_abort("%s() expects the first parameter to be an array", cxt->intrinsic_function->name);
	}
	if(SCALAR(operand2->address)) {
		qb_abort("%s() expects the second parameter to be an array", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_matching_vector_width(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0], *operand2 = &operands[1];

	qb_validate_operands_two_arrays(cxt, f, operands, operand_count);

	if(CONSTANT_DIMENSION(operand1->address, -1) && CONSTANT_DIMENSION(operand2->address, -1)) {
		uint32_t vector_width1 = DIMENSION(operand1->address, -1);
		uint32_t vector_width2 = DIMENSION(operand2->address, -1);
		if(vector_width1 != vector_width2) {
			qb_abort("%s() expects the dimension of the first parameter to match the dimension of the second parameter", cxt->intrinsic_function->name);
		}
	}
}

static void qb_validate_operands_refract(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0], *operand2 = &operands[1], *operand3 = &operands[2];

	qb_validate_operands_matching_vector_width(cxt, f, operands, operand_count);

	if(!SCALAR(operand3->address)) {
		qb_abort("%s() expects the third parameter to be a scalar", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_cross_product(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0], *operand2 = &operands[1];

	qb_validate_operands_matching_vector_width(cxt, f, operands, operand_count);

	if(CONSTANT_DIMENSION(operand1->address, -1)) {
		uint32_t vector_width1 = DIMENSION(operand1->address, -1);
		if(!(2 <= vector_width1 && vector_width1 <= 4)) {
			qb_abort("%s() can only handle two, three, or four-dimensional vectors", cxt->intrinsic_function->name);
		}
	}
}

static void qb_validate_operands_one_matrix(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0];

	if(operand1->address->dimension_count < 2) {
		qb_abort("%s() expects a matrix as parameter", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_square_matrix(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0];

	qb_validate_operands_one_matrix(cxt, f, operands, operand_count);

	if(CONSTANT_DIMENSION(operand1->address, -1) && CONSTANT_DIMENSION(operand1->address, -2)) {
		uint32_t row = DIMENSION(operand1->address, -1);
		uint32_t col = DIMENSION(operand1->address, -2);
		if(row != col) {
			qb_abort("%s() expects a square matrix as parameter", cxt->intrinsic_function->name);
		}
	}
}

static void qb_validate_operands_pixel(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0], *operand2 = &operands[1];

	if(CONSTANT_DIMENSION(operand1->address, -1)) {
		uint32_t channel_count = DIMENSION(operand1->address, -1);
		if(!(3 <= channel_count && channel_count <= 4)) {
			qb_abort("%s() expects an array whose last dimension is 3 or 4", cxt->intrinsic_function->name);
		}
	} else {
		qb_abort("%s() can only handle fixed-length arrays", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_rgba(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *operand1 = &operands[0], *operand2 = &operands[1];

	if(CONSTANT_DIMENSION(operand1->address, -1)) {
		uint32_t channel_count = DIMENSION(operand1->address, -1);
		if(channel_count != 4) {
			qb_abort("%s() expects an array whose last dimension is 4", cxt->intrinsic_function->name);
		}
	} else {
		qb_abort("%s() can only handle fixed-length arrays", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_sampling(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *image = &operands[0];
	uint32_t channel_count;

	if(image->address->dimension_count != 3) {
		qb_abort("%s() expects a three-dimensional array as the first parameter", cxt->intrinsic_function->name);
	}
	if(CONSTANT_DIMENSION(image->address, -1)) {
		channel_count = DIMENSION(image->address, -1);
		if(!(3 <= channel_count && channel_count <= 4)) {
			qb_abort("%s() expects an array whose last dimension is 3 or 4", cxt->intrinsic_function->name);
		}
	}
	if(!(image->address->type >= QB_TYPE_F32)) {
		qb_abort("%s() expects image data to be in floating-point representation", cxt->intrinsic_function->name);
	}
}

static void qb_validate_operands_multidimensional_array(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *container = &operands[0], *column_index = &operands[1];
	if(container->address->dimension_count < 2) {
		qb_abort("%s() expects a multidimensional array as the first parameter", cxt->intrinsic_function->name);
	}
	if(!SCALAR(column_index->address)) {
		qb_abort("%s() expects a scalar as the second parameter", cxt->intrinsic_function->name);
	}
}

static qb_matrix_order qb_get_matrix_order(qb_compiler_context *cxt, qb_op_factory *f) {
	if(f->result_flags & QB_RESULT_IS_COLUMN_MAJOR) {
		return QB_MATRIX_ORDER_COLUMN_MAJOR;
	} else if(f->result_flags & QB_RESULT_IS_ROW_MAJOR) {
		return QB_MATRIX_ORDER_ROW_MAJOR;
	} else {
		return cxt->matrix_order;
	}
}

static qb_address *qb_obtain_matrix_row_address(qb_compiler_context *cxt, qb_address *address, qb_matrix_order order) {
	int32_t row_offset = (order == QB_MATRIX_ORDER_ROW_MAJOR) ? -2 : -1;
	return address->dimension_addresses[address->dimension_count + row_offset];
}

static qb_address *qb_obtain_matrix_column_address(qb_compiler_context *cxt, qb_address *address, qb_matrix_order order) {
	int32_t col_offset = (order == QB_MATRIX_ORDER_ROW_MAJOR) ? -1 : -2;
	return address->dimension_addresses[address->dimension_count + col_offset];
}

static void qb_validate_operands_mm_mult(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *matrix1 = &operands[0], *matrix2 = &operands[1];

	if(matrix1->address->dimension_count < 2) {
		qb_abort("%s() expects the first parameter to be a matrix", cxt->intrinsic_function->name);
	}
	if(matrix2->address->dimension_count < 2) {
		qb_abort("%s() expects the second parameter to be a matrix", cxt->intrinsic_function->name);
	}

	if(CONSTANT_DIMENSION(matrix1->address, -2) && CONSTANT_DIMENSION(matrix1->address, -1) && CONSTANT_DIMENSION(matrix2->address, -2) && CONSTANT_DIMENSION(matrix2->address, -1)) {
		qb_matrix_order order = qb_get_matrix_order(cxt, f);
		qb_address *m1_col_address = qb_obtain_matrix_column_address(cxt, matrix1->address, order);
		qb_address *m2_row_address = qb_obtain_matrix_row_address(cxt, matrix2->address, order);
		uint32_t m1_col_count = VALUE(U32, m1_col_address);
		uint32_t m2_row_count = VALUE(U32, m2_row_address);

		if(m1_col_count != m2_row_count) {
			qb_abort("The number of columns in the first matrix (%d) does not match the number of rows in the second matrix (%d)", m1_col_count, m2_row_count);
		}
	}
}

static void qb_validate_operands_mv_mult(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *matrix1 = &operands[0], *matrix2 = &operands[1];

	if(matrix1->address->dimension_count < 2) {
		qb_abort("%s() expects the first parameter to be a matrix", cxt->intrinsic_function->name);
	}
	if(matrix2->address->dimension_count < 1) {
		qb_abort("%s() expects the second parameter to be a vector", cxt->intrinsic_function->name);
	}

	if(CONSTANT_DIMENSION(matrix1->address, -2) && CONSTANT_DIMENSION(matrix1->address, -1) && CONSTANT_DIMENSION(matrix2->address, -1)) {
		qb_matrix_order order = qb_get_matrix_order(cxt, f);
		qb_address *m1_col_address = qb_obtain_matrix_column_address(cxt, matrix1->address, order);
		qb_address *m2_row_address = matrix2->address->dimension_addresses[matrix2->address->dimension_count - 1];
		uint32_t m1_col_count = VALUE(U32, m1_col_address);
		uint32_t m2_row_count = VALUE(U32, m2_row_address);

		if(m1_col_count != m2_row_count) {
			qb_abort("The number of columns in the matrix (%d) does not match the vector's dimension (%d)", m1_col_count, m2_row_count);
		}
	}
}

static void qb_validate_operands_vm_mult(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *matrix1 = &operands[0], *matrix2 = &operands[1];

	if(matrix1->address->dimension_count < 1) {
		qb_abort("%s() expects the first parameter to be a vector", cxt->intrinsic_function->name);
	}
	if(matrix2->address->dimension_count < 2) {
		qb_abort("%s() expects the second parameter to be a matrix", cxt->intrinsic_function->name);
	}

	if(CONSTANT_DIMENSION(matrix1->address, -1) && CONSTANT_DIMENSION(matrix2->address, -2) && CONSTANT_DIMENSION(matrix2->address, -1)) {
		qb_matrix_order order = qb_get_matrix_order(cxt, f);
		qb_address *m1_col_address = matrix1->address->dimension_addresses[matrix1->address->dimension_count - 1];
		qb_address *m2_row_address = qb_obtain_matrix_row_address(cxt, matrix2->address, order);
		uint32_t m1_col_count = VALUE(U32, m1_col_address);
		uint32_t m2_row_count = VALUE(U32, m2_row_address);

		if(m1_col_count != m2_row_count) {
			qb_abort("The number of rows in the matrix (%d) does not match the vector's dimension (%d)", m2_row_count, m1_col_count);
		}
	}
}

static void qb_validate_operands_matrix_current_mode(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_matrix_op_factory_selector *s = (qb_matrix_op_factory_selector *) f;
	if(cxt->matrix_order == QB_MATRIX_ORDER_COLUMN_MAJOR) {
		f = s->cm_factory;
	} else {
		f = s->rm_factory;
	}
	f->validate_operands(cxt, f, operands, operand_count);
}

static void qb_validate_operands_utf8_decode(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_validate_operands_one_array(cxt, f, operands, operand_count);
}

static void qb_validate_operands_utf8_encode(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	// nothing
}

static void qb_validate_operands_pack(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *value = &operands[0], *index = &operands[1];
	if(!SCALAR(value->address)) {
		qb_abort("%s() expects the first parameter to be a scalar", cxt->intrinsic_function->name);
	}
	if(value->type != QB_OPERAND_ADDRESS) {
		// type coercion had failed earlier
		if(index->type == QB_OPERAND_NONE) {
			qb_abort("%s() requires the second parameter when the input type cannot be determined", cxt->intrinsic_function->name);
		} else {
			qb_abort("%s() expects the second parameter to be a constant indicating the type", cxt->intrinsic_function->name);
		}
	}
}

static void qb_validate_operands_intrinsic(qb_compiler_context *cxt, qb_op_factory *f, qb_operand *operands, uint32_t operand_count) {
	qb_operand *func = &operands[0], *arguments = &operands[1], *argument_count = &operands[2];
	qb_intrinsic_function *ifunc = func->intrinsic_function;
	if((uint32_t) argument_count->number < ifunc->argument_count_min || (uint32_t) argument_count->number > ifunc->argument_count_max) {
		if(ifunc->argument_count_min == ifunc->argument_count_max) {
			qb_abort("%s() expects %d arguments but %d was passed", ifunc->name, ifunc->argument_count_min, argument_count->number);
		} else {
			qb_abort("%s() expects %d to %d arguments but %d was passed", ifunc->name, ifunc->argument_count_min, ifunc->argument_count_max, argument_count->number);
		}
	}
	cxt->intrinsic_function = ifunc;
	f = func->intrinsic_function->extra;
	if(f->validate_operands) {
		f->validate_operands(cxt, f, arguments->arguments, argument_count->number);
	}
	cxt->intrinsic_function = NULL;
}

