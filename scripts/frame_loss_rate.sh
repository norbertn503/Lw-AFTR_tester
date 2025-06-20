#!/bin/bash
#Parameters
impl="lw4o6tester-FLR-snabb-10G-1023_lwB4-Fg100" # name of the tested implementation (used for logging)
dir="b" # valid values: b,f,r; b: bidirectional, f: forward (Left to Right, 6 --> 4), r: reverse (Right to Left, 6 <-- 4) 
rate_start=100000 # starting from this packet rate
rate_end=200000 # ending at this packet rate
rate_step=10000 # increase of the for cycle: for (( r=rate_start; r <= rate_end; r+=rate_step ))
fsizes="84" # IPv6 frame size; IPv4 frame size is always 20 bytes less 
xpts=60 # duration (in seconds) of an experiment instance
to=2000 # timeout in milliseconds
n=2 # foreground traffic, if ( frame_counter % n < m ) 
m=2 # E.g. n=m=2 is all foreground traffic; n=2,m=0 is all background traffic; n=10,m=9 is 90% fg and 10% bg
sleept=10 # sleeping time between the experiments
no_exp=20 # number of experiments
res_dir="results" # base directory for the results (they will be copied there at the end)

# Cycle for the frame size values
for fs in $fsizes
do
	
	
	############################
	
	# Generate log/csv file headers
	echo "#############################" > FLRtest.log
	echo "Tested Implementation: $impl" >> FLRtest.log
	echo "Frame size: $fs:" >> FLRtest.log
	echo "Direction $dir:" >> FLRtest.log
	echo "Value of n: $n" >> FLRtest.log
	echo "Value of m: $m" >> FLRtest.log
	echo "Duration (sec): $xpts" >> FLRtest.log
	echo "Starting rate (QPS): $rate_start" >> FLRtest.log
	echo "Ending rate (QPS): $rate_end" >> FLRtest.log
	echo "Step of rate (QPS): $rate_step" >> FLRtest.log
	echo "Timeout value (sec): $to"  >> FLRtest.log
	echo "Sleep Time (sec): $sleept" >> FLRtest.log
	date +'Date&Time: %Y-%m-%d %H:%M:%S.%N' >> FLRtest.log
	echo "#############################" >> FLRtest.log
	
	# Print header for the result file (incomplete) ...
	printf "No, Size, Dir, n, m, Duration, Timeout" > FLR.csv
	# ... complete header with the tested rates ...
	for (( r=rate_start; r <= rate_end; r+=rate_step ))
	do
		printf ", %i" $r >> FLR.csv
	done
	# ... finish the header line.   
	echo "" >> FLR.csv
	
	# Execute $no_exp number of experiments
	for (( N=1; N <= $no_exp; N++ ))
	do
		# Print parameters into the result file
		printf "%i, %i, %s, %i, %i, %i, %i"  $N $fs $dir $n $m $xpts $to >> FLR.csv
		# Execute the tests for each required rate
		for (( r=rate_start; r <= rate_end; r+=rate_step ))
		do 
			# Log some information about this step
			echo --------------------------------------------------- >> FLRtest.log
			date +'%Y-%m-%d %H:%M:%S.%N Iteration - rate '"$N"'-'"$r" >> FLRtest.log 
			echo ---------------------------------------------------  >> FLRtest.log
			echo "Testing rate: $r fps."
			echo "Command line is: ./build/lw4o6_tester $fs $r $xpts $to $n $m"
			# Execute the test program
	                ./build/lw4o6_tester $fs $r $xpts $to $n $m > temp.out 2>&1
			# Log and print out info
			cat temp.out >> FLRtest.log
			cat temp.out | tail
			# Collect and evaluate the results (depending on the direction of the test)
			if [ "$dir" == "b" ]; then
				fwd_rec=$(grep 'forward frames received:' temp.out | awk '{print $4}')
				rev_rec=$(grep 'reverse frames received:' temp.out | awk '{print $4}')
				echo Forward: $fwd_rec frames were received from the required $((xpts*r)) frames
				echo Forward: $fwd_rec frames were received from the required $((xpts*r)) frames >> FLRtest.log
				echo Reverse: $rev_rec frames were received from the required $((xpts*r)) frames
				echo Reverse: $rev_rec frames were received from the required $((xpts*r)) frames >> FLRtest.log
		        	received=$((fwd_rec+rev_rec))
			fi
			if [ "$dir" == "f" ]; then
				fwd_rec=$(grep 'forward frames received:' temp.out | awk '{print $4}')
				echo Forward: $fwd_rec frames were received from the required $((xpts*r)) frames
				echo Forward: $fwd_rec frames were received from the required $((xpts*r)) frames >> FLRtest.log
				received=$fwd_rec
			fi
			if [ "$dir" == "r" ]; then
				rev_rec=$(grep 'reverse frames received:' temp.out | awk '{print $4}')
				echo Reverse: $rev_rec frames were received from the required $((xpts*r)) frames
				echo Reverse: $rev_rec frames were received from the required $((xpts*r)) frames >> FLRtest.log
		        	received=$rev_rec
			fi
			#sent=$((xpts*r))
			#lost=$((sent-received))
			#flr=$((lost*100/sent))
			# Record the result
			printf ", %i" $received >> FLR.csv
			echo "Sleeping for $sleept seconds..."
			echo "Sleeping for $sleept seconds..." >> FLRtest.log
			# Sleep for $sleept time to give DUT a chance to relax
			sleep $sleept 
		done # (end of the cycle for the different rates)
		# Finish the line in the result file.   
		echo "" >> FLR.csv
		rm temp*
	done # (end of the $no_exp number of experiments)
	# Save the results (may be commented out if not needed)
	dirname="$res_dir/$(date +$impl'-'$dir'-'$fs'-'$n'-'$m'-'$xpts'-'$to'-From-'$rate_start'-To-'$rate_end'-Step-'$rate_step'-%F-%H%M')"
	mkdir -p $dirname
	mv FLRtest.log $dirname/
	mv FLR.csv $dirname/
	mv nohup.out $dirname/ 
	cp -a lw4o6.conf
	
done
