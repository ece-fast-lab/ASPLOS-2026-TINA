#!/bin/bash

## APPS CHOICE L2TouchFwd, KVS, RSA, NAT

## first arg is the app choice
APP_CHOICE=$1
if [ -z "$APP_CHOICE" ]; then
  echo "Usage: $0 <app_choice>"
  exit 1
fi

CONFIG_CHOICE=$2

## convert name to number
case $APP_CHOICE in
    TouchFwd)
        APP_CHOICE_NUM=0
        APP_ARG_B=400
        APP_ARG_C=0
        ;;
    KVS)
        APP_CHOICE_NUM=3
        APP_ARG_B=10000
        APP_ARG_C=0
        ;;
    RSA)
        APP_CHOICE_NUM=4
        APP_ARG_B=dynamic
        APP_ARG_C=RSA_ENC
        ;;
    NAT)
        APP_CHOICE_NUM=7
        APP_ARG_B=1000
        APP_ARG_C=0
        ;;
    *)
    echo "Invalid app choice: $APP_CHOICE"
    exit 1
    ;;
esac


## print APP ARGS
echo "APP_CHOICE_NUM: $APP_CHOICE_NUM"
echo "APP_ARG_B: $APP_ARG_B"
echo "APP_ARG_C: $APP_ARG_C"

## listen to ctrl-c for clean exit
trap 'echo RECIEVED CTRL-C;exit' INT
# Prepare environment




flows/prep.sh

if [ -z "$CONFIG_CHOICE" ]; then
  echo "No config choice provided, using default."
else
  case $CONFIG_CHOICE in
    ## for SNC and NON-SNC
    SNC)
        tina-stack/rx/dpdk-rx -l 3-5 -a 0000:18:00.1 -- -i 100 -l 100 -a $APP_CHOICE_NUM  -b $APP_ARG_B -c $APP_ARG_C -y 2048
        ;;
    NOSNC)
        tina-stack/rx/dpdk-rx -l 3-5 -a 0000:18:00.1 -- -i 100 -l 100 -a $APP_CHOICE_NUM  -b $APP_ARG_B -c $APP_ARG_C -y 2048
        ;;
    TINA)
        tina-stack/rx/dpdk-rx -l 3-5 -a 0000:18:00.1 -- -i 100 -l 100 -a $APP_CHOICE_NUM  -b $APP_ARG_B -c $APP_ARG_C -y 512 -s 2 -d 512
        ;;
    *)
  esac
fi

