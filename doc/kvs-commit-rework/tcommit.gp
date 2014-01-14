# set terminal postscript eps color enhanced
# set output  "tcommit.eps"
set terminal postscript color enhanced
set output  "tcommit.ps"

#set title "tcommit"
set key left top
set xlabel "#commits"
set ylabel "commit time - milliseconds"
plot \
 "tcommit-old.dat" using 1:2 every 100 title 'old commit algorithm', \
 "tcommit-new.dat" using 1:2 every 100 title 'new commit algorithm' 
