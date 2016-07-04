#
# Very simple histogram chart
#

reset
set terminal postscript enhanced color
set output 'gantt.ps'
set object 1 rectangle from screen 0,0 to screen 1,1 fillcolor rgb"white" behind
#set logscale y
n=500 #number of intervals
max=0.01000 #max value
min=0.00000 #min value
width=(max-min)/n #interval width
#function used to map a value to the intervals
hist(x,width)=width*floor(x/width)+width/2.0
set xrange [min:max]
set yrange [1:]
#to put an empty boundary around the
#data inside an autoscaled graph.
#set offset graph 0.05,0.05,0.05,0.0
set xtics min,(max-min)/5,max
set boxwidth width*0.9
set style fill solid 0.5 #fillstyle
set tics out nomirror
set xlabel "x"
set ylabel "Frequency"
#count and plot
plot "f2.txt" u (hist($1,width)):(1.0) smooth freq w boxes lc rgb"green" notitle
