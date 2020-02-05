#================================================================================
#   
#    setup/csm_fvt_pre_install.sh
# 
#  © Copyright IBM Corporation 2015-2020. All Rights Reserved
#
#    This program is licensed under the terms of the Eclipse Public License
#    v1.0 as published by the Eclipse Foundation and available at
#    http://www.eclipse.org/legal/epl-v10.html
#
#    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
#    restricted by GSA ADP Schedule Contract with IBM Corp.
# 
#================================================================================

# Script to install the fvt test suit for CSM for a test environment

# ============================
# Global variables
# ----------------
# where the specific test suit rpm is located
RPM_DIR=/u/nbuonar/repos/CAST/work/rpms
# record for debugging in the morning
LOG_DIR=/test/results/setup
LOG=${LOG_DIR}/csm_fvt_pre_install.log
# Get hostname of master node
master_node=`hostname`
# some path files
# normally part of the fvt config, but this is run before. we dont want to link.
FVT_PATH=/opt/ibm/csm/test
# we will copy the rpm from the rpm dir into this install dir. then install from this file
INSTALL_DIR=${FVT_PATH}/rpms

# Check to see if log directory exists. 
# If not, then make it.
ls ${LOG_DIR} > /dev/null 2>&1
if [ $? -ne 0 ]
	then
		echo "Log directory not found. Creating " ${LOG_DIR} >> $LOG
		mkdir ${LOG_DIR} >> $LOG
	else
		echo "Log directory found... continuing install of RPMs." >> $LOG
		#do nothing
fi

# ===================================
# BEGIN SCRIPT 

date > $LOG
echo "Starting FVT PRE-INSTALL..." >> $LOG

# Check for RPMs installed on Master, store names
curr_rpm_list=""
curr_rpm_list+=`rpm -qa | grep ibm-csm-test`

# Uninstall old test CSM RPM on Master
if [ "$curr_rpm_list" ]
	then
		rpm -e ${curr_rpm_list} >> $LOG
		if [ $? -ne 0 ]
			then
				echo "Failed to Uninstall CSM RPMs on Master" >> $LOG
				echo "rpm -e ${curr_rpm_list}" >> $LOG
				exit 1
			else
				echo "Uninstalled old CSM RPMs on Master" >> $LOG
		fi
	else
		echo "No RPMs installed on Master" >> $LOG
fi



# Check to see if rpm directory exists. 
# If not, then make it.
ls ${INSTALL_DIR} > /dev/null 2>&1
if [ $? -ne 0 ]
	then
		echo "Install directory not found. Creating " ${INSTALL_DIR} >> $LOG
		mkdir ${INSTALL_DIR} >> $LOG
	else
		echo "Install directory found... continuing install of RPMs." >> $LOG
		#do nothing
fi

#----------------------------------------------------------------------
# RPM Replace and Download Section
#----------------------------------------------------------------------

# Replace Old test RPM in INSTALL_DIR on Master with test RPM from RPM_DIR
ls ${INSTALL_DIR}/ibm-csm-test-* > /dev/null 2>&1
if [ $? -eq 0 ]
	then
		rm -rf ${INSTALL_DIR}/ibm-csm-test*.rpm >> $LOG
		cp ${RPM_DIR}/ibm-csm-test-*.rpm ${INSTALL_DIR} >> $LOG
	else
		cp ${RPM_DIR}/ibm-csm-test-*.rpm ${INSTALL_DIR} >> $LOG
fi

#----------------------------------------------------------------------
# Install Section
#----------------------------------------------------------------------

# Install RPMs on Master
rpm -ivh ${INSTALL_DIR}/ibm-csm-test* >> $LOG

