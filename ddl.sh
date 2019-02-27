#!/bin/bash
echo "Usage: ./ddl.sh End (d v l) cwd transportProt Begin DDL"
MaxTreadNum=$(printf "%.0f" `echo "$(cat /proc/cpuinfo| grep "processor"| wc -l)+2"|bc`) 
./waf
if [ -n "$1" ]; then 
	End=$1
else
	End=10
fi
echo "Num = $End"

File="DCTCP_CDF.txt"
case $2 in
	d)
	File="DCTCP_CDF.txt"
	;;
	v)
	File="VL2_CDF.txt"
	;;
	l)
	File="LA_CDF.txt"
	;;
	*)
	File="DCTCP_CDF.txt"
	;;
esac

echo $File

if [ -n "$3" ]; then 
	Cwd=$3
	mkdir -p $3
	cp $File xmdl $3
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

if [ -n "$6" ]; then 
	ddl=$6
else
	ddl=0.1
fi

for ((i=Begin;i<=End;i++))
do
		while(($(ps -auxH|awk '{print $8}'|egrep 'R|D'|wc -l) > MaxTreadNum)) || (($(free -g |awk '{print $7}'|tail -n 2|head -n 1) < 10)) || (($(free -g|awk '{print $3}'|tail -n 1) > 20 )) 
		do
			sleep 5
		done
		echo "RunID = $i"
		./waf --run "deadline --ID=$i --transportProt=$Protocol --DDL=$ddl --cdfFileName=$File" --cwd="$Cwd" 2>&1 >& $Cwd"/log$i.txt" &
		sleep 1.5
done
