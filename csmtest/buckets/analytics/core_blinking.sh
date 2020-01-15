#================================================================================
#   
#    buckets/analytics/core_blinking.sh
# 
#  © Copyright IBM Corporation 2015-2019. All Rights Reserved
#
#    This program is licensed under the terms of the Eclipse Public License
#    v1.0 as published by the Eclipse Foundation and available at
#    http://www.eclipse.org/legal/epl-v10.html
#
#    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
#    restricted by GSA ADP Schedule Contract with IBM Corp.
# 
#================================================================================

# Bucket for analytics testing
# Subject: Core Blinking
# Goals: See performance statistics

# Try to source the configuration file to get global configuration variables
if [ -f "${BASH_SOURCE%/*}/../../csm_test.cfg" ]
then
        . "${BASH_SOURCE%/*}/../../csm_test.cfg"
else
        echo "Could not find csm_test.cfg file expected at "${BASH_SOURCE%/*}/../csm_test.cfg", exitting."
        exit 1
fi

LOG=${LOG_PATH}/buckets/analytics/core_blinking.log
TEMP_LOG=${LOG_PATH}/buckets/analytics/core_blinking_tmp.log
FLAG_LOG=${LOG_PATH}/buckets/analytics/core_blinking_flag.log

if [ -f "${BASH_SOURCE%/*}/../../include/functions.sh" ]
then
        . "${BASH_SOURCE%/*}/../../include/functions.sh"
else
        echo "Could not find functions file expected at /../../include/functions.sh, exitting."
fi

echo "------------------------------------------------------------" >> ${LOG}
echo "           Starting 'core blinking' Bucket                  " >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}
date >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}


# Test Case 1: core blinking
helper_files/testing.sh 100 > $TEMP_LOG 2>&1
check_return_exit $? 0 "Test Case 1: Calling testing.sh"

#rm -f ${TEMP_LOG}


echo "------------------------------------------------------------" >> ${LOG}
echo "           Analytics 'core blinking' Bucket Complete        " >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}
echo "Additional Flags:" >> ${LOG}
echo -e "${FLAGS}" >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}

exit 0
