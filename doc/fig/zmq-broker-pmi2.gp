set terminal postscript eps color enhanced
set output  "zmq-broker-pmi2.eps"

#set title "zmq-broker-mpi"
set key left top
set xlabel "tasks per node"
set xtics ("1" 1, "2" 2, "4" 4, "8" 8, "16" 16, "32" 32, "64" 64)
set xrange [1:64]
set logscale x
set ylabel "run time - seconds"
set y2label "M ops"
set y2tics 
plot \
"zmq-broker-pmi2.dat" using 1:2 title 'slurm PMI' with linespoints lw 3, \
"zmq-broker-pmi2.dat" using 1:3 title 'zmq-broker PMI, single redis' with linespoints lw 3, \
"zmq-broker-pmi2.dat" using 1:4 title 'zmq-broker2 PMI, single redis' with linespoints lw 3, \
"zmq-broker-pmi2.dat" using 1:5 title 'zmq-broker PMI, twemproxy' with linespoints lw 3, \
"zmq-broker-pmi2.dat" using 1:($6/1000000) title 'KVS ops' with linespoints lw 3 axes x1y2
