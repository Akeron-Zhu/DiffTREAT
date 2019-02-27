#!/bin/bash
thre=160
mem=100
swap=10

predir='d/0127'
begin=1
end=1000

cdf=('d' 'v' 'l')
protocol=('NTcp' 'DcTcp' 'Tcpf' 'Tcp')

for((m=0;m<${#cdf[*]};m++))
do
	for((i=0;i<${#protocol[*]};i++))
	do
		for ((j=0;j<3;j++))
		do
			while(($(ps -u $(whoami) -U $(whoami) uh|awk '{print $8}'|egrep 'R|D'|wc -l) > thre)) || (($(free -g |awk '{print $7}'|tail -n 2|head -n 1) < mem)) || (($(free -g|awk '{print $3}'|tail -n 1) > swap )) 
			do
				sleep 10
			done
		done
		echo "Run Part $i"
		dir="${predir}/mto${cdf[m]}_${protocol[i],,}"
		mkr="mkdir -p $dir"
		run="./mmto.sh $end ${cdf[m]} $dir ${protocol[i]} $begin &"
		echo $mkr 
		echo $run
		if [ ! -d $dir ]; then
			$mkr
		fi
		$run
		sleep 120
	done
done
