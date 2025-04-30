#!/bin/bash

# Number of functions
NUM_FUNCTIONS=15

# Number of parallel invocations per function
INVOCATIONS_PER_FUNCTION=100

# Total number of invocations
TOTAL_INVOCATIONS=$((NUM_FUNCTIONS * INVOCATIONS_PER_FUNCTION))

# Function to invoke a single action
invoke_action() {
    local function_index=$1
    wsk -i action invoke slsfs-datafunction-$function_index &
}

# Main loop
for i in $(seq 1 $TOTAL_INVOCATIONS); do
    function_index=$((i % NUM_FUNCTIONS))
    invoke_action $function_index
done

# Wait for any remaining background jobs to finish
wait

echo "All $TOTAL_INVOCATIONS invocations completed"