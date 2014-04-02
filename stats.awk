#!/usr/bin/awk -f

BEGIN {
	min = 10000000
}

{
	val = $2

	if (val < min)
		min = val
	if (val > max)
		max = val
	avg += val

	# percentiles
	array[NR] = val
}

END {
	avg /= NR
	inv = 1000000

	asort(array)

	p50 = int(NR / 2)
	p90 = int(NR * 0.9)
	p95 = int(NR * 0.95)
	p99 = int(NR * 0.99)

	print "Min/avg/max frametimes (us): 	" min " / " avg " / " max
	print "Min/avg/max FPS:		" inv/max " / " inv/avg " / " inv/min
	print ""
	print "50/90/95/99 percentiles (us):	" array[p50] " / " array[p90] " / " \
			array[p95] " / " array[p99]
}
