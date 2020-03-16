#!/bin/sh
# This is a comment!


# Script to help test core isolation, core blinking, and smt mode.

echo Begin Script.         # This is a comment, too!

#global vars
run_total=0
preserve_database=0
tracker=0

function process_options
{
	case $option in
	r)
      echo "-r was triggered, Parameter: $OPTARG"
      run_total=$OPTARG
      ;;
    p)
	  echo "Preserve database."
	  preserve_database=1
	  ;;
	t)
	  echo "Tracker on."
	  tracker=1
	  ;;
	h)
	  echo "Usage: getopts [-r arg] [-c] [-t] [-h]"
	  echo "HELP: "
	  echo "Summary: This script is a helper for the core blinking bucket. It creates and then deletes an allocation."
	  echo "-r [run_total] - number of times to run the test"
	  echo "-p - preserve the database. by default test case wipes database before the test."
	  echo "-t - enable the progress tracker. by default the tracker is off."
	  echo "-h - display the help."
	  exit 0
	  ;;
    *)
      echo "Invalid option: -$OPTARG"
      echo "Usage: getopts [-r arg] [-c] [-t] [-h]"
      exit 1
      ;;
  	esac
}

while getopts "r:pth" option
do
	process_options
done

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

# by default clear the database before we run our testing
# user can override via flag
if (($preserve_database == 0))
then
	# run the helper script to clear the database
	psql -d csmdb -U csmdb -f ${FVT_PATH}/buckets/analytics/helper_files/database_clear_allocation_tables.sql 
fi


printf "Begin Test.\n"
#printf "Progress: %i",

for ((i=1 ; i <= $run_total ; i++))
do
	#echo Round: $i
	if(($tracker == 1))
	then
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
	fi

	# Create an allocation
	# job id doesnt matter, set to 1 every time
	# allocate the compute nodes
	# send log to temp file
	${CSM_PATH}/csm_allocation_create -j 1 -n ${COMPUTE_NODES} --isolated_cores 1 > ${LOG_PATH}/analytics_allocation_create.log
	# Grab & Store Allocation ID from csm_allocation_create.log
	allocation_id=`grep allocation_id ${LOG_PATH}/analytics_allocation_create.log | awk -F': ' '{print $2}'`
	#remove the temp log file
	rm -f ${LOG_PATH}/analytics_allocation_create.log
	#delete that allocation
	${CSM_PATH}/csm_allocation_delete -a ${allocation_id} > ${LOG_PATH}/analytics_allocation_delete.log
	#remove the temp log file
	rm -f ${LOG_PATH}/analytics_allocation_delete.log
	#echo allocation_id: $allocation_id completed.


	#reset back to non isolation
	#${CSM_PATH}/fvt_allocation_create_reset -j 1 -n ${COMPUTE_NODES} --isolated_cores 0 > ${LOG_PATH}/analytics_fvt_allocation_create_reset.log
	# Grab & Store Allocation ID from csm_allocation_create.log
	#allocation_id=`grep allocation_id ${LOG_PATH}/analytics_fvt_allocation_create_reset.log | awk -F': ' '{print $2}'`
	#remove the temp log file
	#rm -f ${LOG_PATH}/analytics_fvt_allocation_create_reset.log
	#delete that allocation
	#${CSM_PATH}/csm_allocation_delete -a ${allocation_id} > ${LOG_PATH}/analytics_allocation_delete.log
	#remove the temp log file
	#rm -f ${LOG_PATH}/analytics_allocation_delete.log
	#echo allocation_id: $allocation_id completed.

done
printf "\nEnd Test.\n"

echo "End script."

exit 0