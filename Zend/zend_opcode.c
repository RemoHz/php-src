/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) 1998-2014 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        | 
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@zend.com>                                |
   |          Zeev Suraski <zeev@zend.com>                                |
   +----------------------------------------------------------------------+
*/

/* $Id$ */

#include <stdio.h>

#include "zend.h"
#include "zend_alloc.h"
#include "zend_compile.h"
#include "zend_extensions.h"
#include "zend_API.h"

#include "zend_vm.h"

static void zend_extension_op_array_ctor_handler(zend_extension *extension, zend_op_array *op_array TSRMLS_DC)
{
	if (extension->op_array_ctor) {
		extension->op_array_ctor(op_array);
	}
}

static void zend_extension_op_array_dtor_handler(zend_extension *extension, zend_op_array *op_array TSRMLS_DC)
{
	if (extension->op_array_dtor) {
		extension->op_array_dtor(op_array);
	}
}

static void op_array_alloc_ops(zend_op_array *op_array, uint32_t size)
{
	op_array->opcodes = erealloc(op_array->opcodes, size * sizeof(zend_op));
}

void init_op_array(zend_op_array *op_array, zend_uchar type, int initial_ops_size TSRMLS_DC)
{
	op_array->type = type;

	op_array->refcount = (uint32_t *) emalloc(sizeof(uint32_t));
	*op_array->refcount = 1;
	op_array->last = 0;
	op_array->opcodes = NULL;
	op_array_alloc_ops(op_array, initial_ops_size);

	op_array->last_var = 0;
	op_array->vars = NULL;

	op_array->T = 0;

	op_array->function_name = NULL;
	op_array->filename = zend_get_compiled_filename(TSRMLS_C);
	op_array->doc_comment = NULL;

	op_array->arg_info = NULL;
	op_array->num_args = 0;
	op_array->required_num_args = 0;

	op_array->scope = NULL;
	op_array->prototype = NULL;

	op_array->brk_cont_array = NULL;
	op_array->try_catch_array = NULL;
	op_array->last_brk_cont = 0;

	op_array->static_variables = NULL;
	op_array->last_try_catch = 0;

	op_array->this_var = -1;

	op_array->fn_flags = 0;

	op_array->early_binding = -1;

	op_array->last_literal = 0;
	op_array->literals = NULL;

	op_array->run_time_cache = NULL;
	op_array->last_cache_slot = 0;

	memset(op_array->reserved, 0, ZEND_MAX_RESERVED_RESOURCES * sizeof(void*));

	zend_llist_apply_with_argument(&zend_extensions, (llist_apply_with_arg_func_t) zend_extension_op_array_ctor_handler, op_array TSRMLS_CC);
}

ZEND_API void destroy_zend_function(zend_function *function TSRMLS_DC)
{
	switch (function->type) {
		case ZEND_USER_FUNCTION:
			destroy_op_array((zend_op_array *) function TSRMLS_CC);
			break;
		case ZEND_INTERNAL_FUNCTION:
			if (function->common.function_name) {
				zend_string_release(function->common.function_name);
			}
			/* do nothing */
			break;
	}
}

ZEND_API void zend_function_dtor(zval *zv)
{
	zend_function *function = Z_PTR_P(zv);
	TSRMLS_FETCH();

	destroy_zend_function(function TSRMLS_CC);
	if (function->type == ZEND_INTERNAL_FUNCTION) {
		pefree(function, 1);
	} else if (!function->common.function_name) {
		efree_size(function, sizeof(zend_op_array));
	}
}

ZEND_API void zend_cleanup_op_array_data(zend_op_array *op_array)
{
	if (op_array->static_variables) {
		zend_hash_clean(op_array->static_variables);
	}
}

ZEND_API void zend_cleanup_user_class_data(zend_class_entry *ce TSRMLS_DC)
{
	/* Clean all parts that can contain run-time data */
	/* Note that only run-time accessed data need to be cleaned up, pre-defined data can
	   not contain objects and thus are not probelmatic */
	if (ce->ce_flags & ZEND_HAS_STATIC_IN_METHODS) {
		zend_function *func;

		ZEND_HASH_FOREACH_PTR(&ce->function_table, func) {
			if (func->type == ZEND_USER_FUNCTION) {
				zend_cleanup_op_array_data((zend_op_array *) func);
			}
		} ZEND_HASH_FOREACH_END();
	}
	if (ce->static_members_table) {
		int i;

		for (i = 0; i < ce->default_static_members_count; i++) {
			if (Z_TYPE(ce->static_members_table[i]) != IS_UNDEF) {
				zval tmp;

				ZVAL_COPY_VALUE(&tmp, &ce->static_members_table[i]);
				ZVAL_UNDEF(&ce->static_members_table[i]);
				zval_ptr_dtor(&tmp);
			}
		}
		ce->static_members_table = NULL;
	}
}

ZEND_API void zend_cleanup_internal_class_data(zend_class_entry *ce TSRMLS_DC)
{
	if (CE_STATIC_MEMBERS(ce)) {
		int i;
		
		for (i = 0; i < ce->default_static_members_count; i++) {
			zval_ptr_dtor(&CE_STATIC_MEMBERS(ce)[i]);
		}
		efree(CE_STATIC_MEMBERS(ce));
#ifdef ZTS
		CG(static_members_table)[(zend_intptr_t)(ce->static_members_table)] = NULL;
#else
		ce->static_members_table = NULL;
#endif
	}
}

void _destroy_zend_class_traits_info(zend_class_entry *ce)
{
	if (ce->num_traits > 0 && ce->traits) {
		efree(ce->traits);
	}
	
	if (ce->trait_aliases) {
		size_t i = 0;
		while (ce->trait_aliases[i]) {
			if (ce->trait_aliases[i]->trait_method) {
				if (ce->trait_aliases[i]->trait_method->method_name) {
	 				zend_string_release(ce->trait_aliases[i]->trait_method->method_name);
				}
				if (ce->trait_aliases[i]->trait_method->class_name) {
	 				zend_string_release(ce->trait_aliases[i]->trait_method->class_name);
				}
				efree(ce->trait_aliases[i]->trait_method);
			}
			
			if (ce->trait_aliases[i]->alias) {
				zend_string_release(ce->trait_aliases[i]->alias);
			}
			
			efree(ce->trait_aliases[i]);
			i++;
		}
		
		efree(ce->trait_aliases);
	}

	if (ce->trait_precedences) {
		size_t i = 0;
		
		while (ce->trait_precedences[i]) {
			zend_string_release(ce->trait_precedences[i]->trait_method->method_name);
			zend_string_release(ce->trait_precedences[i]->trait_method->class_name);
			efree(ce->trait_precedences[i]->trait_method);

			if (ce->trait_precedences[i]->exclude_from_classes) {
				efree(ce->trait_precedences[i]->exclude_from_classes);
			}

			efree(ce->trait_precedences[i]);
			i++;
		}
		efree(ce->trait_precedences);
	}
}

ZEND_API void destroy_zend_class(zval *zv)
{
	zend_class_entry *ce = Z_PTR_P(zv);
	
	if (--ce->refcount > 0) {
		return;
	}
	switch (ce->type) {
		case ZEND_USER_CLASS:
			if (ce->default_properties_table) {
				int i;

				for (i = 0; i < ce->default_properties_count; i++) {
					if (Z_TYPE(ce->default_properties_table[i]) != IS_UNDEF) {
						zval_ptr_dtor(&ce->default_properties_table[i]);
				    }
				}
				efree(ce->default_properties_table);
			}
			if (ce->default_static_members_table) {
				int i;

				for (i = 0; i < ce->default_static_members_count; i++) {
					if (Z_TYPE(ce->default_static_members_table[i]) != IS_UNDEF) {
						zval_ptr_dtor(&ce->default_static_members_table[i]);
					}
				}
				efree(ce->default_static_members_table);
			}
			zend_hash_destroy(&ce->properties_info);
			zend_string_release(ce->name);
			zend_hash_destroy(&ce->function_table);
			zend_hash_destroy(&ce->constants_table);
			if (ce->num_interfaces > 0 && ce->interfaces) {
				efree(ce->interfaces);
			}
			if (ce->info.user.doc_comment) {
				zend_string_release(ce->info.user.doc_comment);
			}
			
			_destroy_zend_class_traits_info(ce);
			
			break;
		case ZEND_INTERNAL_CLASS:
			if (ce->default_properties_table) {
				int i;

				for (i = 0; i < ce->default_properties_count; i++) {
					if (Z_TYPE(ce->default_properties_table[i]) != IS_UNDEF) {
						zval_internal_ptr_dtor(&ce->default_properties_table[i]);
					}
				}
				free(ce->default_properties_table);
			}
			if (ce->default_static_members_table) {
				int i;

				for (i = 0; i < ce->default_static_members_count; i++) {
					zval_internal_ptr_dtor(&ce->default_static_members_table[i]);
				}
				free(ce->default_static_members_table);
			}
			zend_hash_destroy(&ce->properties_info);
			zend_string_release(ce->name);
			zend_hash_destroy(&ce->function_table);
			zend_hash_destroy(&ce->constants_table);
			if (ce->num_interfaces > 0) {
				free(ce->interfaces);
			}
			free(ce);
			break;
	}
}

void zend_class_add_ref(zval *zv)
{
	zend_class_entry *ce = Z_PTR_P(zv);

	ce->refcount++;
}

ZEND_API void destroy_op_array(zend_op_array *op_array TSRMLS_DC)
{
	zval *literal = op_array->literals;
	zval *end;
	uint32_t i;

	if (op_array->static_variables) {
		zend_hash_destroy(op_array->static_variables);
		FREE_HASHTABLE(op_array->static_variables);
	}

	if (op_array->run_time_cache && !op_array->function_name) {
		efree(op_array->run_time_cache);
	}

	if (--(*op_array->refcount)>0) {
		return;
	}

	efree_size(op_array->refcount, sizeof(*(op_array->refcount)));

	if (op_array->vars) {
		i = op_array->last_var;
		while (i > 0) {
			i--;
			zend_string_release(op_array->vars[i]);
		}
		efree(op_array->vars);
	}

	if (literal) {
	 	end = literal + op_array->last_literal;
	 	while (literal < end) {
			zval_dtor(literal);
			literal++;
		}
		efree(op_array->literals);
	}
	efree(op_array->opcodes);

	if (op_array->function_name) {
		zend_string_release(op_array->function_name);
	}
	if (op_array->doc_comment) {
		zend_string_release(op_array->doc_comment);
	}
	if (op_array->brk_cont_array) {
		efree(op_array->brk_cont_array);
	}
	if (op_array->try_catch_array) {
		efree(op_array->try_catch_array);
	}
	if (op_array->fn_flags & ZEND_ACC_DONE_PASS_TWO) {
		zend_llist_apply_with_argument(&zend_extensions, (llist_apply_with_arg_func_t) zend_extension_op_array_dtor_handler, op_array TSRMLS_CC);
	}
	if (op_array->arg_info) {
		for (i=0; i<op_array->num_args; i++) {
			efree((char*)op_array->arg_info[i].name);
			if (op_array->arg_info[i].class_name) {
				efree((char*)op_array->arg_info[i].class_name);
			}
		}
		efree(op_array->arg_info);
	}
}

void init_op(zend_op *op TSRMLS_DC)
{
	memset(op, 0, sizeof(zend_op));
	op->lineno = CG(zend_lineno);
	SET_UNUSED(op->result);
}

zend_op *get_next_op(zend_op_array *op_array TSRMLS_DC)
{
	uint32_t next_op_num = op_array->last++;
	zend_op *next_op;

	if (next_op_num >= CG(context).opcodes_size) {
		CG(context).opcodes_size *= 4;
		op_array_alloc_ops(op_array, CG(context).opcodes_size);
	}
	
	next_op = &(op_array->opcodes[next_op_num]);
	
	init_op(next_op TSRMLS_CC);

	return next_op;
}

int get_next_op_number(zend_op_array *op_array)
{
	return op_array->last;
}

zend_brk_cont_element *get_next_brk_cont_element(zend_op_array *op_array)
{
	op_array->last_brk_cont++;
	op_array->brk_cont_array = erealloc(op_array->brk_cont_array, sizeof(zend_brk_cont_element)*op_array->last_brk_cont);
	return &op_array->brk_cont_array[op_array->last_brk_cont-1];
}

static void zend_update_extended_info(zend_op_array *op_array TSRMLS_DC)
{
	zend_op *opline = op_array->opcodes, *end=opline+op_array->last;

	while (opline<end) {
		if (opline->opcode == ZEND_EXT_STMT) {
			if (opline+1<end) {
				if ((opline+1)->opcode == ZEND_EXT_STMT) {
					opline->opcode = ZEND_NOP;
					opline++;
					continue;
				}
				if (opline+1<end) {
					opline->lineno = (opline+1)->lineno;
				}
			} else {
				opline->opcode = ZEND_NOP;
			}
		}
		opline++;
	}
}

static void zend_extension_op_array_handler(zend_extension *extension, zend_op_array *op_array TSRMLS_DC)
{
	if (extension->op_array_handler) {
		extension->op_array_handler(op_array);
	}
}

static void zend_check_finally_breakout(zend_op_array *op_array, uint32_t op_num, uint32_t dst_num TSRMLS_DC)
{
	uint32_t i;

	for (i = 0; i < op_array->last_try_catch; i++) {
		if ((op_num < op_array->try_catch_array[i].finally_op ||
					op_num >= op_array->try_catch_array[i].finally_end)
				&& (dst_num >= op_array->try_catch_array[i].finally_op &&
					 dst_num <= op_array->try_catch_array[i].finally_end)) {
			CG(in_compilation) = 1;
			CG(active_op_array) = op_array;
			CG(zend_lineno) = op_array->opcodes[op_num].lineno;
			zend_error_noreturn(E_COMPILE_ERROR, "jump into a finally block is disallowed");
		} else if ((op_num >= op_array->try_catch_array[i].finally_op 
					&& op_num <= op_array->try_catch_array[i].finally_end)
				&& (dst_num > op_array->try_catch_array[i].finally_end 
					|| dst_num < op_array->try_catch_array[i].finally_op)) {
			CG(in_compilation) = 1;
			CG(active_op_array) = op_array;
			CG(zend_lineno) = op_array->opcodes[op_num].lineno;
			zend_error_noreturn(E_COMPILE_ERROR, "jump out of a finally block is disallowed");
		}
	} 
}

static void zend_adjust_fast_call(zend_op_array *op_array, uint32_t fast_call, uint32_t start, uint32_t end TSRMLS_DC)
{
	int i;
	uint32_t op_num = 0;

	for (i = 0; i < op_array->last_try_catch; i++) {
		if (op_array->try_catch_array[i].finally_op > start 
				&& op_array->try_catch_array[i].finally_end < end) {
			op_num = op_array->try_catch_array[i].finally_op;
			start = op_array->try_catch_array[i].finally_end;
		}
	}

	if (op_num) {
		/* Must be ZEND_FAST_CALL */
		ZEND_ASSERT(op_array->opcodes[op_num - 2].opcode == ZEND_FAST_CALL);
		op_array->opcodes[op_num - 2].extended_value = ZEND_FAST_CALL_FROM_FINALLY;
		op_array->opcodes[op_num - 2].op2.opline_num = fast_call;
	}
}

static void zend_resolve_fast_call(zend_op_array *op_array, uint32_t fast_call, uint32_t op_num TSRMLS_DC)
{
	int i;
	uint32_t finally_op_num = 0;

	for (i = 0; i < op_array->last_try_catch; i++) {
		if (op_num >= op_array->try_catch_array[i].finally_op
				&& op_num < op_array->try_catch_array[i].finally_end) {
			finally_op_num = op_array->try_catch_array[i].finally_op;
		}
	}

	if (finally_op_num) {
		/* Must be ZEND_FAST_CALL */
		ZEND_ASSERT(op_array->opcodes[finally_op_num - 2].opcode == ZEND_FAST_CALL);
		if (op_array->opcodes[fast_call].extended_value == 0) {
			op_array->opcodes[fast_call].extended_value = ZEND_FAST_CALL_FROM_FINALLY;
			op_array->opcodes[fast_call].op2.opline_num = finally_op_num - 2;
		}
	} 
}

static void zend_resolve_finally_call(zend_op_array *op_array, uint32_t op_num, uint32_t dst_num TSRMLS_DC)
{
	uint32_t start_op;
	zend_op *opline;
	uint32_t i = op_array->last_try_catch;

	if (dst_num != (uint32_t)-1) {
		zend_check_finally_breakout(op_array, op_num, dst_num TSRMLS_CC);
	}

	/* the backward order is mater */
	while (i > 0) {
		i--;
		if (op_array->try_catch_array[i].finally_op &&
		    op_num >= op_array->try_catch_array[i].try_op &&
		    op_num < op_array->try_catch_array[i].finally_op - 1 &&
		    (dst_num < op_array->try_catch_array[i].try_op ||
		     dst_num > op_array->try_catch_array[i].finally_end)) {
			/* we have a jump out of try block that needs executing finally */

			/* generate a FAST_CALL to finally block */
		    start_op = get_next_op_number(op_array);

			opline = get_next_op(op_array TSRMLS_CC);
			opline->opcode = ZEND_FAST_CALL;
			SET_UNUSED(opline->op1);
			SET_UNUSED(opline->op2);
			zend_adjust_fast_call(op_array, start_op,
					op_array->try_catch_array[i].finally_op,
					op_array->try_catch_array[i].finally_end TSRMLS_CC);
			if (op_array->try_catch_array[i].catch_op) {
				opline->extended_value = ZEND_FAST_CALL_FROM_CATCH;
				opline->op2.opline_num = op_array->try_catch_array[i].catch_op;
				opline->op1.opline_num = get_next_op_number(op_array);
				/* generate a FAST_CALL to hole CALL_FROM_FINALLY */
				opline = get_next_op(op_array TSRMLS_CC);
				opline->opcode = ZEND_FAST_CALL;
				SET_UNUSED(opline->op1);
				SET_UNUSED(opline->op2);
				zend_resolve_fast_call(op_array, start_op + 1, op_array->try_catch_array[i].finally_op - 2 TSRMLS_CC);
			} else {
				zend_resolve_fast_call(op_array, start_op, op_array->try_catch_array[i].finally_op - 2 TSRMLS_CC);
			}
			opline->op1.opline_num = op_array->try_catch_array[i].finally_op;

			/* generate a sequence of FAST_CALL to upward finally block */
			while (i > 0) {
				i--;
				if (op_array->try_catch_array[i].finally_op &&
					op_num >= op_array->try_catch_array[i].try_op &&
					op_num < op_array->try_catch_array[i].finally_op - 1 &&
					(dst_num < op_array->try_catch_array[i].try_op ||
					 dst_num > op_array->try_catch_array[i].finally_end)) {

					opline = get_next_op(op_array TSRMLS_CC);
					opline->opcode = ZEND_FAST_CALL;
					SET_UNUSED(opline->op1);
					SET_UNUSED(opline->op2);
					opline->op1.opline_num = op_array->try_catch_array[i].finally_op;
				}
			}

			/* Finish the sequence with original opcode */
			opline = get_next_op(op_array TSRMLS_CC);
			*opline = op_array->opcodes[op_num];

			/* Replace original opcode with jump to this sequence */
			opline = op_array->opcodes + op_num;
			opline->opcode = ZEND_JMP;
			SET_UNUSED(opline->op1);
			SET_UNUSED(opline->op2);
			opline->op1.opline_num = start_op;

			break;
		}
	}	
}

static void zend_resolve_finally_ret(zend_op_array *op_array, uint32_t op_num TSRMLS_DC)
{
	int i;
	uint32_t catch_op_num = 0, finally_op_num = 0;

	for (i = 0; i < op_array->last_try_catch; i++) {
		if (op_array->try_catch_array[i].try_op > op_num) {
			break;
		}
		if (op_num < op_array->try_catch_array[i].finally_op) {
			finally_op_num = op_array->try_catch_array[i].finally_op;
		}
		if (op_num < op_array->try_catch_array[i].catch_op) {
			catch_op_num = op_array->try_catch_array[i].catch_op;
		}
	}

	if (finally_op_num && (!catch_op_num || catch_op_num >= finally_op_num)) {
		/* in case of unhandled exception return to upward finally block */
		op_array->opcodes[op_num].extended_value = ZEND_FAST_RET_TO_FINALLY;
		op_array->opcodes[op_num].op2.opline_num = finally_op_num;
	} else if (catch_op_num) {
		/* in case of unhandled exception return to upward catch block */
		op_array->opcodes[op_num].extended_value = ZEND_FAST_RET_TO_CATCH;
		op_array->opcodes[op_num].op2.opline_num = catch_op_num;
	}
}

static void zend_resolve_finally_calls(zend_op_array *op_array TSRMLS_DC)
{
	uint32_t i, j;
	zend_op *opline;

	for (i = 0, j = op_array->last; i < j; i++) {
		opline = op_array->opcodes + i;
		switch (opline->opcode) {
			case ZEND_RETURN:
			case ZEND_RETURN_BY_REF:
			case ZEND_GENERATOR_RETURN:
				zend_resolve_finally_call(op_array, i, (uint32_t)-1 TSRMLS_CC);
				break;
			case ZEND_BRK:
			case ZEND_CONT:
			{
				int nest_levels, array_offset;
				zend_brk_cont_element *jmp_to;

				nest_levels = Z_LVAL(op_array->literals[opline->op2.constant]);
				if ((array_offset = opline->op1.opline_num) != -1) {
					do {
						jmp_to = &op_array->brk_cont_array[array_offset];
						if (nest_levels > 1) {
							array_offset = jmp_to->parent;
						}
					} while (--nest_levels > 0);
					zend_resolve_finally_call(op_array, i, opline->opcode == ZEND_BRK ? jmp_to->brk : jmp_to->cont TSRMLS_CC);
					break;
				}
			}
			case ZEND_GOTO:
				if (Z_TYPE(op_array->literals[opline->op2.constant]) != IS_LONG) {
					uint32_t num = opline->op2.constant;
					opline->op2.zv = &op_array->literals[opline->op2.constant];
					zend_resolve_goto_label(op_array, opline, 1 TSRMLS_CC);
					opline->op2.constant = num;					
				}
				/* break omitted intentionally */
			case ZEND_JMP:
				zend_resolve_finally_call(op_array, i, opline->op1.opline_num TSRMLS_CC);
				break;
			case ZEND_FAST_CALL:
				zend_resolve_fast_call(op_array, i, i TSRMLS_CC);
				break;
			case ZEND_FAST_RET:
				zend_resolve_finally_ret(op_array, i TSRMLS_CC);
				break;
			default:
				break;
		}
	}
}

ZEND_API int pass_two(zend_op_array *op_array TSRMLS_DC)
{
	zend_op *opline, *end;

	if (!ZEND_USER_CODE(op_array->type)) {
		return 0;
	}
	if (op_array->fn_flags & ZEND_ACC_HAS_FINALLY_BLOCK) {
		zend_resolve_finally_calls(op_array TSRMLS_CC);
	}
	if (CG(compiler_options) & ZEND_COMPILE_EXTENDED_INFO) {
		zend_update_extended_info(op_array TSRMLS_CC);
	}
	if (CG(compiler_options) & ZEND_COMPILE_HANDLE_OP_ARRAY) {
		zend_llist_apply_with_argument(&zend_extensions, (llist_apply_with_arg_func_t) zend_extension_op_array_handler, op_array TSRMLS_CC);
	}

	if (CG(context).vars_size != op_array->last_var) {
		op_array->vars = (zend_string**) erealloc(op_array->vars, sizeof(zend_string*)*op_array->last_var);
		CG(context).vars_size = op_array->last_var;
	}
	if (CG(context).opcodes_size != op_array->last) {
		op_array->opcodes = (zend_op *) erealloc(op_array->opcodes, sizeof(zend_op)*op_array->last);
		CG(context).opcodes_size = op_array->last;
	}
	if (CG(context).literals_size != op_array->last_literal) {
		op_array->literals = (zval*)erealloc(op_array->literals, sizeof(zval) * op_array->last_literal);
		CG(context).literals_size = op_array->last_literal;
	}
	opline = op_array->opcodes;
	end = opline + op_array->last;
	while (opline < end) {
		if (opline->op1_type == IS_CONST) {
			opline->op1.zv = &op_array->literals[opline->op1.constant];
		} else if (opline->op1_type & (IS_VAR|IS_TMP_VAR)) {
			opline->op1.var = (uint32_t)(zend_intptr_t)EX_VAR_NUM_2(NULL, op_array->last_var + opline->op1.var);
		}
		if (opline->op2_type == IS_CONST) {
			opline->op2.zv = &op_array->literals[opline->op2.constant];
		} else if (opline->op2_type & (IS_VAR|IS_TMP_VAR)) {
			opline->op2.var = (uint32_t)(zend_intptr_t)EX_VAR_NUM_2(NULL, op_array->last_var + opline->op2.var);
		}
		if (opline->result_type & (IS_VAR|IS_TMP_VAR)) {
			opline->result.var = (uint32_t)(zend_intptr_t)EX_VAR_NUM_2(NULL, op_array->last_var + opline->result.var);
		}
		switch (opline->opcode) {
			case ZEND_DECLARE_INHERITED_CLASS:
			case ZEND_DECLARE_INHERITED_CLASS_DELAYED:
				opline->extended_value = (uint32_t)(zend_intptr_t)EX_VAR_NUM_2(NULL, op_array->last_var + opline->extended_value);
				break;
			case ZEND_GOTO:
				if (Z_TYPE_P(opline->op2.zv) != IS_LONG) {
					zend_resolve_goto_label(op_array, opline, 1 TSRMLS_CC);
				}
				/* break omitted intentionally */
			case ZEND_JMP:
			case ZEND_FAST_CALL:
				opline->op1.jmp_addr = &op_array->opcodes[opline->op1.opline_num];
				break;
			case ZEND_JMPZNZ:
				/* absolute index to relative offset */
				opline->extended_value = (char*)(op_array->opcodes + opline->extended_value) - (char*)opline;
				/* break omitted intentionally */
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
			case ZEND_JMP_SET:
			case ZEND_JMP_SET_VAR:
			case ZEND_NEW:
			case ZEND_FE_RESET:
			case ZEND_FE_FETCH:
				opline->op2.jmp_addr = &op_array->opcodes[opline->op2.opline_num];
				break;
			case ZEND_RETURN:
			case ZEND_RETURN_BY_REF:
				if (op_array->fn_flags & ZEND_ACC_GENERATOR) {
					if (opline->op1_type != IS_CONST || Z_TYPE_P(opline->op1.zv) != IS_NULL) {
						CG(zend_lineno) = opline->lineno;
						zend_error_noreturn(E_COMPILE_ERROR, "Generators cannot return values using \"return\"");
					}

					opline->opcode = ZEND_GENERATOR_RETURN;
				}
				break;
		}
		ZEND_VM_SET_OPCODE_HANDLER(opline);
		opline++;
	}

	op_array->fn_flags |= ZEND_ACC_DONE_PASS_TWO;
	return 0;
}

int pass_two_wrapper(zval *el TSRMLS_DC)
{
	return pass_two((zend_op_array *) Z_PTR_P(el) TSRMLS_CC);
}

int print_class(zend_class_entry *class_entry TSRMLS_DC)
{
	printf("Class %s:\n", class_entry->name->val);
	zend_hash_apply(&class_entry->function_table, pass_two_wrapper TSRMLS_CC);
	printf("End of class %s.\n\n", class_entry->name->val);
	return 0;
}

ZEND_API unary_op_type get_unary_op(int opcode)
{
	switch (opcode) {
		case ZEND_BW_NOT:
			return (unary_op_type) bitwise_not_function;
		case ZEND_BOOL_NOT:
			return (unary_op_type) boolean_not_function;
		default:
			return (unary_op_type) NULL;
	}
}

ZEND_API binary_op_type get_binary_op(int opcode)
{
	switch (opcode) {
		case ZEND_ADD:
		case ZEND_ASSIGN_ADD:
			return (binary_op_type) add_function;
		case ZEND_SUB:
		case ZEND_ASSIGN_SUB:
			return (binary_op_type) sub_function;
		case ZEND_MUL:
		case ZEND_ASSIGN_MUL:
			return (binary_op_type) mul_function;
		case ZEND_POW:
			return (binary_op_type) pow_function;
		case ZEND_DIV:
		case ZEND_ASSIGN_DIV:
			return (binary_op_type) div_function;
		case ZEND_MOD:
		case ZEND_ASSIGN_MOD:
			return (binary_op_type) mod_function;
		case ZEND_SL:
		case ZEND_ASSIGN_SL:
			return (binary_op_type) shift_left_function;
		case ZEND_SR:
		case ZEND_ASSIGN_SR:
			return (binary_op_type) shift_right_function;
		case ZEND_CONCAT:
		case ZEND_ASSIGN_CONCAT:
			return (binary_op_type) concat_function;
		case ZEND_IS_IDENTICAL:
			return (binary_op_type) is_identical_function;
		case ZEND_IS_NOT_IDENTICAL:
			return (binary_op_type) is_not_identical_function;
		case ZEND_IS_EQUAL:
			return (binary_op_type) is_equal_function;
		case ZEND_IS_NOT_EQUAL:
			return (binary_op_type) is_not_equal_function;
		case ZEND_IS_SMALLER:
			return (binary_op_type) is_smaller_function;
		case ZEND_IS_SMALLER_OR_EQUAL:
			return (binary_op_type) is_smaller_or_equal_function;
		case ZEND_BW_OR:
		case ZEND_ASSIGN_BW_OR:
			return (binary_op_type) bitwise_or_function;
		case ZEND_BW_AND:
		case ZEND_ASSIGN_BW_AND:
			return (binary_op_type) bitwise_and_function;
		case ZEND_BW_XOR:
		case ZEND_ASSIGN_BW_XOR:
			return (binary_op_type) bitwise_xor_function;
		case ZEND_BOOL_XOR:
			return (binary_op_type) boolean_xor_function;
		default:
			return (binary_op_type) NULL;
	}
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
