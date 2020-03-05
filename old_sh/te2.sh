#!/bin/bash

./waf
MaxTreadNum=$(printf "%.0f" `echo "$(cat /proc/cpuinfo| grep "processor"| wc -l)+2"|bc`) 
if [ -n "$1" ]; then 
	End=$1
else
	End=10
fi
echo "Num = $num"

if [ -n "$2" ]; then 
	Mode=$2
else
	Mode="Conga"
fi

if [ -n "$3" ]; then 
	Cwd=$3
	cp DCTCP_CDF.txt calfct xml $3
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

#if [ -n "$5" ]; then 
#	File=$5
#else
File="DCTCP_CDF.txt"
#fi

for ((j=1;j<10;j++))
do
		#echo "RunID=10"
		#./waf --run "simulation --ID=$i --runMode=$Mode --load=0.99 --transportProt=$Protocol --cdfFileName=$File"  --cwd="$Cwd" &
		#sleep 3

		allowThre=$(printf "%.0f" `echo "$MaxTreadNum-j*2"|bc`)
		sum=$(printf "%.1f" `echo "scale=1;$j / 10"|bc`)
		for(( i=Begin;i<=End;i++))
		do
			echo "RunID = $i-$j"
			./waf --run "simulation --ID=$i --runMode=$Mode --load=$sum --transportProt=$Protocol --cdfFileName=$File" --cwd="$Cwd" 2>&1 >& $Cwd"log$i-$j.txt" &
			sleep 3
			#echo "Thread Number now:" $(ps -auxH|awk '{print $8}'|grep R|wc -l)
			while(( $(ps -auxH|awk '{print $8}'|egrep 'R|D'|wc -l) > $allowThre)) #ps -axH or top -bH -n1|grep R|wc -l
			do
				sleep 5
			done

			while [ $(free -g |awk '{print $7}'|tail -n 2|head -n 1) -lt 3 ]
			do
			    sleep 5
			done
		done
done
