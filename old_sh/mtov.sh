#!/bin/bash
MaxTreadNum=$(printf "%.0f" `echo "$(cat /proc/cpuinfo| grep "processor"| wc -l)+2"|bc`) 
./waf
if [ -n "$1" ]; then 
	End=$1
else
	End=10
fi
echo "Num = $End"

if [ -n "$2" ]; then 
	Mode=$2
else
	Mode="Conga"
fi

File="VL2_CDF.txt"
if [ -n "$3" ]; then 
	Cwd=$3
	cp $File calfct xml $3
else
	Cwd="./d"
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



for ((i=Begin;i<=End;i++))
do
		for ((j=1;j<10;j=j+2))
		do
			while(($(ps -auxH|awk '{print $8}'|egrep 'R|D'|wc -l)>$MaxTreadNum)) || (($(free -g |awk '{print $7}'|tail -n 2|head -n 1) < 10)) || (($(free -g|awk '{print $3}'|tail -n 1) > 20 )) 
			do
				sleep 5
			done
			echo "RunID = $i-$j"
			sum=$(printf "%.1f" `echo "scale=1;$j / 10"|bc`)	
			./waf --run "test-prio --ID=$i --load=$sum --transportProt=$Protocol --cdfFileName=$File" --cwd="$Cwd" 2>&1 >& $Cwd"log$i-$j.txt" &
			sleep 2
		done
done
