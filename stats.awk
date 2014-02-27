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
}

END {
	avg /= NR
	inv = 1000000
	print "Min/avg/max frametimes (us): 	" min " / " avg " / " max
	print "Min/avg/max FPS:		" inv/max " / " inv/avg " / " inv/min
}
