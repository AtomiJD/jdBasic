extends Node

# GDScript side of the GDScript-vs-jdBasic micro-benchmark. Same three
# workloads as bench_jdb.jdb, timed with the engine clock.

func _ready() -> void:
	# 1. Tight numeric loop (pure VM arithmetic).
	var t0 := Time.get_ticks_usec()
	var acc := 0.0
	for i in 500000:
		acc += sin(i * 0.001) * sqrt(float(i) + 1.0)
	var t1 := Time.get_ticks_usec()

	# 2. Array fill + element-wise transform.
	var n2 := 200000
	var a := PackedFloat64Array()
	a.resize(n2)
	for i in n2:
		a[i] = float(i) * 0.5
	var b := PackedFloat64Array()
	b.resize(n2)
	for i in n2:
		b[i] = a[i] * 2.0 + 1.0
	var t2 := Time.get_ticks_usec()

	# 3. Node property read-modify-write (engine access).
	var node := Node3D.new()
	add_child(node)
	for i in 50000:
		var x := node.position.x
		node.position.x = x + 0.001
	var t3 := Time.get_ticks_usec()

	print("GD  bench1 numeric loop  : %.1f ms (acc=%.3f)" % [(t1 - t0) / 1000.0, acc])
	print("GD  bench2 array fill+xf : %.1f ms (b0=%.3f)" % [(t2 - t1) / 1000.0, b[0]])
	print("GD  bench3 node get/set  : %.1f ms" % [(t3 - t2) / 1000.0])
	get_tree().quit()
