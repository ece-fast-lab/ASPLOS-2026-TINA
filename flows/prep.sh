sudo cpupower -c 0-31 frequency-set -f 2GHz > /dev/null && sudo wrmsr 0x620 0x1616 > /dev/null
timeout 1s pqos
cd /sys/fs/resctrl/
echo "00000030" > COS1/cpus
echo "L3:0=6000;" > COS1/schemata
echo "L3:0=1fff;" > schemata