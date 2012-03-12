int_generator:
	BEHAVIOR {step:$step, limit:$limit, label:$label, ctx:$ctx, send_to:$send_to}
	{next:$next} -> [
		IF preceeds(0, $step) [
			IF not(preceeds($limit, $next)) [
				SEND $send_to map_put($ctx, $label, $next)
				SEND SELF {next:sum($next, $step)}
			]
		] ELSE [
			IF not(preceeds($next, $limit)) [
				SEND $send_to map_put($ctx, $label, $next)
				SEND SELF {next:sum($next, $step)}
			]
		]
	]
	DONE

seq_generator:
	BEHAVIOR {next:$next, step:$step, limit:$limit, label:$label, ctx:$ctx, send_to:$send_to}
	$msg -> [
		state: map_put_all(map_remove(MINE, next), $msg)
		actor: ACTOR $int_generator $state
		SEND $actor $next
	]
	DONE

cell_actor:
	BEHAVIOR {value:$value, x:$x, y:$y}
	{request:update_grid} -> [
		set_grid_value($x, $y, $value)
	]
	{request:gen_next} -> [
		n: count_neighbors($x, $y)
		IF equals($value, Empty) [
			IF equals($n, 3) [
				BECOME THIS map_put(MINE, value, Full)
			]
		] ELIF equals($value, Full) [
			IF both(preceeds($n, 2), preceeds(3, $n)) [
				BECOME THIS map_put(MINE, value, Empty)
			]
		]
	]
	DONE

ask_cell:
	BEHAVIOR {request:$request, reply_to:$reply_to}
	{x:$x, y:$y} -> [
		cell: get_cell($x, $y)
		SEND $cell MINE
	]
	DONE

ask_cells:
	BEHAVIOR {x_max:$x_max, y_max:$y_max}
	$init -> [
		cell: ACTOR $ask_cell $init
		cols: ACTOR $seq_generator {next:0, step:1, limit:sum($y_max, -1), label:y, ctx:{}, send_to:$cell}
		rows: ACTOR $int_generator {step:1, limit:sum($x_max, -1), label:x, ctx:{}, send_to:$cols}
		SEND $rows {next:0}
	]
	DONE
