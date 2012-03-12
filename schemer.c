/*
 * schemer.c -- A concurrent Scheme built on the Actor model
 *
 * Copyright 2008-2009 Dale Schumacher.  ALL RIGHTS RESERVED.
 */
static char	_Program[] = "Schemer";
static char	_Version[] = "2009-03-12";
static char	_Copyright[] = "Copyright 2008-2009 Dale Schumacher";

#include <getopt.h>
#include "abe.h"

#include "dbug.h"
DBUG_UNIT("schemer");

#define	mk_pair(h,t)	cons((h), (t))
#define	is_pair(p)		(consp(p) && !nilp(p))
#define	pr_head(p)		car(p)
#define	pr_tail(p)		cdr(p)

#define	mk_empty()		NIL
#define	is_empty(p)		nilp(p)
#define	mk_list(p,lst)	cons((p), (lst))
#define	is_list(p)		consp(p)
#define	lst_first(lst)	car(lst)
#define	lst_rest(lst)	cdr(lst)
#define	lst_second(lst)	car(cdr(lst))
#define	lst_third(lst)	car(cdr(cdr(lst)))

#define	UNDEFINED		(undefined())
#define	is_undefined(p)	((p)==UNDEFINED)

#define	TRUE_SYMBOL		(boolean(TRUE))
#define	FALSE_SYMBOL	(boolean(FALSE))
#define	is_boolean(p)	(((p)==TRUE_SYMBOL)||((p)==FALSE_SYMBOL))

#define	is_null(p)		nilp(p)
#define	is_number(p)	numberp(p)
#define	is_symbol(p)	(atomp(p)&&!is_boolean(p))
#define	is_actor(p)		actorp(p)
#define	is_const(p)		(is_null(p)||is_number(p)||is_boolean(p)||is_undefined(p))

static CONS* eval__actor = NULL;
static CONS* eval_list__actor = NULL;
static CONS* eval_par__actor = NULL;
static CONS* eval_seq__actor = NULL;
static CONS* undefined__value = NULL;

static CONS*
undefined()
{
	if (undefined__value == NULL) {
		undefined__value = ATOM("_");
		assert(undefined__value != NULL);
	}
	return undefined__value;
}

static CONS* true__value = NULL;
static CONS* false__value = NULL;

static CONS*
boolean(BOOL p)
{
	if (true__value == false__value) {
		true__value = ATOM("#t");
		false__value = ATOM("#f");
		assert(true__value != false__value);
	}
	return (p ? true__value : false__value);
}

#define FAIL(env,exp)	SEND((env), mk_pair(\
	ACTOR(command_beh, (exp)), \
	mk_pair(mk_list(ATOM("get"), mk_list(UNDEFINED, mk_empty())), (env)) ))

static BOOL
have_n_args(int n, CONS* args)
{
	while (!is_empty(args)) {
		if (!is_pair(args)) {
			return FALSE;
		}
		args = pr_tail(args);
		--n;
	}
	return ((n == 0) ? TRUE : FALSE);
}

BEH_DECL(abort_beh)
{
	char* s = cons_to_str(WHAT);

	DBUG_ENTER("abort_beh");
	DBUG_PRINT("", ("ABORT! %s", s));
	fprintf(stderr, "ABORT! %s\n", s);
	abort();
	DBUG_RETURN;
}

BEH_DECL(println_beh)
{
	DBUG_ENTER("println_beh");
	DBUG_PRINT("", ("WHAT = %s", cons_to_str(WHAT)));
	if (!nilp(MINE)) {
		printf("%s ", cons_to_str(MINE));
	}
	printf("%s\n", cons_to_str(WHAT));
	DBUG_RETURN;
}

/**
one_shot_beh:
	BEHAVIOR $a
	$m -> [
		SEND $a $m
		BECOME $sink_beh NIL
	]
	DONE
**/
BEH_DECL(one_shot_beh)
{
	DBUG_ENTER("one_shot_beh");
	SEND(MINE, WHAT);
	BECOME(sink_beh, NIL);
	DBUG_RETURN;
}

/**
label_beh:
	BEHAVIOR $to:$label
	$value -> SEND $to $label:$value
	DONE
**/
BEH_DECL(label_beh)
{
	CONS* to = pr_head(MINE);
	CONS* label = pr_tail(MINE);
	CONS* value = WHAT;

	DBUG_ENTER("label_beh");
	SEND(to, mk_pair(label, value));
	DBUG_RETURN;
}

/**
command_beh:
	BEHAVIOR $m
	$a -> SEND $a $m
	DONE
**/
BEH_DECL(command_beh)
{
	DBUG_ENTER("command_beh");
	if (actorp(WHAT)) {
		SEND(WHAT, MINE);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_beh:
	BEHAVIOR $to:($expr:$env)
	$a -> [
		IF actor?($a) [
			SEND $a $m
		] ELSE [
			FAIL $a
		]
	]
	DONE
**/
BEH_DECL(apply_beh)
{
	CONS* p;

	DBUG_ENTER("apply_beh");
	if (is_pair(MINE) && is_pair(p = pr_tail(MINE))) {
/*		CONS* to = pr_head(MINE); */
/*		CONS* expr = pr_head(p); */
		CONS* env = pr_tail(p);

		if (actorp(WHAT)) {
			SEND(WHAT, MINE);
		} else {
			p = mk_list(WHAT, mk_empty());
			p = mk_list(ATOM("invalid-application"), p);
			FAIL(env, p);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
binding_beh:
	BEHAVIOR $next:($name:$value)
	$to:((define $n $v):$env) -> [
		IF equals($n, $name) [
			BECOME THIS $next:($n:$v)
			SEND $to "redefined"
		] ELSE [
			SEND $next WHAT
		]
	]
	$to:((set! $n $v):$env) -> [
		IF equals($n, $name) [
			BECOME THIS $next:($n:$v)
			SEND $to "ok"
		] ELSE [
			SEND $next WHAT
		]
	]
	$to:((get $n):$env) -> [
		IF equals($n, $name) [
			SEND $to $value
		] ELSE [
			SEND $next WHAT
		]
	]
	DONE
**/
BEH_DECL(binding_beh)
{
	CONS* next = pr_head(MINE);
	CONS* name;
	CONS* value;
	CONS* p;
	CONS* state;

	DBUG_ENTER("binding_beh");
	XDBUG_PRINT("", ("next = %s", cons_to_str(next)));
	p = pr_tail(MINE);
	name = pr_head(p);
	XDBUG_PRINT("", ("name = %s", cons_to_str(name)));
	value = pr_tail(p);
	XDBUG_PRINT("", ("value = %s", cons_to_str(value)));
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
/*		CONS* env = pr_tail(p); */
		CONS* verb;
		CONS* n;

		XDBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		p = expr;
		verb = lst_first(p);
		p = lst_rest(p);
		n = lst_first(p);
		p = lst_rest(p);
		if (n == name) {
			if (verb == ATOM("get")) {
				DBUG_PRINT("", ("name = %s", cons_to_str(name)));
				DBUG_PRINT("", ("value = %s", cons_to_str(value)));
				SEND(to, value);
			} else if ((verb == ATOM("define")) || (verb == ATOM("set!"))) {
				CONS* v = lst_first(p);

				DBUG_PRINT("", ("n = %s", cons_to_str(n)));
				DBUG_PRINT("", ("v = %s", cons_to_str(v)));
				state = mk_pair(n, v);
				state = mk_pair(next, state);
				BECOME(THIS, state);
				SEND(to, ATOM((verb == ATOM("define")) ? "redefined" : "ok"));
			} else {
				DBUG_PRINT("", ("Unknown! %s", cons_to_str(expr)));
				abort();
			}
		} else {
			SEND(next, WHAT);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
frame_beh:
	BEHAVIOR $parent
	$to:((define $n $v):$env) -> [
		e: ACTOR THIS $parent
		BECOME $binding_beh $e:($n:$v)
		SEND $to "ok"
	]
	$to:((set! $n $v):$env) -> [
		IF null?($parent) [
			FAIL "unknown"
		] ELSE [
			SEND $parent WHAT
		]
	]
	$to:((get $n):$env) -> [
		IF null?($parent) [
			FAIL "unknown"
		] ELSE [
			SEND $parent WHAT
		]
	]
	DONE
**/
BEH_DECL(frame_beh)
{
	CONS* parent = MINE;
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("frame_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);
		CONS* verb;
		CONS* n;

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		p = expr;
		verb = lst_first(p);
		p = lst_rest(p);
		n = lst_first(p);
		p = lst_rest(p);
		if (verb == ATOM("define")) {
			CONS* v = lst_first(p);

			DBUG_PRINT("", ("n = %s", cons_to_str(n)));
			DBUG_PRINT("", ("v = %s", cons_to_str(v)));
			a = ACTOR(THIS, parent);
			state = mk_pair(n, v);
			state = mk_pair(a, state);
			BECOME(binding_beh, state);
			SEND(to, ATOM("ok"));
		} else if (is_actor(parent)) {
			SEND(parent, WHAT);
		} else {
			DBUG_PRINT("", ("unknown"));
			msg = mk_list(n, mk_empty());
			msg = mk_list(ATOM("unknown-symbol"), msg);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_lambda_beh:
	BEHAVIOR $to:($lambda:$env)
	$args -> [
		vars: lst_first($lambda)
		body: lst_rest($lambda)
		IF empty?($vars) -> [
			IF empty?($args) [
				e: ACTOR $eval_seq_beh NIL
				SEND $e $to:($body:$env)
			] ELSE [
				FAIL "too-many-args"
			]
		] ELIF symbol?($vars) -> [
			l: NIL:$body
			b: ACTOR $binding_beh $env:($vars:$args)
			BECOME THIS $to:($l:$b)
			SEND SELF NIL
		] ELIF pair?($vars) -> [
			IF empty?($args) [
				FAIL "too-few-args"
			] ELSE [
				name: lst_first($vars)
				value: lst_first($args)
				l: lst_rest($var):$body
				b: ACTOR $binding_beh $env:($name:$value)
				BECOME THIS $to:($l:$b)
				SEND SELF lst_rest(args)
			]
		] ELSE [
			FAIL "bad-form"
		]
	]
	DONE
**/
BEH_DECL(apply_lambda_beh)
{
	CONS* args = WHAT;
	CONS* to;
	CONS* lambda;
	CONS* vars;
	CONS* body;
	CONS* env;
	CONS* p = MINE;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("apply_lambda_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	to = pr_head(p);
	p = pr_tail(p);
	lambda = pr_head(p);
	env = pr_tail(p);
	vars = lst_first(lambda);
	DBUG_PRINT("", ("vars = %s", cons_to_str(vars)));
	body = lst_rest(lambda);
	DBUG_PRINT("", ("body = %s", cons_to_str(body)));
	if (is_empty(vars)) {
		if (is_empty(args)) {
			/* evaluate body in extended environment */
			DBUG_PRINT("", ("evaluate body"));
			msg = mk_pair(body, env);
			msg = mk_pair(to, msg);
			SEND(eval_seq__actor, msg);
		} else {
			DBUG_PRINT("", ("too many args"));
			FAIL(env, mk_list(ATOM("too-many-args"), args));
		}
	} else if (is_symbol(vars)) {
		/* bind symbol to remaining args */
		DBUG_PRINT("", ("bind remaining args"));
		state = mk_pair(vars, args);
		state = mk_pair(env, state);
		a = ACTOR(binding_beh, state);
		state = mk_pair(NIL, body);
		state = mk_pair(state, a);
		state = mk_pair(to, state);
		BECOME(THIS, state);
		SEND(SELF, NIL);
	} else if (is_pair(vars)) {
		if (is_empty(args)) {
			DBUG_PRINT("", ("too few args"));
			FAIL(env, mk_list(ATOM("too-few-args"), vars));
		} else {
			/* extend environment with new binding */
			DBUG_PRINT("", ("extend environment"));
			state = mk_pair(lst_first(vars), lst_first(args));
			state = mk_pair(env, state);
			a = ACTOR(binding_beh, state);
			state = mk_pair(lst_rest(vars), body);
			state = mk_pair(state, a);
			state = mk_pair(to, state);
			BECOME(THIS, state);
			SEND(SELF, lst_rest(args));
		}
	} else {
		DBUG_PRINT("", ("bad form"));
		FAIL(env, mk_list(ATOM("bad-form"), vars));
	}
	DBUG_RETURN;
}

/**
lambda_beh:
	BEHAVIOR $lambda:$lex
	$to:($expr:$dyn) -> [
		env: ACTOR $frame_beh $lex
		k: ACTOR $apply_lambda_beh $to:($lambda:$env)
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$dyn)		
	]
	DONE
**/
BEH_DECL(lambda_beh)
{
	CONS* lambda = pr_head(MINE);
	CONS* lex = pr_tail(MINE);
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("lambda_beh");
	DBUG_PRINT("", ("lambda = %s", cons_to_str(lambda)));
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* dyn = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		a = ACTOR(frame_beh, lex);
		state = mk_pair(lambda, a);
		state = mk_pair(to, state);
		a = ACTOR(apply_lambda_beh, state);
		msg = mk_pair(expr, dyn);
		msg = mk_pair(a, msg);
		SEND(eval_par__actor, msg);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
eval_lambda_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		a: ACTOR lambda_beh $expr:$env
		SEND $to $a
	]
	DONE
**/
BEH_DECL(eval_lambda_beh)
{
	CONS* p;
	CONS* a;

	DBUG_ENTER("eval_lambda_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		a = ACTOR(lambda_beh, mk_pair(expr, env));
		SEND(to, a);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_form_beh:
	BEHAVIOR $to:$env
	$expr -> [
		e: ACTOR $eval_beh NIL
		SEND $e $to:($expr:$env)
	]
**/
BEH_DECL(apply_form_beh)
{
	CONS* expr = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* msg;

	DBUG_ENTER("apply_form_beh");
	DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
	msg = mk_pair(expr, env);
	msg = mk_pair(to, msg);
	SEND(eval__actor, msg);
	DBUG_RETURN;
}

/**
form_beh:
	BEHAVIOR $form:$lex
	$to:($expr:$dyn) -> [
		env: ACTOR $frame_beh $lex
		a: ACTOR $apply_form_beh $to:$dyn
		k: ACTOR $apply_lambda_beh $a:($form:$env)
		SEND $k $expr
	]
	DONE
**/
BEH_DECL(form_beh)
{
	CONS* form = pr_head(MINE);
	CONS* lex = pr_tail(MINE);
	CONS* p;
	CONS* state;
	CONS* a;
	CONS* env;

	DBUG_ENTER("form_beh");
	DBUG_PRINT("", ("form = %s", cons_to_str(form)));
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* dyn = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		env = ACTOR(frame_beh, lex);
		state =  mk_pair(to, dyn);
		a = ACTOR(apply_form_beh, state);
		state = mk_pair(form, env);
		state = mk_pair(a, state);
		a = ACTOR(apply_lambda_beh, state);
		SEND(a, expr);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
eval_form_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		a: ACTOR form_beh $expr:$env
		SEND $to $a
	]
	DONE
**/
BEH_DECL(eval_form_beh)
{
	CONS* p;
	CONS* a;

	DBUG_ENTER("eval_form_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		a = ACTOR(form_beh, mk_pair(expr, env));
		SEND(to, a);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
// (let ((<var_1> <exp_1>) (<var_2> <exp_2>) ... (<var_n> <exp_n>)) <body>) =>
// ((lambda (<var_1> <var_2> ... <var_n>) <body>) <exp_1> <exp_2> ... <exp_n>)
(define let (form (bindings . body) 
	(define (var-list bindings) 
		(cond ((null? bindings) '())
			  ((= (length (car bindings)) 2) (cons (caar bindings) (var-list (cdr bindings))))
			  (else (fail! 'let 'syntax-error)) ))
	(define (init-list bindings) 
		(cond ((null? bindings) '())
			  ((= (length (car bindings)) 2) (cons (cadar bindings) (init-list (cdr bindings))))
			  (else (fail! 'let 'syntax-error)) ))
	(cons 
		(cons 'lambda 
			  (cons (var-list bindings) body))
		(init-list bindings)) ))
// (letrec ((<var_1> <exp_1>) ... (<var_n> <exp_n>)) <body>) =>
// (let ((<var_1> _) ... (<var_n> _)) (set! <var_1> <exp_1>) ... (set! <var_n> <exp_n>) <body>)
**/

/**
eval_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF const?($expr) [
			SEND $to $expr
		] ELIF actor?($expr) [
			SEND $to $expr		// treat actor as a literal value
		] ELIF symbol?($expr) [
			SEND $env $to:((get $expr):$env)
		] ELIF pair?($expr) [
			a: ACTOR $apply_beh $to:(pr_tail($expr):$env)
			SEND SELF $a:(pr_head($expr):$env)
		] ELSE [
			// signal error
			FAIL "unknown"
		]
	]
	DONE
**/
BEH_DECL(eval_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_const(expr)) {
			DBUG_PRINT("", ("constant"));
			SEND(to, expr);
		} else if (is_actor(expr)) {
			DBUG_PRINT("", ("actor"));
			SEND(to, expr);
		} else if (is_symbol(expr)) {
			DBUG_PRINT("", ("symbol"));
			msg = mk_empty();
			msg = mk_list(expr, msg);
			msg = mk_list(ATOM("get"), msg);
			msg = mk_pair(msg, env);
			msg = mk_pair(to, msg);
			SEND(env, msg);
		} else if (is_pair(expr)) {
			DBUG_PRINT("", ("combination"));
			state = mk_pair(pr_tail(expr), env);
			state = mk_pair(to, state);
			a = ACTOR(apply_beh, state);
			msg = mk_pair(pr_head(expr), env);
			msg = mk_pair(a, msg);
			SEND(SELF, msg);
		} else {
			DBUG_PRINT("", ("unknown"));
			msg = mk_list(expr, mk_empty());
			msg = mk_list(ATOM("unknown-expr"), msg);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
eval_quote_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		SEND $to lst_first($expr)
	]
	DONE
**/
BEH_DECL(eval_quote_beh)
{
	CONS* p;

	DBUG_ENTER("eval_quote_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (have_n_args(1, expr)) {
			SEND(to, lst_first(expr));
		} else {
			DBUG_PRINT("", ("error"));
			FAIL(env, mk_list(ATOM("quote"), expr));
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
eval_spawn_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		e: ACTOR $eval_seq_beh NIL
		k: ACTOR $sink_beh NIL
		SEND $e $k:($expr:$env)
		SEND $to "ok"
	]
	DONE
**/
BEH_DECL(eval_spawn_beh)
{
	CONS* p;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_spawn_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		SEND(to, ATOM("ok"));
		a = ACTOR(sink_beh, NIL);
		msg = mk_pair(expr, env);
		msg = mk_pair(a, msg);
		SEND(eval_seq__actor, msg);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
eval_time_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		SEND $to NOW
	]
	DONE
**/
BEH_DECL(eval_time_beh)
{
	CONS* p;

	DBUG_ENTER("eval_time_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
/*		CONS* expr = pr_head(p); */
/*		CONS* env = pr_tail(p); */

		p = NOW;
		DBUG_PRINT("", ("now = %s", cons_to_str(p)));
		SEND(to, p);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_define_beh:
	BEHAVIOR $to:($name:$env)
	$value -> SEND $env $to:((define $name $value):$env)
	DONE
**/
BEH_DECL(apply_define_beh)
{
	CONS* value = WHAT;
	CONS* to;
	CONS* name;
	CONS* env;
	CONS* p = MINE;
	CONS* msg;

	DBUG_ENTER("apply_define_beh");
	to = pr_head(p);
	p = pr_tail(p);
	name = pr_head(p);
	DBUG_PRINT("", ("name = %s", cons_to_str(name)));
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	env = pr_tail(p);
	msg = mk_empty();
	msg = mk_list(value, msg);
	msg = mk_list(name, msg);
	msg = mk_list(ATOM("define"), msg);
	msg = mk_pair(msg, env);
	msg = mk_pair(to, msg);
	SEND(env, msg);
	DBUG_RETURN;
}

/**
eval_define_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		form: lst_first($expr)
		IF symbol?($form) [
			a: ACTOR $apply_define_beh $to:($form:$env)
			e: ACTOR $eval_beh
			SEND $e $a:(lst_second($expr):$env)
		] ELIF pair?($form) [
			name: lst_first($form)
			vars: lst_rest($form)
			body: lst_rest($expr)
			lambda: $vars:$body
			a: ACTOR $lambda_beh $lambda:$env
			SEND $env $to:((define $name $a):$env)
		] ELSE [
			FAIL "bad-form"
		]
	]
	DONE
**/
BEH_DECL(eval_define_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_define_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);
		CONS* form = is_pair(expr) ? lst_first(expr) : NIL;

		DBUG_PRINT("", ("form = %s", cons_to_str(form)));
		if (is_symbol(form)) {
			DBUG_PRINT("", ("symbol"));
			state = mk_pair(form, env);
			state = mk_pair(to, state);
			a = ACTOR(apply_define_beh, state);
			DBUG_PRINT("", ("expr = %s", cons_to_str(lst_second(expr))));
			msg = mk_pair(lst_second(expr), env);
			msg = mk_pair(a, msg);
			SEND(eval__actor, msg);
		} else if (is_pair(form)) {
			CONS* name = lst_first(form);
			CONS* vars = lst_rest(form);
			CONS* body = lst_rest(expr);
			
			DBUG_PRINT("", ("lambda"));
			DBUG_PRINT("", ("name = %s", cons_to_str(name)));
			DBUG_PRINT("", ("vars = %s", cons_to_str(vars)));
			DBUG_PRINT("", ("body = %s", cons_to_str(body)));
			state = mk_pair(vars, body);
			state = mk_pair(state, env);
			a = ACTOR(lambda_beh, state);
			msg = mk_empty();
			msg = mk_list(a, msg);
			msg = mk_list(name, msg);
			msg = mk_list(ATOM("define"), msg);
			msg = mk_pair(msg, env);
			msg = mk_pair(to, msg);
			SEND(env, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("define"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_set_beh:
	BEHAVIOR $to:($name:$env)
	$value -> SEND $env $to:((set! $name $value):$env)
	DONE
**/
BEH_DECL(apply_set_beh)
{
	CONS* value = WHAT;
	CONS* to;
	CONS* name;
	CONS* env;
	CONS* p = MINE;
	CONS* msg;

	DBUG_ENTER("apply_set_beh");
	to = pr_head(p);
	p = pr_tail(p);
	name = pr_head(p);
	DBUG_PRINT("", ("name = %s", cons_to_str(name)));
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	env = pr_tail(p);
	msg = mk_empty();
	msg = mk_list(value, msg);
	msg = mk_list(name, msg);
	msg = mk_list(ATOM("set!"), msg);
	msg = mk_pair(msg, env);
	msg = mk_pair(to, msg);
	SEND(env, msg);
	DBUG_RETURN;
}

/**
eval_set_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		form: lst_first($expr)
		IF symbol?($form) [
			a: ACTOR $apply_define_beh $to:($form:$env)
			e: ACTOR $eval_beh
			SEND $e $a:(lst_second($expr):$env)
		] ELSE [
			FAIL "bad-form"
		]
	]
	DONE
**/
BEH_DECL(eval_set_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_set_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);
		CONS* form = is_pair(expr) ? lst_first(expr) : NIL;

		DBUG_PRINT("", ("form = %s", cons_to_str(form)));
		if (is_symbol(form) && is_pair(expr) && is_pair(pr_tail(expr))) {
			state = mk_pair(form, env);
			state = mk_pair(to, state);
			a = ACTOR(apply_set_beh, state);
			DBUG_PRINT("", ("expr = %s", cons_to_str(lst_second(expr))));
			msg = mk_pair(lst_second(expr), env);
			msg = mk_pair(a, msg);
			SEND(eval__actor, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("set!"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
join_list_beh:
	BEHAVIOR $to:$value
	$list -> SEND $to mk_list($value, $list)
	DONE
**/
BEH_DECL(join_list_beh)
{
	CONS* list = WHAT;
	CONS* to;
	CONS* value;

	DBUG_ENTER("join_list_beh");
	to = pr_head(MINE);
	value = pr_tail(MINE);
	if (is_list(list)) {
		DBUG_PRINT("", ("list = %s", cons_to_str(list)));
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, mk_list(value, list));
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
next_list_beh:
	BEHAVIOR $to:($expr:$env)
	$value -> [
		k: ACTOR $join_list_beh $to:$value
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$env)
	]
	DONE
**/
BEH_DECL(next_list_beh)
{
	CONS* value = WHAT;
	CONS* to;
	CONS* expr;
	CONS* env;
	CONS* p;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("next_list_beh");
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	to = pr_head(MINE);
	p = pr_tail(MINE);
	expr = pr_head(p);
	DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
	env = pr_tail(p);
	a = ACTOR(join_list_beh, mk_pair(to, value));
	msg = mk_pair(expr, env);
	msg = mk_pair(a, msg);
	SEND(eval_list__actor, msg);
	DBUG_RETURN;
}

/**
eval_list_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF empty?($expr) [
			SEND $to mk_empty()
		] ELSE [
			k: ACTOR $next_list_beh $to:(pr_tail($expr):$env)
			e: ACTOR $eval_beh NIL
			SEND $e $k:(pr_head($expr):$env)
		]
	]
	DONE
**/
BEH_DECL(eval_list_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_list_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_empty(expr)) {
			SEND(to, mk_empty());
		} else if (is_pair(expr)) {
			state = mk_pair(pr_tail(expr), env);
			state = mk_pair(to, state);
			a = ACTOR(next_list_beh, state);
			msg = mk_pair(pr_head(expr), env);
			msg = mk_pair(a, msg);
			SEND(eval__actor, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("list"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
join_value_beh:
	BEHAVIOR $to:$list
	$to:$value -> SEND $to mk_list($value, $list)
	DONE
**/
BEH_DECL(join_value_beh)
{
	CONS* to;
	CONS* value;
	CONS* list;

	DBUG_ENTER("join_value_beh");
	to = pr_head(MINE);
	list = pr_tail(MINE);
	if (is_pair(WHAT) && (pr_head(WHAT) == to)) {
		DBUG_PRINT("", ("list = %s", cons_to_str(list)));
		value = pr_tail(WHAT);
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, mk_list(value, list));
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
join_par_beh:
	BEHAVIOR $to
	$to:$value -> BECOME $join_list_beh $to:$value
	$list -> BECOME $join_value_beh $to:$list
	DONE
**/
BEH_DECL(join_par_beh)
{
	CONS* to;
	CONS* value;
	CONS* list;

	DBUG_ENTER("join_par_beh");
	to = MINE;
	if (is_pair(WHAT) && (pr_head(WHAT) == to)) {
		value = pr_tail(WHAT);
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		BECOME(join_list_beh, mk_pair(to, value));
	} else if (is_list(WHAT)) {
		list = WHAT;
		DBUG_PRINT("", ("list = %s", cons_to_str(list)));
		BECOME(join_value_beh, mk_pair(to, list));
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
eval_par_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF empty?($expr) [
			SEND $to mk_empty()
		] ELSE [
			k: ACTOR $join_par_beh $to
			SEND SELF $k:(pr_tail($expr):$env)
			e: ACTOR $eval_beh NIL
			a: ACTOR $label_beh $k:$to
			SEND $e $a:(pr_head($expr):$env)
		]
	]
	DONE
**/
BEH_DECL(eval_par_beh)
{
	CONS* p;
	CONS* msg;
	CONS* a;
	CONS* k;

	DBUG_ENTER("eval_par_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_empty(expr)) {
			SEND(to, mk_empty());
		} else if (is_pair(expr)) {
			k = ACTOR(join_par_beh, to);
			msg = mk_pair(pr_tail(expr), env);
			msg = mk_pair(k, msg);
			SEND(SELF, msg);
			a = ACTOR(label_beh, mk_pair(k, to));
			msg = mk_pair(pr_head(expr), env);
			msg = mk_pair(a, msg);
			SEND(eval__actor, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("par"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
next_seq_beh:
	BEHAVIOR $eval:($to:($expr:$env))
	$value -> SEND $eval $to:($expr:$env)
	DONE
**/
BEH_DECL(next_seq_beh)
{
	CONS* value = WHAT;
	CONS* eval = pr_head(MINE);
	CONS* msg = pr_tail(MINE);

	DBUG_ENTER("next_seq_beh");
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	SEND(eval, msg);
	DBUG_RETURN;
}

/**
eval_seq_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF pair?($expr) [
			next: lst_rest($expr)
			e: ACTOR $eval_beh NIL
			IF empty?($next) [
				SEND $e $to:(lst_first($expr):$env)
			] ELSE [
				k: ACTOR $next_seq_beh SELF:($to:($next:$env))
				SEND $e $k:(lst_first($expr):$env)
			]
		] ELSE [
			SEND $to "undefined"
		]
	]
	DONE
**/
BEH_DECL(eval_seq_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_seq_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_pair(expr)) {
			CONS* next = lst_rest(expr);

			expr = lst_first(expr);
			msg = mk_pair(expr, env);
			if (is_empty(next)) {		/* tail-call optimization */
				msg = mk_pair(to, msg);
			} else {
				state = mk_pair(next, env);
				state = mk_pair(to, state);
				state = mk_pair(SELF, state);
				a = ACTOR(next_seq_beh, state);
				msg = mk_pair(a, msg);
			}
			SEND(eval__actor, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("..."), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
next_and_beh:
	BEHAVIOR $eval:($to:($expr:$env))
	$value -> [
		IF eq?($value, FALSE_SYMBOL) [
			SEND $to $value
		] ELSE [
			SEND $eval $to:($expr:$env)
		]
	]
	DONE
**/
BEH_DECL(next_and_beh)
{
	CONS* value = WHAT;
	CONS* eval = pr_head(MINE);
	CONS* msg = pr_tail(MINE);

	DBUG_ENTER("next_and_beh");
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	if (value == FALSE_SYMBOL) {
		SEND(pr_head(msg), value);
	} else {
		SEND(eval, msg);
	}
	DBUG_RETURN;
}

/**
eval_and_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF empty?($expr) [
			SEND $to TRUE_SYMBOL
		] ELIF pair?($expr) [
			next: lst_rest($expr)
			e: ACTOR $eval_beh NIL
			IF empty?($next) [
				SEND $e $to:(lst_first($expr):$env)
			] ELSE [
				k: ACTOR $next_and_beh SELF:($to:($next:$env))
				SEND $e $k:(lst_first($expr):$env)
			]
		] ELSE [
			SEND $to "undefined"
		]
	]
	DONE
**/
BEH_DECL(eval_and_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_and_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_empty(expr)) {
			SEND(to, TRUE_SYMBOL);
		} else if (is_pair(expr)) {
			CONS* next = lst_rest(expr);

			expr = lst_first(expr);
			msg = mk_pair(expr, env);
			if (is_empty(next)) {		/* tail-call optimization */
				msg = mk_pair(to, msg);
			} else {
				state = mk_pair(next, env);
				state = mk_pair(to, state);
				state = mk_pair(SELF, state);
				a = ACTOR(next_and_beh, state);
				msg = mk_pair(a, msg);
			}
			SEND(eval__actor, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("and"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
next_or_beh:
	BEHAVIOR $eval:($to:($expr:$env))
	$value -> [
		IF eq?($value, FALSE_SYMBOL) [
			SEND $eval $to:($expr:$env)
		] ELSE [
			SEND $to $value
		]
	]
	DONE
**/
BEH_DECL(next_or_beh)
{
	CONS* value = WHAT;
	CONS* eval = pr_head(MINE);
	CONS* msg = pr_tail(MINE);

	DBUG_ENTER("next_or_beh");
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	if (value == FALSE_SYMBOL) {
		SEND(eval, msg);
	} else {
		SEND(pr_head(msg), value);
	}
	DBUG_RETURN;
}

/**
eval_or_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF empty?($expr) [
			SEND $to FALSE_SYMBOL
		] ELIF pair?($expr) [
			next: lst_rest($expr)
			e: ACTOR $eval_beh NIL
			IF empty?($next) [
				SEND $e $to:(lst_first($expr):$env)
			] ELSE [
				k: ACTOR $next_or_beh SELF:($to:($next:$env))
				SEND $e $k:(lst_first($expr):$env)
			]
		] ELSE [
			SEND $to "undefined"
		]
	]
	DONE
**/
BEH_DECL(eval_or_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_or_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_empty(expr)) {
			SEND(to, FALSE_SYMBOL);
		} else if (is_pair(expr)) {
			CONS* next = lst_rest(expr);

			expr = lst_first(expr);
			msg = mk_pair(expr, env);
			if (is_empty(next)) {		/* tail-call optimization */
				msg = mk_pair(to, msg);
			} else {
				state = mk_pair(next, env);
				state = mk_pair(to, state);
				state = mk_pair(SELF, state);
				a = ACTOR(next_or_beh, state);
				msg = mk_pair(a, msg);
			}
			SEND(eval__actor, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("or"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_cond_beh:
	BEHAVIOR $f:$t
	FALSE_SYMBOL -> [
		eval: pr_head($f)
		msg: pr_tail($f)
		SEND $eval $msg
	]
	$value -> [
		e: ACTOR $eval_seq_beh NIL
		SEND $e $t
	]
	DONE
**/
BEH_DECL(apply_cond_beh)
{
	CONS* f = pr_head(MINE);
	CONS* t = pr_tail(MINE);

	DBUG_ENTER("apply_cond_beh");
	if (WHAT == FALSE_SYMBOL) {
		DBUG_PRINT("", ("false"));
		SEND(pr_head(f), pr_tail(f));
	} else {
		DBUG_PRINT("", ("true"));
		SEND(eval_seq__actor, t);
	}
	DBUG_RETURN;
}

/**
eval_cond_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		IF both(pair?($expr), pair?(pr_head($expr))) [
			phrase: lst_first($expr)
			pred: lst_first($phrase)
			t: $to:(lst_rest($phrase):$env)
			f: SELF:($to:(lst_rest($expr):$env))
			k: ACTOR $apply_cond_beh $f:$t
			IF eq($pred, "else") [
				SEND $k TRUE_SYMBOL
			] ELSE [
				e: ACTOR $eval_beh NIL
				SEND $e $k:($pred:$env)
			]
		] ELSE [
			SEND $to "undefined"
		]
	]
	DONE
**/
BEH_DECL(eval_cond_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_cond_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (is_pair(expr) && is_pair(pr_head(expr))) {
			CONS* phrase = lst_first(expr);
			CONS* pred = lst_first(phrase);

			DBUG_PRINT("", ("phrase = %s", cons_to_str(phrase)));
			DBUG_PRINT("", ("pred = %s", cons_to_str(pred)));
			msg = mk_pair(lst_rest(phrase), env);
			msg = mk_pair(to, msg);
			state = mk_pair(lst_rest(expr), env);
			state = mk_pair(to, state);
			state = mk_pair(SELF, state);
			a = ACTOR(apply_cond_beh, mk_pair(state, msg));
			if (pred == ATOM("else")) {
				SEND(a, TRUE_SYMBOL);
			} else {
				msg = mk_pair(pred, env);
				msg = mk_pair(a, msg);
				SEND(eval__actor, msg);
			}
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("cond"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
// (if <predicate> <consequent> <alternative>) =>
// (cond (<predicate> <consequent>) (else <alternative>))
eval_if_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		pred: lst_first($expr)
		cnsq: lst_second($expr)
		altn: lst_third($expr)
		cond: ACTOR $eval_cond_beh NIL
		SEND $cond $to:((($pred $cnsq) (else $altn)):$env)
	]
	DONE
**/
#if 0
BEH_DECL(eval_if_beh)
{
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_if_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		if (have_n_args(3, expr)) {
			CONS* pred = lst_first(expr);
			CONS* cnsq = lst_second(expr);
			CONS* altn = lst_third(expr);

			state = mk_empty();
			state = mk_list(mk_list(ATOM("else"), mk_list(altn, mk_empty())), state);
			state = mk_list(mk_list(pred, mk_list(cnsq, mk_empty())), state);
			DBUG_PRINT("", ("cond = %s", cons_to_str(state)));
			msg = mk_pair(state, env);
			msg = mk_pair(to, msg);
			a = ACTOR(eval_cond_beh, NIL);
			SEND(a, msg);
		} else {
			DBUG_PRINT("", ("error"));
			msg = mk_list(ATOM("if"), expr);
			FAIL(env, msg);
		}
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}
#endif

/**
fn_eval_beh:
	BEHAVIOR $beh
	$to:($expr:$env) -> [
		k: ACTOR $beh $to:$env
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$env)		
	]
	DONE
**/
BEH_DECL(fn_eval_beh)
{
	CONS* beh = MINE;
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("fn_eval_beh");
	DBUG_PRINT("", ("beh = %s", cons_to_str(beh)));
	assert(funcp(beh));
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
		CONS* to = pr_head(WHAT);
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		state = mk_pair(to, env);
		a = ACTOR(MK_BEH(beh), state);
		msg = mk_pair(expr, env);
		msg = mk_pair(a, msg);
		SEND(eval_par__actor, msg);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_cons_beh:
	BEHAVIOR $to:$env
	$args -> [
		value: mk_pair(lst_first($args), lst_second($args))
		SEND $to $value
	]
	DONE
eval_cons_beh:	// not implemented, see fn_eval_beh
	BEHAVIOR
	$to:($expr:$env) -> [
		k: ACTOR $apply_cons_beh $to:$env
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$env)		
	]
	DONE
**/
BEH_DECL(apply_cons_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_cons_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(2, args)) {
		value = mk_pair(lst_first(args), lst_second(args));
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("cons"), args));
	}
	DBUG_RETURN;
}

/**
apply_car_beh:
	BEHAVIOR $to:$env
	$args -> [
		value: pr_head(lst_first($args))
		SEND $to $value
	]
	DONE
**/
BEH_DECL(apply_car_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_car_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args) && is_pair(lst_first(args))) {
		value = pr_head(lst_first(args));
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("car"), args));
	}
	DBUG_RETURN;
}

/**
apply_cdr_beh:
	BEHAVIOR $to:$env
	$args -> [
		value: pr_tail(lst_first($args))
		SEND $to $value
	]
	DONE
**/
BEH_DECL(apply_cdr_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_cdr_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args) && is_pair(lst_first(args))) {
		value = pr_tail(lst_first(args));
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("cdr"), args));
	}
	DBUG_RETURN;
}

/**
apply_is_eq_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF eq?(lst_first($args), lst_second($args)) [
			SEND $to TRUE_VALUE
		] ELSE [
			SEND $to FALSE_VALUE
		]
	]
	DONE
**/
BEH_DECL(apply_is_eq_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_is_eq_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(2, args)) {
		if (lst_first(args) == lst_second(args)) {
			value = TRUE_SYMBOL;
		} else {
			value = FALSE_SYMBOL;
		}
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("eq?"), args));
	}
	DBUG_RETURN;
}

/**
apply_is_pair_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF pair?(lst_first($args)) [
			SEND $to TRUE_VALUE
		] ELSE [
			SEND $to FALSE_VALUE
		]
	]
	DONE
**/
BEH_DECL(apply_is_pair_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_is_pair_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args)) {
		if (is_pair(lst_first(args))) {
			value = TRUE_SYMBOL;
		} else {
			value = FALSE_SYMBOL;
		}
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("pair?"), args));
	}
	DBUG_RETURN;
}

/**
apply_is_symbol_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF symbol?(lst_first($args)) [
			SEND $to TRUE_VALUE
		] ELSE [
			SEND $to FALSE_VALUE
		]
	]
	DONE
**/
BEH_DECL(apply_is_symbol_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_is_symbol_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args)) {
		if (is_symbol(lst_first(args))) {
			value = TRUE_SYMBOL;
		} else {
			value = FALSE_SYMBOL;
		}
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("symbol?"), args));
	}
	DBUG_RETURN;
}

/**
apply_is_boolean_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF boolean?(lst_first($args)) [
			SEND $to TRUE_VALUE
		] ELSE [
			SEND $to FALSE_VALUE
		]
	]
	DONE
**/
BEH_DECL(apply_is_boolean_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_is_boolean_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args)) {
		if (is_boolean(lst_first(args))) {
			value = TRUE_SYMBOL;
		} else {
			value = FALSE_SYMBOL;
		}
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("boolean?"), args));
	}
	DBUG_RETURN;
}

/**
apply_is_number_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF number?(lst_first($args)) [
			SEND $to TRUE_VALUE
		] ELSE [
			SEND $to FALSE_VALUE
		]
	]
	DONE
**/
BEH_DECL(apply_is_number_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_is_number_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args)) {
		if (is_number(lst_first(args))) {
			value = TRUE_SYMBOL;
		} else {
			value = FALSE_SYMBOL;
		}
		DBUG_PRINT("", ("value = %s", cons_to_str(value)));
		SEND(to, value);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("number?"), args));
	}
	DBUG_RETURN;
}

/**
apply_plus_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF is_empty(lst_rest($args)) [
			value: add(0, lst_first($args))
			SEND $to $value
		] else [
			value: add(lst_first($args), lst_second($args))
			SEND $to $value
		]
	]
	DONE
**/
BEH_DECL(apply_plus_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	int n = 0;
	CONS* value;

	DBUG_ENTER("apply_plus_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	while (!is_empty(args)) {
		if (is_pair(args) && is_number(lst_first(args))) {
			int m = MK_INT(lst_first(args));

			n += m;
		} else {
			DBUG_PRINT("", ("error"));
			FAIL(env, mk_list(ATOM("+"), mk_list(NUMBER(n), args)));
			DBUG_RETURN;
		}
		args = lst_rest(args);
	}
	value = NUMBER(n);
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	SEND(to, value);
	DBUG_RETURN;
}

/**
apply_minus_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF is_empty(lst_rest($args)) [
			value: subtract(0, lst_first($args))
			SEND $to $value
		] else [
			value: subtract(lst_first($args), lst_second($args))
			SEND $to $value
		]
	]
	DONE
**/
BEH_DECL(apply_minus_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	int n = 0;
	CONS* value;

	DBUG_ENTER("apply_minus_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (!is_pair(args) || !is_number(lst_first(args))) {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("-"), args));
		DBUG_RETURN;
	}
	n = MK_INT(lst_first(args));
	args = lst_rest(args);
	if (is_empty(args)) {
		n = -n;
	} else {
		while (!is_empty(args)) {
			if (is_pair(args) && is_number(lst_first(args))) {
				int m = MK_INT(lst_first(args));

				n -= m;
			} else {
				DBUG_PRINT("", ("error"));
				FAIL(env, mk_list(ATOM("-"), mk_list(NUMBER(n), args)));
				DBUG_RETURN;
			}
			args = lst_rest(args);
		}
	}
	value = NUMBER(n);
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	SEND(to, value);
	DBUG_RETURN;
}

/**
apply_times_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF is_empty(lst_rest($args)) [
			value: multiply(0, lst_first($args))
			SEND $to $value
		] else [
			value: multiply(lst_first($args), lst_second($args))
			SEND $to $value
		]
	]
	DONE
**/
BEH_DECL(apply_times_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	int n = 1;
	CONS* value;

	DBUG_ENTER("apply_times_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	while (!is_empty(args)) {
		if (is_pair(args) && is_number(lst_first(args))) {
			int m = MK_INT(lst_first(args));

			n *= m;
		} else {
			DBUG_PRINT("", ("error"));
			FAIL(env, mk_list(ATOM("*"), mk_list(NUMBER(n), args)));
			DBUG_RETURN;
		}
		args = lst_rest(args);
	}
	value = NUMBER(n);
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	SEND(to, value);
	DBUG_RETURN;
}

/**
apply_less_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF both(is_pair($args), is_pair(lst_rest($args))) [
			IF preceeds(lst_first($args), lst_second($args)) [
				next: lst_rest($args);
				IF is_empty(lst_rest($next)) [
					SEND $to TRUE_SYMBOL
				] ELSE [
					SEND SELF $next
				]
			] ELSE [
				SEND $to FALSE_SYMBOL
			]
		] ELSE [
			FAIL "bad-args"
		]
	]
	DONE
**/
BEH_DECL(apply_less_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_less_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (is_pair(args) && is_number(lst_first(args)) 
	&&  is_pair(lst_rest(args)) && is_number(lst_second(args))) {
		int n = MK_INT(lst_first(args));
		int m = MK_INT(lst_second(args));

		if (n < m) {
			args = lst_rest(args);
			if (is_empty(lst_rest(args))) {
				value = TRUE_SYMBOL;
			} else {
				SEND(SELF, args);
				DBUG_RETURN;
			}
		} else {
			value = FALSE_SYMBOL;
		}
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("<"), args));
		DBUG_RETURN;
	}
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	SEND(to, value);
	DBUG_RETURN;
}

/**
apply_greater_beh:
	BEHAVIOR $to:$env
	$args -> [
		IF both(is_pair($args), is_pair(lst_rest($args))) [
			IF succeeds(lst_first($args), lst_second($args)) [
				next: lst_rest($args);
				IF is_empty(lst_rest($next)) [
					SEND $to TRUE_SYMBOL
				] ELSE [
					SEND SELF $next
				]
			] ELSE [
				SEND $to FALSE_SYMBOL
			]
		] ELSE [
			FAIL "bad-args"
		]
	]
	DONE
**/
BEH_DECL(apply_greater_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* value;

	DBUG_ENTER("apply_greater_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (is_pair(args) && is_number(lst_first(args)) 
	&&  is_pair(lst_rest(args)) && is_number(lst_second(args))) {
		int n = MK_INT(lst_first(args));
		int m = MK_INT(lst_second(args));

		if (n > m) {
			args = lst_rest(args);
			if (is_empty(lst_rest(args))) {
				value = TRUE_SYMBOL;
			} else {
				SEND(SELF, args);
				DBUG_RETURN;
			}
		} else {
			value = FALSE_SYMBOL;
		}
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM(">"), args));
		DBUG_RETURN;
	}
	DBUG_PRINT("", ("value = %s", cons_to_str(value)));
	SEND(to, value);
	DBUG_RETURN;
}

/**
apply_sleep_beh:
	BEHAVIOR $to:$env
	$args -> [
		n: product(lst_first($args), 1000)
		a: ACTOR $eval_time_beh NIL
		SEND_AFTER $n $a $to(NIL:$env)
	]
	DONE
**/
BEH_DECL(apply_sleep_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* msg;
	CONS* a;

	DBUG_ENTER("apply_sleep_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args) && is_number(lst_first(args))) {
		int n = MK_INT(lst_first(args));
		
		n *= 1000;		/* convert ms to us */
		DBUG_PRINT("", ("sleep %dus", n));
		a = ACTOR(eval_time_beh, NIL);
		msg = mk_pair(NIL, env);
		msg = mk_pair(to, msg);
		SEND_AFTER(NUMBER(n), a, msg);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("sleep"), args));
	}
	DBUG_RETURN;
}

/**
apply_cont_beh:
	BEHAVIOR $to:$env
	$args -> [
		SEND $to lst_first(args)
	]
	DONE
**/
BEH_DECL(apply_cont_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);

	DBUG_ENTER("apply_cont_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args)) {
		SEND(to, lst_first(args));
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("one-arg-required"), args));
	}
	DBUG_RETURN;
}

/**
cont_beh:
	BEHAVIOR $cc:$lex
	$to:($expr:$dyn) -> [
		k: ACTOR $apply_cont_beh $cc:$lex
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$dyn)
	]
	DONE
**/
BEH_DECL(cont_beh)
{
	CONS* cc = pr_head(MINE);
	CONS* lex = pr_tail(MINE);
	CONS* p;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("cont_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
/*		CONS* to = pr_head(WHAT); */
		CONS* expr = pr_head(p);
		CONS* dyn = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		state = mk_pair(cc, lex);
		a = ACTOR(apply_cont_beh, state);
		msg = mk_pair(expr, dyn);
		msg = mk_pair(a, msg);
		SEND(eval_par__actor, msg);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
apply_call_cc_beh:
	BEHAVIOR $to:$env
	$args -> [
		k: lst_first($args)
		IF actor?(k) [
			cc: ACTOR $cont_beh $to:$env
			SEND $k $to:(mk_list($cc, mk_empty()):$env)
		] ELSE [
			FAIL "procedure-required"
		]
	]
	DONE
**/
BEH_DECL(apply_call_cc_beh)
{
	CONS* args = WHAT;
	CONS* to = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("apply_call_cc_beh");
	DBUG_PRINT("", ("args = %s", cons_to_str(args)));
	if (have_n_args(1, args) && is_actor(lst_first(args))) {
		state = mk_pair(to, env);
		a = ACTOR(cont_beh, state);
		msg = mk_list(a, mk_empty());
		msg = mk_pair(msg, env);
		msg = mk_pair(to, msg);
		SEND(lst_first(args), msg);
	} else {
		DBUG_PRINT("", ("error"));
		FAIL(env, mk_list(ATOM("call/cc"), args));
	}
	DBUG_RETURN;
}

/**
fail_beh:
	BEHAVIOR $to
	$value -> [
		SEND $to FAILURE!:$value
	]
	DONE
**/
BEH_DECL(fail_beh)
{
	DBUG_ENTER("fail_beh");
	SEND(MINE, mk_pair(ATOM("FAILURE!"), WHAT));
	DBUG_RETURN;
}

/**
apply_fail_beh:
	BEHAVIOR $expr:$env
	$fail -> [
		// evaluate args with fail-safe error handler
		a: ACTOR $abort_beh NIL
		f: ACTOR $fail_beh $a
		x: ACTOR $binding_beh $env:(UNDEFINED:$f)
		e: ACTOR $eval_list_beh NIL
		SEND $e $fail:($expr:$x)
	]
	DONE
**/
BEH_DECL(apply_fail_beh)
{
	CONS* expr = pr_head(MINE);
	CONS* env = pr_tail(MINE);
	CONS* fail = WHAT;
	CONS* state;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("apply_fail_beh");
	assert(actorp(fail));
	DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
	a = ACTOR(abort_beh, NIL);
	a = ACTOR(fail_beh, a);
	state = mk_pair(UNDEFINED, a);
	state = mk_pair(env, state);
	env = ACTOR(binding_beh, state);
	msg = mk_pair(expr, env);
	msg = mk_pair(fail, msg);
	SEND(eval_list__actor, msg);
	DBUG_RETURN;
}

/**
eval_fail_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		a: ACTOR $apply_fail_beh $expr:$env
		SEND $env $a:((get _):$env)
	]
	DONE
**/
BEH_DECL(eval_fail_beh)
{
	CONS* p;
	CONS* msg;
	CONS* a;

	DBUG_ENTER("eval_fail_beh");
	if (is_pair(WHAT) && is_pair(p = pr_tail(WHAT))) {
/*		CONS* to = pr_head(WHAT); */
		CONS* expr = pr_head(p);
		CONS* env = pr_tail(p);

		DBUG_PRINT("", ("expr = %s", cons_to_str(expr)));
		a = ACTOR(apply_fail_beh, p);
		msg = mk_empty();
		msg = mk_list(UNDEFINED, msg);
		msg = mk_list(ATOM("get"), msg);
		msg = mk_pair(msg, env);
		msg = mk_pair(a, msg);
		SEND(env, msg);
	} else {
		DBUG_PRINT("", ("Ignored! %s", cons_to_str(WHAT)));
		abort();
	}
	DBUG_RETURN;
}

/**
**/

static CONS* initial__environment = NULL;

static CONS*
init_add_binding(CONFIG* cfg, CONS* env, CONS* name, CONS* value)
{
	assert(actorp(env));
	assert(atomp(name));
	return CFG_ACTOR(cfg, binding_beh, mk_pair(env, mk_pair(name, value)));
}

static CONS*
init_schemer(CONFIG* cfg)
{
	CONS* env;
	CONS* a;
	
	DBUG_ENTER("init_schemer");
	if (initial__environment == NULL) {
		eval__actor = CFG_ACTOR(cfg, eval_beh, NIL);
		eval_list__actor = CFG_ACTOR(cfg, eval_list_beh, NIL);
		eval_par__actor = CFG_ACTOR(cfg, eval_par_beh, NIL);
		eval_seq__actor = CFG_ACTOR(cfg, eval_seq_beh, NIL);
		env = CFG_ACTOR(cfg, frame_beh, NIL);

		a = CFG_ACTOR(cfg, eval_fail_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("fail!"), a);	/* WARNING: non-standard exception signal */
		a = CFG_ACTOR(cfg, fail_beh, CFG_ACTOR(cfg, abort_beh, NIL));
		env = init_add_binding(cfg, env, UNDEFINED, a);		/* default exception handler */
		env = init_add_binding(cfg, env, ATOM("false"), FALSE_SYMBOL);
		env = init_add_binding(cfg, env, ATOM("true"), TRUE_SYMBOL);
/*		env = init_add_binding(cfg, env, ATOM("eval"), eval__actor); */
		a = CFG_ACTOR(cfg, eval_quote_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("quote"), a);
		env = init_add_binding(cfg, env, ATOM("literal"), a);	/* FIXME: non-standard alias */
		a = CFG_ACTOR(cfg, eval_spawn_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("spawn"), a);	/* non-standard extension */
		a = CFG_ACTOR(cfg, eval_time_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("time"), a);		/* non-standard extension */
		env = init_add_binding(cfg, env, ATOM("list"), eval_list__actor);
		env = init_add_binding(cfg, env, ATOM("par"), eval_par__actor);	/* non-standard extension */
		env = init_add_binding(cfg, env, ATOM("begin"), eval_seq__actor);
		a = CFG_ACTOR(cfg, eval_lambda_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("lambda"), a);
		a = CFG_ACTOR(cfg, eval_form_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("form"), a);		/* non-standard extension */
		a = CFG_ACTOR(cfg, eval_define_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("define"), a);
		a = CFG_ACTOR(cfg, eval_set_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("set!"), a);
		a = CFG_ACTOR(cfg, eval_and_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("and"), a);
		a = CFG_ACTOR(cfg, eval_or_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("or"), a);
		a = CFG_ACTOR(cfg, eval_cond_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("cond"), a);
#if 0  /* replaced by "form" definition in library.scm */
		a = CFG_ACTOR(cfg, eval_if_beh, NIL);
		env = init_add_binding(cfg, env, ATOM("if"), a);
#endif
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_cons_beh));
		env = init_add_binding(cfg, env, ATOM("cons"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_car_beh));
		env = init_add_binding(cfg, env, ATOM("car"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_cdr_beh));
		env = init_add_binding(cfg, env, ATOM("cdr"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_is_pair_beh));
		env = init_add_binding(cfg, env, ATOM("pair?"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_is_symbol_beh));
		env = init_add_binding(cfg, env, ATOM("symbol?"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_is_boolean_beh));
		env = init_add_binding(cfg, env, ATOM("boolean?"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_is_number_beh));
		env = init_add_binding(cfg, env, ATOM("number?"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_is_eq_beh));
		env = init_add_binding(cfg, env, ATOM("eq?"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_plus_beh));
		env = init_add_binding(cfg, env, ATOM("+"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_minus_beh));
		env = init_add_binding(cfg, env, ATOM("-"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_times_beh));
		env = init_add_binding(cfg, env, ATOM("*"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_less_beh));
		env = init_add_binding(cfg, env, ATOM("<"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_greater_beh));
		env = init_add_binding(cfg, env, ATOM(">"), a);
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_sleep_beh));
		env = init_add_binding(cfg, env, ATOM("sleep"), a);		/* non-standard extension */
		a = CFG_ACTOR(cfg, fn_eval_beh, MK_FUNC(apply_call_cc_beh));
		env = init_add_binding(cfg, env, ATOM("call/cc"), a);

		cfg_add_gc_root(cfg, env);
		initial__environment = env;
	}
	DBUG_RETURN initial__environment;
}

BOOL
reduce_line(CONFIG* cfg, char* line, CONS* cont)
{
	CONS* expr;
	CONS* env;
	CONS* msg;
	CONS* a;
	int n;

	DBUG_ENTER("reduce_line");
	assert(actorp(cont));
	cont = CFG_ACTOR(cfg, one_shot_beh, cont);
	DBUG_PRINT("", ("line=%s", line));
	expr = str_to_cons(line);
	if (expr == NULL) {
		fprintf(stderr, "Syntax Error: %s\n", line);
		DBUG_RETURN FALSE;
	}
	env = init_schemer(cfg);
#if 1
#if 0
	env = CFG_ACTOR(cfg, frame_beh, env);	/* create a new frame to protect the global environment */
#endif
	a = CFG_ACTOR(cfg, fail_beh, cont);
	env = init_add_binding(cfg, env, UNDEFINED, a);		/* install exception handler */
#endif
	msg = mk_pair(cont, mk_pair(expr, env));
	CFG_SEND(cfg, eval__actor, msg);
#if 0
	cfg_start_gc(cfg);
#endif
	n = 0;
	while (n == 0) {
		n = run_configuration(cfg, 100000);
		DBUG_PRINT("", ("n = %d"));
		if (cfg->t_count > 0) {
			DBUG_PRINT("", ("waiting for timed event..."));
			sleep(1);
#if 0
			cfg_start_gc(cfg);
#endif
			n = 0;		/* reset */
		}
	}
#if 1
	cfg_force_gc(cfg);
#endif
	DBUG_RETURN TRUE;
}

static void
read_reduce_print(CONFIG* cfg, FILE* in, CONS* out)
{
	char buf[1024];
	
	DBUG_ENTER("read_reduce_print");
	while (fgets(buf, sizeof(buf), in)) {
		if (!reduce_line(cfg, buf, out)) {
			break;
		}
	}
	DBUG_RETURN;
}

static void
assert_reduce(CONFIG* cfg, char* expr, char* value)
{
	CONS* expect;
	CONS* state;
	CONS* cont;
	
	expect = str_to_cons(value);
	state = map_put(NIL, ATOM("expect"), expect);
	cont = CFG_ACTOR(cfg, assert_msg, state);
	if (!expect || !reduce_line(cfg, expr, cont)) {
		fprintf(stderr, "assert_reduce: FAILED! %s -> %s\n", expr, value);
		abort();
	}
}

void
test_schemer(CONFIG* cfg)
{
	DBUG_ENTER("test_schemer");
	TRACE(printf("--test_schemer--\n"));

	assert_reduce(cfg, "0", "0");
	assert_reduce(cfg, "1", "1");
	assert_reduce(cfg, "_", "_");	/* NOTE: this is non-standard */

	assert_reduce(cfg, "#t", "#t");
	assert_reduce(cfg, "#f", "#f");
	assert_reduce(cfg, "true", "#t");
	assert_reduce(cfg, "false", "#f");

	assert_reduce(cfg, "()", "()");			/* FIXME: this SHOULD signal FAILURE! */
	assert_reduce(cfg, "()", "NIL");		/* FIXME: this SHOULD signal FAILURE! */
	assert_reduce(cfg, "undefined", "(FAILURE! unknown-symbol undefined)");
	
	assert_reduce(cfg, "(quote 0)", "0");
	assert_reduce(cfg, "(quote x)", "x");
	assert_reduce(cfg, "(quote true)", "true");
	assert_reduce(cfg, "(quote ())", "()");
	assert_reduce(cfg, "(quote (x))", "(x)");
	assert_reduce(cfg, "(quote (x y))", "(x y)");
	assert_reduce(cfg, "(quote ((x) y))", "((x) y)");

	assert_reduce(cfg, "'y", "y");
	assert_reduce(cfg, "'()", "NIL");
	assert_reduce(cfg, "'(a b c)", "(a b c)");

	assert_reduce(cfg, "(define z -1)", "ok");	/* NOTE: this is unspecified */
	assert_reduce(cfg, "z", "-1");
	assert_reduce(cfg, "(define z 1)", "redefined");	/* NOTE: this is unspecified */
	assert_reduce(cfg, "(begin (define z 0) z)", "0");
	assert_reduce(cfg, "(list 1 2 3)", "(1 2 3)");
	assert_reduce(cfg, "(list z 0)", "(0 0)");
	assert_reduce(cfg, "(list)", "()");
	assert_reduce(cfg, "(list 'z)", "(z)");
	assert_reduce(cfg, "(list 'x '(y))", "(x (y))");

	assert_reduce(cfg, "(cons 0 NIL)", "(0)");
	assert_reduce(cfg, "(cons 0 (cons 1 '()))", "(0 1)");
	assert_reduce(cfg, "(cons 0 (quote (1)))", "(0 1)");
	assert_reduce(cfg, "(cons 'x '(y))", "(x y)");
	assert_reduce(cfg, "(car '(x))", "x");
	assert_reduce(cfg, "(cdr '(x))", "()");
	assert_reduce(cfg, "(car '(x y))", "x");
	assert_reduce(cfg, "(cdr '(x y))", "(y)");
	assert_reduce(cfg, "(car (cons #t #f))", "#t");
	assert_reduce(cfg, "(cdr (cons #t #f))", "#f");

	assert_reduce(cfg, "(cond (#f 0) (#t 1))", "1");
	assert_reduce(cfg, "(cond (true 0) (else 1))", "0");
	assert_reduce(cfg, "(cond (false 'no) (else 'or 'yes))", "yes");
#if 0 /* "if" is now defined in "library.scm" */
	assert_reduce(cfg, "(if (eq? true #f) #t #f)", "#f");
	assert_reduce(cfg, "(if (eq? false #f) #t #f)", "#t");
#endif

	assert_reduce(cfg, "((lambda (x) x) 1)", "1");
	assert_reduce(cfg, "(((lambda (x) (lambda (y) (list '+ x y))) 3) 4)", "(+ 3 4)");

	assert_reduce(cfg, "(eq? 0 0)", "#t");
	assert_reduce(cfg, "(eq? 0 1)", "#f");
	assert_reduce(cfg, "(eq? NIL '())", "#t");
	assert_reduce(cfg, "(eq? NIL 0)", "#f");
	assert_reduce(cfg, "(eq? 'x 'x)", "#t");
	assert_reduce(cfg, "(eq? '() '())", "#t");
	assert_reduce(cfg, "(eq? '(x) '(x))", "#f");
	assert_reduce(cfg, "(eq? (cons 0 1) (cons 0 1))", "#f");
	assert_reduce(cfg, "(eq? (car (cons 0 1)) 0)", "#t");
	assert_reduce(cfg, "(eq? (cdr (cons 0 1)) 1)", "#t");

	assert_reduce(cfg, "(pair? #t)", "#f");
	assert_reduce(cfg, "(pair? 0)", "#f");
	assert_reduce(cfg, "(pair? 'x)", "#f");
	assert_reduce(cfg, "(pair? '())", "#f");
	assert_reduce(cfg, "(pair? '(0))", "#t");
	assert_reduce(cfg, "(pair? (cons 0 1))", "#t");
	
	assert_reduce(cfg, "(symbol? #t)", "#f");
	assert_reduce(cfg, "(symbol? 0)", "#f");
	assert_reduce(cfg, "(symbol? 'x)", "#t");
	assert_reduce(cfg, "(symbol? '())", "#f");
	assert_reduce(cfg, "(symbol? '(0))", "#f");
	assert_reduce(cfg, "(symbol? (cons 0 1))", "#f");
	
	assert_reduce(cfg, "(boolean? #t)", "#t");
	assert_reduce(cfg, "(boolean? 0)", "#f");
	assert_reduce(cfg, "(boolean? 'x)", "#f");
	assert_reduce(cfg, "(boolean? '())", "#f");
	assert_reduce(cfg, "(boolean? '(0))", "#f");
	assert_reduce(cfg, "(boolean? (cons 0 1))", "#f");
	
	assert_reduce(cfg, "(number? #t)", "#f");
	assert_reduce(cfg, "(number? 0)", "#t");
	assert_reduce(cfg, "(number? 'x)", "#f");
	assert_reduce(cfg, "(number? '())", "#f");
	assert_reduce(cfg, "(number? '(0))", "#f");
	assert_reduce(cfg, "(number? (cons 0 1))", "#f");

	assert_reduce(cfg, "(and)", "#t");
	assert_reduce(cfg, "(and false)", "#f");
	assert_reduce(cfg, "(and true)", "#t");
	assert_reduce(cfg, "(and false false)", "#f");
	assert_reduce(cfg, "(and false true)", "#f");
	assert_reduce(cfg, "(and true false)", "#f");
	assert_reduce(cfg, "(and true true)", "#t");
	assert_reduce(cfg, "(and 0 1 2)", "2");

	assert_reduce(cfg, "(or)", "#f");
	assert_reduce(cfg, "(or false)", "#f");
	assert_reduce(cfg, "(or true)", "#t");
	assert_reduce(cfg, "(or false false)", "#f");
	assert_reduce(cfg, "(or false true)", "#t");
	assert_reduce(cfg, "(or true false)", "#t");
	assert_reduce(cfg, "(or true true)", "#t");
	assert_reduce(cfg, "(or 0 1 2)", "0");

	assert_reduce(cfg, "(+)", "0");
	assert_reduce(cfg, "(+ 2)", "2");
	assert_reduce(cfg, "(+ 0 0)", "0");
	assert_reduce(cfg, "(+ 0 2)", "2");
	assert_reduce(cfg, "(+ 2 0)", "2");
	assert_reduce(cfg, "(+ 2 3)", "5");
	assert_reduce(cfg, "(+ 1 2 3)", "6");

	assert_reduce(cfg, "(- 2)", "-2");
	assert_reduce(cfg, "(- 0 0)", "0");
	assert_reduce(cfg, "(- 0 2)", "-2");
	assert_reduce(cfg, "(- 2 0)", "2");
	assert_reduce(cfg, "(- 2 3)", "-1");
	assert_reduce(cfg, "(- 1 2 3)", "-4");

	assert_reduce(cfg, "(*)", "1");
	assert_reduce(cfg, "(* 2)", "2");
	assert_reduce(cfg, "(* 0 0)", "0");
	assert_reduce(cfg, "(* 0 2)", "0");
	assert_reduce(cfg, "(* 2 0)", "0");
	assert_reduce(cfg, "(* 2 3)", "6");
	assert_reduce(cfg, "(* 1 2 3)", "6");

	assert_reduce(cfg, "(< 0 0)", "#f");
	assert_reduce(cfg, "(< 0 1)", "#t");
	assert_reduce(cfg, "(< 1 0)", "#f");
	assert_reduce(cfg, "(< 0 1 2)", "#t");
	assert_reduce(cfg, "(< 2 1 0)", "#f");

	assert_reduce(cfg, "(> 0 0)", "#f");
	assert_reduce(cfg, "(> 0 1)", "#f");
	assert_reduce(cfg, "(> 1 0)", "#t");
	assert_reduce(cfg, "(> 0 1 2)", "#f");
	assert_reduce(cfg, "(> 2 1 0)", "#t");
	
	assert_reduce(cfg, "(+ 1 (call/cc (lambda (esc) (+ 2 (esc 3)))))", "4");

#if 0
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

	report_actor_usage(reduce_cfg);
#endif

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
	{
		CONFIG* cfg = new_configuration(1000);
		CONS* show_result;

		init_schemer(cfg);		/* pre-load global definitions */
		if (test_mode) {
			test_schemer(cfg);	/* this test involves running the dispatch loop */
		}
#if 1
		show_result = CFG_ACTOR(cfg, println_beh, ATOM("="));
		cfg_add_gc_root(cfg, show_result);		/* protect from gc */
		while (optind < argc) {
			FILE* f;
			char* filename = argv[optind++];

			DBUG_PRINT("", ("filename=%s", filename));
			if ((f = fopen(filename, "r")) == NULL) {
				perror(filename);
				exit(EXIT_FAILURE);
			}
			TRACE(printf("Loading %s...\n", filename));
			read_reduce_print(cfg, f, show_result);
			fclose(f);
		}
		if (interactive) {
			printf("Entering INTERACTIVE mode.\n");
			read_reduce_print(cfg, stdin, show_result);
		}
#endif
		report_actor_usage(cfg);
		report_cons_stats();
	}
	DBUG_RETURN (exit(EXIT_SUCCESS), 0);
}
