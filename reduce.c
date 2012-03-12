/*
 * reduce.c -- SCHEME-like language built on the Actor model
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 *
 * x -> <value of symbol 'x'>
 * (literal x) -> x
 * () -> ()
 * (literal (f x)) -> (f x)
 * (first (literal (x))) -> x
 * (rest (literal (x))) -> ()
 * (first (literal (x y))) -> x
 * (rest (literal (x y))) -> (y)
 * (prepend (literal x) ()) -> (x)
 * (prepend () ()) -> (())
 * (first ()) -> ()
 * (rest ()) -> ()
 * TRUE -> TRUE
 * FALSE -> FALSE
 * (list? TRUE) -> FALSE
 * (list? ()) -> TRUE
 * (list? (literal (x))) -> TRUE
 * (empty? TRUE) -> _
 * (empty? ()) -> TRUE
 * (empty? (literal (x))) -> FALSE
 * (f x) -> <application of value of 'f' to value of 'x'>
 * (if TRUE (literal Yes) (error)) -> Yes
 * (if FALSE (error) (literal No)) -> No
 */
static char	_Program[] = "Reduce";
static char	_Version[] = "2009-02-08";
static char	_Copyright[] = "Copyright 2008-2009 Dale Schumacher";

#include <getopt.h>
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("reduce");

static CONFIG* reduce_cfg = NULL;

static CONS* undefined_symbol = NIL;
static CONS* true_symbol = NIL;
static CONS* false_symbol = NIL;
static CONS* self_symbol = NIL;
static CONS* expr_symbol = NIL;
static CONS* cont_symbol = NIL;
static CONS* env_symbol = NIL;
static CONS* beh_symbol = NIL;
static CONS* form_symbol = NIL;

static CONS* a_reduce = NIL;
static CONS* a_reduce_args = NIL;
static CONS* a_become = NIL;
static CONS* an_error = NIL;
static CONS* a_print = NIL;
static CONS* a_sink = NIL;

static CONS* eval_env = NIL;

BEH_DECL(print_msg)
{
	CONS* text = map_get(MINE, ATOM("message"));

	DBUG_ENTER("print_msg");
	if (atomp(text)) {
		printf("%s", atom_str(text));
	}
	printf("%s\n", cons_to_str(WHAT));
	DBUG_RETURN;
}

/**
label_msg(<msg>){label:<label>, ctx:<ctx>, cont:<cont>} =
	{<label>:<msg>} + <ctx> => <cont>
**/
BEH_DECL(label_msg)
{
	CONS* state = MINE;
	CONS* label = map_get(state, ATOM("label"));
	CONS* ctx = map_get(state, ATOM("ctx"));
	CONS* cont = map_get(state, cont_symbol);

	DBUG_ENTER("label_msg");
	assert(!nilp(label));
	SEND(cont, map_put(ctx, label, WHAT));
	DBUG_RETURN;
}

/**
unlabel_msg(<label>:<value>){label:<label>, cont:<cont>} =
	<value> => <cont>
**/
BEH_DECL(unlabel_msg)
{
	CONS* state = MINE;
	CONS* label = map_get(state, ATOM("label"));
	CONS* cont = map_get(state, cont_symbol);
	CONS* value = map_get(WHAT, label);

	DBUG_ENTER("unlabel_msg");
	assert(!nilp(label));
	SEND(cont, value);
	DBUG_RETURN;
}

/**
apply_expr(expr:<expr>){form:<form>, cont:<cont>, env:<env>} =
	{expr:<form>, cont:<cont>, env:<env>} => expr
**/
BEH_DECL(apply_expr)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* form = map_get(state, form_symbol);
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	
	DBUG_ENTER("apply_expr");
	if (actorp(expr)) {
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, cont);
		msg = map_put(msg, expr_symbol, form);
		SEND(expr, msg);
	} else {	/* error */
		/* FIXME: consider the meaning of non-actor applications, such as numbers */
		SEND(an_error, ATOM("apply: actor required"));
		SEND(cont, map_put(NIL, expr_symbol, undefined_symbol));
	}
	DBUG_RETURN;
}

/**
reduce_expr(expr:<expr>, cont:<cont>, env:<env>){} =
	IF symbol?(<expr>)
		{expr:map(<env>, <expr>)} => <cont>
	ELIF empty?(<expr>)
		{expr:<expr>} => <cont>
	ELSE
		{expr:first(<expr>), cont:actor(apply_expr, {form:rest(<expr>), cont:<cont>, env:<env>}), env:<env>} => self()
**/
BEH_DECL(reduce_expr)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* result = NIL;

	DBUG_ENTER("reduce_expr");
	DBUG_PRINT("", ("expr@%p=%s", expr, cons_to_str(expr)));
	if ((expr == true_symbol)
	||  (expr == false_symbol)
	||  (expr == undefined_symbol)) {
		/* built-in constant */
		DBUG_PRINT("", ("built-in constant"));
		result = expr;
	} else if (numberp(expr)) {
		/* numeric constant */
		DBUG_PRINT("", ("numeric constant"));
		result = expr;
	} else if (atomp(expr)) {
		/* atomic symbol */
		DBUG_PRINT("", ("atomic symbol"));
		result = map_get(env, expr);
		if (result == NULL) {
			DBUG_PRINT("", ("env=%s", cons_to_str(env)));
			SEND(an_error, cons(ATOM("reduce: undefined symbol"), expr));
			result = undefined_symbol;
		}
	} else if (nilp(expr)) {
		/* empty list */
		DBUG_PRINT("", ("empty list"));
		result = expr;
	} else if (consp(expr)) {
		/* evaluate first element to determine the actor to apply */
		CONS* state;
		CONS* actor;

		DBUG_PRINT("", ("cons list"));
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, cont_symbol, cont);
		state = map_put(state, form_symbol, cdr(expr));
		actor = ACTOR(apply_expr, state);
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, actor);
		msg = map_put(msg, expr_symbol, car(expr));
		SEND(a_reduce, msg);			/* self message */
		DBUG_RETURN;
	} else {	/* error */
		DBUG_PRINT("", ("undefined expression"));
		SEND(an_error, cons(ATOM("reduce: undefined expression"), expr));
		result = undefined_symbol;
	}
	DBUG_PRINT("", ("result=%s", cons_to_str(result)));
	SEND(cont, map_put(NIL, expr_symbol, result));
	DBUG_RETURN;
}

/**
apply_reduce(expr:<expr>){cont:<cont>, env:<env>} =
	{expr:<expr>, cont:<cont>, env:<env>} => reduce
**/
BEH_DECL(apply_reduce)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	
	DBUG_ENTER("apply_reduce");
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, cont);
	msg = map_put(msg, expr_symbol, expr);
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/**
reduce_reduce(expr:<expr>, cont:<cont>, env:<env>){} =
	IF list?(expr)
		{expr:first(<expr>), cont:actor(apply_reduce, {cont:<cont>, env:<env>}), env:<env>} => reduce
	ELSE
		{expr:_} => cont
**/
BEH_DECL(reduce_reduce)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);

	DBUG_ENTER("reduce_reduce");
	if (consp(expr)) {
		CONS* state;
		CONS* actor;

		/* evaluate first argument and send to apply actor */
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, cont_symbol, cont);
		actor = ACTOR(apply_reduce, state);
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, actor);
		msg = map_put(msg, expr_symbol, car(expr));
		SEND(a_reduce, msg);
	} else {	/* error */
		SEND(an_error, ATOM("reduce: illegal argument"));
		msg = NIL;
		msg = map_put(msg, expr_symbol, undefined_symbol);
		SEND(cont, msg);
	}
	DBUG_RETURN;
}

/**
reduce_literal(expr:<expr>, cont:<cont>, env:<env>){} =
	{expr:<expr>} => <cont>
**/
BEH_DECL(reduce_literal)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
/*	CONS* env = map_get(msg, env_symbol); */
	
	DBUG_ENTER("reduce_literal");
	expr = car(expr);
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
reduce_and_apply(expr:<expr>, cont:<cont>, env:<env>){beh:<beh>} =
	<apply> = actor(<beh>, {cont:<cont>, env:<env>})
	{expr:<expr>, cont:<apply>, env:<env>} => reduce_args
**/
BEH_DECL(reduce_and_apply)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state = MINE;
	CONS* beh = map_get(state, beh_symbol);
	CONS* apply;

	DBUG_ENTER("reduce_and_apply");
	assert(funcp(beh));
	state = NIL;
	state = map_put(state, env_symbol, env);
	state = map_put(state, cont_symbol, cont);
	apply = ACTOR(MK_BEH(beh), state);
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, apply);
	msg = map_put(msg, expr_symbol, expr);
	SEND(a_reduce_args, msg);
	DBUG_RETURN;
}

/**
eval_list(expr:<expr>){cont:<cont>, env:<env>} =
	{expr:<expr>} => <cont>
**/
BEH_DECL(eval_list)	/* FIXME:  this is a NO-OP, we should probably just use reduce_seq() directly */
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_list");
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_first(expr:<expr>){cont:<cont>, env:<env>} =
	IF list?(<expr>)
		{expr:first(first(<expr>))} => <cont>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(eval_first)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_first");
	if (consp(expr) && consp(car(expr))) {
		CONS* p = car(expr);

		if (nilp(p)) {
			expr = NIL;
		} else {
			expr = car(p);
		}
	} else {	/* error */
		SEND(an_error, ATOM("first: list required"));
		expr = undefined_symbol;
	}
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_rest(expr:<expr>){cont:<cont>, env:<env>} =
	IF list?(<expr>)
		{expr:rest(first(<expr>))} => <cont>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(eval_rest)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_rest");
	if (consp(expr) && consp(car(expr))) {
		CONS* p = car(expr);

		if (nilp(p)) {
			expr = NIL;
		} else {
			expr = cdr(p);
		}
	} else {	/* error */
		SEND(an_error, ATOM("rest: list required"));
		expr = undefined_symbol;
	}
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_prepend(expr:<expr>){cont:<cont>, env:<env>} =
	{expr:prepend(first(<expr>), second(<expr>))} => <cont>
**/
BEH_DECL(eval_prepend)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_prepend");
	expr = cons(car(expr), car(cdr(expr)));
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_list?(expr:<expr>){cont:<cont>, env:<env>} =
	IF list?(first(<expr>))
		{expr:TRUE} => <cont>
	ELSE
		{expr:FALSE} => <cont>
**/
BEH_DECL(eval_listp)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_listp");
	expr = consp(car(expr)) ? true_symbol : false_symbol;
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_empty?(expr:<expr>){cont:<cont>, env:<env>} =
	IF empty?(first(<expr>))
		{expr:TRUE} => <cont>
	ELSE
		{expr:FALSE} => <cont>
**/
BEH_DECL(eval_emptyp)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_emptyp");
	expr = nilp(car(expr)) ? true_symbol : false_symbol;
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_number?(expr:<expr>){cont:<cont>, env:<env>} =
	IF number?(first(<expr>))
		{expr:TRUE} => <cont>
	ELSE
		{expr:FALSE} => <cont>
**/
BEH_DECL(eval_numberp)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_numberp");
	expr = numberp(car(expr)) ? true_symbol : false_symbol;
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_zero?(expr:<expr>){cont:<cont>, env:<env>} =
	IF zero?(first(<expr>))
		{expr:TRUE} => <cont>
	ELSE
		{expr:FALSE} => <cont>
**/
BEH_DECL(eval_zerop)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_zerop");
	expr = (car(expr) == NUMBER(0)) ? true_symbol : false_symbol;
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_equal?(expr:<expr>){cont:<cont>, env:<env>} =
	IF equal?(first(<expr>), second(<expr>))
		{expr:TRUE} => <cont>
	ELSE
		{expr:FALSE} => <cont>
**/
BEH_DECL(eval_equalp)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	
	DBUG_ENTER("eval_equalp");
	expr = equal(car(expr), car(cdr(expr))) ? true_symbol : false_symbol;
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
eval_preceed?(expr:<expr>){cont:<cont>, env:<env>} =
	IF number?(first(<expr>)) AND number?(second(<expr>))
		IF preceed?(first(<expr>), second(<expr>))
			{expr:TRUE} => <cont>
		ELSE
			{expr:FALSE} => <cont>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(eval_preceedp)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	CONS* arg1;
	CONS* arg2;
	
	DBUG_ENTER("eval_preceedp");
	arg1 = car(expr);
	arg2 = car(cdr(expr));
	if (numberp(arg1) && numberp(arg2)) {
		expr = (MK_INT(arg1) < MK_INT(arg2)) ? true_symbol : false_symbol;
	} else {
		SEND(an_error, ATOM("preceed?: numbers required"));
		expr = undefined_symbol;
	}
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
apply_if(expr:<expr>){form:<form>, cont:<cont>, env:<env>} =
	IF equal?(<expr>, TRUE)
		{expr:first(<form>), cont:<cont>, env:<env>} => reduce
	ELIF equal?(<expr>, FALSE)
		{expr:second(<form>), cont:<cont>, env:<env>} => reduce
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(apply_if)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* form = map_get(state, form_symbol);
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	
	DBUG_ENTER("apply_if");
	if (expr == true_symbol) {
		expr = car(form);
	} else if (expr == false_symbol) {
		expr = car(cdr(form));
	} else {	/* error */
		SEND(an_error, ATOM("if: predicate must reduce to TRUE or FALSE"));
		SEND(cont, map_put(NIL, expr_symbol, undefined_symbol));
		DBUG_RETURN;
	}
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, cont);
	msg = map_put(msg, expr_symbol, expr);
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/**
reduce_if(expr:<expr>, cont:<cont>, env:<env>){} =
	IF list?(<expr>) AND list?(rest(expr)) AND list?(rest(rest(expr)))
		{expr:first(<expr>), cont:actor(apply_if, {form:rest(expr), cont:<cont>, env:<env>}), env:<env>} => reduce
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(reduce_if)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);

	DBUG_ENTER("reduce_if");
	if (consp(expr) && consp(cdr(expr)) && consp(cdr(cdr(expr)))) {
		CONS* state;
		CONS* actor;
		
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, cont_symbol, cont);
		state = map_put(state, form_symbol, cdr(expr));
		actor = ACTOR(apply_if, state);
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, actor);
		msg = map_put(msg, expr_symbol, car(expr));
		SEND(a_reduce, msg);
	} else {	/* error */
		SEND(an_error, ATOM("if: illegal argument"));
		msg = NIL;
		msg = map_put(msg, expr_symbol, undefined_symbol);
		SEND(cont, msg);
	}
	DBUG_RETURN;
}

/**
apply_define(expr:<expr>){binding:<binding>, cont:<cont>} =
	rplacd(<binding>, <expr>) => <cont>
**/
BEH_DECL(apply_define)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* binding = map_get(state, ATOM("binding"));
	CONS* cont = map_get(state, cont_symbol);
	DBUG_ENTER("apply_define");

	assert(atomp(car(binding)));
	rplacd(binding, expr);
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	SEND(cont, map_put(NIL, expr_symbol, car(binding)));
	DBUG_RETURN;
}

static CONS*
extend_env(CONS* env, CONS* name, CONS* value)
{
	CONS* binding = NULL;

	assert(atomp(name));
	/* find first "self" binding in the current environment */
	while (!nilp(env)) {
		assert(consp(env));
		if (car(car(env)) == self_symbol) {
			break;
		}
		env = cdr(env);
	}
	assert(!nilp(env));
	/* link in new binding to immediately follow "self" */
	binding = cons(name, value);
	rplacd(env, cons(binding, cdr(env)));
	return binding;
}

/**
reduce_define(expr:<expr>, cont:<cont>, env:<env>){} =
	IF atom?(first(<expr>))
		<binding> := extend_env(<env>, first(<expr>), _)
		<apply> := actor(apply_define, {binding:<binding>, cont:<cont>})
		{expr:second(<expr>), cont:<apply>, env:<env>} => reduce
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(reduce_define)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* binding;
	CONS* state;
	CONS* apply;

	DBUG_ENTER("reduce_define");
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	if (!consp(expr) || !atomp(car(expr))) {
		SEND(an_error, ATOM("define: atom required"));
		SEND(cont, map_put(NIL, expr_symbol, undefined_symbol));
		DBUG_RETURN;
	}
	binding = extend_env(env, car(expr), undefined_symbol);
	state = NIL;
	state = map_put(state, cont_symbol, cont);
	state = map_put(state, ATOM("binding"), binding);
	apply = ACTOR(apply_define, state);
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, apply);
	msg = map_put(msg, expr_symbol, car(cdr(expr)));
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/**
apply_sum(expr:<expr>){acc:<acc>, form:<form>, cont:<cont>, env:<env>} =
	IF number?(<expr>)
		<acc> = <acc> + <expr>
		IF empty?(<form>)
			{expr:<acc>} => <cont>
		ELSE
			become(apply_sum, {acc:<acc>, form:rest(<form>), cont:<cont>, env:<env>})
			{expr:first(<form>), cont:self(), env:<env>} => reduce
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(apply_sum)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* acc = map_get(state, ATOM("acc"));
	CONS* form = map_get(state, form_symbol);
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	
	DBUG_ENTER("apply_sum");
	if (numberp(expr)) {
		assert(numberp(acc));
		acc = NUMBER(MK_INT(acc) + MK_INT(expr));
		if (nilp(form) || !consp(form)) {
			expr = acc;
		} else {
			state = NIL;
			state = map_put(state, env_symbol, env);
			state = map_put(state, cont_symbol, cont);
			state = map_put(state, form_symbol, cdr(form));
			state = map_put(state, ATOM("acc"), acc);
			BECOME(THIS, state);
			msg = NIL;
			msg = map_put(msg, env_symbol, env);
			msg = map_put(msg, cont_symbol, SELF);
			msg = map_put(msg, expr_symbol, car(form));
			SEND(a_reduce, msg);
			DBUG_RETURN;
		}
	} else {	/* error */
		SEND(an_error, ATOM("numeric args required"));
		expr = undefined_symbol;
	}
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, expr_symbol, expr);
	SEND(cont, msg);
	DBUG_RETURN;
}

/**
reduce_sum(expr:<expr>, cont:<cont>, env:<env>){} =
	<actor> = actor(apply_sum, {acc:0, form:rest(<expr>), cont:<cont>, env:<env>})
	{expr:first(<expr>), cont:<actor>, env:<env>} => reduce
**/
BEH_DECL(reduce_sum)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state;
	CONS* actor;

	DBUG_ENTER("reduce_sum");
	state = NIL;
	state = map_put(state, env_symbol, env);
	state = map_put(state, cont_symbol, cont);
	state = map_put(state, form_symbol, cdr(expr));
	state = map_put(state, ATOM("acc"), NUMBER(0));
	actor = ACTOR(apply_sum, state);
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, actor);
	msg = map_put(msg, expr_symbol, car(expr));
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/* FIXME: this method duplicates a lot of code from apply_sum() */
BEH_DECL(apply_product)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* acc = map_get(state, ATOM("acc"));
	CONS* form = map_get(state, form_symbol);
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	
	DBUG_ENTER("apply_product");
	if (numberp(expr)) {
		assert(numberp(acc));
		acc = NUMBER(MK_INT(acc) * MK_INT(expr));
		if (nilp(form) || !consp(form)) {
			expr = acc;
		} else {
			state = NIL;
			state = map_put(state, env_symbol, env);
			state = map_put(state, cont_symbol, cont);
			state = map_put(state, form_symbol, cdr(form));
			state = map_put(state, ATOM("acc"), acc);
			BECOME(THIS, state);
			msg = NIL;
			msg = map_put(msg, env_symbol, env);
			msg = map_put(msg, cont_symbol, SELF);
			msg = map_put(msg, expr_symbol, car(form));
			SEND(a_reduce, msg);
			DBUG_RETURN;
		}
	} else {	/* error */
		SEND(an_error, ATOM("numeric args required"));
		expr = undefined_symbol;
	}
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, expr_symbol, expr);
	SEND(cont, msg);
	DBUG_RETURN;
}

/* FIXME: this method duplicates a lot of code from reduce_sum() */
BEH_DECL(reduce_product)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state;
	CONS* actor;

	DBUG_ENTER("reduce_product");
	state = NIL;
	state = map_put(state, env_symbol, env);
	state = map_put(state, cont_symbol, cont);
	state = map_put(state, form_symbol, cdr(expr));
	state = map_put(state, ATOM("acc"), NUMBER(1));
	actor = ACTOR(apply_product, state);
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, actor);
	msg = map_put(msg, expr_symbol, car(expr));
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/**
template(expr:<expr>, cont:<cont>, env:<env>){vars:<vars>, body:<body>} =
	{expr:replace(<body>, map_def(<vars>, <expr>)), cont:<cont>, env:<env>} => reduce
**/
BEH_DECL(template)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state = MINE;
	CONS* vars = map_get(state, ATOM("vars"));
	CONS* body = map_get(state, ATOM("body"));

	DBUG_ENTER("template");
	expr = replace(body, map_def(NIL, vars, expr));		/* FIXME: check for binding to a single symbol, like eval_function */
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, cont);
	msg = map_put(msg, expr_symbol, expr);
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/**
reduce_template(expr:<expr>, cont:<cont>, env:<env>){} =
	{expr:actor(template, {vars:first(<expr>), body:second(<expr>)})} => <cont>
**/
BEH_DECL(reduce_template)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
/*	CONS* env = map_get(msg, env_symbol); */
	CONS* state;
	CONS* actor;

	DBUG_ENTER("reduce_template");
	state = NIL;
	state = map_put(state, ATOM("body"), car(cdr(expr)));
	state = map_put(state, ATOM("vars"), car(expr));
	actor = ACTOR(template, state);
	msg = NIL;
	msg = map_put(msg, expr_symbol, actor);
	SEND(cont, msg);
	DBUG_RETURN;
}

/**
eval_function(expr:<expr>){vars:<vars>, body:<body>, cont:<cont>, env:<env>} =
	IF list?(<vars>)
		<env> := map_put_all(<env>, map_def(<vars>, <expr>))
	ELIF atom?(<vars>)
		<env> := map_put(<env>, <vars>, <expr>)
	{expr:<body>, cont:<cont>, env:<env>} => reduce
**/
BEH_DECL(eval_function)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* vars = map_get(state, ATOM("vars"));
	CONS* body = map_get(state, ATOM("body"));
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);

	DBUG_ENTER("eval_function");
	DBUG_PRINT("", ("vars=%s", cons_to_str(vars)));
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	if (consp(vars)) {
		/* bind vars to values */
		env = map_def(env, vars, expr);
	} else if (atomp(vars)) {
		/* bind single var to list of values */
		env = map_put(env, vars, expr);
	}
	DBUG_PRINT("", ("body=%s", cons_to_str(body)));
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, cont);
	msg = map_put(msg, expr_symbol, body);
	SEND(a_reduce, msg);
	DBUG_RETURN;
}

/**
function(expr:<expr>, cont:<cont>, env:<dyn>){vars:<vars>, body:<body>, env:<lex>} =
	<apply> := actor(eval_function, {vars:<vars>, body:<body>, cont:<cont>, env:<lex>})
	{expr:<expr>, cont:<apply>, env:<dyn>} => reduce_args
**/
BEH_DECL(function)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* dyn = map_get(msg, env_symbol);
	CONS* state = MINE;
	CONS* vars = map_get(state, ATOM("vars"));
	CONS* body = map_get(state, ATOM("body"));
	CONS* lex = map_get(state, env_symbol);
	CONS* apply;

	DBUG_ENTER("function");
	state = NIL;
	state = map_put(state, env_symbol, lex);
	state = map_put(state, cont_symbol, cont);
	state = map_put(state, ATOM("body"), body);
	state = map_put(state, ATOM("vars"), vars);
	apply = ACTOR(eval_function, state);
	msg = NIL;
	msg = map_put(msg, env_symbol, dyn);
	msg = map_put(msg, cont_symbol, apply);
	msg = map_put(msg, expr_symbol, expr);
	SEND(a_reduce_args, msg);
	DBUG_RETURN;
}

/**
reduce_function(expr:<expr>, cont:<cont>, env:<env>){} =
	{expr:actor(function, {vars:first(<expr>), body:second(<expr>), env:<env>})} => <cont>
**/
BEH_DECL(reduce_function)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state;
	CONS* actor;

	DBUG_ENTER("reduce_function");
	state = NIL;
	state = map_put(state, env_symbol, env);
	state = map_put(state, ATOM("body"), car(cdr(expr)));
	state = map_put(state, ATOM("vars"), car(expr));
	actor = ACTOR(function, state);
	SEND(cont, map_put(NIL, expr_symbol, actor));
	DBUG_RETURN;
}

/**
eval_send(expr:<expr>){cont:<cont>, env:<env>} =
	IF actor?(first(expr))
		second(expr) => first(expr)
		{expr:second(expr)} => <cont>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(eval_send)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
/*	CONS* env = map_get(state, env_symbol); */
	CONS* actor;

	DBUG_ENTER("eval_send");
	actor = car(expr);
	if (actorp(actor)) {
		DBUG_PRINT("", ("actor=%s", cons_to_str(actor)));
		expr = car(cdr(expr));
		DBUG_PRINT("", ("msg=%s", cons_to_str(expr)));
		SEND(actor, expr);
		/* FIXME: should send return NIL, rather than the message sent? */
	} else {	/* error */
		SEND(an_error, ATOM("send: actor required"));
		expr = undefined_symbol;
	}
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/*
 * FIXME:  REDUCE-ACTOR MESSAGE DISPATCH DOES NOT PREVENT RE-ENTRANCY,
 *         IT REQUIRES DISPATCH TO MULTIPLE PRIMITIVE ACTORS TO PROCESS
 *         EACH REDUCE-MESSAGE.
 *         THIS ALLOWS ANOTHER MESSAGE TO BE DISPATCHED TO THIS ACTOR
 *         BEFORE PROCESSING OF THE CURRENT MESSAGE IS COMPLETE,
 *         WHICH COULD BE BEFORE A REPLACEMENT BEHAVIOR IS SPECIFIED.
 */

BEH_DECL(eval_become);		/* FORWARD DECLARATION */

/**
invoke_actor(<msg>){beh:<beh>, env:<env>} =
	{expr:quote_list(<msg>), cont:sink, env:<env>} => <beh>
**/
BEH_DECL(invoke_actor)
{
	CONS* msg = WHAT;
	CONS* state = MINE;
	CONS* beh = map_get(state, beh_symbol);
	CONS* env = map_get(state, env_symbol);
	CONS* expr = NIL;

	DBUG_ENTER("invoke_actor");
	DBUG_PRINT("", ("beh=%s", cons_to_str(beh)));
	DBUG_PRINT("", ("msg=%s", cons_to_str(msg)));
	while (consp(msg) && !nilp(msg)) {			/* quote already-evaluated arguments */
		expr = cons(cons(ATOM("literal"), cons(car(msg), NIL)), expr);
		/* FIXME: consider using CQ macros for faster list construction */
		msg = cdr(msg);
	}
	expr = reverse(expr);
	DBUG_PRINT("", ("env=%s", cons_to_str(env)));
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, a_sink);	/* actors do not return a value */
	msg = map_put(msg, expr_symbol, expr);
	SEND(beh, msg);
	DBUG_RETURN;
}

/**
eval_actor(expr:<expr>){cont:<cont>, env:<env>} =
	<binding> := map_find(env, self())
	IF equal?(rest(<binding>, _)) AND actor?(first(<expr>))
		<actor> := actor(invoke_actor, {beh:first(expr) , env:<env>})
		rplacd(<binding>, <actor>)
		{expr:<actor>} => <cont>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(eval_actor)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	CONS* binding;
	CONS* beh;
	
	DBUG_ENTER("eval_actor");
	binding = map_find(env, self_symbol);
	DBUG_PRINT("", ("binding=%s", cons_to_str(binding)));
	beh = car(expr);
	DBUG_PRINT("", ("beh=%s", cons_to_str(beh)));
	if ((cdr(binding) == undefined_symbol) && actorp(beh)) {
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, beh_symbol, beh);
		expr = ACTOR(invoke_actor, state);
		rplacd(binding, expr);
		DBUG_PRINT("", ("env=%s", cons_to_str(env)));
		DBUG_PRINT("", ("actor=%s", cons_to_str(expr)));
	} else {	/* error */
		SEND(an_error, ATOM("actor: behavior required"));
		expr = undefined_symbol;
	}
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
reduce_actor(expr:<expr>, cont:<cont>, env:<env>){} =
	<become> := actor(reduce_and_apply, {beh:eval_become})
	<env> := prepend(prepend(become, <become>), <env>)
	<env> := prepend(prepend(self, _), <env>)
	<actor> := actor(eval_actor, {cont:<cont>, env:<env>})
	{expr:<expr>, cont:<actor>, env:<env>} => reduce_args
**/
BEH_DECL(reduce_actor)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state;
	CONS* actor;

	DBUG_ENTER("reduce_actor");
	env = map_put(env, ATOM("become"), a_become);			/* bind "become" in actor's environment */
	env = map_put(env, self_symbol, undefined_symbol);		/* reserve space for "self" binding */
	DBUG_PRINT("", ("env=%s", cons_to_str(env)));
	state = NIL;
	state = map_put(state, env_symbol, env);
	state = map_put(state, cont_symbol, cont);
	actor = ACTOR(eval_actor, state);
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, env_symbol, env);
	msg = map_put(msg, cont_symbol, actor);
	msg = map_put(msg, expr_symbol, expr);
	SEND(a_reduce_args, msg);
	DBUG_RETURN;
}

/**
eval_become(expr:<expr>){cont:<cont>, env:<env>} =
	IF actor?(first(<expr>))
		<actor> := map_get(<env>, self)
		replace_beh(<actor>, first(<expr>));
		{expr:<actor>} => <cont>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(eval_become)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);
	CONS* actor;
	CONS* beh;
	
	DBUG_ENTER("eval_become");
	actor = map_get(env, self_symbol);
	DBUG_PRINT("", ("actor=%s", cons_to_str(actor)));
	if (actorp(actor) && (car(actor) == MK_FUNC(invoke_actor))) {
		beh = car(expr);
		if (actorp(beh)) {
			DBUG_PRINT("", ("beh=%s", cons_to_str(beh)));
			state = NIL;
			state = map_put(state, env_symbol, env);
			state = map_put(state, beh_symbol, beh);
			rplacd(actor, state);		/* replace actor state & behavior function */
			expr = actor;
			DBUG_PRINT("", ("actor'=%s", cons_to_str(expr)));
		} else {	/* error */
			SEND(an_error, ATOM("become: behavior required"));
			expr = undefined_symbol;
		}
	} else {	/* error */
		SEND(an_error, ATOM("become: self must be an actor"));
		expr = undefined_symbol;
	}
	SEND(cont, map_put(NIL, expr_symbol, expr));
	DBUG_RETURN;
}

/**
reduce_background(expr:<expr>, cont:<cont>, env:<env>){} =
	{expr:<expr>, cont:sink, env:<env>} => reduce_args
	{expr:NIL} => <cont>
**/
BEH_DECL(reduce_background)
{
	CONS* msg = WHAT;
	CONS* cont = map_get(msg, cont_symbol);

	DBUG_ENTER("reduce_background");
	SEND(cont, map_put(NIL, expr_symbol, NIL));
	msg = map_put(msg, cont_symbol, a_sink);		/* add to inbound message */
	SEND(a_reduce_args, msg);		/* FIXME: use parallel evaluation, rather than sequential, when available */
	DBUG_RETURN;
}

/**
apply_seq(expr:<expr>){acc:<acc>, form:<form>, cont:<cont>, env:<env>} =
	<acc> := prepend(<expr>, <acc>)
	IF empty?(<form>)
		{expr:reverse(<acc>)} => <cont>
	ELSE
		become(apply_seq, {acc:<acc>, form:rest(<form>), cont:<cont>, env:<env>})
		{expr:first(<form>), cont:self(), env:<env>} => reduce
**/
BEH_DECL(apply_seq)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* acc = map_get(state, ATOM("acc"));
	CONS* form = map_get(state, form_symbol);
	CONS* cont = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);

	DBUG_ENTER("apply_seq");
	DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
	acc = cons(expr, acc);
	if (nilp(form) || !consp(form)) {
		msg = NIL;
		expr = reverse(acc);
		DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
		msg = map_put(msg, expr_symbol, expr);
		SEND(cont, msg);
	} else {
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, cont_symbol, cont);
		state = map_put(state, form_symbol, cdr(form));
		state = map_put(state, ATOM("acc"), acc);
		BECOME(THIS, state);
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, SELF);
		msg = map_put(msg, expr_symbol, car(form));
		SEND(a_reduce, msg);
	}
	DBUG_RETURN;
}

/**
reduce_seq(expr:<expr>, cont:<cont>, env:<env>){} =
	IF list?(<expr>)
		IF empty?(<expr>)
			{expr:()} => <cont>
		ELSE
			<apply> := actor(apply_seq, {acc:(), form:rest(<expr>), cont:<cont>, env:<env>})
			{expr:first(<expr>), cont:<apply>, env:<env>} => reduce
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(reduce_seq)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state;
	CONS* actor;

	DBUG_ENTER("reduce_seq");
	if (nilp(expr) || !consp(expr)) {
		SEND(cont, map_put(NIL, expr_symbol, expr));
	} else {
		DBUG_PRINT("", ("form=%s", cons_to_str(expr)));
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, cont_symbol, cont);
		state = map_put(state, form_symbol, cdr(expr));
		state = map_put(state, ATOM("acc"), NIL);
		actor = ACTOR(apply_seq, state);
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, actor);
		msg = map_put(msg, expr_symbol, car(expr));
		SEND(a_reduce, msg);
	}
	DBUG_RETURN;
}

/**
collect_par(op:<op>, i:<i>, expr:<expr>){acc:<acc>, n:<n>, cont:<cont>}
	IF equal?(<op>, n)
		<n> := <n> + <i>
	ELIF equal?(<op>, i)
		<acc> := map_put(<acc>, <i>, <expr>)
		<n> := <n> - 1
	IF zero?(n)
		{expr:<acc>} => <cont>
	ELSE
		become(collect_par, {acc:<acc>, n:<n>, cont:<cont>})
**/
BEH_DECL(collect_par)
{
	CONS* msg = WHAT;
	CONS* op = map_get(msg, ATOM("op"));
	CONS* i = map_get(msg, ATOM("i"));
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* acc = map_get(state, ATOM("acc"));
	CONS* n = map_get(state, ATOM("n"));
	CONS* cont = map_get(state, cont_symbol);

	DBUG_ENTER("collect_par");
	assert(numberp(i));
	assert(numberp(n));
	assert(consp(acc));
	assert(actorp(cont));
	if (op == ATOM("n")) {
		n = NUMBER(MK_INT(n) + MK_INT(i));
	} else if (op == ATOM("i")) {
		DBUG_PRINT("", ("value=%s", cons_to_str(expr)));
		acc = map_put(acc, i, expr);
		n = NUMBER(MK_INT(n) - 1);
	}
	DBUG_PRINT("", ("n=%d", MK_INT(n)));
	if (n == NUMBER(0)) {
		DBUG_PRINT("", ("expr=%s", cons_to_str(acc)));
		SEND(cont, map_put(NIL, expr_symbol, acc));
	} else {
		state = NIL;
		state = map_put(state, cont_symbol, cont);
		state = map_put(state, ATOM("n"), n);
		state = map_put(state, ATOM("acc"), acc);
		BECOME(THIS, state);
	}
	DBUG_RETURN;
}

/**
eval_par(expr:<expr>){i:<i>, cont:<collect>}
	{op:i, i:<i>, expr:<expr>} => <collect>
**/
BEH_DECL(eval_par)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* state = MINE;
	CONS* i = map_get(state, ATOM("i"));
	CONS* collect = map_get(state, cont_symbol);

	DBUG_ENTER("eval_par");
	assert(numberp(i));
	assert(actorp(collect));
	DBUG_PRINT("", ("i=%d expr=%s", MK_INT(i), cons_to_str(expr)));
	msg = NIL;
	msg = map_put(msg, ATOM("op"), ATOM("i"));
	msg = map_put(msg, ATOM("i"), i);
	msg = map_put(msg, expr_symbol, expr);
	SEND(collect, msg);
	DBUG_RETURN;
}

/**
apply_par(i:<i>, form:<form>){cont:<collect>, env:<env>} =
	IF empty?(<form>)
		{op:n, i:<i>} => <collect>
	ELSE
		<actor> = actor(eval_par, {i:<i>, cont:<collect>})
		{expr:first(<form>), cont:<actor>, env:<env>} => reduce
		{i:(<i> + 1), form:rest(<form>)} => self()
**/
BEH_DECL(apply_par)
{
	CONS* msg = WHAT;
	CONS* i = map_get(msg, ATOM("i"));
	CONS* form = map_get(msg, form_symbol);
	CONS* state = MINE;
	CONS* collect = map_get(state, cont_symbol);
	CONS* env = map_get(state, env_symbol);

	DBUG_ENTER("apply_par");
	assert(numberp(i));
	assert(consp(form));
	assert(actorp(collect));
	DBUG_PRINT("", ("i=%d form=%s", MK_INT(i), cons_to_str(form)));
	if (nilp(form)) {
		msg = NIL;
		msg = map_put(msg, ATOM("op"), ATOM("n"));
		msg = map_put(msg, ATOM("i"), i);
		SEND(collect, msg);		
	} else {
		CONS* actor;
		
		state = NIL;
		state = map_put(state, cont_symbol, collect);
		state = map_put(state, ATOM("i"), i);
		actor = ACTOR(eval_par, state);
		msg = NIL;
		msg = map_put(msg, env_symbol, env);
		msg = map_put(msg, cont_symbol, actor);
		msg = map_put(msg, expr_symbol, car(form));
		SEND(a_reduce, msg);
		msg = NIL;
		msg = map_put(msg, form_symbol, cdr(form));
		msg = map_put(msg, ATOM("i"), NUMBER(MK_INT(i) + 1));
		SEND(SELF, msg);		
	}
	DBUG_RETURN;
}

/**
reduce_par(expr:<expr>, cont:<cont>, env:<env>){} =
	IF list?(<expr>)
		IF empty?(<expr>)
			{expr:()} => <cont>
		ELSE
			<collect> := actor(collect_par, {acc:(), n:0, cont:<cont>})
			<apply> := actor(apply_par, {cont:<collect>, env:<env>})
			{i:0, form:<expr>} => <apply>
	ELSE
		{expr:_} => <cont>
**/
BEH_DECL(reduce_par)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
	CONS* env = map_get(msg, env_symbol);
	CONS* state = NIL;

	DBUG_ENTER("reduce_par");
	if (nilp(expr) || !consp(expr)) {
		msg = NIL;
		msg = map_put(msg, expr_symbol, expr);
		SEND(cont, msg);
	} else {
		CONS* collect;
		CONS* apply;
		
		DBUG_PRINT("", ("form=%s", cons_to_str(expr)));
		state = NIL;
		state = map_put(state, cont_symbol, cont);
		state = map_put(state, ATOM("n"), NUMBER(0));
		state = map_put(state, ATOM("acc"), NIL);
		collect = ACTOR(collect_par, state);
		state = NIL;
		state = map_put(state, env_symbol, env);
		state = map_put(state, cont_symbol, collect);
		apply = ACTOR(apply_par, state);
		msg = NIL;
		msg = map_put(msg, ATOM("i"), NUMBER(0));
		msg = map_put(msg, form_symbol, expr);
		SEND(apply, msg);
	}
	DBUG_RETURN;
}

/**
reduce_dbug(expr:<expr>, cont:<cont>, env:<env>){} =
	{expr:first(<expr>)} => <cont>
**/
BEH_DECL(reduce_dbug)
{
	CONS* msg = WHAT;
	CONS* expr = map_get(msg, expr_symbol);
	CONS* cont = map_get(msg, cont_symbol);
/*	CONS* env = map_get(msg, env_symbol); */
	CONS* args;

	DBUG_ENTER("reduce_dbug");
	DBUG_PRINT("", ("expr=%s", cons_to_str(expr)));
	args = car(expr);
	DBUG_PRINT("", ("args=%s", cons_to_str(args)));
	if (atomp(args)) {
		char* s;
		char* t;
		
		s = atom_str(args);	 /* DBUG_PUSH needs this string to stick around */
		t = (char*)malloc(strlen(s) + 1);
		strcpy(t, s);
		DBUG_PUSH(t);
		DBUG_PRINT("", ("dbug=%s", cons_to_str(args)));
		SEND(cont, map_put(NIL, expr_symbol, args));
	}
	DBUG_RETURN;
}

static CONFIG*
init_reduce()
{
	CONFIG* cfg;
	
	if (reduce_cfg != NULL) {
		return reduce_cfg;
	}
	DBUG_PRINT("", ("initializing configuration"));
	cfg = new_configuration(1000);
	reduce_cfg = cfg;

	DBUG_PRINT("", ("initializing actors"));
	undefined_symbol = ATOM("_");
	true_symbol = ATOM("TRUE");
	false_symbol = ATOM("FALSE");
	self_symbol = ATOM("self");
	expr_symbol = ATOM("expr");
	cont_symbol = ATOM("cont");
	env_symbol = ATOM("env");
	beh_symbol = ATOM("beh");
	form_symbol = ATOM("form");
	
	a_reduce = CFG_ACTOR(cfg, reduce_expr, NIL);
	cfg_add_gc_root(cfg, a_reduce);
	a_reduce_args = CFG_ACTOR(cfg, reduce_seq, NIL);
	cfg_add_gc_root(cfg, a_reduce_args);
	a_become = CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_become)));
	cfg_add_gc_root(cfg, a_become);

	an_error = CFG_ACTOR(cfg, error_msg, NIL);
	a_print = CFG_ACTOR(cfg, print_msg, NIL);
	a_sink = CFG_ACTOR(cfg, sink_beh, NIL);

	DBUG_PRINT("", ("initializing environment"));
	eval_env = map_put(eval_env, ATOM("System_Info"), system_info());
	eval_env = map_put(eval_env, ATOM("#"), CFG_ACTOR(cfg, reduce_dbug, NIL));

	eval_env = map_put(eval_env, ATOM("@sink"), a_sink);
	eval_env = map_put(eval_env, ATOM("@error"), an_error);
	eval_env = map_put(eval_env, ATOM("@print"), a_print);

	eval_env = map_put(eval_env, ATOM("product"), CFG_ACTOR(cfg, reduce_product, NIL));
	
	eval_env = map_put(eval_env, ATOM("sum"), CFG_ACTOR(cfg, reduce_sum, NIL));
	
	eval_env = map_put(eval_env, ATOM("actor"), CFG_ACTOR(cfg, reduce_actor, NIL));

	eval_env = map_put(eval_env, ATOM("send"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_send))));

	eval_env = map_put(eval_env, ATOM("par"), CFG_ACTOR(cfg, reduce_par, NIL));
	
	eval_env = map_put(eval_env, ATOM("background"), CFG_ACTOR(cfg, reduce_background, NIL));

	eval_env = map_put(eval_env, ATOM("template"), CFG_ACTOR(cfg, reduce_template, NIL));

	eval_env = map_put(eval_env, ATOM("function"), CFG_ACTOR(cfg, reduce_function, NIL));

	eval_env = map_put(eval_env, ATOM("define"), CFG_ACTOR(cfg, reduce_define, NIL));

	eval_env = map_put(eval_env, ATOM("reduce"), CFG_ACTOR(cfg, reduce_reduce, NIL));
	
	eval_env = map_put(eval_env, ATOM("preceed?"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_preceedp))));

	eval_env = map_put(eval_env, ATOM("zero?"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_zerop))));

	eval_env = map_put(eval_env, ATOM("number?"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_numberp))));

	eval_env = map_put(eval_env, ATOM("equal?"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_equalp))));

	eval_env = map_put(eval_env, ATOM("list"),		/* FIXME:  should probably just use reduce_seq directly */
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_list))));
	
	eval_env = map_put(eval_env, ATOM("seq"), CFG_ACTOR(cfg, reduce_seq, NIL));
	
	eval_env = map_put(eval_env, ATOM("if"), CFG_ACTOR(cfg, reduce_if, NIL));

	eval_env = map_put(eval_env, ATOM("empty?"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_emptyp))));

	eval_env = map_put(eval_env, ATOM("list?"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_listp))));

	eval_env = map_put(eval_env, ATOM("first"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_first))));

	eval_env = map_put(eval_env, ATOM("rest"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_rest))));

	eval_env = map_put(eval_env, ATOM("prepend"),
		CFG_ACTOR(cfg, reduce_and_apply, map_put(NIL, beh_symbol, MK_FUNC(eval_prepend))));

	eval_env = map_put(eval_env, ATOM("literal"), CFG_ACTOR(cfg, reduce_literal, NIL));

	eval_env = map_put(eval_env, self_symbol, a_sink);		/*** << WARNING!! >>   THIS BINDING MUST BE LAST! ***/
															/*** IMPLEMENTATION OF extend_env() DEPENDS ON IT ***/

	cfg_add_gc_root(cfg, eval_env);
	return cfg;
}

BOOL
reduce_line(CONFIG* cfg, char* line, CONS* cont)
{
	CONS* expr;
	CONS* msg;
	CONS* state;
	int n;

	DBUG_ENTER("reduce_line");
	assert(actorp(cont));
	init_reduce();
	DBUG_PRINT("", ("line=%s", line));
	expr = str_to_cons(line);
	if (expr == NULL) {
		fprintf(stderr, "Syntax Error: %s\n", line);
		DBUG_RETURN FALSE;
	}
	state = NIL;
	state = map_put(state, cont_symbol, cont);
	state = map_put(state, ATOM("label"), expr_symbol);
	msg = NIL;
	msg = map_put(msg, env_symbol, eval_env);
	msg = map_put(msg, cont_symbol, CFG_ACTOR(cfg, unlabel_msg, state));
	msg = map_put(msg, expr_symbol, expr);
	CFG_SEND(cfg, a_reduce, msg);
	cfg_start_gc(cfg);
	n = run_configuration(cfg, 1000000);
	if (cfg->q_count > 0) {
		TRACE(printf("queue length %d with %d budget remaining\n", cfg->q_count, n));
		DBUG_RETURN FALSE;
	}
	cfg_force_gc(cfg);
	DBUG_RETURN TRUE;
}

static void
read_reduce_print(FILE* in, CONS* out)
{
	char buf[1024];
	CONFIG* cfg;
	
	DBUG_ENTER("read_reduce_print");
	cfg = init_reduce();
	if ((out == NULL) || !actorp(out)) {
		out = a_sink;
	}
	while (fgets(buf, sizeof(buf), in)) {
		if (!reduce_line(cfg, buf, out)) {
			break;
		}
	}
	DBUG_RETURN;
}

static void
assert_reduce(char* expr, char* value)
{
	CONS* expect;
	CONS* state;
	CONS* cont;
	
	expect = str_to_cons(value);
	state = map_put(NIL, ATOM("expect"), expect);
	cont = CFG_ACTOR(reduce_cfg, assert_msg, state);
	if (!expect || !reduce_line(reduce_cfg, expr, cont)) {
		fprintf(stderr, "assert_reduce: FAILED! %s -> %s\n", expr, value);
		abort();
	}
}

void
test_reduce()
{
	DBUG_ENTER("test_reduce");
	init_reduce();
	TRACE(printf("--test_reduce--\n"));

	assert_reduce("0", "0");
	assert_reduce("1", "1");
	assert_reduce("TRUE", "TRUE");
	assert_reduce("FALSE", "FALSE");
	assert_reduce("()", "()");
	assert_reduce("()", "NIL");
	
	assert_reduce("(literal 0)", "0");
	assert_reduce("(literal TRUE)", "TRUE");
	assert_reduce("(literal ())", "()");
	assert_reduce("(literal (x))", "(x)");

	assert_reduce("(list? TRUE)", "FALSE");
	assert_reduce("(list? ())", "TRUE");
	assert_reduce("(list? (literal (x)))", "TRUE");

	assert_reduce("(empty? TRUE)", "FALSE");
	assert_reduce("(empty? ())", "TRUE");
	assert_reduce("(empty? (literal (x)))", "FALSE");

	assert_reduce("(first (literal (x)))", "x");
	assert_reduce("(rest (literal (x)))", "()");
	assert_reduce("(first (literal (x y)))", "x");
	assert_reduce("(rest (literal (x y)))", "(y)");
	assert_reduce("(prepend 0 ())", "(0)");
	assert_reduce("(prepend 0 (prepend 1 ()))", "(0 1)");
	assert_reduce("(prepend 0 (literal (1)))", "(0 1)");
	assert_reduce("(prepend (literal x) (literal (y)))", "(x y)");
	assert_reduce("(first (prepend 0 (literal (1))))", "0");
	assert_reduce("(rest (prepend 0 (literal (1))))", "(1)");
	assert_reduce("(first ())", "()");
	assert_reduce("(rest ())", "()");
	assert_reduce("(list)", "()");
	assert_reduce("(list 0)", "(0)");
	assert_reduce("(list 0 1)", "(0 1)");
	assert_reduce("(list (literal x) (literal (y)))", "(x (y))");

	assert_reduce("(seq)", "NIL");
	assert_reduce("(seq 1)", "(1)");
	assert_reduce("(seq 1 2)", "(1 2)");
	assert_reduce("(seq (literal x) (literal (y)))", "(x (y))");

	assert_reduce("(equal? 0 0)", "TRUE");
	assert_reduce("(equal? 0 1)", "FALSE");
	assert_reduce("(equal? NIL ())", "TRUE");
	assert_reduce("(equal? 0 ())", "FALSE");

	assert_reduce("(if TRUE (literal Yes) (?))", "Yes");
	assert_reduce("(if FALSE (?) (literal No))", "No");

	assert_reduce("(number? 0)", "TRUE");
	assert_reduce("(number? 1)", "TRUE");
	assert_reduce("(number? -1)", "TRUE");
	assert_reduce("(number? TRUE)", "FALSE");
	assert_reduce("(number? ())", "FALSE");

	assert_reduce("(zero? 0)", "TRUE");
	assert_reduce("(zero? 1)", "FALSE");
	assert_reduce("(zero? -1)", "FALSE");
	assert_reduce("(zero? TRUE)", "FALSE");
	assert_reduce("(zero? ())", "FALSE");

	assert_reduce("(sum 1 1)", "2");
	assert_reduce("(sum 1 2 3)", "6");
	assert_reduce("(sum 1 0 -1)", "0");
	assert_reduce("(sum 41)", "41");
	assert_reduce("(zero? (sum 0 -1))", "FALSE");
	assert_reduce("(zero? (sum 1 -1))", "TRUE");

	assert_reduce("(product 1 1)", "1");
	assert_reduce("(product -1 2 3)", "-6");
	assert_reduce("(product 1 0)", "0");
	assert_reduce("(product 41)", "41");
	assert_reduce("(zero? (product 0 -1))", "TRUE");
	assert_reduce("(zero? (product 1 -1))", "FALSE");
	
	assert_reduce("((template (x) (literal x)) 0)", "0");
	assert_reduce("((template (x y) (literal (y x))) TRUE FALSE)", "(FALSE TRUE)");
	assert_reduce("((template (x y) (literal (y x))) foo bar)", "(bar foo)");
	reduce_line(reduce_cfg, "(define 2tuple (template (x y) (prepend x (prepend y ()))))", a_sink);
	assert_reduce("(2tuple () (sum 1 2))", "(() 3)");
	
	assert_reduce("((function (x) (prepend x ())) 0)", "(0)");
	assert_reduce("((function (x) (if x FALSE TRUE)) TRUE)", "FALSE");
	assert_reduce("((function (x) (if x FALSE TRUE)) FALSE)", "TRUE");	
	assert_reduce("((function (x) (list (literal Hello) x)) (literal World))", "(Hello World)");
	assert_reduce("(((function (x) (function (y) (sum x y))) 3) 4)", "7");
	reduce_line(reduce_cfg, "(define fact (function (n) (if (zero? n) 1 (product n (fact (sum n -1))))))", a_sink);
	assert_reduce("(fact 3)", "6");

	assert_reduce("(reduce 0)", "0");
	assert_reduce("(reduce (literal (equal? 0 0)))", "TRUE");

	reduce_line(reduce_cfg, "(define not (function (x) (if x FALSE TRUE)))", a_sink);
	assert_reduce("(not TRUE)", "FALSE");
	assert_reduce("(not FALSE)", "TRUE");
#if 0
	assert_reduce("(not 0)", "_");		/* error expected */
#endif
	reduce_line(reduce_cfg, "(define symbol? (function (x) (not (list? x))))", a_sink);
	assert_reduce("(symbol? 0)", "TRUE");
	assert_reduce("(symbol? ())", "FALSE");
	assert_reduce("(symbol? (literal x))", "TRUE");
	reduce_line(reduce_cfg, "(define swap2 (function (x y) (prepend y (prepend x NIL))))", a_sink);
	assert_reduce("(swap2 0 1 2)", "(1 0)");
	
#if 0
	assert_reduce("(?)", "_");	/* this case produces error messages, but should pass */
#endif
	report_actor_usage(reduce_cfg);
	DBUG_RETURN;
}

CONS*
system_info()
{
	CONS* info = NIL;
	
	info = map_put(info, ATOM("Program"), ATOM(_Program));
	info = map_put(info, ATOM("Version"), ATOM(_Version));
	info = map_put(info, ATOM("Copyright"), ATOM(_Copyright));
	return info;
}

void
report_cons_stats()
{
	report_atom_usage();
	report_cons_usage();
}

void
usage(void)
{
	fprintf(stderr, "\
usage: %s [-ti] [-# dbug] file...\n",
		_Program);
	exit(EXIT_FAILURE);
}

void
banner(void)
{
	printf("%s v%s -- %s\n", _Program, _Version, _Copyright);
}

int
main(int argc, char** argv)
{
	int c;
	BOOL test_mode = FALSE;			/* flag to run unit tests */
	BOOL interactive = FALSE;		/* flag to run unit tests */

	DBUG_ENTER("main");
	DBUG_PROCESS(argv[0]);
	while ((c = getopt(argc, argv, "ti#:V")) != EOF) {
		switch(c) {
		case 't':	test_mode = TRUE;		break;
		case 'i':	interactive = TRUE;		break;
		case '#':	DBUG_PUSH(optarg);		break;
		case 'V':	banner();				exit(EXIT_SUCCESS);
		case '?':							usage();
		default:							usage();
		}
	}
	banner();
	if (test_mode) {
		test_reduce();	/* this test involves running the dispatch loop */
	} else {
		CONFIG* cfg;
		CONS* show_result;

		cfg = init_reduce();
		show_result = CFG_ACTOR(cfg, print_msg,
			map_put(NIL, ATOM("message"), ATOM("= ")));
		while (optind < argc) {
			FILE* f;
			char* filename = argv[optind++];
			DBUG_PRINT("", ("filename=%s", filename));
			if ((f = fopen(filename, "r")) == NULL) {
				perror(filename);
				exit(EXIT_FAILURE);
			}
			TRACE(printf("Loading %s...\n", filename));
			read_reduce_print(f, show_result);
			fclose(f);
		}
		if (interactive) {
			printf("Entering INTERACTIVE mode.\n");
			read_reduce_print(stdin, show_result);
		}
		report_actor_usage(cfg);
	}
	report_cons_stats();
	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
