#!/bin/sh
# This is a comment!

echo Begin Script.         # This is a comment, too!

# env vars should already be defined because of water fvt nightly regression 

# old paths. sourced via config file. I want to leave these here for future reference in case something gets messed up. 
# export LOG_PATH=/test/results
# export CSM_PATH=/opt/ibm/csm/bin
# export COMPUTE_NODES="c650f99p18,c650f99p26,c650f99p28,c650f99p36"

# source it again here. (in case someone wants to run manually) vs sourced in the script that calls this script. 
# Try to source the configuration file to get global configuration variables
if [ -f "${BASH_SOURCE%/*}/../../../csm_test.cfg" ]
then
        . "${BASH_SOURCE%/*}/../../../csm_test.cfg"
else
        echo "Could not find csm_test.cfg file expected at "${BASH_SOURCE%/*}/../../../csm_test.cfg", exitting."
        exit 1
fi

# run the helper script to clear the database
psql -d csmdb -U csmdb -f ${FVT_PATH}/buckets/analytics/helper_files/database_clear_allocation_tables.sql 


printf "Begin Test.\n"
#printf "Progress: %i",

for ((i=1 ; i <= $1 ; i++))
do
	#echo Round: $i

	#simple tracker bar
	if(($i % 10 == 0))
	then
		printf "$i\n"
	elif(($i % 5 == 0))
	then
		printf "$i"
	else
		printf "."
	fi

	# Create an allocation
	# job id doesnt matter, set to 1 every time
	# allocate the compute nodes
	# send log to temp file
	${CSM_PATH}/csm_allocation_create -j 1 -n ${COMPUTE_NODES} > ${LOG_PATH}/analytics_allocation_create.log
	# Grab & Store Allocation ID from csm_allocation_create.log
	allocation_id=`grep allocation_id ${LOG_PATH}/analytics_allocation_create.log | awk -F': ' '{print $2}'`
	#remove the temp log file
	rm -f ${LOG_PATH}/analytics_allocation_create.log
	#delete that allocation
	${CSM_PATH}/csm_allocation_delete -a ${allocation_id} > ${LOG_PATH}/analytics_allocation_delete.log
	#remove the temp log file
	rm -f ${LOG_PATH}/analytics_allocation_delete.log
	#echo allocation_id: $allocation_id completed.
done
printf "\nEnd Test.\n"

echo "End script."

exit 0