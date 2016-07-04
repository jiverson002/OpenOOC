#
# Very simple Gantt chart
#

set terminal postscript enhanced color
set output 'gantt.ps'
set object 1 rectangle from screen 0,0 to screen 1,1 fillcolor rgb"white" behind
set yrange [-1:128]
set xtics nomirror scale 0.0
set ytics nomirror scale 0.0
unset key
set title "{/=15 Simple Gantt Chart}\n\n{/:Bold Task start and end times in columns 2 and 3}"
set border 3

set style arrow 1 nohead linecolor 1
set style arrow 2 nohead

#set xrange[004871316:006413926]
set xrange[004885000:004895000]

plot '../results/f-24477' using 2:(1):($3-$2):(0.0):yticlabel(1) with vectors as 1, \
     '../results/t-24478' using 2:(2):($3-$2):(0.0):yticlabel(1) with vectors as 2
