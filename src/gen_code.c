/**
 * @file   gen_code.c
 * @author Kieran Colford <colfordk@gmail.com>
 * 
 * @brief  This is the code generation routine of the compiler.
 * 
 * Copyright (C) 2014 Kieran Colford
 *
 * This file is part of Compiler.
 *
 * Compiler is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Compiler is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Compiler; see the file COPYING.  If not see
 * <http://www.gnu.org/licenses/>.
 *
 * @todo The current implementation restricts itself in that it cannot
 * save the value of a register when it runs out of registers.  Thus
 * we cannot manage when presented with an expression that is too
 * long, a function call is passed as the paramater to another
 * function, or there are more parameters for a function than
 * registers available.
 *
 */

#include "config.h"

/** 
 * Check if the register string S is allowed to be freed.
 * 
 * @param S A string representation of a register.
 * 
 * @return true if S can be freed, false otherwise.
 */
#define VALID_REGISTER_CHECK(S)						\
  ((S) != NULL								\
   && STREQ ((S), regis (general_regis (avail < 1 ? 0 : avail - 1))))

/**
 * A special hook that must be defined before including loc.h.  It
 * checks if the location that is about to be freed, is dependant on
 * any registers.  If it is, then those registers are freed in the
 * correct manner too so that they can be reused.
 * 
 * @param X The location to be freed.
 */
#define FREE_LOC_HOOK(X) do {				\
    if (IS_REGISTER (X) || IS_MEMORY (X))		\
      {							\
	if (IS_MEMORY (X))				\
	  {						\
	    if (VALID_REGISTER_CHECK ((X)->index))	\
	      avail -= 1;				\
	  }						\
	if (VALID_REGISTER_CHECK ((X)->base))		\
	  avail -= 1;					\
      }							\
  } while (0)

#include "ast.h"
#include "compiler.h"
#include "lib.h"
#include "parse.h"
#include "xalloc.h"

#include <stdlib.h>
#include <stdio.h>

#include <assert.h>

#define USE_REGISTER_CHECKING 1

/** 
 * Emit code to move the data stored in location X to location Y using
 * the operator OP.
 * 
 * @param OP Operation to move the data.
 * @param X Source operand.
 * @param Y Destionation operand.
 */
#define MOVE_LOC_WITH(OP, X, Y) do {					\
    if ((X) != NULL)							\
      {									\
	PUT ("\t%s\t%s, %s\n", (OP), print_loc (X), print_loc (Y));	\
	FREE_LOC (X);							\
      }									\
    (X) = (Y);								\
  } while (0)

/** 
 * Allocate a register in the variable X.
 * 
 * @param X Variable to recieve a register.
 */
#define ALLOC_REGISTER(X) do {				\
    const char *_d = regis (general_regis (avail++));	\
    MAKE_BASE_LOC (X, register_loc, xstrdup (_d));	\
  } while (0)

/** 
 * Emit the code specified in the format string.
 * 
 */
#define PUT(...) do {				\
    fprintf (outfile, __VA_ARGS__);		\
    if (debug)					\
      fprintf (stderr, __VA_ARGS__);		\
  } while (0)

/** 
 * Get the string variant of a register index.
 * 
 * @param a Index to turn into a string.
 * 
 * @return String version of register.
 */
static inline const char *
regis (int a)
{
  const char *storage[] =
    { "%rax", "%rbx", "%rcx", "%rdx", "%rdi", "%rsi", "%r8", "%r9", "%r10",
      "%r11", "%r12", "%r13", "%r14", "%r15" };
#if USE_REGISTER_CHECKING
  CHECK_BOUNDS (storage, a);
#endif
  return storage[a];
}

/** 
 * Yield registers in the order that a function call requires them.
 * 
 * These are the order of registers that function arguments are passed
 * through.  Currently, they seriously conflict with the general-use
 * registers and a function call in the middle of an expression is
 * likely to fail.
 * 
 * @param a Argument number.
 * 
 * @return The ath function call register index.
 */
static inline int
call_regis(int a)
{
  const int storage[] =
    { 4, 5, 2, 3, 6, 7 };
#if USE_REGISTER_CHECKING
  CHECK_BOUNDS (storage, a);
#endif
  return storage[a];
}

/** 
 * These registers are available for use in expression calculation.
 * Registers which are needed for special purposes have been left out.
 * These include: %rax, %rdx, and %rcx.
 *
 * The idea to make the allocation behave like a stack (and operations
 * are done like an RPN calculator) so that we only need to increment
 * and decrement the avail variable to check which register we should
 * use.
 * 
 * @param a 
 * 
 * @return 
 */
static inline int
general_regis(int a)
{
  const int storage[] =
    { 1, 8, 9, 10, 11, 12, 13
#if 1
      , 7, 6, 3, 2, 5, 4
#endif
    };
#if USE_REGISTER_CHECKING
  CHECK_BOUNDS (storage, a);
#endif
  return storage[a];
}

static int avail = 0;		/**< Top of available register
				   stack. */
static int str_labelno = 0;	/**< Current label number for strings
				   in the data section. */
static char *data_section = NULL; /**< The data section. */
static int branch_labelno = 0;	/**< Current label number for branch
				   destinations in the text
				   section. */

/** 
 * Macro to allocate a register to a location while also checking to
 * see if it can reuse any of the locations that it is about to free.
 *
 * This macro will preserve the data found in S and simply apply the
 * operator I to it.
 * 
 * @param I The instruction to use.
 * @param S The variable to be moved into a register.
 */
#define GIVE_REGISTER_HOW(I, S) do {					\
    unsigned _addto_avail = 0;						\
    struct loc *_t = NULL;						\
    if (IS_MEMORY (S))							\
      {									\
	_addto_avail += 1;						\
	if (STRNEQ ((S)->base, "%rbp"))					\
	  MAKE_BASE_LOC (_t, register_loc, xstrdup ((S)->base));	\
	else if ((S)->index != NULL)					\
	  MAKE_BASE_LOC (_t, register_loc, xstrdup ((S)->index));	\
	else								\
	  _addto_avail -= 1;						\
      }									\
    if (_t == NULL)							\
      ALLOC_REGISTER (_t);						\
    MOVE_LOC_WITH (I, S, _t);						\
    avail += _addto_avail;						\
  } while (0)

/** 
 * Give a register to location S preserving its data by moving it to
 * the new location.
 *
 * @see GIVE_REGISTER_HOW
 * 
 * @param S The location to receive a register.
 */
#define GIVE_REGISTER(S)			\
  GIVE_REGISTER_HOW ("mov", (S))

/** 
 * Wrapper macro around sub macros to decide on which method of
 * register selection is necessary.
 * 
 * @param N Method to use.
 * @param X First location.
 * @param Y Section location.
 */
#define ENSURE_DESTINATION_REGISTER(N, X, Y) do {	\
    ENSURE_DESTINATION_REGISTER##N (X, Y);		\
    assert ((X) != NULL);				\
    assert ((Y) != NULL);				\
  } while (0)

/**
 * Ensure that that X is in a register. 
 *
 */
#define ENSURE_DESTINATION_REGISTER_UNI(X) do {	\
    if (!IS_REGISTER (X))			\
      GIVE_REGISTER (X);			\
  } while (0)

/**
 * Designed for the addition and subtraction operations.  These
 * require one of the destination/source operands to be in a register
 * while the other can be memory or (if it's the source operand) an
 * immediate. 
 */
#define ENSURE_DESTINATION_REGISTER1(X, Y) do {	\
    if (!IS_REGISTER (X))			\
      {						\
	if (IS_REGISTER (Y))			\
	  SWAP (X, Y);				\
	else					\
	  GIVE_REGISTER (X);			\
      }						\
  } while (0)

/**
 * Designed to behave exactly as ENSURE_DESTINATION_REGISTER1, but is
 * guaranteed to keep the ordering of its arguments.
 */
#define ENSURE_DESTINATION_REGISTER2(X, Y) do {	\
    if (!IS_REGISTER (X))			\
      {						\
	if (IS_REGISTER (Y))			\
	  {					\
	    struct loc *t = loc_dup (Y);	\
	    GIVE_REGISTER (Y);			\
	    MOVE_LOC_WITH ("mov", (X), t);	\
	  }					\
	else					\
	  GIVE_REGISTER (X);			\
      }						\
  } while (0)

/**
 * Designed for use with the right and left shift operators.  These
 * require the right-hand argument to either be and immediate value or
 * placed in the %cl register. 
 */
#define ENSURE_DESTINATION_REGISTER3(X, Y) do {		\
    if (!IS_LITERAL (Y))				\
      {							\
	struct loc *_l;					\
	MAKE_BASE_LOC (_l, register_loc, "%rcx");	\
	MOVE_LOC_WITH ("mov", (Y), _l);			\
	(Y)->base = xstrdup ("%cl");			\
      }							\
    if (IS_LITERAL (X))					\
      GIVE_REGISTER (X);				\
  } while (0)

/**
 * Designed for when both X and Y must be registers but cannot have
 * their arguments swapped.
 */
#define ENSURE_DESTINATION_REGISTER4(X, Y) do {	\
    ENSURE_DESTINATION_REGISTER2 (X, Y);	\
    if (!IS_REGISTER (Y))			\
      GIVE_REGISTER (Y);			\
  } while (0)

/** 
 * Store the construction of the binary operators that work with the
 * branching routine. 
 */
struct binop_branching
{
  int op;
  const char *check;
  const char *jump;
  const char *not;
} branchable_binops[] = {
  { '<', "cmp", "jl", "jnl" },
  { '>', "cmp", "jg", "jng" },
  { EQ, "cmp", "je", "jne" },
  { NE, "cmp", "jne", "je" },
  { LE, "cmp", "jle", "jnle" },
  { GE, "cmp", "jge", "jnge" } };

/* Forward declaration for more specific functions. */
static void gen_code_r (struct ast *);

static void
gen_code_function (struct ast *s)
{
  /* Enter the .text section and declare this symbol as global. */
  PUT ("\t.global\t%s\n", s->op.function.name);
  PUT ("%s:\n", s->op.function.name);

  /* Set up the stack frame. */
  PUT ("\tpush\t%%rbp\n\tmov\t%%rsp, %%rbp\n");

  /* Walk over the list of arguments and push them into the
     stack. */
  struct ast *i;
  int argnum;
  argnum = 0;
  for (i = s->ops[0]; i != NULL; i = i->next)
    {
      if (i->type != variable_type)
	continue;
      PUT ("\tsub\t$%d, %%rsp\n", i->op.variable.alloc);
      assert (i->loc != NULL);
      PUT ("\tmov\t%s, %s\n", regis(call_regis(argnum)),
	   print_loc (i->loc));
      argnum++;
    }

  /* Generate the body of the function. */
  gen_code_r (s->ops[1]);
}

static void
gen_code_ret (struct ast *s)
{
  /* Move the return value into the %rax register. */
  if (s->ops[0] != NULL)
    {
      gen_code_r (s->ops[0]);
      assert (s->ops[0]->loc != NULL);
      struct loc *ret;
      MAKE_BASE_LOC (ret, register_loc, xstrdup ("%rax"));
      MOVE_LOC_WITH ("mov", s->ops[0]->loc, ret);
    }
  /* Function footer. */
  PUT ("\tmov\t%%rbp, %%rsp\n\tpop\t%%rbp\n\tret\n");
}

static void
gen_code_cond (struct ast *s)
{
  struct binop_branching *code;
  code = s->ops[0]->type != binary_type ? NULL :
    bsearch (&s->ops[0]->op.binary.op, branchable_binops,
	     LEN (branchable_binops), sizeof *branchable_binops,
	     compare);
  if (code != NULL)
    {
      gen_code_r (s->ops[0]->ops[0]);
      assert (s->ops[0]->ops[0]->loc != NULL);
      gen_code_r (s->ops[0]->ops[1]);
      assert (s->ops[0]->ops[1]->loc != NULL);
      ENSURE_DESTINATION_REGISTER (2, s->ops[0]->ops[0]->loc,
				   s->ops[0]->ops[1]->loc);
      PUT ("\t%s\t%s, %s\n\t%s\t%s\n", code->check,
	   print_loc (s->ops[0]->ops[1]->loc),
	   print_loc (s->ops[0]->ops[0]->loc),
	   (s->ops[0]->boolean_not ? code->not : code->jump),
	   print_loc (s->loc));
      FREE_LOC (s->ops[0]->ops[1]->loc);
      FREE_LOC (s->ops[0]->ops[0]->loc);
    }
  else
    {
      /* When in doubt, the C standard requires that if an
	 expression evaluates to 0 then it is false, otherwise it
	 is true. */
      gen_code_r (s->ops[0]);
      assert (s->ops[0]->loc != NULL);
      PUT ("\tcmpq\t%s, $0\n\tjz\t%s\n", print_loc (s->ops[0]->loc),
	   print_loc (s->loc));
      FREE_LOC (s->ops[0]->loc);
    }
}

static void
gen_code_binary (struct ast *s)
{
  gen_code_r (s->ops[0]);
  assert (s->ops[0]->loc != NULL);
  gen_code_r (s->ops[1]);
  assert (s->ops[1]->loc != NULL);
  s->loc = loc_dup (s->ops[0]->loc);
  struct ast _from, *from;
  from = &_from;
  from->loc = loc_dup (s->ops[1]->loc);
  struct loc *l;
  switch (s->op.binary.op)
    {
    case '=':
      /* Either the source or the destination must be a
	 register/immediate, if that's not the case, then we have
	 to move the source operand into a register first. */
      if (IS_MEMORY (from->loc))
	GIVE_REGISTER (from->loc);
      assert (IS_MEMORY (s->loc));
      PUT ("\tmovq\t%s, %s\n", print_loc (from->loc),
	   print_loc (s->loc));
      break;

    case '&':
      ENSURE_DESTINATION_REGISTER (1, s->loc, from->loc);
      PUT ("\tand\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case '|':
      ENSURE_DESTINATION_REGISTER (1, s->loc, from->loc);
      PUT ("\tor\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case '^':
      ENSURE_DESTINATION_REGISTER (1, s->loc, from->loc);
      PUT ("\txor\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case '+':
      ENSURE_DESTINATION_REGISTER (1, s->loc, from->loc);
      PUT ("\tadd\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case '-':
      ENSURE_DESTINATION_REGISTER (2, s->loc, from->loc);
      PUT ("\tsub\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case '*':
      MAKE_BASE_LOC (l, register_loc, "%rax");
      MOVE_LOC_WITH ("mov", s->loc, l);
      PUT ("\tmov\t$0, %%rdx\n");
      if (IS_LITERAL (from->loc))
	GIVE_REGISTER (from->loc);
      PUT ("\timulq\t%s\n", print_loc (from->loc));
      FREE_LOC (from->loc);
      s->loc->base = xstrdup ("%rax");
      GIVE_REGISTER (s->loc);
      break;

    case '/':
      MAKE_BASE_LOC (l, register_loc, "%rax");
      MOVE_LOC_WITH ("mov", s->loc, l);
      PUT ("\tmov\t$0, %%rdx\n");
      if (IS_LITERAL (from->loc))
	GIVE_REGISTER (from->loc);
      PUT ("\tidivq\t%s\n", print_loc (from->loc));
      FREE_LOC (from->loc);
      s->loc->base = xstrdup ("%rax");
      GIVE_REGISTER (s->loc);
      break;

    case '%':
      MAKE_BASE_LOC (l, register_loc, "%rax");
      MOVE_LOC_WITH ("mov", s->loc, l);
      PUT ("\tmov\t$0, %%rdx\n");
      if (IS_LITERAL (from->loc))
	GIVE_REGISTER (from->loc);
      PUT ("\tidivq\t%s\n", print_loc (from->loc));
      FREE_LOC (from->loc);
      s->loc->base = xstrdup ("%rdx");
      GIVE_REGISTER (s->loc);
      break;

    case RS:
      ENSURE_DESTINATION_REGISTER (3, s->loc, from->loc);
      PUT ("\tshr\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case LS:
      ENSURE_DESTINATION_REGISTER (3, s->loc, from->loc);
      PUT ("\tshl\t%s, %s\n", print_loc (from->loc), print_loc (s->loc));
      break;

    case '[':
      assert (!IS_LITERAL (s->loc));
      ENSURE_DESTINATION_REGISTER (4, s->loc, from->loc);
      s->loc->kind = memory_loc;
      s->loc->index = from->loc->base;
      s->loc->scale = 8;
      from->loc->base = NULL;
      break;

    default:
      error (1, 0, _("invalid binary operator op-code: %d"),
	     s->op.binary.op);
    }
  /* Release the previously allocated register. */
  FREE_LOC (from->loc);
}

static void
gen_code_unary (struct ast *s)
{
  gen_code_r (s->ops[0]);
  s->loc = loc_dup (s->ops[0]->loc);
  switch (s->op.unary.op)
    {
    case '*':
      ENSURE_DESTINATION_REGISTER_UNI (s->loc);
      s->loc->kind = memory_loc;
      break;

    case '&':
      if (IS_MEMORY (s->loc))
	GIVE_REGISTER_HOW ("lea", s->loc);
      else if (IS_SYMBOL (s->loc))
	s->loc->kind = literal_loc;
      else
	assert (0);
      break;

    case '-':
      ENSURE_DESTINATION_REGISTER_UNI (s->loc);
      PUT ("\tnegq\t%s\n", print_loc (s->loc));
      break;

    case INC:
      if (!s->unary_prefix)
	GIVE_REGISTER (s->loc);
      PUT ("\tincq\t%s\n", print_loc (s->ops[0]->loc));
      break;

    case DEC:
      if (!s->unary_prefix)
	GIVE_REGISTER (s->loc);
      PUT ("\tdecq\t%s\n", print_loc (s->ops[0]->loc));
      break;

    default:
      error (1, 0, _("invalid unary operator opcode: %d"),
	     s->op.unary.op);
    }
  assert (s->loc != NULL);
}

static void
gen_code_function_call (struct ast *s)
{
  /* Certain functions are considered builtin and thus require
     special treatment. */
  if (STREQ (s->ops[0]->loc->base, "__builtin_alloca"))
    {
      gen_code_r (s->ops[1]);
      PUT ("\tsub\t%s, %%rsp\n", print_loc (s->ops[1]->loc));
      FREE_LOC (s->ops[1]->loc);
      MAKE_BASE_LOC (s->loc, register_loc, xstrdup ("%rsp"));
    }
  else
    {
      /** @todo Don't clobber other registers when making a
	  function call. */
      int a = 0;
      gen_code_r (s->ops[1]);
      struct ast *i;
      for (i = s->ops[1]; i != NULL; i = i->next)
	{
	  assert (i->loc != NULL);
	  struct loc *call;
	  MAKE_BASE_LOC (call, register_loc,
			 xstrdup (regis(call_regis(a++))));
	  MOVE_LOC_WITH ("mov", i->loc, call);
	}
      assert (s->ops[0]->type == variable_type);
      PUT ("\tmov\t$0, %%rax\n\tcall\t%s\n", s->ops[0]->loc->base);
      FREE_LOC (s->ops[0]->loc);
      MAKE_BASE_LOC (s->loc, register_loc, xstrdup ("%rax"));
    }
  GIVE_REGISTER (s->loc);
}

/** 
 * Recursive version of @c gen_code.
 * 
 * @param s The AST to operate on.
 */
static void
gen_code_r (struct ast *s)
{
  if (s == NULL)
    return;
  switch (s->type)
    {
    case function_type:
      gen_code_function (s);
      break;

    case ret_type:
      gen_code_ret (s);
      break;

    case cond_type:
      gen_code_cond (s);
      break;

    case label_type:
      assert (s->loc != NULL);
      PUT ("%s:\n", print_loc (s->loc));
      break;

    case jump_type:
      assert (s->loc != NULL);
      PUT ("\tjmp\t%s\n", print_loc (s->loc));
      break;

    case integer_type:
      if (s->loc == NULL)
	MAKE_BASE_LOC (s->loc, literal_loc,
		       my_printf ("%lld", s->op.integer.i));
      break;

    case string_type:
      if (s->loc == NULL)
	MAKE_BASE_LOC (s->loc, symbol_loc,
		       my_printf (".LS%d", str_labelno++));
      if (s->op.string.val != NULL)
	EXTENDF (data_section, "%s:\n\t.string\t\"%s\"\n",
		 print_loc (s->loc), s->op.string.val);
      break;

    case binary_type:
      gen_code_binary (s);
      break;

    case unary_type:
      gen_code_unary (s);
      break;

    case function_call_type:
      gen_code_function_call (s);
      break;

    case alloc_type:
      if (s->ops[0] != NULL)
	{
	  gen_code_r (s->ops[0]);
	  PUT ("\tsub\t%s, %%rsp\n", print_loc (s->ops[0]->loc));
	  FREE_LOC (s->ops[0]->loc);
	  MAKE_BASE_LOC (s->loc, register_loc, xstrdup ("%rsp"));
	  GIVE_REGISTER (s->loc);
	}
      break;

    default:
      ;
      int j;
      for (j = 0; j < s->num_ops; j++)
	gen_code_r (s->ops[j]);
    }
  /* Since registers are lost during the coarse of this routine, we
     must free every single last one of them once we are done with a
     certain expression. */
  if (s->throw_away)
    avail = 0;
  gen_code_r (s->next);
}

/**
 * Top level entry point to the code generation phase. 
 */
int
gen_code (struct ast *s)
{
  avail = 0;
  str_labelno = 0;
  data_section = NULL;
  branch_labelno = 0;

  qsort (branchable_binops, LEN (branchable_binops),
	 sizeof *branchable_binops, compare);
  data_section = xstrdup ("\t.data\n");
  gen_code_r (s);
  PUT ("%s", data_section);
  FREE (data_section);
  return 0;
}
