sink_beh:
	BEHAVIOR
	DONE

one_shot_beh:
	BEHAVIOR $a
	$m -> [
		SEND $a $m
		BECOME $sink_beh NIL
	]
	DONE

label_beh:
	BEHAVIOR $to:$label
	$value -> SEND $to $label:$value
	DONE

command_beh:
	BEHAVIOR $m
	$a -> SEND $a $m
	DONE

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

lambda_beh:
	BEHAVIOR $lambda:$lex
	$to:($expr:$dyn) -> [
		env: ACTOR $frame_beh $lex
		k: ACTOR $apply_lambda_beh $to:($lambda:$env)
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$dyn)		
	]
	DONE

eval_lambda_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		a: ACTOR lambda_beh $expr:$env
		SEND $to $a
	]
	DONE

apply_form_beh:
	BEHAVIOR $to:$env
	$expr -> [
		e: ACTOR $eval_beh NIL
		SEND $e $to:($expr:$env)
	]

form_beh:
	BEHAVIOR $form:$lex
	$to:($expr:$dyn) -> [
		env: ACTOR $frame_beh $lex
		a: ACTOR $apply_form_beh $to:$dyn
		k: ACTOR $apply_lambda_beh $a:($form:$env)
		SEND $k $expr
	]
	DONE

eval_form_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		a: ACTOR form_beh $expr:$env
		SEND $to $a
	]
	DONE

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

eval_quote_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		SEND $to lst_first($expr)
	]
	DONE

eval_spawn_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		e: ACTOR $eval_seq_beh NIL
		k: ACTOR $sink_beh NIL
		SEND $e $k:($expr:$env)
		SEND $to "ok"
	]
	DONE

eval_time_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		SEND $to NOW
	]
	DONE

apply_define_beh:
	BEHAVIOR $to:($name:$env)
	$value -> SEND $env $to:((define $name $value):$env)
	DONE

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

apply_set_beh:
	BEHAVIOR $to:($name:$env)
	$value -> SEND $env $to:((set! $name $value):$env)
	DONE

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

join_list_beh:
	BEHAVIOR $to:$value
	$list -> SEND $to mk_list($value, $list)
	DONE

next_list_beh:
	BEHAVIOR $to:($expr:$env)
	$value -> [
		k: ACTOR $join_list_beh $to:$value
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$env)
	]
	DONE

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

join_value_beh:
	BEHAVIOR $to:$list
	$to:$value -> SEND $to mk_list($value, $list)
	DONE

join_par_beh:
	BEHAVIOR $to
	$to:$value -> BECOME $join_list_beh $to:$value
	$list -> BECOME $join_value_beh $to:$list
	DONE

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

next_seq_beh:
	BEHAVIOR $eval:($to:($expr:$env))
	$value -> SEND $eval $to:($expr:$env)
	DONE

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

fn_eval_beh:
	BEHAVIOR $beh
	$to:($expr:$env) -> [
		k: ACTOR $beh $to:$env
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$env)		
	]
	DONE

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

apply_car_beh:
	BEHAVIOR $to:$env
	$args -> [
		value: pr_head(lst_first($args))
		SEND $to $value
	]
	DONE

apply_cdr_beh:
	BEHAVIOR $to:$env
	$args -> [
		value: pr_tail(lst_first($args))
		SEND $to $value
	]
	DONE

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

apply_sleep_beh:
	BEHAVIOR $to:$env
	$args -> [
		n: product(lst_first($args), 1000)
		a: ACTOR $eval_time_beh NIL
		SEND_AFTER $n $a $to(NIL:$env)
	]
	DONE

apply_cont_beh:
	BEHAVIOR $to:$env
	$args -> [
		SEND $to lst_first(args)
	]
	DONE

cont_beh:
	BEHAVIOR $cc:$lex
	$to:($expr:$dyn) -> [
		k: ACTOR $apply_cont_beh $cc:$lex
		e: ACTOR $eval_list_beh NIL
		SEND $e $k:($expr:$dyn)
	]
	DONE

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

fail_beh:
	BEHAVIOR $to
	$value -> [
		SEND $to FAILURE!:$value
	]
	DONE

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

eval_fail_beh:
	BEHAVIOR
	$to:($expr:$env) -> [
		a: ACTOR $apply_fail_beh $expr:$env
		SEND $env $a:((get _):$env)
	]
	DONE
