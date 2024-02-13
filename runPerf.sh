mkfifo ctl.fifo
exec {ctl}<>ctl.fifo
mkfifo ack.fifo
exec {ack}<>ack.fifo
echo $ctl
echo $ack
export PERF_CTL_FD=$ctl
export PERF_CTL_ACK_FD=$ack
perf record -g --delay=-1 --control fd:${PERF_CTL_FD},${PERF_CTL_ACK_FD} ./dynamicSetTest --verbose
export PERF_CTL_FD=
export PERF_CTL_ACK_FD=
exec {ctl}>&-
exec {ack}>&-
unlink ctl.fifo
unlink ack.fifo
