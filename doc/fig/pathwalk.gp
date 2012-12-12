set terminal postscript eps color enhanced
set output  "pathwalk.eps"

#set title "pathwalk: NFS versus ext4/loop over 9p"
set key left top
set xlabel "node count (1 task per node)"
#set xtics ("1" 1, "2" 2, "4" 4, "8" 8, "16" 16, "32" 32, "64" 64, "128" 128, \
#          "256" 256, "512" 512, "1024" 1024, "2048" 2048)
#set xrange [1:2048]
set xtics ("1" 1, "2" 2, "4" 4, "8" 8, "16" 16, "32" 32, "64" 64, "128" 128)
set xrange [1:132]
set logscale x
set ylabel "run time - seconds"
set yrange [0:200]
plot \
 "pathwalk.dat" using 1:2 title 'srun hostname' with linespoints lw 3, \
 "pathwalk.dat" using 1:3 title 'ext4 loop over 9p' with linespoints lw 3, \
 "pathwalk.dat" using 1:4 title 'NFS' with linespoints lw 3
