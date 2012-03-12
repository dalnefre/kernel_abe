countdown_ring:
	BEHAVIOR {n:$n, a:$a}
	$m -> [
		IF preceeds($m, 0) [
			SEND $a $n
		] ELIF preceeds(0, $n) [
			SEND $a $m
		] ELIF preceeds(0, $m) [
			SEND $a sum($m, -1)
		]
	]
	DONE

challenge:
	BEHAVIOR {n:$n}
	$m -> [
		IF preceeds(0, $n) [
			a: ACTOR $challenge {n:sum($n, -1)}
			BECOME $countdown_ring {n:$n, a:$a}
			SEND $a $m
		] ELSE [
			BECOME $countdown_ring {n:$n, a:$m}
			SEND $m -1
		]
	]
	DONE
