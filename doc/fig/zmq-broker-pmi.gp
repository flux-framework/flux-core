set terminal postscript eps color enhanced
set output  "zmq-broker-pmi.eps"

#set title "zmq-broker-mpi"
set key left top
set xlabel "tasks per node"
set xtics ("1" 1, "2" 2, "4" 4, "8" 8, "16" 16, "32" 32, "64" 64, "128" 128)
set xrange [1:128]
set logscale x
set ylabel "run time - seconds"
set y2label "M ops"
set y2tics 
plot \
 "zmq-broker-pmi.dat" using 1:2 title 'slurm PMI' with linespoints lw 3, \
 "zmq-broker-pmi.dat" using 1:3 title 'zmq-broker PMI, single redis' with linespoints lw 3, \
 "zmq-broker-pmi.dat" using 1:4 title 'zmq-broker PMI, twemproxy' with linespoints lw 3, \
 "zmq-broker-pmi.dat" using 1:($5/1000000) title 'KVS ops' with linespoints lw 3 axes x1y2
