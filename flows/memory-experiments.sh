#!/bin/bash

################################################### Helpers ########################################################

printf_c() {

	R=31
	B=34
	Y=33

	RB=31
	BB=34
	YB=33

    local color=$1
    shift
    local text="$*"

    local color_code=${!color}
    local bold_code=""

    if [[ ${#color} -gt 1 && $color =~ B$ ]]; then
        bold_code="1;"
    fi

    echo -ne "\e[${bold_code}${color_code}m${text}\e[0m"
}


################################################# Arguments ######################################################

RX_OP_MODE_ARRAY=("SNC" "NOSNC")
script_usage () {
	printf_c RB Argument Invalid "\n"
	printf_c Y "Usage: sudo mlc_exp.sh <RX_OP_MODE> <LATENCY_MODE>\n";
	printf_c Y "RX_OP_MODE: ${RX_OP_MODE_ARRAY[*]}\n";
    printf_c Y "LATENCY_MODE: LAT or BW\n";
}


[ $# -ge 2 ] ||  { script_usage; exit 1; }
[[ " ${RX_OP_MODE_ARRAY[@]} " =~ " $1 " ]] || { script_usage; exit 1; }


RX_OP_MODE=$1
BW_OR_LAT=$2

################################################### Main ########################################################

freq_Ghz=2

if [ "$(id -u)" != "0" ]; then
   printf_c RB "This script must be run as root" 
   exit 1
fi

echo $(date) > ${RX_OP_MODE}_mlc_exp.log
echo "Setting Frequency to ${freq_Ghz}GHz and set uncore frequency to max" | tee ${RX_OP_MODE}_mlc_exp.log
sudo cpupower -c 0-31 frequency-set -f ${freq_Ghz}GHz > /dev/null && sudo wrmsr 0x620 0x1616 > /dev/null

size_loop_kb=(1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288)
delay_loop_us=(4000 2000 1000 800 600 400 200 100 0)

trap "echo 'exiting script'; exit" SIGINT

if [ "$BW_OR_LAT" == "LAT" ]; then
    printf_c Y "Running mlc with LAT\n"
    for i in ${size_loop_kb[@]}
    do
        printf_c Y "Running mlc with buffer ${i}KB\n"
        mlc -b${i} --idle_latency -h >> ${RX_OP_MODE}_mlc_exp.log
    done

elif [ "$BW_OR_LAT" == "BW" ]; then
    printf_c Y "Running mlc with BW\n"
    for i in ${delay_loop_us[@]}
    do
        printf_c Y "Running mlc with delay ${i}us\n"
        mlc --loaded_latency -d${i} -k1-7 >> ${RX_OP_MODE}_mlc_exp_bw.log
    done

else 
    script_usage
    exit 1
fi



################################################### Graphing ########################################################
