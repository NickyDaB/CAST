RESULTS_DIR=${FVT_PATH}/results
ARCHIVE_DIR=${FVT_PATH}/archive
TEMP_LOG=${ARCHIVE_DIR}/archive_tmp.log

# Make current archive directory 
# TODO: Add input, default to date
DATE=20$(date +%y-%m-%d)
CURR_ARCHIVE_DIR=${ARCHIVE_DIR}/${DATE}

# Check current archiving directory exists.  If not, create it
ls ${CURR_ARCHIVE_DIR} > ${TEMP_LOG} 2>&1
if [ $? -eq 0 ]
then
	echo "Current Archive Directory ${CURR_ARCHIVE_DIR} exists..."
else
	echo "Creating ${CURR_ARCHIVE_DIR}..."
	mkdir ${CURR_ARCHIVE_DIR}
fi

# Copy bucket log files to current archive directory
# TODO add options for copying files, default to all bucket logs
echo "copying files from ${RESULTS_DIR}/buckets/ to ${CURR_ARCHIVE_DIR}"
cp -R ${RESULTS_DIR}/buckets/ ${CURR_ARCHIVE_DIR}
echo "copying files from ${RESULTS_DIR}/test to ${CURR_ARCHIVE_DIR}"
cp -R ${RESULTS_DIR}/test/ ${CURR_ARCHIVE_DIR}
echo "copying files from ${RESULTS_DIR}/setup to ${CURR_ARCHIVE_DIR}"
cp -R ${RESULTS_DIR}/setup/ ${CURR_ARCHIVE_DIR}
echo "copying files from ${RESULTS_DIR}/performance to ${CURR_ARCHIVE_DIR}"
cp -R ${RESULTS_DIR}/performance/ ${CURR_ARCHIVE_DIR}

# Clean current results directory
# TODO: Add option to enable clean results logs
echo "Cleaning logs in ${RESULTS_DIR}"
${RESULTS_DIR}/clean_logs.sh

# Clean up temp log
rm -f ${TEMP_LOG}
