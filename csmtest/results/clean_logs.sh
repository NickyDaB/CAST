if [ -f "${BASH_SOURCE%/*}/../csm_test.cfg" ]
then
        . "${BASH_SOURCE%/*}/../csm_test.cfg"
else
        echo "Could not find csm_test.cfg file expected at "${BASH_SOURCE%/*}/../csm_test.cfg", exitting."
        exit 1
fi


rm -f ${FVT_PATH}/results/test/*.log
rm -f ${FVT_PATH}/results/buckets/basic/*.log
rm -f ${FVT_PATH}/results/buckets/advanced/*.log
rm -f ${FVT_PATH}/results/buckets/error_injection/*.log
rm -f ${FVT_PATH}/results/buckets/timing/*.log
rm -f ${FVT_PATH}/results/buckets/performance/*.log
rm -f ${FVT_PATH}/results/setup/*.log
rm -f ${FVT_PATH}/results/*.log
rm -f ${FVT_PATH}/results/performance/*.log
rm -f ${FVT_PATH}/results/buckets/BDS/*.log
rm -f ${FVT_PATH}/results/buckets/analytics/*.log
