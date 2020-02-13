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

date > ${TEMP_LOG}
echo "begin testing..." >> ${TEMP_LOG}

#Important to touch the logs
#this will cut the current log
/opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
#it will always save as csm_master.log.old.1
mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_pre_core_blinking_tests.log
#restart the master daemon to reset the main log file
systemctl restart csmd-master

#gotta wait to make sure master has been brought back up
#find a better way
echo "Restarting Master daemon" >> ${TEMP_LOG}
sleep 60

# ====================================================================================================
#
# Test Case 1: baseline - 1
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
${FVT_PATH}/buckets/analytics/helper_files/testing.sh -r 1 >> $TEMP_LOG 2>&1
check_return_exit $? 0 "Test Case 1:baseline - 1 Calling testing.sh"

#rm -f ${TEMP_LOG}

#Important to touch the logs for analytics
#cut the current log
/opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
#it will always save as csm_master.log.old.1
mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_baseline_1.log
#restart the master daemon to reset the main log file
systemctl restart csmd-master

#gotta wait to make sure master has been brought back up
#find a better way
echo "Restarting Master daemon" >> ${TEMP_LOG}
sleep 60
# ====================================================================================================
# ====================================================================================================
#
# Test Case 2: baseline - 10
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
${FVT_PATH}/buckets/analytics/helper_files/testing.sh -r 10 >> $TEMP_LOG 2>&1
check_return_exit $? 0 "Test Case 2:baseline - 10 Calling testing.sh"

#rm -f ${TEMP_LOG}

#Important to touch the logs for analytics
#cut the current log
/opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
#it will always save as csm_master.log.old.1
mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_baseline_10.log
#restart the master daemon to reset the main log file
systemctl restart csmd-master

#gotta wait to make sure master has been brought back up
#find a better way
echo "Restarting Master daemon" >> ${TEMP_LOG}
sleep 60
# ====================================================================================================
# ====================================================================================================
#
# Test Case 3: baseline - 100
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
# ${FVT_PATH}/buckets/analytics/helper_files/testing.sh -r 100 >> $TEMP_LOG 2>&1
# check_return_exit $? 0 "Test Case 3:baseline - 100 Calling testing.sh"

# #rm -f ${TEMP_LOG}

# #Important to touch the logs for analytics
# #cut the current log
# /opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
# #it will always save as csm_master.log.old.1
# mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_baseline_100.log
# #restart the master daemon to reset the main log file
# systemctl restart csmd-master

# #gotta wait to make sure master has been brought back up
# #find a better way
# echo "Restarting Master daemon" >> ${TEMP_LOG}
# sleep 60
# ====================================================================================================
# ====================================================================================================
#
# Test Case 4: baseline - 1000
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
# ${FVT_PATH}/buckets/analytics/helper_files/testing.sh -r 1000 >> $TEMP_LOG 2>&1
# check_return_exit $? 0 "Test Case 4:baseline - 1000 Calling testing.sh"

# #rm -f ${TEMP_LOG}

# #Important to touch the logs for analytics
# #cut the current log
# /opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
# #it will always save as csm_master.log.old.1
# mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_baseline_1000.log
# #restart the master daemon to reset the main log file
# systemctl restart csmd-master

# #gotta wait to make sure master has been brought back up
# #find a better way
# echo "Restarting Master daemon" >> ${TEMP_LOG}
# sleep 60
# ====================================================================================================
# ====================================================================================================
#
# Test Case 5: baseline - 10000
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
# ${FVT_PATH}/buckets/analytics/helper_files/testing.sh -r 10000 >> $TEMP_LOG 2>&1
# check_return_exit $? 0 "Test Case 5:baseline - 10000 Calling testing.sh"

# #rm -f ${TEMP_LOG}

# #Important to touch the logs for analytics
# #cut the current log
# /opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
# #it will always save as csm_master.log.old.1
# mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_baseline_10000.log
# #restart the master daemon to reset the main log file
# systemctl restart csmd-master

# #gotta wait to make sure master has been brought back up
# #find a better way
# echo "Restarting Master daemon" >> ${TEMP_LOG}
# sleep 60
# ====================================================================================================
# ====================================================================================================
#
# Test Case 6: baseline - 10000 with large database of 10,000 pre records
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
# ${FVT_PATH}/buckets/analytics/helper_files/testing.sh -r 10000 -p >> $TEMP_LOG 2>&1
# check_return_exit $? 0 "Test Case 6:baseline - 10000 with large prepopulated database. Calling testing.sh"

# #rm -f ${TEMP_LOG}

# #Important to touch the logs for analytics
# #cut the current log
# /opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
# #it will always save as csm_master.log.old.1
# mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_baseline_10000_with_large_prepopulated_database.log
# #restart the master daemon to reset the main log file
# systemctl restart csmd-master

# #gotta wait to make sure master has been brought back up
# #find a better way
# echo "Restarting Master daemon" >> ${TEMP_LOG}
# sleep 60
# ====================================================================================================
# ====================================================================================================
#
# Test Case 7: 0 -> N - blink the core once
# Now here. We want to test some core blinking stuff.
# By default there is no system c group.
# aka 
# --isolated_cores = 0
# going from 0 to any number causes a core blink to happen. which we expect to have a large time footprint. 
# this test case we want to see that happening
# we are going from 0 to 1 in this test
#
# ${newpath}/helper_files/testing.sh 100 > $TEMP_LOG 2>&1
${FVT_PATH}/buckets/analytics/helper_files/core_on.sh -r 1 >> $TEMP_LOG 2>&1
check_return_exit $? 0 "Test Case 7: 0 -> N - blink the core once. Calling core_on.sh"

#rm -f ${TEMP_LOG}

#Important to touch the logs for analytics
#cut the current log
/opt/ibm/csm/sbin/rotate-log-file.sh /etc/ibm/csm/csm_master.cfg
#it will always save as csm_master.log.old.1
mv /var/log/ibm/csm/csm_master.log.old.1 /var/log/ibm/csm/fvt_analytics/csm_master_0_to_N_blink_core_once.log
#restart the master daemon to reset the main log file
systemctl restart csmd-master

#gotta wait to make sure master has been brought back up
#find a better way
echo "Restarting Master daemon" >> ${TEMP_LOG}
sleep 60
# ====================================================================================================

#eventually run analytics
python /opt/ibm/csm/tools/API_Statistics.py -p /var/log/ibm/csm/fvt_analytics >> ${TEMP_LOG} 2>&1
#this will put things into reports
# ie: /opt/ibm/csm/tools/Reports/Master_Reports/var/log/ibm/csm




echo "------------------------------------------------------------" >> ${LOG}
echo "           Analytics 'core blinking' Bucket Complete        " >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}
echo "Additional Flags:" >> ${LOG}
echo -e "${FLAGS}" >> ${LOG}
echo "------------------------------------------------------------" >> ${LOG}

exit 0
