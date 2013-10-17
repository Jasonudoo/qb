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

#include "qb.h"

static int32_t qb_retrieve_operand(qb_php_translater_context *cxt, uint32_t zoperand_type, znode_op *zoperand, qb_operand *operand) {
	switch(zoperand_type) {
		case Z_OPERAND_CV: {
			uint32_t var_index = Z_OPERAND_INFO(*zoperand, var);
			qb_variable *qvar = cxt->compiler_context->variables[var_index];
			if(!qvar->address) {
				// the variable type hasn't been set yet
				qvar->flags |= QB_VARIABLE_LOCAL;
				qb_apply_type_declaration(cxt->compiler_context, qvar);
			}
			operand->address = qvar->address;
			operand->type = QB_OPERAND_ADDRESS;
		}	break;
		case Z_OPERAND_CONST: {
			operand->constant = Z_OPERAND_ZV(*zoperand);
			operand->type = QB_OPERAND_ZVAL;
		}	break;
		case Z_OPERAND_TMP_VAR:
		case Z_OPERAND_VAR: {
			uint32_t temp_var_index = Z_OPERAND_TMP_INDEX(zoperand);
			if(temp_var_index < cxt->compiler_context->temp_variable_count) {
				qb_temporary_variable *temp_variable = &cxt->compiler_context->temp_variables[temp_var_index];
				*operand = temp_variable->operand;
				if(cxt->compiler_context->stage == QB_STAGE_RESULT_TYPE_RESOLUTION) {
					temp_variable->last_access_op_index = cxt->zend_op_index;
				}
				break;
			}
		}	// fall through in case of invalid index
		default: {
			operand->type = QB_OPERAND_NONE;
			operand->generic_pointer = NULL;
			return FALSE;
		}
	}
	return TRUE;
}

static void qb_retire_operand(qb_php_translater_context *cxt, uint32_t zoperand_type, znode_op *zoperand, qb_operand *operand) {
	switch(zoperand_type) {
		case Z_OPERAND_TMP_VAR:
		case Z_OPERAND_VAR: {
			uint32_t temp_var_index = Z_OPERAND_TMP_INDEX(zoperand);
			if(temp_var_index < cxt->compiler_context->temp_variable_count) {
				qb_temporary_variable *temp_variable = &cxt->compiler_context->temp_variables[temp_var_index];
				if(cxt->compiler_context->stage == QB_STAGE_RESULT_TYPE_RESOLUTION) {
					temp_variable->operand = *operand;
				} else if(cxt->compiler_context->stage == QB_STAGE_OPCODE_TRANSLATION) {
					if(temp_variable->last_access_op_index == cxt->zend_op_index) {
						// unlock the operand if the current op is the last to access it
						qb_unlock_operand(cxt->compiler_context, operand);
						temp_variable->operand.type = QB_OPERAND_EMPTY;
						temp_variable->operand.generic_pointer = NULL;
					} else {
						if(memcmp(&temp_variable->operand, operand, sizeof(qb_operand)) != 0) {
							// unlock what was there before if it's different
							qb_unlock_operand(cxt->compiler_context, &temp_variable->operand);
							temp_variable->operand = *operand;
							qb_lock_operand(cxt->compiler_context, &temp_variable->operand);
						}
					}
				}
			}
		}	break;
	}
}

static qb_address * qb_obtain_write_target_size(qb_php_translater_context *cxt, qb_result_prototype *result_prototype) {
	if(result_prototype->destination) {
		qb_result_destination *destination = result_prototype->destination;

		switch(destination->type) {
			case QB_RESULT_DESTINATION_VARIABLE: {
				qb_address *address = destination->variable.address;
				return address->array_size_address;
			}	break;
			case QB_RESULT_DESTINATION_ELEMENT: {
				if(destination->element.container.type == QB_OPERAND_ADDRESS) {
					qb_address *address = destination->element.container.address;
					if(address->dimension_count > 1) {
						return address->array_size_addresses[1];
					}
				}
			}	break;
			case QB_RESULT_DESTINATION_PROPERTY: {
				if(destination->property.container.type == QB_OPERAND_ADDRESS) {
					qb_address *address = destination->property.container.address;
					if(address->dimension_count > 1) {
						return address->array_size_addresses[1];
					}
				}
			}	break;
			default: break;
		}
	}
	return NULL;
}

static void qb_translate_cast(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	uint32_t zend_type = cxt->zend_op->extended_value;
	qb_primitive_type type;

	switch(zend_type) {
		case IS_ARRAY:	type = QB_TYPE_ANY; break;
		case IS_BOOL:	type = QB_TYPE_S32; break;
		case IS_LONG:	type = QB_TYPE_S64; break;
		case IS_DOUBLE:	type = QB_TYPE_F64; break;
		default:		type = QB_TYPE_UNKNOWN;
	}
}

static void qb_translate_basic_op(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);
}

static void qb_translate_combo_op(qb_php_translater_context *cxt, void *op_factories, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	void **list = op_factories, *op_factory;

	if(cxt->zend_op->extended_value == ZEND_ASSIGN_DIM) {
		op_factory = list[1];
	} else if(cxt->zend_op->extended_value == ZEND_ASSIGN_OBJ) {
		op_factory = list[2];
	} else {
		op_factory = list[0];
	}
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);
}

static void qb_translate_cast_op(qb_php_translater_context *cxt, void *op_factories, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	void **list = op_factories, *op_factory;
	uint32_t zend_type = cxt->zend_op->extended_value;

	switch(zend_type) {
#if SIZEOF_LONG == 8
		case IS_LONG:	op_factory = list[1]; break;
#else
		case IS_LONG:	op_factory = list[0]; break;
#endif
		case IS_DOUBLE:	op_factory = list[2]; break;
		case IS_BOOL:	op_factory = list[3]; break;
		case IS_ARRAY:	op_factory = list[4]; break;
		case IS_STRING: op_factory = list[5]; break;
		default:		
			qb_abort("Illegal cast");
	}
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);
}

static void qb_translate_fetch_class(qb_php_translater_context *cxt, void *op_factories, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	int32_t fetch_type = cxt->zend_op->extended_value & ZEND_FETCH_CLASS_MASK;
	void **list = op_factories, *op_factory;

	if(fetch_type == ZEND_FETCH_CLASS_SELF) {
		op_factory = list[0];
	} else if(fetch_type == ZEND_FETCH_CLASS_PARENT) {
		op_factory = list[1];
#if defined(ZEND_FETCH_CLASS_STATIC)
	} else if(fetch_type == ZEND_FETCH_CLASS_STATIC) {
		op_factory = list[2];
#endif
	}
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);
}

static void qb_translate_fetch(qb_php_translater_context *cxt, void *op_factories, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	USE_TSRM
	qb_operand *name = &operands[0], *scope = &operands[1];
	uint32_t fetch_type = FETCH_TYPE(cxt->zend_op);
	void **list = op_factories, *op_factory;

	if(fetch_type == ZEND_FETCH_LOCAL) {
		op_factory = list[0];
	} else if(fetch_type == ZEND_FETCH_GLOBAL || fetch_type == ZEND_FETCH_GLOBAL_LOCK) {
		op_factory = list[1];
	} else if(fetch_type == ZEND_FETCH_STATIC) {
		op_factory = list[2];
	} else if(fetch_type == ZEND_FETCH_STATIC_MEMBER) {
		op_factory = list[3];
	}
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);
}

static void qb_do_function_call_translation(qb_php_translater_context *cxt, void *op_factories, qb_operand *name, qb_operand *object, qb_operand **stack_pointer, uint32_t argument_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_intrinsic_function *ifunc = NULL;
	zend_function *zfunc = NULL;
	qb_function *qfunc = NULL;

	qb_operand *arguments;
	qb_operand func_operands[4];
	uint32_t max_operand_count = argument_count;
	uint32_t i;
	void **list = op_factories, *op_factory;
	ALLOCA_FLAG(use_heap);

	if(object->type == QB_OPERAND_NONE) {
		ifunc = qb_find_intrinsic_function(cxt, name->constant);
	}
	if(ifunc) {
		if(ifunc->argument_count_max != (uint32_t) -1 &&  ifunc->argument_count_max > max_operand_count) {
			max_operand_count = ifunc->argument_count_max;
		}
	} else {
		USE_TSRM
		zval *class_name = NULL;
		if(object->type == QB_OPERAND_ZEND_CLASS || object->type == QB_OPERAND_ZEND_STATIC_CLASS) {
			zend_class_entry *ce;
			if(object->type == QB_OPERAND_ZEND_STATIC_CLASS) {
				// static:: qualifier--use the base class for now 
				// since we need to know whether it's going to be a zend call or not
				ce = cxt->zend_op_array->scope;
			} else {
				ce = object->zend_class;
			}
			if(ce) {
				class_name = qb_string_to_zval(ce->name, ce->name_length TSRMLS_CC);
			}
		} else if(object->type == QB_OPERAND_ZVAL) {
			class_name = object->constant;
		}
		zfunc = qb_find_zend_function(class_name, name->constant TSRMLS_CC);
		if(zfunc) {
			qfunc = qb_find_compiled_function(zfunc);
		} else {
			qb_abort("Missing function");
		}
	}

	// copy the arguments
	arguments = do_alloca(sizeof(qb_operand *) * max_operand_count, use_heap);
	for(i = 0; i < max_operand_count; i++) {
		if(i < argument_count) {
			arguments[i] = *stack_pointer[i];
		} else {
			// set unused arguments to none to simplify things
			arguments[i].type = QB_OPERAND_NONE;
			arguments[i].generic_pointer = NULL;
		}
	}

	if(ifunc) {
		op_factory = list[0];
		func_operands[0].intrinsic_function = ifunc;
		func_operands[0].type = QB_OPERAND_INTRINSIC_FUNCTION;
	} else {
		if(qfunc) {
			op_factory = list[1];
		} else {
			op_factory = list[2];
		}
		func_operands[0].zend_function = zfunc;
		func_operands[0].type = (object->type == QB_OPERAND_ZEND_STATIC_CLASS) ? QB_OPERAND_STATIC_ZEND_FUNCTION : QB_OPERAND_ZEND_FUNCTION;
	}
	func_operands[1].arguments = arguments;
	func_operands[1].type = QB_OPERAND_ARGUMENTS;
	func_operands[2].number = argument_count;
	func_operands[2].type = QB_OPERAND_NUMBER;

	if(cxt->compiler_context->stage == QB_STAGE_RESULT_TYPE_RESOLUTION) {
		if(qfunc && cxt->compiler_context->dependencies) {
			USE_TSRM
			// note that this function is dependent on the target function
			qb_compiler_context *other_compiler_cxt = qb_find_compiler_context(QB_G(build_context), qfunc);
			if(other_compiler_cxt) {
				cxt->compiler_context->dependencies[other_compiler_cxt->dependency_index] = TRUE;
			}
		}
	}

	qb_produce_op(cxt->compiler_context, op_factory, func_operands, 3, result, NULL, 0, result_prototype);
	free_alloca(arguments, use_heap);
}

static void qb_translate_function_call(qb_php_translater_context *cxt, void *op_factories, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *name = &operands[0], *object = &operands[1];
	uint32_t argument_count = cxt->zend_op->extended_value;
	qb_operand **stack_pointer = qb_pop_stack_items(cxt->compiler_context, argument_count);
	qb_do_function_call_translation(cxt, op_factories, name, object, stack_pointer, argument_count, result, result_prototype);
}

static void qb_translate_init_method_call(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *object = &operands[0], *name = &operands[1], *stack_item;
	stack_item = qb_push_stack_item(cxt->compiler_context);	// function name
	*stack_item = *name;

	stack_item = qb_push_stack_item(cxt->compiler_context);	// object
#if ZEND_ENGINE_2_2 || ZEND_ENGINE_2_1
	if(object->type == QB_OPERAND_ZVAL) {
		qb_retrieve_operand(cxt, Z_OPERAND_TMP_VAR, &cxt->zend_op->op1, object);
	}
#endif
	if(object->type == QB_OPERAND_NONE) {
		stack_item->zend_class = cxt->zend_op_array->scope;
		stack_item->type = QB_OPERAND_ZEND_CLASS;
	} else {
		*stack_item = *object;
	}
}

static void qb_translate_init_function_call(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *object = &operands[0], *name = &operands[1], *stack_item;
	stack_item = qb_push_stack_item(cxt->compiler_context);	// function name
	*stack_item = *name;

	stack_item = qb_push_stack_item(cxt->compiler_context);	// object
#if !ZEND_ENGINE_2_3
	*stack_item = *object;
#else
	// for some reason in Zend Engine 2.3, the name of the function would show up here
	if(object->type == QB_OPERAND_ZVAL && object->constant->type == IS_OBJECT) {
		*stack_item = *object;
	} else {
		stack_item->type = QB_OPERAND_NONE;
		stack_item->address = NULL;
	}
#endif
}

static void qb_translate_function_call_by_name(qb_php_translater_context *cxt, void *op_factories, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	uint32_t argument_count = cxt->zend_op->extended_value;
	qb_operand **stack_pointer = qb_pop_stack_items(cxt->compiler_context, argument_count + 2);
	qb_operand *name = stack_pointer[0];
	qb_operand *object = stack_pointer[1];
	qb_do_function_call_translation(cxt, op_factories, name, object, stack_pointer + 2, argument_count, result, result_prototype);
}

static void qb_translate_receive_argument(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *argument = &operands[0];
	argument->type = QB_OPERAND_NUMBER;
#if !ZEND_ENGINE_2_3 && !ZEND_ENGINE_2_2 && !ZEND_ENGINE_2_1
	argument->number = Z_OPERAND_INFO(cxt->zend_op->op1, num);
#else
	argument->number = Z_LVAL_P(Z_OPERAND_ZV(cxt->zend_op->op1));
#endif
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);
}

static void qb_translate_return(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);

	cxt->jump_target_index1 = QB_INSTRUCTION_END;
}

static void qb_translate_exit(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, NULL, 0, result_prototype);

	cxt->jump_target_index1 = QB_INSTRUCTION_END;
}

static void qb_translate_jump(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	zend_op *target_op = Z_OPERAND_INFO(cxt->zend_op->op1, jmp_addr);
	uint32_t target_index = ZEND_OP_INDEX(target_op);

	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, &target_index, 1, result_prototype);

	cxt->jump_target_index1 = target_index;
}

static void qb_translate_jump_set(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	// TODO
	// jump over the false clause first
	//cxt->jump_target_index1 = ZEND_OP_INDEX(target_op);
	//cxt->jump_target_index2 = cxt->zend_op_index + 1;
}

static void qb_translate_branch(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *condition = &operands[0];
	zend_op *target_op1 = Z_OPERAND_INFO(cxt->zend_op->op2, jmp_addr);
	uint32_t target_indices[2];

	target_indices[0] = ZEND_OP_INDEX(target_op1);
	target_indices[1] = cxt->zend_op_index + 1;

	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, target_indices, 2, result_prototype);

	// start down the next instruction first before going down the branch
	cxt->jump_target_index1 = target_indices[1];
	cxt->jump_target_index2 = target_indices[0];
}

static void qb_translate_for_loop(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *condition = &operands[0];
	uint32_t target_indices[2];
	
	target_indices[0] = cxt->zend_op->extended_value;
	target_indices[1] = Z_OPERAND_INFO(cxt->zend_op->op2, opline_num);

	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, target_indices, 2, result_prototype);

	cxt->jump_target_index1 = target_indices[0];
	cxt->jump_target_index2 = target_indices[1];
}

static zend_brk_cont_element * qb_find_break_continue_element(qb_php_translater_context *cxt, int32_t nest_levels, int32_t array_offset) {
	zend_brk_cont_element *brk_cont_array = cxt->zend_op_array->brk_cont_array, *jmp_to;
	int32_t i;
	for(i = 0; i < nest_levels; i++) {
		if(array_offset == -1) {
			qb_abort("Cannot break/continue %d level%s", nest_levels, (nest_levels == 1) ? "" : "s");
		}
		jmp_to = &brk_cont_array[array_offset];
		array_offset = jmp_to->parent;
	}
	return jmp_to;
}


static void qb_translate_break(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	zval *nest_level = Z_OPERAND_ZV(cxt->zend_op->op2);
	zend_brk_cont_element *jmp_to = qb_find_break_continue_element(cxt, Z_LVAL_P(nest_level), Z_OPERAND_INFO(cxt->zend_op->op1, opline_num));
	uint32_t target_index = jmp_to->brk;

	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, &target_index, 1, result_prototype);

	cxt->jump_target_index1 = target_index;
}

static void qb_translate_continue(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	zval *nest_level = Z_OPERAND_ZV(cxt->zend_op->op2);
	zend_brk_cont_element *jmp_to = qb_find_break_continue_element(cxt, Z_LVAL_P(nest_level), Z_OPERAND_INFO(cxt->zend_op->op1, opline_num));
	uint32_t target_index = jmp_to->cont;

	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, &target_index, 1, result_prototype);

	cxt->jump_target_index1 = jmp_to->cont;
}

static void qb_translate_foreach_fetch(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
	qb_operand *container = &operands[0];
	uint32_t target_indices[2];

	target_indices[0] = QB_INSTRUCTION_NEXT;
	target_indices[1] = Z_OPERAND_INFO(cxt->zend_op->op2, opline_num);

	if(cxt->zend_op->extended_value & ZEND_FE_FETCH_BYREF) {
		qb_abort("reference is currently not supported");
	}

	qb_produce_op(cxt->compiler_context, op_factory, operands, operand_count, result, target_indices, 2, result_prototype);

	cxt->jump_target_index1 = cxt->zend_op_index + 2;
	cxt->jump_target_index2 = target_indices[1];
}

static void qb_translate_user_opcode(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
}

static void qb_translate_extension_op(qb_php_translater_context *cxt, void *op_factory, qb_operand *operands, uint32_t operand_count, qb_operand *result, qb_result_prototype *result_prototype) {
}

static qb_php_op_translator op_translators[] = {
	{	qb_translate_basic_op,				&factory_nop								},	// ZEND_NOP
	{	qb_translate_basic_op,				&factory_add								},	// ZEND_ADD
	{	qb_translate_basic_op,				&factory_subtract							},	// ZEND_SUB
	{	qb_translate_basic_op,				&factory_multiply							},	// ZEND_MUL
	{	qb_translate_basic_op,				&factory_divide								},	// ZEND_DIV
	{	qb_translate_basic_op,				&factory_modulo								},	// ZEND_MOD
	{	qb_translate_basic_op,				&factory_shift_left							},	// ZEND_SL
	{	qb_translate_basic_op,				&factory_shift_right						},	// ZEND_SR
	{	qb_translate_basic_op,				&factory_concat								},	// ZEND_CONCAT
	{	qb_translate_basic_op,				&factory_bitwise_or							},	// ZEND_BW_OR
	{	qb_translate_basic_op,				&factory_bitwise_and						},	// ZEND_BW_AND
	{	qb_translate_basic_op,				&factory_bitwise_xor						},	// ZEND_BW_XOR
	{	qb_translate_basic_op,				&factory_bitwise_not						},	// ZEND_BW_NOT
	{	qb_translate_basic_op,				&factory_logical_not						},	// ZEND_BOOL_NOT
	{	qb_translate_basic_op,				&factory_logical_xor						},	// ZEND_BOOL_XOR
	{	qb_translate_basic_op,				&factory_identical							},	// ZEND_IS_IDENTICAL
	{	qb_translate_basic_op,				&factory_not_identical						},	// ZEND_IS_NOT_IDENTICAL
	{	qb_translate_basic_op,				&factory_equal								},	// ZEND_IS_EQUAL
	{	qb_translate_basic_op,				&factory_not_equal							},	// ZEND_IS_NOT_EQUAL
	{	qb_translate_basic_op,				&factory_less_than							},	// ZEND_IS_SMALLER
	{	qb_translate_basic_op,				&factory_less_equal							},	// ZEND_IS_SMALLER_OR_EQUAL
	{	qb_translate_cast_op,				factories_cast								},	// ZEND_CAST
	{	qb_translate_basic_op,				NULL						},	// ZEND_QM_ASSIGN
	{	qb_translate_combo_op,				factories_add_assign						},	// ZEND_ASSIGN_ADD
	{	qb_translate_combo_op,				factories_subtract_assign					},	// ZEND_ASSIGN_SUB
	{	qb_translate_combo_op,				factories_multiply_assign					},	// ZEND_ASSIGN_MUL
	{	qb_translate_combo_op,				factories_divide_assign						},	// ZEND_ASSIGN_DIV
	{	qb_translate_combo_op,				factories_modulo_assign						},	// ZEND_ASSIGN_MOD
	{	qb_translate_combo_op,				factories_shift_left_assign					},	// ZEND_ASSIGN_SL
	{	qb_translate_combo_op,				factories_shift_right_assign				},	// ZEND_ASSIGN_SR
	{	qb_translate_basic_op,				&factory_concat_assign						},	// ZEND_ASSIGN_CONCAT
	{	qb_translate_combo_op,				factories_bitwise_or_assign					},	// ZEND_ASSIGN_BW_OR
	{	qb_translate_combo_op,				factories_bitwise_and_assign				},	// ZEND_ASSIGN_BW_AND
	{	qb_translate_combo_op,				factories_bitwise_xor_assign				},	// ZEND_ASSIGN_BW_XOR
	{	qb_translate_basic_op,				&factory_increment_pre						},	// ZEND_PRE_INC
	{	qb_translate_basic_op,				&factory_decrement_pre						},	// ZEND_PRE_DEC
	{	qb_translate_basic_op,				&factory_increment_post						},	// ZEND_POST_INC
	{	qb_translate_basic_op,				&factory_decrement_post						},	// ZEND_POST_DEC
	{	qb_translate_basic_op,				&factory_assign								},	// ZEND_ASSIGN
	{	qb_translate_basic_op,				&factory_assign_ref							},	// ZEND_ASSIGN_REF
	{	qb_translate_basic_op,				&factory_echo								},	// ZEND_ECHO
	{	qb_translate_basic_op,				&factory_print								},	// ZEND_PRINT
	{	qb_translate_jump,					&factory_jump								},	// ZEND_JMP
	{	qb_translate_branch,				&factory_branch_on_false					},	// ZEND_JMPZ
	{	qb_translate_branch,				&factory_branch_on_true						},	// ZEND_JMPNZ
	{	qb_translate_for_loop,				&factory_branch_on_true						},	// ZEND_JMPZNZ
	{	qb_translate_branch,				&factory_branch_on_false					},	// ZEND_JMPZ_EX
	{	qb_translate_branch,				&factory_branch_on_true						},	// ZEND_JMPNZ_EX
	{	qb_translate_basic_op,				&factory_case								},	// ZEND_CASE
	{	qb_translate_basic_op,				&factory_free								},	// ZEND_SWITCH_FREE
	{	qb_translate_break,					&factory_jump								},	// ZEND_BRK
	{	qb_translate_continue,				&factory_jump								},	// ZEND_CONT
	{	qb_translate_basic_op,				&factory_boolean_cast						},	// ZEND_BOOL
	{	qb_translate_basic_op,				&factory_empty_string						},	// ZEND_INIT_STRING
	{	qb_translate_basic_op,				&factory_add_string							},	// ZEND_ADD_CHAR
	{	qb_translate_basic_op,				&factory_add_string							},	// ZEND_ADD_STRING
	{	qb_translate_basic_op,				&factory_add_variable						},	// ZEND_ADD_VAR
	{	qb_translate_basic_op,				NULL						},	// ZEND_BEGIN_SILENCE
	{	qb_translate_basic_op,				NULL						},	// ZEND_END_SILENCE
	{	qb_translate_init_function_call,	NULL										},	// ZEND_INIT_FCALL_BY_NAME
	{	qb_translate_function_call,			factories_fcall								},	// ZEND_DO_FCALL
	{	qb_translate_function_call_by_name,	factories_fcall								},	// ZEND_DO_FCALL_BY_NAME
	{	qb_translate_return,				&factory_return								},	// ZEND_RETURN
	{	qb_translate_receive_argument,		&factory_receive_argument					},	// ZEND_RECV
	{	qb_translate_receive_argument,		&factory_receive_argument					},	// ZEND_RECV_INIT
	{	qb_translate_basic_op,				&factory_send_argument						},	// ZEND_SEND_VAL
	{	qb_translate_basic_op,				&factory_send_argument						},	// ZEND_SEND_VAR
	{	qb_translate_basic_op,				&factory_send_argument						},	// ZEND_SEND_REF
	{	NULL,								NULL										},	// ZEND_NEW
	{	NULL,								NULL										},	// ZEND_INIT_NS_FCALL_BY_NAME
	{	qb_translate_basic_op,				&factory_free								},	// ZEND_FREE
	{	qb_translate_basic_op,				&factory_array_init							},	// ZEND_INIT_ARRAY
	{	qb_translate_basic_op,				&factory_array_append						},	// ZEND_ADD_ARRAY_ELEMENT
	{	NULL,								NULL										},	// ZEND_INCLUDE_OR_EVAL
	{	qb_translate_basic_op,				&factory_unset								},	// ZEND_UNSET_VAR
	{	qb_translate_basic_op,				&factory_unset_array_element				},	// ZEND_UNSET_DIM
	{	qb_translate_basic_op,				&factory_unset_object_property				},	// ZEND_UNSET_OBJ
	{	qb_translate_basic_op,				&factory_foreach_reset						},	// ZEND_FE_RESET
	{	qb_translate_foreach_fetch,			&factory_foreach_fetch						},	// ZEND_FE_FETCH
	{	qb_translate_exit,					&factory_exit								},	// ZEND_EXIT
	{	qb_translate_fetch,					factories_fetch_variable					},	// ZEND_FETCH_R
	{	qb_translate_basic_op,				&factory_fetch_array_element_read			},	// ZEND_FETCH_DIM_R
	{	qb_translate_basic_op,				&factory_fetch_object_property_read			},	// ZEND_FETCH_OBJ_R
	{	qb_translate_fetch,					factories_fetch_variable					},	// ZEND_FETCH_W
	{	qb_translate_basic_op,				&factory_fetch_array_element_write			},	// ZEND_FETCH_DIM_W
	{	qb_translate_basic_op,				&factory_fetch_object_property_write		},	// ZEND_FETCH_OBJ_W
	{	qb_translate_fetch,					factories_fetch_variable					},	// ZEND_FETCH_RW
	{	qb_translate_basic_op,				&factory_fetch_array_element_write			},	// ZEND_FETCH_DIM_RW
	{	qb_translate_basic_op,				&factory_fetch_object_property_write		},	// ZEND_FETCH_OBJ_RW
	{	qb_translate_fetch,					factories_fetch_variable					},	// ZEND_FETCH_IS
	{	qb_translate_basic_op,				&factory_fetch_array_element_isset			},	// ZEND_FETCH_DIM_IS
	{	qb_translate_basic_op,				&factory_fetch_object_property_isset		},	// ZEND_FETCH_OBJ_IS
	{	qb_translate_fetch,					factories_fetch_variable					},	// ZEND_FETCH_FUNC_ARG

	// TODO: fix this so the correct factory is used when the function accepts reference
	{	qb_translate_basic_op,				&factory_fetch_array_element_read			},	// ZEND_FETCH_DIM_FUNC_ARG
	{	qb_translate_basic_op,				&factory_fetch_object_property_read			},	// ZEND_FETCH_OBJ_FUNC_ARG

	{	qb_translate_fetch,					factories_fetch_variable					},	// ZEND_FETCH_UNSET
	{	qb_translate_basic_op,				&factory_fetch_array_element_isset			},	// ZEND_FETCH_DIM_UNSET
	{	qb_translate_basic_op,				&factory_fetch_object_property_isset		},	// ZEND_FETCH_OBJ_UNSET
	{	qb_translate_basic_op,				&factory_fetch_array_element_read			},	// ZEND_FETCH_DIM_TMP_VAR
	{	qb_translate_basic_op,				&factory_fetch_constant						},	// ZEND_FETCH_CONSTANT
	{	NULL,								NULL										},	// ZEND_GOTO
	{	qb_translate_extension_op,			&factory_ext				},	// ZEND_EXT_STMT
	{	qb_translate_extension_op,			&factory_ext				},	// ZEND_EXT_FCALL_BEGIN
	{	qb_translate_extension_op,			&factory_ext				},	// ZEND_EXT_FCALL_END
	{	qb_translate_extension_op,			&factory_ext				},	// ZEND_EXT_NOP
	{	qb_translate_basic_op,				&factory_nop								},	// ZEND_TICKS
	{	qb_translate_basic_op,				&factory_send_argument						},	// ZEND_SEND_VAR_NO_REF
	{	NULL,								NULL										},	// ZEND_CATCH
	{	NULL,								NULL										},	// ZEND_THROW
	{	qb_translate_fetch_class,			factories_fetch_class						},	// ZEND_FETCH_CLASS
	{	NULL,								NULL										},	// ZEND_CLONE
	{	NULL,								NULL										},	// ZEND_RETURN_BY_REF
	{	qb_translate_init_method_call,		NULL										},	// ZEND_INIT_METHOD_CALL
	{	qb_translate_init_method_call,		NULL										},	// ZEND_INIT_STATIC_METHOD_CALL
	{	qb_translate_basic_op,				&factory_boolean_cast						},	// ZEND_ISSET_ISEMPTY_VAR
	{	qb_translate_basic_op,				&factory_array_element_isset				},	// ZEND_ISSET_ISEMPTY_DIM_OBJ
	{	NULL,								NULL										},	// 116
	{	NULL,								NULL										},	// 117
	{	NULL,								NULL										},	// 118
	{	NULL,								NULL										},	// 119
	{	NULL,								NULL										},	// 120
	{	NULL,								NULL										},	// 121
	{	NULL,								NULL										},	// 122
	{	NULL,								NULL										},	// 123
	{	NULL,								NULL										},	// 124
	{	NULL,								NULL										},	// 125
	{	NULL,								NULL										},	// 126
	{	NULL,								NULL										},	// 127
	{	NULL,								NULL										},	// 128
	{	NULL,								NULL										},	// 129
	{	NULL,								NULL										},	// 130
	{	NULL,								NULL										},	// 131
	{	qb_translate_basic_op,				&factory_increment_object_property_pre		},	// ZEND_PRE_INC_OBJ
	{	qb_translate_basic_op,				&factory_decrement_object_property_pre		},	// ZEND_PRE_DEC_OBJ
	{	qb_translate_basic_op,				&factory_increment_object_property_post		},	// ZEND_POST_INC_OBJ
	{	qb_translate_basic_op,				&factory_decrement_object_property_post		},	// ZEND_POST_DEC_OBJ
	{	qb_translate_basic_op,				&factory_assign_object_property				},	// ZEND_ASSIGN_OBJ
	{	NULL,								NULL										},	// ZEND_OP_DATA
	{	NULL,								NULL										},	// ZEND_INSTANCEOF
	{	NULL,								NULL										},	// ZEND_DECLARE_CLASS
	{	NULL,								NULL										},	// ZEND_DECLARE_INHERITED_CLASS
	{	NULL,								NULL										},	// ZEND_DECLARE_FUNCTION
	{	NULL,								NULL										},	// ZEND_RAISE_ABSTRACT_ERROR
	{	NULL,								NULL										},	// ZEND_DECLARE_CONST
	{	NULL,								NULL										},	// ZEND_ADD_INTERFACE
	{	NULL,								NULL										},	// ZEND_DECLARE_INHERITED_CLASS_DELAYED
	{	NULL,								NULL										},	// ZEND_VERIFY_ABSTRACT_CLASS
	{	qb_translate_basic_op,				&factory_assign_array_element				},	// ZEND_ASSIGN_DIM
	{	qb_translate_basic_op,				&factory_object_property_isset				},	// ZEND_ISSET_ISEMPTY_PROP_OBJ
	{	NULL,								NULL										},	// ZEND_HANDLE_EXCEPTION
	{	qb_translate_user_opcode,			NULL						},	// ZEND_USER_OPCODE
	{	NULL,								NULL										},	// 151
	{	qb_translate_jump_set,				NULL						},	// ZEND_JMP_SET
	{	NULL,								NULL										},	// ZEND_DECLARE_LAMBDA_FUNCTION
	{	NULL,								NULL										},	// ZEND_ADD_TRAIT
	{	NULL,								NULL										},	// ZEND_BIND_TRAITS
	{	NULL,								NULL										},	// ZEND_SEPARATE
	{	qb_translate_basic_op,				NULL						},	// ZEND_QM_ASSIGN_VAR
	{	qb_translate_jump_set,				NULL						},	// ZEND_JMP_SET_VAR
	{	NULL,								NULL,										},	// ZEND_DISCARD_EXCEPTION
	{	NULL,								NULL,										},	// ZEND_YIELD
	{	NULL,								NULL,										},	// ZEND_GENERATOR_RETURN
	{	NULL,								NULL,										},	// ZEND_FAST_CALL
	{	NULL,								NULL,										},	// ZEND_FAST_RET
};

static void qb_translate_current_instruction(qb_php_translater_context *cxt) {
	if(cxt->zend_op->opcode != ZEND_OP_DATA && cxt->zend_op->opcode != qb_user_opcode) {
		USE_TSRM
		qb_operand operands[3], results[2];
		qb_result_prototype *result_prototype = &cxt->compiler_context->result_prototypes[cxt->zend_op_index];
		qb_php_op_translator *t;
		uint32_t operand_count = 0;
		int32_t result_count = RETURN_VALUE_USED(cxt->zend_op);
		uint32_t zend_opcode = cxt->zend_op->opcode;

		QB_G(current_line_number) = cxt->zend_op->lineno;

		// retrieve operands 
		if(qb_retrieve_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op->op1), &cxt->zend_op->op1, &operands[0])) {
			operand_count = 1;
		}
		if(qb_retrieve_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op->op2), &cxt->zend_op->op2, &operands[1])) {
			operand_count = 2;
		}

		// see whether the op returns a value
		if(result_count) {
			qb_retrieve_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op->result), &cxt->zend_op->result, &results[0]);
		} else {
			results[0].type = QB_OPERAND_NONE;
			results[0].address = NULL;
		}

		if(cxt->zend_op[1].opcode == ZEND_OP_DATA) {
			// retrieve the extra data
			if(qb_retrieve_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op[1].op1), &cxt->zend_op[1].op1, &operands[2])) {
				operand_count = 3;
			}
			if(qb_retrieve_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op[1].result), &cxt->zend_op[1].result, &results[1])) {
				result_count = 2;
			}
		}

		// look up the translator for this opcode
		if(EXPECTED(zend_opcode < sizeof(op_translators) / sizeof(op_translators[0]))) {
			t = &op_translators[zend_opcode];
		} else {
			t = NULL;
		}
		if(t && t->translate) {
			cxt->compiler_context->line_number = cxt->zend_op->lineno;
			t->translate(cxt, t->extra, operands, operand_count, &results[0], result_prototype);

			if(operand_count >= 1) {
				qb_retire_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op->op1), &cxt->zend_op->op1, &operands[0]);
				if(operand_count >= 2) {
					qb_retire_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op->op2), &cxt->zend_op->op2, &operands[1]);
					if(operand_count >= 3) {
						qb_retire_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op[1].op1), &cxt->zend_op[1].op1, &operands[0]);
					}
				}
			}
			if(result_count >= 1) {
				qb_retire_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op->result), &cxt->zend_op->result, &results[0]);
				if(result_count >= 2) {
					qb_retire_operand(cxt, Z_OPERAND_TYPE(cxt->zend_op[1].result), &cxt->zend_op[1].result, &results[1]);
				}
			}

			// lock operands kept as temporary variables
			qb_lock_temporary_variables(cxt->compiler_context);
		} else {
			qb_abort("Unsupported language feature");
		}
	}
}

static qb_intrinsic_function intrinsic_functions[] = {
	{	0,	"count",				1,		2,		&factory_fetch_array_size	},
	{	0,	"sizeof",				1,		2,		&factory_fetch_array_size	},
	{	0,	"strlen",				1,		2,		&factory_fetch_array_size	},
	{	0,	"int8",					1,		1,		&factory_cast_S08			},
	{	0,	"uint8",				1,		1,		&factory_cast_U08			},
	{	0,	"int16",				1,		1,		&factory_cast_S16			},
	{	0,	"uint16",				1,		1,		&factory_cast_U16			},
	{	0,	"int32",				1,		1,		&factory_cast_S32			},
	{	0,	"uint32",				1,		1,		&factory_cast_U32			},
	{	0,	"int64",				1,		2,		&factory_cast_S64			},
	{	0,	"uint64",				1,		2,		&factory_cast_U64			},
	{	0,	"float32",				1,		1,		&factory_cast_F32			},
	{	0,	"float64",				1,		1,		&factory_cast_F64			},
	{	0,	"defined",				1,		1,		&factory_defined			},
	{	0,	"define",				2,		2,		&factory_define				},
	{	0,	"equal",				2,		2,		&factory_set_equal			},
	{	0,	"not_equal",			2,		2,		&factory_set_not_equal		},
	{	0,	"less_than",			2,		2,		&factory_set_less_than		},
	{	0,	"less_than_equal",		2,		2,		&factory_set_less_equal		},
	{	0,	"greater_than",			2,		2,		&factory_set_greater_than	},
	{	0,	"greater_than_equal",	2,		2,		&factory_set_greater_equal	},
	{	0,	"min",					1,		-1,		&factory_min				},
	{	0,	"max",					1,		-1,		&factory_max				},
	{	0,	"any",					1,		1,		&factory_any				},
	{	0,	"all",					1,		1,		&factory_all				},
	{	0,	"not",					1,		1,		&factory_set_not			},
	{	0,	"abs",					1,		1,		&factory_abs				},
	{	0,	"sin",					1,		1,		&factory_sin				},
	{	0,	"asin",					1,		1,		&factory_asin				},
	{	0,	"cos",					1,		1,		&factory_cos				},
	{	0,	"acos",					1,		1,		&factory_acos				},
	{	0,	"tan",					1,		1,		&factory_tan				},
	{	0,	"atan",					1,		1,		&factory_atan				},
	{	0,	"sinh",					1,		1,		&factory_sinh				},
	{	0,	"asinh",				1,		1,		&factory_asinh				},
	{	0,	"cosh",					1,		1,		&factory_cosh				},
	{	0,	"acosh",				1,		1,		&factory_acosh				},
	{	0,	"tanh",					1,		1,		&factory_tanh				},
	{	0,	"atanh",				1,		1,		&factory_atanh				},
	{	0,	"ceil",					1,		1,		&factory_ceil				},
	{	0,	"floor",				1,		1,		&factory_floor				},
	{	0,	"round",				1,		3,		&factory_round				},
	{	0,	"log",					1,		1,		&factory_log				},
	{	0,	"log1p",				1,		1,		&factory_log1p				},
	{	0,	"log10",				1,		1,		&factory_log10				},
	{	0,	"log2",					1,		1,		&factory_log2				},
	{	0,	"exp",					1,		1,		&factory_exp				},
	{	0,	"exp2",					1,		1,		&factory_exp2				},
	{	0,	"expm1",				1,		1,		&factory_expm1				},
	{	0,	"sqrt",					1,		1,		&factory_sqrt				},
	{	0,	"pow",					2,		2,		&factory_pow				},
	{	0,	"fmod",					2,		2,		&factory_modulo				},
	{	0,	"mod",					2,		2,		&factory_floor_modulo		},
	{	0,	"lcg_value",			0,		0,		&factory_lcg				},
	{	0,	"is_finite",			1,		1,		&factory_is_finite			},
	{	0,	"is_infinite",			1,		1,		&factory_is_infinite		},
	{	0,	"is_nan",				1,		1,		&factory_is_nan				},
	{	0,	"rand",					0,		2,		&factory_rand				},
	{	0,	"mt_rand",				0,		2,		&factory_mt_rand			},
	{	0,	"hypot",				2,		2,		&factory_hypot				},
	{	0,	"deg2rad",				1,		1,		&factory_deg2rad			},
	{	0,	"rad2deg",				1,		1,		&factory_rad2deg			},
	{	0,	"sign",					1,		1,		&factory_sign				},
	{	0,	"rsqrt",				1,		1,		&factory_rsqrt				},
	{	0,	"fract",				1,		1,		&factory_fract				},
	{	0,	"step",					2,		2,		&factory_step				},
	{	0,	"clamp",				3,		3,		&factory_clamp				},
	{	0,	"mix",					3,		3,		&factory_mix				},
	{	0,	"smooth_step",			3,		3,		&factory_smooth_step		},
	{	0,	"normalize",			1,		1,		&factory_normalize			},
	{	0,	"length",				1,		1,		&factory_length				},
	{	0,	"distance",				2,		2,		&factory_distance			},
	{	0,	"dot",					2,		2,		&factory_dot_product		},
	{	0,	"cross",				2,		3,		&factory_cross_product		},
	{	0,	"faceforward",			2,		2,		&factory_faceforward		},
	{	0,	"reflect",				2,		2,		&factory_reflect			},
	{	0,	"refract",				3,		3,		&factory_refract			},
	{	0,	"mm_mult",				2,		2,		&factory_mm_mult			},
	{	0,	"mv_mult",				2,		2,		&factory_mv_mult			},
	{	0,	"vm_mult",				2,		2,		&factory_vm_mult			},
	{	0,	"transpose",			1,		1,		&factory_transpose			},
	{	0,	"det",					1,		1,		&factory_determinant		},
	{	0,	"inverse",				1,		1,		&factory_inverse			},
	{	0,	"mm_mult_cm",			2,		2,		&factory_mm_mult_cm			},
	{	0,	"mv_mult_cm",			2,		2,		&factory_mv_mult_cm			},
	{	0,	"vm_mult_cm",			2,		2,		&factory_vm_mult_cm			},
	{	0,	"transpose_cm",			1,		1,		&factory_transpose			},
	{	0,	"det_cm",				1,		1,		&factory_determinant		},
	{	0,	"inverse_cm",			1,		1,		&factory_inverse			},
	{	0,	"mm_mult_rm",			2,		2,		&factory_mm_mult_rm			},
	{	0,	"mv_mult_rm",			2,		2,		&factory_mv_mult_rm			},
	{	0,	"vm_mult_rm",			2,		2,		&factory_vm_mult_rm			},
	{	0,	"transpose_rm",			1,		1,		&factory_transpose			},
	{	0,	"det_rm",				1,		1,		&factory_determinant		},
	{	0,	"inverse_rm",			1,		1,		&factory_inverse			},
/*
	{	0,	"transform_cm",			2,		2,		&factory_transform_cm		},
	{	0,	"transform_rm",			2,		2,		&factory_transform_rm		},
	{	0,	"transform",			2,		2,		&factory_transform			},
*/
	{	0,	"sample_nearest",		3,		3,		&factory_sample_nearest		},
	{	0,	"sample_bilinear",		3,		3,		&factory_sample_bilinear	},
	{	0,	"blend",				2,		2,		&factory_alpha_blend		},

	{	0,	"array_column",			2,		2,		&factory_array_column		},
	{	0,	"array_diff",			2,		-1,		&factory_array_diff			},
/*
	{	0,	"array_fill",			3,		3,		&factory_array_fill			},
	{	0,	"array_filter",			1,		1,		&factory_array_diff			},
*/
	{	0,	"array_intersect",		2,		-1,		&factory_array_intersect	},
/*
	{	0,	"array_merge",			2,		-1,		&factory_array_merge		},
	{	0,	"array_pad",			3,		3,		&factory_array_pad			},
	{	0,	"array_pop",			1,		1,		NULL						},
*/
	{	0,	"array_pos",			2,		3,		&factory_array_pos			},
	{	0,	"array_product",		1,		1,		&factory_array_product		},
	{	0,	"array_push",			2,		-1,		&factory_array_push			},
/*
	{	0,	"array_rand",			1,		2,		&factory_array_rand			},
	{	0,	"array_resize",			2,		-1,		&factory_array_resize		},
*/
	{	0,	"array_reverse",		1,		1,		&factory_array_reverse		},
	{	0,	"array_rpos",			2,		3,		&factory_array_rpos			},
	{	0,	"array_search",			2,		2,		&factory_array_search		},
/*
	{	0,	"array_shift",			1,		1,		NULL						},
*/
	{	0,	"array_slice",			2,		3,		&factory_array_slice		},
/*
	{	0,	"array_splice",			2,		4,		NULL						},
*/
	{	0,	"array_sum",			1,		1,		&factory_array_sum			},
	{	0,	"array_unique",			1,		1,		&factory_array_unique		},
/*
	{	0,	"array_unshift",		2,		-1,		NULL						},
	{	0,	"in_array",				2,		2,		&factory_in_array			},
	{	0,	"range",				2,		3,		&factory_range				},
*/
	{	0,	"rsort",				1,		1,		&factory_rsort				},
	{	0,	"shuffle",				1,		1,		&factory_shuffle			},
	{	0,	"sort",					1,		1,		&factory_sort				},
	{	0,	"substr",				2,		3,		&factory_array_slice		},
	{	0,	"strpos",				2,		3,		&factory_array_pos			},
	{	0,	"strrpos",				2,		3,		&factory_array_rpos			},
	{	0,	"pack_le",				1,		2,		&factory_pack_le			},
	{	0,	"pack_be",				1,		2,		&factory_pack_be			},
	{	0,	"unpack_le",			1,		3,		&factory_unpack_le			},
	{	0,	"unpack_be",			1,		3,		&factory_unpack_be			},
	{	0,	"utf8_decode",			1,		1,		&factory_utf8_decode		},
	{	0,	"utf8_encode",			1,		1,		&factory_utf8_encode		},
	{	0,	"cabs",					1,		1,		&factory_complex_abs		},
	{	0,	"carg",					1,		1,		&factory_complex_arg		},
	{	0,	"cmult",				2,		2,		&factory_complex_multiply	},
	{	0,	"cdiv",					2,		2,		&factory_complex_divide		},
	{	0,	"cexp",					1,		1,		&factory_complex_exp		},
	{	0,	"clog",					1,		1,		&factory_complex_log		},
	{	0,	"csqrt",				1,		1,		&factory_complex_sqrt		},
	{	0,	"cpow",					2,		2,		&factory_complex_pow		},
	{	0,	"csin",					1,		1,		&factory_complex_sin		},
	{	0,	"ccos",					1,		1,		&factory_complex_cos		},
	{	0,	"ctan",					1,		1,		&factory_complex_tan		},
	{	0,	"csinh",				1,		1,		&factory_complex_sinh		},
	{	0,	"ccosh",				1,		1,		&factory_complex_cosh		},
	{	0,	"ctanh",				1,		1,		&factory_complex_tanh		},
	{	0,	"rgb2hsv",				1,		1,		&factory_rgb2hsv			},
	{	0,	"hsv2rgb",				1,		1,		&factory_hsv2rgb			},
	{	0,	"rgb2hsl",				1,		1,		&factory_rgb2hsl			},
	{	0,	"hsl2rgb",				1,		1,		&factory_hsl2rgb			},
	{	0,	"rgb_premult",			1,		1,		&factory_apply_premult		},
	{	0,	"rgb_demult",			1,		1,		&factory_remove_premult		},

	// unsupported functions
	{	0,	"compact",				0,		-1,		NULL						},
	{	0,	"extract",				0,		-1,		NULL						},
	{	0,	"each",					0,		-1,		NULL						},
	{	0,	"pos"	,				0,		-1,		NULL						},
	{	0,	"key"	,				0,		-1,		NULL						},
	{	0,	"current",				0,		-1,		NULL						},
	{	0,	"prev",					0,		-1,		NULL						},
	{	0,	"next",					0,		-1,		NULL						},
	{	0,	"reset",				0,		-1,		NULL						},
	{	0,	"end",					0,		-1,		NULL						},
	{	0,	"array_map",			0,		-1,		NULL						},
	{	0,	"array_reduce",			0,		-1,		NULL						},
	{	0,	"array_walk",			0,		-1,		NULL						},
};

#define MAX_INLINE_FUNCTION_NAME_LEN		32

qb_intrinsic_function * qb_find_intrinsic_function(qb_php_translater_context *cxt, zval *callable) {
	const char *name = Z_STRVAL_P(callable);
	uint32_t len = Z_STRLEN_P(callable);

	if(len < MAX_INLINE_FUNCTION_NAME_LEN) {
		// make it lower case
		char name_buffer[MAX_INLINE_FUNCTION_NAME_LEN];
		uint32_t i;
		ulong hash_value;

		for(i = 0; i < len; i++) {
			name_buffer[i] = tolower(name[i]);
		}
		name_buffer[i] = '\0';

		// calculate the hash value and look up the function
		hash_value = zend_hash_func(name_buffer, len + 1);
		for(i = 0; i < sizeof(intrinsic_functions) / sizeof(qb_intrinsic_function); i++) {
			qb_intrinsic_function *f = &intrinsic_functions[i];
			if(f->hash_value == hash_value && strcmp(name_buffer, f->name) == 0) {
				return f;
			}
		}
	}
	return NULL;
}

static void qb_translate_instruction_range(qb_php_translater_context *cxt, uint32_t start_index, uint32_t end_index) {
	uint32_t zend_op_index;

	// save states
	zend_op_index = cxt->zend_op_index;

	// process zend instructions until we reach the end 
	// or if an instruction is already translated
	cxt->zend_op_index = start_index;
	cxt->zend_op = ZEND_OP(start_index);
	for(;;) {
		if(cxt->compiler_context->stage == QB_STAGE_RESULT_TYPE_RESOLUTION) {
			if(cxt->compiler_context->result_prototypes[cxt->zend_op_index].preliminary_type != QB_TYPE_UNKNOWN) {
				// the result prototype has been built already
				break;
			}
			qb_translate_current_instruction(cxt);
		} else if(cxt->compiler_context->stage == QB_STAGE_OPCODE_TRANSLATION) {
			int32_t is_jump_target;
			uint32_t current_op_count;

			if(cxt->compiler_context->op_translations[cxt->zend_op_index] != QB_OP_INDEX_NONE && cxt->compiler_context->op_translations[cxt->zend_op_index] != QB_OP_INDEX_JUMP_TARGET) {
				// instruction has already been translated--do a jump there and exit
				qb_invalidate_all_on_demand_expressions(cxt->compiler_context);
				qb_create_op(cxt->compiler_context, &factory_jump, NULL, 0, NULL, &cxt->zend_op_index, 1, FALSE);
				break;
			}

			// see if the next op is going to be a jump target
			is_jump_target = (cxt->compiler_context->op_translations[cxt->zend_op_index] == QB_OP_INDEX_JUMP_TARGET);
			if(is_jump_target) {
				// the result of on-demand expressions might no longer be valid
				qb_invalidate_all_on_demand_expressions(cxt->compiler_context);
			}

			// translate the current instruction, saving the op-count
			// so we know where the first resulting new op is
			current_op_count = cxt->compiler_context->op_count;
			qb_translate_current_instruction(cxt);

			// add a nop if new one wasn't generated
			if(current_op_count == cxt->compiler_context->op_count) {
				qb_create_op(cxt->compiler_context, &factory_nop, NULL, 0, NULL, 0, 0, TRUE);
			}

			// flag the first new op as a jump target 
			if(is_jump_target) {
				qb_op *first_op = cxt->compiler_context->ops[current_op_count];
				first_op->flags |= QB_OP_JUMP_TARGET;
			}
			cxt->compiler_context->op_translations[cxt->zend_op_index] = current_op_count;
		}

		// see if it was a branch or a jump
		if(cxt->jump_target_index1) {
			uint32_t target_index1 = cxt->jump_target_index1;
			uint32_t target_index2 = cxt->jump_target_index2;
			cxt->jump_target_index1 = 0;
			cxt->jump_target_index2 = 0;
			if(target_index1 == QB_INSTRUCTION_END) {
				break;
			} else if(target_index2) {
				qb_translate_instruction_range(cxt, target_index1, target_index2);
			} else {
				cxt->zend_op = ZEND_OP(target_index1);
				cxt->zend_op_index = target_index1;
			}
			if(target_index2) {
				cxt->zend_op = ZEND_OP(target_index2);
				cxt->zend_op_index = target_index2;
			}
		} else {
			cxt->zend_op++;
			cxt->zend_op_index++;
		}
		if(cxt->zend_op_index == end_index) {
			break;
		}
	}

	// restore the state
	cxt->zend_op = ZEND_OP(zend_op_index);
	cxt->zend_op_index = zend_op_index;
}

void qb_survey_instructions(qb_php_translater_context *cxt) {
	uint32_t i;
	cxt->compiler_context->op_translations = qb_allocate_indices(cxt->pool, cxt->zend_op_array->last);
	memset(cxt->compiler_context->op_translations, 0xFF, cxt->zend_op_array->last * sizeof(uint32_t));

	if(cxt->zend_op_array->T > 0) {
		qb_attach_new_array(cxt->pool, (void **) &cxt->compiler_context->temp_variables, &cxt->compiler_context->temp_variable_count, sizeof(qb_temporary_variable), cxt->zend_op_array->T);
		qb_enlarge_array((void **) &cxt->compiler_context->temp_variables, cxt->zend_op_array->T);

		// set the operand type to EMPTY (which is somewhat different from NONE)
		for(i = 0; i < cxt->compiler_context->temp_variable_count; i++) {
			qb_temporary_variable *temp_variable = &cxt->compiler_context->temp_variables[i];
			temp_variable->operand.type = QB_OPERAND_EMPTY;
		}
	}

	qb_attach_new_array(cxt->pool, (void **) &cxt->compiler_context->result_prototypes, &cxt->compiler_context->result_prototype_count, sizeof(qb_result_prototype), cxt->zend_op_array->last);
	qb_enlarge_array((void **) &cxt->compiler_context->result_prototypes, cxt->zend_op_array->last);
	for(i = 0; i < cxt->compiler_context->result_prototype_count; i++) {
		qb_result_prototype *prototype = &cxt->compiler_context->result_prototypes[i];
		prototype->preliminary_type = QB_TYPE_UNKNOWN;
		prototype->final_type = QB_TYPE_UNKNOWN;
	}

	// scan through the opcodes to determine the type of each expression
	cxt->compiler_context->stage = QB_STAGE_RESULT_TYPE_RESOLUTION;
	qb_translate_instruction_range(cxt, 0, cxt->zend_op_array->last);
}

void qb_translate_instructions(qb_php_translater_context *cxt) {
	uint32_t i;
	for(i = 0; i < cxt->compiler_context->temp_variable_count; i++) {
		qb_temporary_variable *temp_variable = &cxt->compiler_context->temp_variables[i];
		temp_variable->operand.type = QB_OPERAND_EMPTY;
		temp_variable->operand.generic_pointer = NULL;
	}

	cxt->compiler_context->stage = QB_STAGE_OPCODE_TRANSLATION;
	qb_translate_instruction_range(cxt, 0, cxt->zend_op_array->last);

	// make sure there's always a RET at the end
	if(cxt->compiler_context->op_count == 0 || cxt->compiler_context->ops[cxt->compiler_context->op_count - 1]->opcode != QB_RET) {
		qb_operand operand = { QB_OPERAND_EMPTY, NULL };
		qb_create_op(cxt->compiler_context, &factory_return, &operand, 1, NULL, NULL, 0, FALSE);
	}
}

void qb_initialize_php_translater_context(qb_php_translater_context *cxt, qb_compiler_context *compiler_cxt TSRMLS_DC) {
	static int hash_initialized = FALSE;
	if(!hash_initialized) {
		// calculate the hash of intrinsic function names for quicker look-ups
		uint32_t i;
		for(i = 0; i < sizeof(intrinsic_functions) / sizeof(qb_intrinsic_function); i++) {
			qb_intrinsic_function *f = &intrinsic_functions[i];
			f->hash_value = zend_hash_func(f->name, strlen(f->name) + 1);
		}
		hash_initialized = TRUE;
	}

	cxt->pool = compiler_cxt->pool;
	cxt->compiler_context = compiler_cxt;
	cxt->zend_op_index = 0;
	cxt->jump_target_index1 = 0;
	cxt->jump_target_index2 = 0;
	cxt->silence = 0;
	cxt->foreach_index_address = NULL;

	if(compiler_cxt->function_declaration) {
		cxt->zend_op_array = compiler_cxt->zend_op_array;
	}
	SAVE_TSRMLS
}

void qb_free_php_translater_context(qb_php_translater_context *cxt) {
}
