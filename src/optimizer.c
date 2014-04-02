#include "compiler.h"
#include "ast.h"
#include "parse.h"
#include <stdlib.h>
#include <assert.h>

#define FOLD_INTEGER_UNI(OP) do {			\
    if (s->op.unary.arg->type == integer_type)		\
      {							\
	long long a = s->op.unary.arg->op.integer.i;	\
	free (s->op.unary.arg);				\
	free (s);					\
	s = make_integer (OP a);			\
      }							\
  } while (0)

/* Fold up expressions involving constant integer expressions into a
   single constant integer. */
#define FOLD_INTEGER_BIN(OP) do {			\
    if (s->op.binary.left->type == integer_type &&	\
	s->op.binary.right->type == integer_type)	\
      {							\
	long long l = s->op.binary.left->op.integer.i;	\
	free (s->op.binary.left);			\
	long long r = s->op.binary.right->op.integer.i;	\
	free (s->op.binary.right);			\
	free (s);					\
	s = make_integer (l OP r);			\
      }							\
  } while (0)

/* Convience macro for declaring how to fold up each binary
   operator. */
#define FOLD_INT_BIN(CASE, OP)			\
  case CASE:					\
  FOLD_INTEGER_BIN (OP);			\
  break

static void
optimizer_r (struct ast **ss)
{
  assert (ss != NULL);
#define s (*ss)
  if (s == NULL)
    return;
  switch (s->type)
    {
    case block_type:
      optimizer_r (&s->op.block.val);
      optimizer_r (&s->op.block.next);
      break;

    case function_type:
      optimizer_r (&s->op.function.body);
      break;

    case ret_type:
      optimizer_r (&s->op.ret.val);
      break;

    case cond_type:
      optimizer_r (&s->op.cond.cond);
      optimizer_r (&s->op.cond.body);
      if (s->op.cond.cond->type == integer_type)
	{
	  if (s->op.cond.cond->op.integer.i)
	    s = s->op.cond.body;
	  else
	    s = NULL;
	}
      break;

    case label_type:
      optimizer_r (&s->op.label.stuff);
      break;

    case jump_type:
      break;

    case integer_type:
      break;

    case variable_type:
      break;

    case string_type:
      break;

    case binary_type:
      optimizer_r (&s->op.binary.left);
      optimizer_r (&s->op.binary.right);
      if (optimize > 0)
	{
	  switch (s->op.binary.op)
	    {
	      FOLD_INT_BIN ('+', +);
	      FOLD_INT_BIN ('-', -);
	      FOLD_INT_BIN ('*', *);
	      FOLD_INT_BIN ('/', /);
	      FOLD_INT_BIN ('%', %);
	      FOLD_INT_BIN ('>', >);
	      FOLD_INT_BIN ('<', <);
	      FOLD_INT_BIN (GE, >=);
	      FOLD_INT_BIN (LE, <=);
	      FOLD_INT_BIN (EQ, ==);
	      FOLD_INT_BIN (NE, !=);
	      FOLD_INT_BIN (RS, >>);
	      FOLD_INT_BIN (LS, <<);
	    }
	}
      break;

    case unary_type:
      optimizer_r (&s->op.unary.arg);
      if (optimize > 0)
	{
	  switch (s->op.unary.op)
	    {
	    case '-':
	      FOLD_INTEGER_UNI (-);
	      break;
	    }
	}
      break;

    case function_call_type:
      break;
    }
#undef s
}

int
optimizer (struct ast **ss)
{
  optimizer_r (ss);
  return 0;
}
