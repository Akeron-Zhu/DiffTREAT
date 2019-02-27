#!/bin/bash
thre=160
mem=100
swap=10

predir='d/0211'
begin=1
end=1000

cdf=('d' 'v' 'l')
protocol=('NTcp' 'DcTcp' 'Tcpf' 'Tcp')
ddl=('0.1' '1' '10' '100')

for((m=0;m<${#cdf[*]};m++))
do
	for((i=0;i<${#protocol[*]};i++))
	do
		for ((j=0;j<${#ddl[*]};j++))
		do
			while(($(ps -u $(whoami) -U $(whoami) uh|awk '{print $8}'|egrep 'R|D'|wc -l) > thre)) || (($(free -g |awk '{print $7}'|tail -n 2|head -n 1) < mem)) || (($(free -g|awk '{print $3}'|tail -n 1) > swap )) 
			do
				sleep 10
			done
			echo "Run Part $i"
			dir="${predir}/${cdf[m]}${ddl[j]}_${protocol[i],,}"
			mkr="mkdir -p $dir"
			run="./mddl.sh $end ${cdf[m]} $dir ${protocol[i]} $begin ${ddl[j]} &"
			echo $mkr 
			echo $run
			$mkr
			$run
			sleep 120
		done
	done
done
