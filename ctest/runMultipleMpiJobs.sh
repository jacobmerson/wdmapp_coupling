#!/bin/bash -x
[[ $# != 10 && $# != 14 && $# != 20 ]] && \
  echo "Usage: <run command> <process flag> <name0> <procs0> <exe0> <args0> ... <name2> <procs2> <exe2> <args2>" && \
  exit 1
runCmd=${1}
numProcsFlag=${2}
shift 2

declare -a PIDS
declare -a LOGS
run() {
  local name=${1}
  local procs=${2}
  local exe=${3}
  local args=${4}
  IFS=';' read -a argsArray <<< "${args}" #split the cmake list of args
  ${runCmd} ${numProcsFlag} ${procs} ${exe} ${argsArray[@]} &> ${name}.log &
  PIDS+=($!)
  LOGS+=(${name}.log)
}

run $@
shift 4
run $@
shift 4
[[ $# == 4 ]] && run $@
for i in "${!PIDS[@]}"; do
  wait ${PIDS[$i]}
  status=$?
  if [[ $status -ne 0 ]]; then
    cat ${LOGS[$i]}
    exit $status
  fi
done
