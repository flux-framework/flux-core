set terminal postscript eps color enhanced
set output  "zmq-broker-hkvs.eps"

set title "kvstest -n-squared, 4-ary, 64 nodes"
set key left top
set xlabel "task count"
set xtics ("64" 64, "128" 128, "256" 256, "512" 512, "1K" 1024, "2K" 2048, "4K" 4096, "8K" 8192)
#set xrange [1:128]
set logscale x
set ylabel "run time - seconds"
set yrange [0:800]
#set y2label "M ops"
#set y2tics 

plot \
"zmq-broker-hkvs.dat" using 1:2 title 'cmb hkvs' with linespoints lw 3, \
"zmq-broker-hkvs.dat" using 1:3 title 'cmb kvs (single redis)' with linespoints lw 3, \
"zmq-broker-hkvs.dat" using 1:4 title 'slurm PMI' with linespoints lw 3
