#!/bin/bash
echo "Usage: ./mto.sh End (v d l) cwd transportProt Begin"
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
	if [ ! -d $3 ]; then
		mkdir $3
	fi
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
			while(($(ps -auxH|awk '{print $8}'|egrep 'R|D'|wc -l) > MaxTreadNum)) || (($(free -g |awk '{print $7}'|tail -n 2|head -n 1) < 10)) || (($(free -g|awk '{print $3}'|tail -n 1) > 20 )) 
			do
				sleep 5
			done
			echo "RunID = $i-$j"
			sum=$(printf "%.1f" `echo "scale=1;$j / 10"|bc`)	
			./waf --run "mto --ID=$i --load=$sum --transportProt=$Protocol --cdfFileName=$File" --cwd="$Cwd" 2>&1 >& $Cwd"/log$i-$j.txt" &
			sleep 1.5
		done
done
