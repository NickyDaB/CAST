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

#Important to touch the logs
#this will cut the current log
/opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
#it will always save as csm_master.log.old.1
mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/FVT_ANALYTICS_csm_master_pre_core_blinking_tests.log
#restart the master daemon to reset the main log file
systemctl restart csmd-master

#gotta wait to make sure master has been brought back up
#find a better way
echo "Restarting Master daemon" >> ${TEMP_LOG}
sleep 60

# Test Case 1: core blinking
helper_files/testing.sh 100 > $TEMP_LOG 2>&1
check_return_exit $? 0 "Test Case 1: Calling testing.sh"

#rm -f ${TEMP_LOG}

#Important to touch the logs for analytics
#cut the current log
/opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
#it will always save as csm_master.log.old.1
mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/FVT_ANALYTICS_csm_master_first_test.log
#restart the master daemon to reset the main log file
systemctl restart csmd-master

#gotta wait to make sure master has been brought back up
#find a better way
echo "Restarting Master daemon" >> ${TEMP_LOG}
sleep 60

#eventually run analytics
python /opt/ibm/csm/tools/API_Statistics.py > ${TEMP_LOG} 2>&1
#this will put things into reports
# ie: /opt/ibm/csm/tools/Reports/Master_Reports/var/log/ibm/csm




echo "------------------------------------------------------------" >> ${LOG}
echo "           Analytics 'core blinking' Bucket Complete        " >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}
echo "Additional Flags:" >> ${LOG}
echo -e "${FLAGS}" >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}

exit 0
