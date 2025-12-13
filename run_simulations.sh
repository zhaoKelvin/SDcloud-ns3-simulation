#!/bin/bash

# -------------------------------------------------------------------
# CONFIGURATION
# -------------------------------------------------------------------

# The name that will appear in results/<EXPERIMENT_NAME>/
EXPERIMENT_NAME="distance_vs_pdr_4"

# Environments to test
ENVIRONMENTS=("field" "forest")
# ENVIRONMENTS=("field")

# Distances (meters)
DISTANCES=(10 30 50 75 100 150 200 300 500 1000 5000 7500 10000)
# DISTANCES=(7500)
# DISTANCES=(10 100 300 1000)

# Number of repeated runs per (env, distance)
NUM_RUNS=5

# Additional common ns-3 parameters
SIM_TIME=150
INTERVAL=30
PAYLOAD=32
DEVICES=64
TXPOWER=20

NS3="./ns3 run"

# SIM_NAME="scratch/sdcloud/main"
SIM_NAME="scratch/sdcloud-lora/lora"

echo "Starting SDcloud experiment batch..."
echo "Experiment Name: ${EXPERIMENT_NAME}"
echo "------------------------------------------------"

for ENV in "${ENVIRONMENTS[@]}"; do
    for DIST in "${DISTANCES[@]}"; do
        bg_pids=()
        for RUN in $(seq 1 "${NUM_RUNS}"); do

            # wifi command
            # CMD="${NS3} \"${SIM_NAME} \
            #     --topology=star \
            #     --technology=wifi \
            #     --experimentName=${EXPERIMENT_NAME} \
            #     --intervalSec=${INTERVAL} \
            #     --payloadBytes=${PAYLOAD} \
            #     --simTimeSec=${SIM_TIME} \
            #     --nDevices=${DEVICES} \
            #     --txPowerDbm=${TXPOWER} \
            #     --environment=${ENV} \
            #     --distance=${DIST} \
            #     --runSeed=${RUN}\""

            # lora command
            CMD="${NS3} \"${SIM_NAME} \
                --experimentName=${EXPERIMENT_NAME} \
                --intervalSec=${INTERVAL} \
                --payloadBytes=${PAYLOAD} \
                --simTimeSec=${SIM_TIME} \
                --nDevices=${DEVICES} \
                --environment=${ENV} \
                --distance=${DIST} \
                --runSeed=${RUN}\""

            echo "Starting ENV=${ENV}, DIST=${DIST}, RUN=${RUN}"
            eval "${CMD}" &
            bg_pids+=($!)
        done

        # Wait for all RUNs for this (ENV, DIST)
        for pid in "${bg_pids[@]}"; do
            wait "${pid}"
        done
        echo "Completed all RUNs for ENV=${ENV}, DIST=${DIST}"
        echo "------------------------------------------------"
    done
done

echo "All experiments finished!"
