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

static qb_opcode qb_select_type_dependent_opcode(qb_compiler_context *cxt, qb_opcode opcodes[], qb_primitive_type expr_type) {
	qb_opcode opcode = opcodes[QB_TYPE_F64 - expr_type];
#ifdef ZEND_DEBUG
	if(expr_type >= QB_TYPE_COUNT) {
		qb_debug_abort("Invalid type");
	}
#endif
	return opcode;
}

static qb_opcode qb_select_opcode_simple(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_simple_op_factory *sf = (qb_simple_op_factory *) f;
	return sf->opcode;
}

static qb_opcode qb_select_opcode_basic(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	return qb_select_type_dependent_opcode(cxt, bf->opcodes, expr_type);
}

static qb_opcode qb_select_opcode_basic_first_operand(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	qb_primitive_type operand_type = operands[0].address->type;
	return qb_select_type_dependent_opcode(cxt, bf->opcodes, operand_type);
}

static qb_opcode qb_select_opcode_derived(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_derived_op_factory *df = (qb_derived_op_factory *) f;
	f = df->parent;
	return f->select_opcode(cxt, f, expr_type, operands, operand_count, result);
}

static qb_opcode qb_select_opcode_derived_modify_assign(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_operand *value = &operands[operand_count - 1];
	qb_operand binary_operands[2];
	qb_derived_op_factory *df = (qb_derived_op_factory *) f;
	binary_operands[0] = *result;
	binary_operands[1] = *value;
	f = df->parent;
	return f->select_opcode(cxt, f, expr_type, binary_operands, 2, result);
}

static qb_opcode qb_select_opcode_boolean_cast(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	qb_operand *variable = &operands[0];
	qb_primitive_type operand_type = variable->address->type;
	if(SCALAR(variable->address)) {
		return qb_select_type_dependent_opcode(cxt, bf->opcodes, operand_type);
	} else {
		// will be checking the length of the array
		return bf->opcodes[QB_TYPE_F64 - QB_TYPE_U32];
	}
}

static qb_opcode qb_select_opcode_array_element_isset(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	qb_operand *container = &operands[0];
	qb_primitive_type operand_type = container->address->type;
	if(container->address->dimension_count == 1) {
		return qb_select_type_dependent_opcode(cxt, bf->opcodes, operand_type);
	} else {
		// will be checking the length of the sub-array
		return bf->opcodes[QB_TYPE_F64 - QB_TYPE_U32];
	}
}

static qb_opcode qb_select_opcode_object_property_isset(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	qb_operand *container = &operands[0], *name = &operands[1];
	qb_address *address = qb_obtain_object_property(cxt, container, name, 0);
	qb_primitive_type operand_type = container->address->type;

	if(SCALAR(address)) {
		return qb_select_type_dependent_opcode(cxt, bf->opcodes, operand_type);
	} else {
		return bf->opcodes[QB_TYPE_F64 - QB_TYPE_U32];
	}
}

static qb_opcode qb_select_opcode_unset(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_unset_op_factory *uf = (qb_unset_op_factory *) f;
	qb_operand *variable = &operands[0];
	qb_primitive_type operand_type = variable->address->type;
	if(SCALAR(variable->address)) {
		return qb_select_type_dependent_opcode(cxt, uf->scalar_opcodes, operand_type);
	} else {
		if(RESIZABLE(variable->address)) {
			if(MULTIDIMENSIONAL(variable->address)) {
				return qb_select_type_dependent_opcode(cxt, uf->resizing_dim_opcodes, operand_type);
			} else {
				return qb_select_type_dependent_opcode(cxt, uf->resizing_opcodes, operand_type);
			}
		} else {
			return qb_select_type_dependent_opcode(cxt, uf->no_resizing_opcodes, operand_type);
		}
	}
}

static qb_opcode qb_select_opcode_unset_array_element(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_unset_element_op_factory *uf = (qb_unset_element_op_factory *) f;
	qb_operand *container = &operands[0];
	qb_primitive_type operand_type = container->address->type;
	if(RESIZABLE(container->address)) {
		if(MULTIDIMENSIONAL(container->address)) {
			return qb_select_type_dependent_opcode(cxt, uf->resizing_dim_opcodes, operand_type);
		} else {
			return qb_select_type_dependent_opcode(cxt, uf->resizing_opcodes, operand_type);
		}
	} else {
		return qb_select_type_dependent_opcode(cxt, uf->no_resizing_opcodes, operand_type);
	}
}

static qb_opcode qb_select_opcode_unset_object_property(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_unset_op_factory *uf = (qb_unset_op_factory *) f;
	qb_operand *container = &operands[0], *name = &operands[1];
	qb_address *address = qb_obtain_object_property(cxt, container, name, 0);
	qb_primitive_type operand_type = address->type;
	if(SCALAR(address)) {
		return qb_select_type_dependent_opcode(cxt, uf->scalar_opcodes, operand_type);
	} else {
		if(RESIZABLE(address)) {
			if(MULTIDIMENSIONAL(address)) {
				return qb_select_type_dependent_opcode(cxt, uf->resizing_dim_opcodes, operand_type);
			} else {
				return qb_select_type_dependent_opcode(cxt, uf->resizing_opcodes, operand_type);
			}
		} else {
			return qb_select_type_dependent_opcode(cxt, uf->no_resizing_opcodes, operand_type);
		}
	}
}

static qb_opcode qb_select_multidata_opcode(qb_compiler_context *cxt, qb_opcode opcode) {
	uint32_t op_flags = qb_get_op_flags(opcode);
	if(op_flags & QB_OP_VERSION_AVAILABLE_MIO) {
		if(op_flags & QB_OP_VERSION_AVAILABLE_ELE) {
			opcode += 2;
		} else {
			opcode += 1;
		}
	}
	return opcode;
}

static uint32_t qb_get_minimum_width(qb_compiler_context *cxt, qb_operand *operand) {
	qb_address *address = operand->address;
	uint32_t i, width = 1;
	for(i = 0; i < address->dimension_count; i++) {
		qb_address *array_size_address = address->array_size_addresses[i];
		if(CONSTANT(array_size_address)) {
			width = VALUE(U32, array_size_address);
			break;
		}
	}
	return width;
}

static qb_opcode qb_select_vectorized_nullary_opcode(qb_compiler_context *cxt, qb_opcode opcodes[][2], qb_operand *operand1) {
	if(operand1->address->type >= QB_TYPE_F32) {
		uint32_t width1 = qb_get_minimum_width(cxt, operand1);
		uint32_t denominator;
		qb_opcode opcode;

		if((width1 % 4) == 0) {
			denominator = 4;
		} else if((width1 % 3) == 0) {
			denominator = 3;
		} else if((width1 % 2) == 0) {
			denominator = 2;
		} else {
			return QB_NOP;
		}
		opcode = opcodes[denominator - 2][QB_TYPE_F64 - operand1->address->type];
		if(width1 > denominator) {
			opcode = qb_select_multidata_opcode(cxt, opcode);
		}
		return opcode;
	} else {
		return QB_NOP;
	}
}

static qb_opcode qb_select_opcode_nullary_arithmetic(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_arithmetic_op_factory *af = (qb_arithmetic_op_factory *) f;
	qb_opcode opcode = qb_select_vectorized_nullary_opcode(cxt, af->vector_opcodes, &operands[0]);
	if(opcode == QB_NOP) {
		opcode = qb_select_type_dependent_opcode(cxt, af->regular_opcodes, expr_type);
	}
	return opcode;
}

static qb_opcode qb_select_vectorized_unary_opcode(qb_compiler_context *cxt, qb_opcode opcodes[][2], qb_operand *operand1, qb_operand *result) {
	if(operand1->address->type >= QB_TYPE_F32) {
		uint32_t width1 = qb_get_minimum_width(cxt, operand1);
		uint32_t width2 = qb_get_minimum_width(cxt, result);
		uint32_t denominator;
		qb_opcode opcode;

		if((width1 % 4) == 0 && (width2 % 4) == 0) {
			denominator = 4;
		} else if((width1 % 3) == 0 && (width2 % 3) == 0) {
			denominator = 3;
		} else if((width1 % 2) == 0 && (width2 % 2) == 0) {
			denominator = 2;
		} else {
			return QB_NOP;
		}
		opcode = opcodes[denominator - 2][QB_TYPE_F64 - operand1->address->type];
		if(!FIXED_LENGTH(operand1->address) || !FIXED_LENGTH(result->address) || width1 > denominator || width2 > denominator) {
			opcode = qb_select_multidata_opcode(cxt, opcode);
		}
		return opcode;
	} else {
		return QB_NOP;
	}
}

static qb_opcode qb_select_opcode_reciprocal(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_derived_op_factory *df = (qb_derived_op_factory *) f;
	qb_arithmetic_op_factory *af = (qb_arithmetic_op_factory *) df->parent;
	return qb_select_type_dependent_opcode(cxt, af->regular_opcodes, expr_type);
}

static qb_opcode qb_select_vectorized_binary_opcode(qb_compiler_context *cxt, qb_opcode opcodes[][2], qb_operand *operand1, qb_operand *operand2, qb_operand *result) {
	if(operand1->address->type >= QB_TYPE_F32) {
		uint32_t width1 = qb_get_minimum_width(cxt, operand1);
		uint32_t width2 = qb_get_minimum_width(cxt, operand2);
		uint32_t width3 = qb_get_minimum_width(cxt, result);
		uint32_t denominator;
		qb_opcode opcode;

		if((width1 % 4) == 0 && (width2 % 4) == 0 && (width3 % 4) == 0) {
			denominator = 4;
		} else if((width1 % 3) == 0 && (width2 % 3) == 0 && (width3 % 3) == 0) {
			denominator = 3;
		} else if((width1 % 2) == 0 && (width2 % 2) == 0 && (width3 % 2) == 0) {
			denominator = 2;
		} else {
			return QB_NOP;
		}
		opcode = opcodes[denominator - 2][QB_TYPE_F64 - operand1->address->type];
		if(!FIXED_LENGTH(operand1->address) || !FIXED_LENGTH(operand2->address) || !FIXED_LENGTH(result->address) || width1 > denominator || width2 > denominator || width3 > denominator) {
			opcode = qb_select_multidata_opcode(cxt, opcode);
		}
		return opcode;
	} else {
		return QB_NOP;
	}
}

static qb_opcode qb_select_opcode_binary_arithmetic(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_arithmetic_op_factory *af = (qb_arithmetic_op_factory *) f;
	qb_opcode opcode = qb_select_vectorized_binary_opcode(cxt, af->vector_opcodes, &operands[0], &operands[1], result);
	if(opcode == QB_NOP) {
		opcode = qb_select_type_dependent_opcode(cxt, af->regular_opcodes, expr_type);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_assign(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_operand *value = &operands[operand_count - 1];
	qb_copy_op_factory *cf = (qb_copy_op_factory *) f;
	qb_address *src_address = value->address;
	qb_address *dst_address = result->address;	
	qb_opcode opcode = QB_NOP;
	if(src_address != dst_address) {
		// if the expression type was set to void, then an earlier op has used the r-value as write target
		// so there's no need to perform the assignment
		if(expr_type != QB_TYPE_VOID) {
			if(src_address->type == dst_address->type) {
				// vectorized instructions are available only for copying between variables of the same type
				opcode = qb_select_vectorized_unary_opcode(cxt, cf->vector_opcodes, value, result);
			}
			if(opcode == QB_NOP) {
				opcode = cf->opcodes[QB_TYPE_F64 - src_address->type][QB_TYPE_F64 - dst_address->type];
			}
		}
	}
	return opcode;
}

static qb_opcode qb_select_opcode_assign_return_value(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	if(cxt->return_variable->address) {
		return qb_select_opcode_derived(cxt, f, expr_type, operands, operand_count, result);
	} else {
		return QB_NOP;
	}
}

static qb_opcode qb_select_opcode_assign_generator_key(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	if(cxt->return_key_variable->address) {
		return qb_select_opcode_derived(cxt, f, expr_type, operands, operand_count, result);
	} else {
		return QB_NOP;
	}
}

static qb_opcode qb_select_opcode_increment_generator_key(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	if(cxt->return_key_variable->address) {
		qb_derived_op_factory *df = (qb_derived_op_factory *) f;
		qb_arithmetic_op_factory *af = (qb_arithmetic_op_factory *) df->parent;
		qb_opcode opcode = qb_select_type_dependent_opcode(cxt, af->regular_opcodes, cxt->return_key_variable->address->type);
		return opcode;
	} else {
		return QB_NOP;
	}
}

static qb_opcode qb_select_opcode_gather(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_gather_op_factory *gf = (qb_gather_op_factory *) f;
	qb_operand *dest = &operands[0];
	uint32_t width = DIMENSION(dest->address, -1);
	qb_opcode opcode = qb_select_type_dependent_opcode(cxt, gf->opcodes[width - 2], dest->address->type);
	return opcode;
}

static qb_opcode qb_select_opcode_scatter(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_gather_op_factory *gf = (qb_gather_op_factory *) f;
	qb_operand *source = &operands[1];
	uint32_t width = DIMENSION(source->address, -1);
	qb_opcode opcode = qb_select_type_dependent_opcode(cxt, gf->opcodes[width - 2], source->address->type);
	return opcode;
}

static qb_opcode qb_select_opcode_copy_dimension(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	uint32_t dimension_count = (operand_count - 2) / 4;
	qb_opcode opcode = bf->opcodes[dimension_count - 2];
	return opcode;
}

static qb_opcode qb_select_opcode_add_variable(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_string_op_factory *sf = (qb_string_op_factory *) f;
	qb_address *address = operands[1].address;
	qb_opcode opcode;

	if(address->dimension_count > 1) {
		opcode = qb_select_type_dependent_opcode(cxt, sf->multidim_opcodes, address->type);
	} else {
		if(address->flags & QB_ADDRESS_STRING) {
			opcode = sf->text_opcode;
		} else {
			opcode = qb_select_type_dependent_opcode(cxt, sf->opcodes, address->type);
		}
	}
	return opcode;
}

static qb_opcode qb_select_opcode_print(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_string_op_factory *sf = (qb_string_op_factory *) f;
	qb_address *address = operands[0].address;
	qb_opcode opcode;

	if(address->dimension_count > 1) {
		opcode = qb_select_type_dependent_opcode(cxt, sf->multidim_opcodes, address->type);
	} else {
		if(address->flags & QB_ADDRESS_STRING) {
			opcode = sf->text_opcode;
		} else {
			opcode = qb_select_type_dependent_opcode(cxt, sf->opcodes, address->type);
		}
	}
	return opcode;
}

static qb_opcode qb_select_opcode_sampling(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_pixel_op_factory *pf = (qb_pixel_op_factory *) f;
	qb_address *image_address = operands[0].address;
	qb_address *address_x = operands[1].address;
	qb_address *address_y = operands[2].address;
	uint32_t channel_count = DIMENSION(image_address, -1);
	qb_opcode opcode = pf->opcodes[channel_count - 1][QB_TYPE_F64 - image_address->type];

	if(address_x->dimension_count > 1 || address_y->dimension_count > 1) {
		// handling multiple pixels
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_sampling_vector(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_derived_op_factory *df = (qb_derived_op_factory *) f;
	qb_pixel_op_factory *pf = (qb_pixel_op_factory *) df->parent;
	qb_address *image_address = operands[0].address;
	uint32_t channel_count = DIMENSION(image_address, -1);
	qb_opcode opcode = pf->opcodes[channel_count - 1][QB_TYPE_F64 - image_address->type];
	return opcode;
}

static qb_opcode qb_select_opcode_array_resize(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_array_resize_op_factory *af = (qb_array_resize_op_factory *) f;
	qb_address *address = operands[0].address;
	qb_opcode opcode = af->opcodes[address->dimension_count - 1][QB_TYPE_F64 - address->type];
	return opcode;
}

static qb_opcode qb_select_opcode_pixel(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_pixel_op_factory *pf = (qb_pixel_op_factory *) f;
	qb_address *address = operands[0].address;
	uint32_t channel_count = DIMENSION(address, -1);
	qb_opcode opcode = pf->opcodes[channel_count - 1][QB_TYPE_F64 - address->type];

	if(address->dimension_count > 1) {
		// handling multiple pixels
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_one_vector(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_vector_op_factory *vf = (qb_vector_op_factory *) f;
	qb_address *address = operands[0].address;
	qb_opcode opcode = QB_NOP;
	uint32_t dimension = 0;
	if(CONSTANT_DIMENSION(address, -1)) {
		dimension = DIMENSION(address, -1);
	}
	if(2 <= dimension && dimension <= 4) {
		opcode = vf->opcodes_fixed_size[dimension - 2][QB_TYPE_F64 - address->type];
	}
	if(opcode == QB_NOP) {
		opcode = vf->opcodes_any_size[QB_TYPE_F64 - address->type];
	}
	if(address->dimension_count > 1) {
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_two_vectors(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_vector_op_factory *vf = (qb_vector_op_factory *) f;
	qb_address *address1 = operands[0].address;
	qb_address *address2 = operands[1].address;
	qb_opcode opcode = QB_NOP;
	uint32_t dimension = 0;
	if(CONSTANT_DIMENSION(address1, -1)) {
		dimension = DIMENSION(address1, -1);
	} else if(CONSTANT_DIMENSION(address2, -1)) {
		dimension = DIMENSION(address2, -1);
	}
	if(2 <= dimension && dimension <= 4) {
		opcode = vf->opcodes_fixed_size[dimension - 2][QB_TYPE_F64 - address1->type];
	}
	if(opcode == QB_NOP) {
		opcode = vf->opcodes_any_size[QB_TYPE_F64 - address1->type];
	}
	if(address1->dimension_count > 1 || address2->dimension_count > 1) {
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_cross_product(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_vector_op_factory *vf = (qb_vector_op_factory *) f;
	qb_opcode opcode = QB_NOP;
	if(operand_count == 3) {
		qb_address *address1 = operands[0].address;
		qb_address *address2 = operands[1].address;
		qb_address *address3 = operands[2].address;
		uint32_t dimension = 4;
		opcode = vf->opcodes_fixed_size[dimension - 2][QB_TYPE_F64 - address1->type];
		if(address1->dimension_count > 1 || address2->dimension_count > 1 || address3->dimension_count > 1) {
			opcode = qb_select_multidata_opcode(cxt, opcode);
		}
	} else {
		qb_address *address1 = operands[0].address;
		qb_address *address2 = operands[1].address;
		uint32_t dimension = 0;
		if(CONSTANT_DIMENSION(address1, -1)) {
			dimension = DIMENSION(address1, -1);
		} else if(CONSTANT_DIMENSION(address2, -1)) {
			dimension = DIMENSION(address2, -1);
		}
		if(2 <= dimension && dimension <= 3) {
			opcode = vf->opcodes_fixed_size[dimension - 2][QB_TYPE_F64 - address1->type];
		}
		if(address1->dimension_count > 1 || address2->dimension_count > 1) {
			opcode = qb_select_multidata_opcode(cxt, opcode);
		}
	}
	return opcode;
}

static qb_opcode qb_select_fixed_size_matrix_opcode(qb_compiler_context *cxt, qb_opcode opcodes[][2], qb_primitive_type expr_type, qb_address *address) {
	uint32_t rows = DIMENSION(address, -2);
	uint32_t cols = DIMENSION(address, -1);
	qb_opcode opcode; 
	if(rows == cols && 2 <= rows && rows <= 4) {
		opcode = opcodes[rows - 2][QB_TYPE_F64 - expr_type];
	} else {
		opcode = QB_NOP;
	}
	return opcode;
}

static qb_opcode qb_select_opcode_mm_mult_cm(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_matrix_op_factory *mf = (qb_matrix_op_factory *) f;
	qb_address *address1 = operands[0].address;
	qb_address *address2 = operands[1].address;
	qb_opcode opcode = QB_NOP;
	if(CONSTANT_DIMENSION(address1, -1) && CONSTANT_DIMENSION(address1, -2) && CONSTANT_DIMENSION(address2, -1) && CONSTANT_DIMENSION(address2, -2)) {
		uint32_t m1_cols = DIMENSION(address1, -2);
		uint32_t m1_rows = DIMENSION(address1, -1);
		uint32_t m2_cols = DIMENSION(address2, -2);
		uint32_t m2_rows = DIMENSION(address2, -1);
		if(cxt->matrix_padding && m1_rows == 4 && m1_cols == 3 && m2_rows == 4 && m2_cols == 3) {
			opcode = mf->opcode_3x3_padded;
		} else {
			if(m1_rows == m1_cols && m2_rows == m2_cols) {
				opcode = qb_select_fixed_size_matrix_opcode(cxt, mf->opcodes_fixed_size, expr_type, address1);
			}
		}
	}
	if(opcode == QB_NOP) {
		opcode = qb_select_type_dependent_opcode(cxt, mf->opcodes_any_size, expr_type);
	}
	if(address1->dimension_count > 2 || address2->dimension_count > 2) {
		opcode = qb_select_multidata_opcode(cxt, opcode);
	} 
	return opcode;
}

static qb_opcode qb_select_opcode_mv_mult_cm(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_matrix_op_factory *mf = (qb_matrix_op_factory *) f;
	qb_address *address1 = operands[0].address;
	qb_address *address2 = operands[1].address;
	qb_opcode opcode = QB_NOP;
	if(CONSTANT_DIMENSION(address1, -1) && CONSTANT_DIMENSION(address1, -2) && CONSTANT_DIMENSION(address2, -1)) {
		uint32_t m_cols = DIMENSION(address1, -2);
		uint32_t m_rows = DIMENSION(address1, -1);
		if(cxt->matrix_padding && m_rows == 4 && m_cols == 3) {
			opcode = mf->opcode_3x3_padded;
		} else {
			opcode = qb_select_fixed_size_matrix_opcode(cxt, mf->opcodes_fixed_size, expr_type, address1);
		}
	}
	if(opcode == QB_NOP) {
		opcode = qb_select_type_dependent_opcode(cxt, mf->opcodes_any_size, expr_type);
	}
	if(address1->dimension_count > 2 || address2->dimension_count > 1) {
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_vm_mult_cm(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_matrix_op_factory *mf = (qb_matrix_op_factory *) f;
	qb_address *address1 = operands[0].address;
	qb_address *address2 = operands[1].address;
	qb_opcode opcode = QB_NOP;
	if(CONSTANT_DIMENSION(address1, -1) && CONSTANT_DIMENSION(address2, -1) && CONSTANT_DIMENSION(address2, -2)) {
		uint32_t m_cols = DIMENSION(address2, -2);
		uint32_t m_rows = DIMENSION(address2, -1);
		if(cxt->matrix_padding && m_rows == 4 && m_cols == 3) {
			opcode = mf->opcode_3x3_padded;
		} else {
			opcode = qb_select_fixed_size_matrix_opcode(cxt, mf->opcodes_fixed_size, expr_type, address2);
		}
	}
	if(opcode == QB_NOP) {
		opcode = qb_select_type_dependent_opcode(cxt, mf->opcodes_any_size, expr_type);
	}
	if(address1->dimension_count > 1 || address2->dimension_count > 2) {
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_transpose_equivalent(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_derived_op_factory *df = (qb_derived_op_factory *) f;
	qb_operand reversed_operands[2];
	reversed_operands[0] = operands[1];
	reversed_operands[1] = operands[0];
	f = df->parent;
	return f->select_opcode(cxt, f, expr_type, reversed_operands, operand_count, result);
}

static qb_opcode qb_select_opcode_matrix_current_mode(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	USE_TSRM
	qb_matrix_op_factory_selector *s = (qb_matrix_op_factory_selector *) f;
	if(QB_G(column_major_matrix)) {
		f = s->cm_factory;
	} else {
		f = s->rm_factory;
	}
	return f->select_opcode(cxt, f, expr_type, operands, operand_count, result);
}

static qb_opcode qb_select_opcode_matrix_unary(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_vector_op_factory *vf = (qb_vector_op_factory *) f;
	qb_address *address = operands[0].address;
	qb_opcode opcode = QB_NOP;
	if(CONSTANT_DIMENSION(address, -1) && CONSTANT_DIMENSION(address, -2)) {
		opcode = qb_select_fixed_size_matrix_opcode(cxt, vf->opcodes_fixed_size, expr_type, address);
	}
	if(opcode == QB_NOP) {
		opcode = qb_select_type_dependent_opcode(cxt, vf->opcodes_any_size, expr_type);
	}
	if(address->dimension_count > 2) {
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_transform_cm(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_matrix_op_factory *mf = (qb_matrix_op_factory *) f;
	qb_address *address1 = operands[0].address;
	qb_address *address2 = operands[1].address;
	uint32_t m_rows = DIMENSION(address1, -1);
	qb_opcode opcode;

	if(2 <= m_rows && m_rows <= 4) {
		opcode = mf->opcodes_fixed_size[m_rows - 2][QB_TYPE_F64 - expr_type];
	} else {
		opcode = mf->opcodes_any_size[QB_TYPE_F64 - expr_type];
	}
	if(address1->dimension_count > 1 || address2->dimension_count > 2) {
		// handling multiple matrices or vectors		
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_transform_rm(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_matrix_op_factory *mf = (qb_matrix_op_factory *) f;
	qb_address *address1 = operands[0].address;
	qb_address *address2 = operands[1].address;
	uint32_t m_rows = DIMENSION(address1, -2);
	qb_opcode opcode;

	if(2 <= m_rows && m_rows <= 4) {
		opcode = mf->opcodes_fixed_size[m_rows - 2][QB_TYPE_F64 - expr_type];
	} else {
		opcode = mf->opcodes_any_size[QB_TYPE_F64 - expr_type];
	}
	if(address1->dimension_count > 1 || address2->dimension_count > 2) {
		// handling multiple matrices or vectors		
		opcode = qb_select_multidata_opcode(cxt, opcode);
	}
	return opcode;
}

static qb_opcode qb_select_opcode_complex_number(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_opcode opcode = qb_select_opcode_basic(cxt, f, expr_type, operands, operand_count, result);
	uint32_t i;
	for(i = 0; i < operand_count; i++) {
		qb_address *address = operands[i].address;
		if(address->dimension_count > 1) {
			opcode = qb_select_multidata_opcode(cxt, opcode);
			break;
		}
	}
	return opcode;
}

static qb_opcode qb_select_opcode_utf8_decode(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_utf8_op_factory *uf = (qb_utf8_op_factory *) f;
	if(result->address->type == QB_TYPE_U32) {
		return uf->ucs32_opcode;
	} else {
		return uf->ucs16_opcode;
	}
}

static qb_opcode qb_select_opcode_utf8_encode(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_operand *codepoints = &operands[0];
	qb_utf8_op_factory *uf = (qb_utf8_op_factory *) f;
	if(codepoints->address->type == QB_TYPE_U32) {
		return uf->ucs32_opcode;
	} else {
		return uf->ucs16_opcode;
	}
}

static qb_opcode qb_select_opcode_expression_type(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	qb_basic_op_factory *bf = (qb_basic_op_factory *) f;
	return qb_select_type_dependent_opcode(cxt, bf->opcodes, expr_type);
}

static qb_opcode qb_select_opcode_ext(qb_compiler_context *cxt, qb_op_factory *f, qb_primitive_type expr_type, qb_operand *operands, uint32_t operand_count, qb_operand *result) {
	USE_TSRM
	if(QB_G(allow_debugger_inspection)) {
		qb_simple_op_factory *sf = (qb_simple_op_factory *) f;
		return sf->opcode;
	} else {
		return QB_NOP;
	}
}
