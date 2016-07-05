#
# Very simple Gantt chart
#

set style arrow 1 nohead linecolor 7 linewidth 8
set style arrow 2 nohead linecolor 8 linewidth 8
set style arrow 3 nohead linecolor 3 linewidth 8

set terminal postscript enhanced color
set output 'gantt.ps'
set object 1 rectangle from screen 0,0 to screen 1,1 fillcolor rgb"white" behind
set border 3

unset key

set xtics nomirror scale 0.0
set ytics nomirror

#set xrange[0:50000]
#set xrange[44990000:44995000]
set yrange [-1:36]

set title "{/=15 Simple Gantt Chart}\n\n{/:Bold Task start and end times in columns 2 and 3}"

plot '../results/f-28607-scale' u 2:($1):($3-$2):(0.0):yticlabel(1) \
  w vectors as 1, \
     '../results/t-28608-scale' u 4:(35):($5-$4):(0.0):yticlabel("t") \
  w vectors as 2, \
     '../results/m-28607-scale' u 1:(33):($2-$1):(0.0):yticlabel("m") \
  w vectors as 3
