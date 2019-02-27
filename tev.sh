#!/bin/bash

./waf
MaxTreadNum=$(printf "%.0f" `echo "$(cat /proc/cpuinfo| grep "processor"| wc -l)+2"|bc`) 
if [ -n "$1" ]; then 
	End=$1
else
	End=10
fi

if [ -n "$2" ]; then 
	Mode=$2
else
	Mode="Conga"
fi


File="VL2_CDF.txt"
if [ -n "$3" ]; then 
	Cwd=$3
	if [ ! -d $3 ]; then
		mkdir $3
	fi
	cp $File calfct xml $3
else
	Cwd="./Data"
fi

if [ -n "$4" ]; then 
	Protocol=$4
else
	Protocol="Tcp"
fi

if [ -n "$5" ]; then 
	Begin=$5
else
	Begin=1
fi

if [ -n "$6" ]; then 
	asym=$6
else
	asym=false
fi

for(( i=Begin;i<=End;i++))
do
		#echo "RunID=10"
		#./waf --run "simulation --ID=$i --runMode=$Mode --load=0.99 --transportProt=$Protocol --cdfFileName=$File"  --cwd="$Cwd" &
		#sleep 3
		for ((j=1;j<10;j=j+2))
		do
			while(($(ps -u $(whoami) -U $(whoami) uh|awk '{print $8}'|grep R|wc -l)>=$MaxTreadNum)) || (($(free -g |awk '{print $7}'|tail -n 2|head -n 1) < 10)) || (($(free -g|awk '{print $3}'|tail -n 1) > 20 )) 
			do
				sleep 5
			done
			echo "RunID = $i-$j"
			sum=$(printf "%.1f" `echo "scale=1;$j / 10"|bc`)	
			./waf --run "simulation --ID=$i --runMode=$Mode --load=$sum --transportProt=$Protocol --cdfFileName=$File --asymCapacity2=${asym}" --cwd="$Cwd" 2>&1 >& $Cwd"/log$i-$j.txt" &
			sleep 1.5
			#echo "Thread Number now:" $(ps -auxH|awk '{print $8}'|grep R|wc -l)
			#while [ $(ps -auxH|awk '{print $8}'|egrep 'R|D'|wc -l) -gt $MaxTreadNum -o $(free -g |awk '{print $7}'|tail -n 2|head -n 1) -lt 10 -o $(free -g|awk '{print $3}'|tail -n 1) -gt 50 ] 
		done
done
