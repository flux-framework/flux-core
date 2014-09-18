#!/bin/sh
#
# Author: Dong H. Ahn, LLNL
#
#set -x

########################################################
#
# Init variables 
#

# zmq-broker path
zmq=$1
# kap path
kap_exe=$2
# total node counts
nn=$3 
# total core counts
tcc=$(($3 * $4))
# scaling factor for actor counts (nconsumers and nproducers)
# a_inc=4
#
#
a_inc=4
a_i=1
# scaling factor for value size
v_inc=8 
v_max=$((256*1024)) # max valsize is 256KB
v_i=8
# scaling factor for access count
ac_inc=4
ac_i=1


########################################################
#
# Function to vary access count and run tests
#
vary_acc_count() {
    local t_v=$1
    local t_f=$2
    local i=$3
    local v=$4
    local ac=$ac_i
    local v_opt=""
    local v_count=0
    local f_opt=""
    local f_count=$tcc
    local dirn="empty"
    local prodn=""
    local consn=""

    if [ $t_v = "P" ] 
    then
        v_opt="--nproducers="
        v_count=$i
        nprod=$v_count
        f_opt="--nconsumers="
        ncons=$f_count
    else
        v_opt="--nconsumers="
        v_count=$i
        ncons=$v_count
        f_opt="--nproducers="
        nprod=$f_count
    fi

    while [ $ac -le $tcc ]
    do
        dirn="T.$tcc:P.$nprod:C.$ncons:V.$v:A.$ac"
        mkdir $dirn 
        sed -e "s,@TCC@,$tcc,g" \
            -e "s,@NN@,$nn,g" \
            -e "s,@P@,$nprod,g" \
            -e "s,@C@,$ncons,g" \
            -e "s,@A@,$ac,g" \
            -e "s,@V@,$v,g" \
            -e "s,@D@,$(ap $dirn),g" \
            -e "s,@K@,$kap_exe,g" < ../run.sh.in > $dirn/run.sh
        runs_abs=$(ap ./$dirn/run.sh)
        chmod u+x $runs_abs
        rd=$(pwd)
        cd $zmq 
        ./scripts/launch -b -c binary $runs_abs
        sleep 1
        cd $rd
        ac=$(($ac * $ac_inc))
    done

}


########################################################
#
# Function to vary value size
#
vary_val_size() {
    local t_v=$1
    local t_f=$2
    local i=$3
    local v=$v_i
    local rc=0

    while [ $v -le $v_max ]
    do
        rc=$(vary_acc_count $t_v $t_f $i $v)
        echo "$rc"
        v=$(($v * $v_inc))
    done
}


########################################################
#
# Function to vary num of actors 
#
vary_nactor() {
    local t_v=$1
    local t_f=$2
    local i=$a_i
    local e=$tcc
    local rc=0

    if [ $t_v = "C" ]
    then
        e=$(($tcc / $a_inc))
        echo "well"
    fi

    while [ $i -le $e ]
    do
        rc=$(vary_val_size $t_v $t_f $i) 
        echo $rc
        i=$(($i * $a_inc))
    done
}


########################################################
#
# Starting experiments
#
if [ $# != 4 ] 
then
	echo "Usage: run-kap.sh zmq_path kap_path nnode ncores_per_node"
	exit 1
fi

rc=$(vary_nactor "P" "C") 
echo -e $rc
rc=$(vary_nactor "C" "P") 
echo -e $rc

echo "Done"
exit 0
    
#
# vi:tabstop=4 shiftwidth=4 expandtab
# 
