/*******************************************************************************
 |    bbwrkqmgr.cc
 |
 |  � Copyright IBM Corporation 2015,2016. All Rights Reserved
 |
 |    This program is licensed under the terms of the Eclipse Public License
 |    v1.0 as published by the Eclipse Foundation and available at
 |    http://www.eclipse.org/legal/epl-v10.html
 |
 |    U.S. Government Users Restricted Rights:  Use, duplication or disclosure
 |    restricted by GSA ADP Schedule Contract with IBM Corp.
 *******************************************************************************/

#include <cmath>

#include <stdio.h>
#include <string.h>

#include <boost/filesystem.hpp>
#include <sys/stat.h>

#include "bbinternal.h"
#include "BBLocalAsync.h"
#include "BBLV_Info.h"
#include "BBLV_Metadata.h"
#include "bbserver_flightlog.h"
#include "bbwrkqmgr.h"
#include "identity.h"
#include "tracksyscall.h"
#include "Uuid.h"

namespace bfs = boost::filesystem;

FL_SetName(FLError, "Errordata flightlog")
FL_SetSize(FLError, 16384)

FL_SetName(FLAsyncRqst, "Async Request Flightlog")
FL_SetSize(FLAsyncRqst, 16384)

#ifndef GPFS_SUPER_MAGIC
#define GPFS_SUPER_MAGIC 0x47504653
#endif


/*
 * Static data
 */
static int asyncRequestFile_ReadSeqNbr = 0;
static FILE* asyncRequestFile_Read = (FILE*)0;
static LVKey LVKey_Null = LVKey();


/*
 * Helper methods
 */
int isGpfsFile(const char* pFileName, bool& pValue)
{
    ENTRY(__FILE__,__FUNCTION__);
    int rc = -1;
    stringstream errorText;

    struct statfs l_Statbuf;

    pValue = false;
    try
    {
        bfs::path l_Path(pFileName);
        rc = statfs(pFileName, &l_Statbuf);
        while ((rc) && (errno == ENOENT))
        {
            l_Path = l_Path.parent_path();
            if (l_Path.string() == "")
            {
                break;
            }
            rc = statfs(l_Path.c_str(), &l_Statbuf);
        }

        if (!rc)
        {
            if (l_Statbuf.f_type == GPFS_SUPER_MAGIC)
            {
                pValue = true;
            }
            FL_Write(FLServer, Statfs_isGpfsFile, "rc=%ld, isGpfsFile=%ld, magic=%lx", rc, pValue, l_Statbuf.f_type, 0);
        }
        else
        {
            FL_Write(FLServer, StatfsFailedGpfs, "Statfs failed", 0, 0 ,0, 0);
            errorText << "Unable to statfs file " << l_Path.string();
            LOG_ERROR_TEXT_ERRNO(errorText, errno);
        }
    }
    catch(ExceptionBailout& e) { }
    catch(exception& e)
    {
        rc = -1;
        LOG_ERROR_RC_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e, rc);
    }

    EXIT(__FILE__,__FUNCTION__);
    return rc;
}


/*
 * Static methods
 */
void HeartbeatEntry::getCurrentTime(struct timeval& pTime)
{
    gettimeofday(&pTime, NULL);

    return;
}


/*
 * Non-static methods
 */
int HeartbeatEntry::serverDeclaredDead(const uint64_t pAllowedNumberOfSeconds)
{
    struct timeval l_CurrentTime = timeval {.tv_sec=0, .tv_usec=0};
    getCurrentTime(l_CurrentTime);
    struct timeval l_LastTimeServerReported = getTime();

    return ((uint64_t)(l_CurrentTime.tv_sec - l_LastTimeServerReported.tv_sec) < pAllowedNumberOfSeconds ? 0 : 1);
}

void WRKQMGR::addHPWorkItem(LVKey* pLVKey, BBTagID& pTagId)
{
    // Build the high priority work item
    WorkID l_WorkId(*pLVKey, (BBLV_Info*)0, pTagId);

    if (g_LogAllAsyncRequestActivity)
    {
        l_WorkId.dump("info", "addHPWorkItem(): ");
    }
    else
    {
        l_WorkId.dump("debug", "addHPWorkItem(): ");
    }

    // Push the work item onto the HP work queue and post
    HPWrkQE->addWorkItem(l_WorkId, DO_NOT_VALIDATE_WORK_QUEUE);

    // NOTE: The transfer queue is not locked when this
    //       method is invoked.
    WRKQMGR::post();

    return;
}

int WRKQMGR::addWrkQ(const LVKey* pLVKey, BBLV_Info* pLV_Info, const uint64_t pJobId, const int pSuspendIndicator)
{
    int rc = 0;

    stringstream l_Prefix;
    l_Prefix << " - addWrkQ() before adding " << *pLVKey << " for jobid " << pJobId << ", suspend indicator " << pSuspendIndicator;
    dump("debug", l_Prefix.str().c_str(), DUMP_UNCONDITIONALLY);

    int l_LocalMetadataUnlocked = 0;
    lockWorkQueueMgr(pLVKey, "addWrkQ", &l_LocalMetadataUnlocked);

    std::map<LVKey,WRKQE*>::iterator it = wrkqs.find(*pLVKey);
    if (it == wrkqs.end())
    {
        WRKQE* l_WrkQE = new WRKQE(pLVKey, pLV_Info, pJobId, pSuspendIndicator);
        l_WrkQE->setDumpOnRemoveWorkItem(config.get("bb.bbserverDumpWorkQueueOnRemoveWorkItem", DEFAULT_DUMP_QUEUE_ON_REMOVE_WORK_ITEM));
        wrkqs.insert(std::pair<LVKey,WRKQE*>(*pLVKey, l_WrkQE));
    }
    else
    {
        rc = -1;
        stringstream errorText;
        errorText << " Failure when attempting to add workqueue for " << *pLVKey << " for jobid " << pJobId;
        dump("info", errorText.str().c_str(), DUMP_UNCONDITIONALLY);
        LOG_ERROR_TEXT_RC(errorText, rc);
    }

    l_Prefix << " - addWrkQ() after adding " << *pLVKey << " for jobid " << pJobId;
    dump("debug", l_Prefix.str().c_str(), DUMP_UNCONDITIONALLY);

    unlockWorkQueueMgr(pLVKey, "addWrkQ", &l_LocalMetadataUnlocked);

    return rc;
}

int WRKQMGR::appendAsyncRequest(AsyncRequest& pRequest)
{
    int rc = 0;
    size_t l_Size;

    LOG(bb,debug) << "appendAsyncRequest(): Attempting an append to the async request file...";

    uid_t l_Uid = getuid();
    gid_t l_Gid = getgid();

  	becomeUser(0, 0);

    int l_Retry = DEFAULT_RETRY_VALUE;
    while (l_Retry--)
    {
        rc = 0;
        l_Size = 0;
        try
        {
            stringstream errorText;

            int l_SeqNbr = 0;
            FILE* fd = openAsyncRequestFile("ab", l_SeqNbr);
            if (fd != NULL)
            {
                char l_Buffer[sizeof(AsyncRequest)+1] = {'\0'};
                pRequest.str(l_Buffer, sizeof(l_Buffer));

                threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fwritesyscall, fd, __LINE__, sizeof(AsyncRequest));
                l_Size = ::fwrite(l_Buffer, sizeof(char), sizeof(AsyncRequest), fd);
                threadLocalTrackSyscallPtr->clearTrack();
                FL_Write(FLAsyncRqst, Append, "Append to async request file having seqnbr %ld for %ld bytes. File pointer %p, %ld bytes written.", l_SeqNbr, sizeof(AsyncRequest), (uint64_t)(void*)fd, l_Size);

                if (l_Size == sizeof(AsyncRequest))
                {
                    l_Retry = 0;
                }
                else
                {
                    // NOTE: If the size written is anything but zero, we are in a world of hurt.
                    //       Current async request file would now be hosed.
                    FL_Write(FLAsyncRqst, AppendFail, "Failed fwrite() request, sequence number %ld. Wrote %ld bytes, expected %ld bytes, ferror %ld.", l_SeqNbr, l_Size, sizeof(AsyncRequest), ferror(fd));
                    if (l_Size)
                    {
                        // Don't retry if a partial result was written
                        // \todo - What to do...  @@DLH
                        l_Retry = 0;
                    }
                    errorText << "appendAsyncRequest(): Failed fwrite() request, sequence number " << l_SeqNbr << ". Wrote " << l_Size \
                              << " bytes, expected " << sizeof(AsyncRequest) << " bytes." \
                              << (l_Retry ? " Request will be retried." : "");
                    LOG_ERROR_TEXT_ERRNO(errorText, ferror(fd));
                    clearerr(fd);
                    rc = -1;
                }
                FL_Write(FLAsyncRqst, CloseAfterAppend, "Close for async request file having seqnbr %ld, file pointer %p.", l_SeqNbr, (uint64_t)(void*)fd, 0, 0);
                ::fclose(fd);
                fd = NULL;
            }
            else
            {
                errorText << "appendAsyncRequest(): Failed open request, sequence number " << l_SeqNbr \
                             << (l_Retry ? ". Request will be retried." : "");
                LOG_ERROR_TEXT_ERRNO(errorText, errno);
                rc = -1;
            }
        }
        catch(exception& e)
        {
            LOG_ERROR_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e);
        }
    }

    // Reset the heartbeat timer count...
    g_Heartbeat_Controller.setCount(0);

  	becomeUser(l_Uid, l_Gid);

    if (!rc)
    {
        if (strstr(pRequest.data, "heartbeat"))
        {
            if (g_LogAllAsyncRequestActivity)
            {
                LOG(bb,info) << "AsyncRequest -> appendAsyncRequest(): Host name " << pRequest.hostname << " => " << pRequest.data;
            }
            else
            {
                LOG(bb,debug) << "AsyncRequest -> appendAsyncRequest(): Host name " << pRequest.hostname << " => " << pRequest.data;
            }
        }
        else
        {
            LOG(bb,info) << "AsyncRequest -> appendAsyncRequest(): Host name " << pRequest.hostname << " => " << pRequest.data;
        }
    }
    else
    {
        LOG(bb,error) << "appendAsyncRequest(): Could not append to the async request file. Failing append was: Host name " << pRequest.hostname << " => " << pRequest.data;
    }

    return rc;
}

void WRKQMGR::calcLastWorkQueueWithEntries()
{
    for (map<LVKey,WRKQE*>::reverse_iterator qe = wrkqs.rbegin(); qe != wrkqs.rend(); ++qe)
    {
        if (qe->second != HPWrkQE && qe->second->getWrkQ_Size())
        {
            // Update the last work with entries
            setLastQueueWithEntries(qe->first);
            break;
        }
    }

    return;
}

void WRKQMGR::calcNextOffsetToProcess(int& pSeqNbr, uint64_t& pOffset)
{
    if (lastOffsetProcessed != START_PROCESSING_AT_OFFSET_ZERO)
    {
        pOffset += sizeof(AsyncRequest);
        if (crossingAsyncFileBoundary(pSeqNbr, pOffset))
        {
            // Crossing into new async request file...
            // Set offset to zero and bump the async request file sequence number.
            pOffset = 0;
            ++pSeqNbr;
        }
    }
    else
    {
        // Special case when bbServer is started and creates a new async request file.
        // pSeqNbr is passed in for the current async request file.
        pOffset = 0;
    }

    return;
}

void WRKQMGR::calcThrottleMode()
{
    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "calcThrottleMode", &l_LocalMetadataUnlockedInd);

    int l_NewThrottleMode = 0;
    for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); qe != wrkqs.end(); ++qe)
    {
        if (qe->second->getRate())
        {
            l_NewThrottleMode = 1;
            break;
        }
    }

    if (throttleMode != l_NewThrottleMode)
    {
        LOG(bb,info) << "calcThrottleMode(): Throttle mode changing from " << (throttleMode ? "true" : "false") << " to " << (l_NewThrottleMode ? "true" : "false");
        if (l_NewThrottleMode)
        {
            Throttle_Timer.forcePop();
            loadBuckets();
        }

        throttleMode = l_NewThrottleMode;
    }

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "calcThrottleMode", &l_LocalMetadataUnlockedInd);
    }

    return;
}

uint64_t WRKQMGR::checkForNewHPWorkItems()
{
    uint64_t l_CurrentNumber = HPWrkQE->getNumberOfWorkItems();
    uint64_t l_NumberAdded = 0;

    int l_AsyncRequestFileSeqNbr = 0;
    int64_t l_OffsetToNextAsyncRequest = 0;

    int rc = findOffsetToNextAsyncRequest(l_AsyncRequestFileSeqNbr, l_OffsetToNextAsyncRequest);
    if (!rc)
    {
        if (l_AsyncRequestFileSeqNbr > 0 && l_OffsetToNextAsyncRequest >= 0)
        {
            bool l_FirstFile = true;
            int l_CurrentAsyncRequestFileSeqNbr = 0;
            uint64_t l_CurrentOffsetToNextAsyncRequest = 0;
            uint64_t l_TargetOffsetToNextAsyncRequest = 0;
            getOffsetToNextAsyncRequest(l_CurrentAsyncRequestFileSeqNbr, l_CurrentOffsetToNextAsyncRequest);
            while (l_CurrentAsyncRequestFileSeqNbr <= l_AsyncRequestFileSeqNbr)
            {
                l_TargetOffsetToNextAsyncRequest = (l_CurrentAsyncRequestFileSeqNbr < l_AsyncRequestFileSeqNbr ? findAsyncRequestFileSize(l_CurrentAsyncRequestFileSeqNbr) : (uint64_t)l_OffsetToNextAsyncRequest);
                if (!l_FirstFile)
                {
                    // We crossed the boundary to a new async request file...  Start at offset zero...
                    l_CurrentOffsetToNextAsyncRequest = 0;
                }
                while (l_CurrentOffsetToNextAsyncRequest < l_TargetOffsetToNextAsyncRequest)
                {
                    // Enqueue the data items to the high priority work queue
                    const string l_ConnectionName = "None";
                    LVKey l_LVKey = std::pair<string, Uuid>(l_ConnectionName, Uuid(HP_UUID));
                    size_t l_Offset = l_CurrentOffsetToNextAsyncRequest;
                    BBTagID l_TagId(BBJob(), l_Offset);

                    // Build/push the work item onto the HP work queue and post
                    if (!l_CurrentOffsetToNextAsyncRequest)
                    {
                        LOG(bb,info) << "Starting to process async requests from async request file sequence number " << l_CurrentAsyncRequestFileSeqNbr;
                    }
                    l_CurrentOffsetToNextAsyncRequest += sizeof(AsyncRequest);
                    addHPWorkItem(&l_LVKey, l_TagId);
                    ++l_NumberAdded;
                }
                l_FirstFile = false;
                ++l_CurrentAsyncRequestFileSeqNbr;
            }

            if (l_NumberAdded)
            {
                // NOTE:  Set the file seqnbr/offset to that of the request file we obtained above,
                //        as we have now enqueued everything up to this point from that request file.
                setOffsetToNextAsyncRequest(l_AsyncRequestFileSeqNbr, l_OffsetToNextAsyncRequest);
                processTurboFactorForFoundRequest();
                LOG(bb,debug) << "checkForNewHPWorkItems(): Found " << l_NumberAdded << " new async requests";
            }
            else
            {
                processTurboFactorForNotFoundRequest();
            }
        }
        else
        {
            LOG(bb,error) << "Error occured when attempting to read the cross bbserver async request file, l_AsyncRequestFileSeqNbr = " \
                          << l_AsyncRequestFileSeqNbr << ", l_OffsetToNextAsyncRequest = " << l_OffsetToNextAsyncRequest;
        }

        // Reset the timer count here, as this method is invoked in places other than checkThrottleTimer()
        g_RemoteAsyncRequest_Controller.setCount(0);
    }
    else
    {
        LOG(bb,error) << "Error occured when attempting to read the cross bbserver async request file, rc = " << rc;
    }

    return l_CurrentNumber + l_NumberAdded;
}

int WRKQMGR::checkLoggingLevel(const char* pSev)
{
    int rc = 1;

    // Simplistic logging level check for early exit for dump() methods...
    // NOTE: Currently, pSev can only come in as "info" or "debug"
    string l_Sev = string(pSev);
    if (((loggingLevel == "info") && (l_Sev == "debug")) ||
        (loggingLevel == "off") ||
        (loggingLevel == "disable"))
    {
        rc = 0;
    }

    return rc;
};


void WRKQMGR::checkThrottleTimer()
{
    HPWrkQE->lock((LVKey*)0, "checkThrottleTimer");

    if (Throttle_Timer.popped())
    {
        LOG(bb,off) << "WRKQMGR::checkThrottleTimer(): Popped";

        // See if it is time to check/add new high priority
        // work items from the cross bbserver metadata
        if (g_RemoteAsyncRequest_Controller.timeToFire())
        {
            checkForNewHPWorkItems();
        }

        // See if it is time to reload the work queue throttle buckets
        if (g_ThrottleBucket_Controller.timeToFire())
        {
            // If any workqueue in throttle mode, load the buckets
            if (inThrottleMode())
            {
                loadBuckets();
            }
            g_ThrottleBucket_Controller.setCount(0);
            setDelayMessageSent(false);
        }

        if (!g_CycleActivities_Controller.alreadyFired())
        {
            BBCheckCycleActivities* l_Request = new BBCheckCycleActivities();
            g_LocalAsync.issueAsyncRequest(l_Request);
            g_CycleActivities_Controller.setTimerFired(1);
        }
    }

    HPWrkQE->unlock((LVKey*)0, "checkThrottleTimer");

    return;
}

int WRKQMGR::createAsyncRequestFile(const char* pAsyncRequestFileName)
{
    int rc = 0;

    char l_Cmd[PATH_MAX+64] = {'\0'};
    snprintf(l_Cmd, sizeof(l_Cmd), "touch %s 2>&1;", pAsyncRequestFileName);

    for (auto&l_Line : runCommand(l_Cmd)) {
        // No expected output...
        LOG(bb,error) << l_Line;
        rc = -1;
    }

    if (!rc)
    {
        // NOTE:  This could be slightly misleading.  We have a race condition between multiple
        //        bbServers possibly attempting to create the same named async request file.
        //        But, touch is being used.  So if the file were to be created just before this bbServer
        //        attempts to create it, all should still be OK with just the last reference dates
        //        being updated.  However, both bbServers will claim to have created the file.
        LOG(bb,info) << "WRKQMGR: New async request file " << pAsyncRequestFileName << " created";
    }

    return rc;
}

int WRKQMGR::crossingAsyncFileBoundary(const int pSeqNbr, const uint64_t pOffset)
{
    int rc = 0;

    FILE* l_FilePtr = 0;
    char l_AsyncRequestFileName[PATH_MAX+1] = {'\0'};
    string l_DataStorePath = g_BBServer_Metadata_Path;

    if (pOffset > ASYNC_REQUEST_FILE_SIZE_FOR_SWAP)
    {
        // Now, determine where the actual end of the async request file is...
        uid_t l_Uid = getuid();
        gid_t l_Gid = getgid();
      	becomeUser(0, 0);

        int l_Retry = DEFAULT_RETRY_VALUE;
        while (l_Retry--)
        {
            rc = 0;
            try
            {
                stringstream errorText;
                snprintf(l_AsyncRequestFileName, PATH_MAX+1, "%s/%s_%d", l_DataStorePath.c_str(), XBBSERVER_ASYNC_REQUEST_BASE_FILENAME.c_str(), pSeqNbr);
                threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fopensyscall, l_AsyncRequestFileName, __LINE__);
                l_FilePtr = ::fopen(l_AsyncRequestFileName, "rb");
                threadLocalTrackSyscallPtr->clearTrack();
                if (l_FilePtr != NULL)
                {
                    threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fseeksyscall, l_FilePtr, __LINE__);
                    rc = ::fseek(l_FilePtr, 0, SEEK_END);
                    threadLocalTrackSyscallPtr->clearTrack();
                    FL_Write(FLAsyncRqst, SeekEndForCrossing, "Seeking the end of async request file having seqnbr %ld, rc %ld.", pSeqNbr, rc, 0, 0);
                    if (!rc)
                    {
                        int64_t l_Offset = -1;
                        threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::ftellsyscall, l_FilePtr, __LINE__);
                        l_Offset = (int64_t)::ftell(l_FilePtr);
                        threadLocalTrackSyscallPtr->clearTrack();
                        if (l_Offset >= 0)
                        {
                            if (l_Offset <= (int64_t)pOffset)
                            {
                                rc = 1;
                            }
                            l_Retry = 0;
                        }
                        else
                        {
                            FL_Write(FLAsyncRqst, RtvEndOffsetForCrossingFail, "Failed ftell() request, sequence number %ld, errno %ld.", pSeqNbr, errno, 0, 0);
                            errorText << "crossingAsyncFileBoundary(): Failed ftell() request, sequence number " << pSeqNbr \
                                      << (l_Retry ? ". Request will be retried." : "");
                            LOG_ERROR_TEXT_ERRNO(errorText, errno);
                            rc = -1;
                        }
                    }
                    else
                    {
                        FL_Write(FLAsyncRqst, SeekEndFailForCrossing, "Failed fseek() request for end of file, sequence number %ld, ferror %ld.", pSeqNbr, ferror(l_FilePtr), 0, 0);
                        errorText << "crossingAsyncFileBoundary(): Failed fseek() request for end of file, sequence number " << pSeqNbr \
                                  << (l_Retry ? ". Request will be retried." : "");
                        LOG_ERROR_TEXT_ERRNO(errorText, ferror(l_FilePtr));
                        clearerr(l_FilePtr);
                        rc = -1;
                    }
                }
                else
                {
                    errorText << "crossingAsyncFileBoundary(): Failed open request, sequence number " << pSeqNbr \
                              << (l_Retry ? ". Request will be retried." : "");
                    LOG_ERROR_TEXT_ERRNO(errorText, errno);
                    rc = -1;
                }
            }
            catch(exception& e)
            {
                LOG_ERROR_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e);
            }
        }
  	    becomeUser(l_Uid, l_Gid);
  	}

    if (rc < 0)
    {
        LOG(bb,error) << "crossingAsyncFileBoundary(): Failed to determine if offset " << pOffset << " for async request file sequence number " << pSeqNbr << " is crossing into the next async request file.";
        endOnError();

        // If we continue, just pass back a zero indicating the offset does not cross into the next async request file...
        rc = 0;
    }

    return rc;
}

void WRKQMGR::decrementNumberOfConcurrentCancelRequests()
{
    --numberOfConcurrentCancelRequests;
    if (numberOfConcurrentCancelRequests > 999999)
    {
        LOG(bb,error) << "decrementNumberOfConcurrentCancelRequests(): numberOfConcurrentCancelRequests is out of range with a value of " << numberOfConcurrentCancelRequests;
        endOnError();
    }

    return;
}

void WRKQMGR::decrementNumberOfConcurrentHPRequests()
{
    --numberOfConcurrentHPRequests;
    if (numberOfConcurrentHPRequests > 999999)
    {
        LOG(bb,error) << "decrementNumberOfConcurrentHPRequests(): numberOfConcurrentHPRequests is out of range with a value of " << numberOfConcurrentHPRequests;
        endOnError();
    }

    return;
}

void WRKQMGR::dump(const char* pSev, const char* pPostfix, DUMP_OPTION pDumpOption) {
    // NOTE: We early exit based on the logging level because we don't want to 'reset'
    //       dump counters, etc. if the logging facility filters out an entry.
    if (wrkqmgr.checkLoggingLevel(pSev))
    {
        int l_TransferQueueUnlocked = 0;
        int l_LocalMetadataUnlockedInd = 0;
        int l_WorkQueueMgrLocked = 0;
        int l_HP_TransferQueueUnlocked = 0;
        if (HPWrkQE && HPWrkQE->transferQueueIsLocked())
        {
            HPWrkQE->unlock((LVKey*)0, "WRKQMGR::dump - start");
            l_HP_TransferQueueUnlocked = 1;
        }
        if (!workQueueMgrIsLocked())
        {
            l_TransferQueueUnlocked = unlockTransferQueueIfNeeded((LVKey*)0, "WRKQMGR::dump - before lock of WRKQMGR");
            lockWorkQueueMgr((LVKey*)0, "WRKQMGR::dump - start", &l_LocalMetadataUnlockedInd);
            l_WorkQueueMgrLocked = 1;
        }

        if (pSev == loggingLevel)
        {
            const char* l_PostfixOverride = " Work Queue Mgr (Not an error - Skip Interval)";
            char* l_PostfixStr = const_cast<char*>(pPostfix);

            if (allowDump)
            {
                if (pDumpOption == DUMP_UNCONDITIONALLY || pDumpOption == DUMP_ALWAYS || inThrottleMode())
                {
                    bool l_DumpIt = false;
                    if (pDumpOption != DUMP_UNCONDITIONALLY)
                    {
                        if (numberOfWorkQueueItemsProcessed != lastDumpedNumberOfWorkQueueItemsProcessed)
                        {
                            l_DumpIt = true;
                        }
                        else
                        {
                            if (numberOfAllowedSkippedDumpRequests && numberOfSkippedDumpRequests >= numberOfAllowedSkippedDumpRequests)
                            {
                                l_DumpIt = true;
                                l_PostfixStr = const_cast<char*>(l_PostfixOverride);
                            }
                        }
                    }
                    else
                    {
                        l_DumpIt = true;
                    }

                    if (HPWrkQE && l_DumpIt)
                    {
                        HPWrkQE->lock((LVKey*)0, "WRKQMGR::dump");

                        stringstream l_OffsetStr;
                        if (outOfOrderOffsets.size())
                        {
                            // Build an output stream for the out of order async request offsets...
                            l_OffsetStr << "(";
                            size_t l_NumberOfOffsets = outOfOrderOffsets.size();
                            for(size_t i=0; i<l_NumberOfOffsets; ++i)
                            {
                                if (i!=l_NumberOfOffsets-1) {
                                    l_OffsetStr << hex << uppercase << setfill('0') << outOfOrderOffsets[i] << setfill(' ') << nouppercase << dec << ",";
                                } else {
                                    l_OffsetStr << hex << uppercase << setfill('0') << outOfOrderOffsets[i] << setfill(' ') << nouppercase << dec;
                                }
                            }
                            l_OffsetStr << ")";
                        }

                        int l_CheckForCanceledExtents = checkForCanceledExtents;
                        if (!strcmp(pSev,"debug"))
                        {
                            LOG(bb,debug) << ">>>>> Start: WRKQMGR" << l_PostfixStr << " <<<<<";
//                            LOG(bb,debug) << "                 Throttle Mode: " << (throttleMode ? "true" : "false") << "  TransferQueue Locked: " << (transferQueueLocked ? "true" : "false");
                            LOG(bb,debug) << "                 Throttle Mode: " << (throttleMode ? "true" : "false") << "  Number of Workqueue Items Processed: " << numberOfWorkQueueItemsProcessed \
                                          << "  Check Canceled Extents: " << (l_CheckForCanceledExtents ? "true" : "false") << "  Snoozing: " << (Throttle_Timer.isSnoozing() ? "true" : "false");
                            if (numberOfConcurrentCancelRequests)
                            {
                                LOG(bb,debug) << "      ConcurrentCancelRequests: " << numberOfConcurrentCancelRequests << "  AllowedConcurrentCancelRequests: " << numberOfAllowedConcurrentCancelRequests;
                            }
                            if (numberOfConcurrentHPRequests)
                            {
                                LOG(bb,debug) << "          ConcurrentHPRequests: " << numberOfConcurrentHPRequests << "  AllowedConcurrentHPRequests: " << numberOfAllowedConcurrentHPRequests;
                            }
                            if (useAsyncRequestReadTurboMode)
                            {
                                LOG(bb,debug) << " AsyncRequestRead Turbo Factor: " << asyncRequestReadTurboFactor << "  AsyncRequestRead ConsecutiveNoNewRequests: " << asyncRequestReadConsecutiveNoNewRequests;
                            }
//                            LOG(bb,debug) << "     Declare Server Dead Count: " << declareServerDeadCount;
                            LOG(bb,debug) << "          Last Queue Processed: " << lastQueueProcessed << "  Last Queue With Entries: " << lastQueueWithEntries;
                            LOG(bb,debug) << "           Lst prc'd AsyncSeq#: " << asyncRequestFileSeqNbrLastProcessed << "  AsyncSeq#: " << asyncRequestFileSeqNbr << "  LstOff: 0x" << hex << uppercase << setfill('0') \
                                          << setw(8) << lastOffsetProcessed << "  NxtOff: 0x" << setw(8) << offsetToNextAsyncRequest \
                                          << setfill(' ') << nouppercase << dec << "  #OutOfOrd " << outOfOrderOffsets.size() << "  #InfltHP_Rqsts: " << inflightHP_Requests.size();
                            if (outOfOrderOffsets.size())
                            {
                                LOG(bb,debug) << " Out of Order Offsets (in hex): " << l_OffsetStr.str();
                            }
                            LOG(bb,debug) << "   Number of Workqueue Entries: " << wrkqs.size();

                            for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); qe != wrkqs.end(); ++qe)
                            {
                                qe->second->dump(pSev, "          ");
                            }

                            LOG(bb,debug) << ">>>>>   End: WRKQMGR" << l_PostfixStr << " <<<<<";
                        }
                        else if (!strcmp(pSev,"info"))
                        {
                            LOG(bb,info) << ">>>>> Start: WRKQMGR" << l_PostfixStr << " <<<<<";
//                            LOG(bb,info) << "                 Throttle Mode: " << (throttleMode ? "true" : "false") << "  TransferQueue Locked: " << (transferQueueLocked ? "true" : "false");
                            LOG(bb,info) << "                 Throttle Mode: " << (throttleMode ? "true" : "false") << "  Number of Workqueue Items Processed: " << numberOfWorkQueueItemsProcessed \
                                         << "  Check Canceled Extents: " << (l_CheckForCanceledExtents ? "true" : "false") << "  Snoozing: " << (Throttle_Timer.isSnoozing() ? "true" : "false");
                            if (numberOfConcurrentCancelRequests)
                            {
                                LOG(bb,info) << "      ConcurrentCancelRequests: " << numberOfConcurrentCancelRequests << "  AllowedConcurrentCancelRequests: " << numberOfAllowedConcurrentCancelRequests;
                            }
                            if (numberOfConcurrentHPRequests)
                            {
                                LOG(bb,info) << "          ConcurrentHPRequests: " << numberOfConcurrentHPRequests << "  AllowedConcurrentHPRequests: " << numberOfAllowedConcurrentHPRequests;
                            }
                            if (useAsyncRequestReadTurboMode)
                            {
                                LOG(bb,info) << " AsyncRequestRead Turbo Factor: " << asyncRequestReadTurboFactor << "  AsyncRequestRead ConsecutiveNoNewRequests: " << asyncRequestReadConsecutiveNoNewRequests;
                            }
//                            LOG(bb,info) << "     Declare Server Dead Count: " << declareServerDeadCount;
                            LOG(bb,info) << "          Last Queue Processed: " << lastQueueProcessed << "  Last Queue With Entries: " << lastQueueWithEntries;
                            LOG(bb,info) << "           Lst prc'd AsyncSeq#: " << asyncRequestFileSeqNbrLastProcessed << "  AsyncSeq#: " << asyncRequestFileSeqNbr << "  LstOff: 0x" << hex << uppercase << setfill('0') \
                                         << setw(8) << lastOffsetProcessed << "  NxtOff: 0x" << setw(8) << offsetToNextAsyncRequest \
                                         << setfill(' ') << nouppercase << dec << "  #OutOfOrd " << outOfOrderOffsets.size() << "  #InfltHP_Rqsts: " << inflightHP_Requests.size();
                            if (outOfOrderOffsets.size())
                            {
                                LOG(bb,info) << " Out of Order Offsets (in hex): " << l_OffsetStr.str();
                            }
                            LOG(bb,info) << "   Number of Workqueue Entries: " << wrkqs.size();

                            for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); qe != wrkqs.end(); ++qe)
                            {
                                qe->second->dump(pSev, "          ");
                            }

                            LOG(bb,info) << ">>>>>   End: WRKQMGR" << l_PostfixStr << " <<<<<";
                        }

                        HPWrkQE->unlock((LVKey*)0, "WRKQMGR::dump");

                        lastDumpedNumberOfWorkQueueItemsProcessed = numberOfWorkQueueItemsProcessed;
                        numberOfSkippedDumpRequests = 0;
                        g_Dump_WrkQMgr_Controller.setCount(0);
                    }
                    else
                    {
                        // Not dumped...
                        ++numberOfSkippedDumpRequests;
                    }
                }
            }
        }

        if (l_WorkQueueMgrLocked)
        {
            unlockWorkQueueMgr((LVKey*)0, "WRKQMGR::dump - end", &l_LocalMetadataUnlockedInd);
        }
        if (l_TransferQueueUnlocked)
        {
            lockTransferQueue((LVKey*)0, "WRKQMGR::dump - end");
        }
        if (l_HP_TransferQueueUnlocked)
        {
            HPWrkQE->lock((LVKey*)0, "WRKQMGR::dump - end");
        }
    }

    return;
}

void WRKQMGR::dump(queue<WorkID>* pWrkQ, WRKQE* pWrkQE, const char* pSev, const char* pPrefix)
{
    char l_Temp[64] = {'\0'};

    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "dump", &l_LocalMetadataUnlockedInd);

    if (wrkqs.size() == 1)
    {
        strCpy(l_Temp, " queue exists: ", sizeof(l_Temp));
    }
    else
    {
        strCpy(l_Temp, " queues exist: ", sizeof(l_Temp));
    }

    pWrkQE->dump(pSev, pPrefix);

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "dump", &l_LocalMetadataUnlockedInd);
    }

    return;
}

void WRKQMGR::dumpHeartbeatData(const char* pSev, const char* pPrefix)
{
    bool l_HP_TransferQueueLocked = false;
    if (!HPWrkQE->transferQueueIsLocked())
    {
        HPWrkQE->lock((LVKey*)0, "WRKQMGR::dumpHeartbeatData");
        l_HP_TransferQueueLocked = true;
    }

    if (heartbeatData.size())
    {
        int i = 1;
        if (!strcmp(pSev,"debug"))
        {
            LOG(bb,debug) << ">>>>> Start: " << (pPrefix ? pPrefix : "") << heartbeatData.size() \
                          << (heartbeatData.size()==1 ? " reporting bbServer <<<<<" : " reporting bbServers <<<<<");
            for (auto it=heartbeatData.begin(); it!=heartbeatData.end(); ++it)
            {
                LOG(bb,debug) << i << ") " << it->first << " -> Count " << (it->second).getCount() \
                              << ", time reported: " << timevalToStr((it->second).getTime()) \
                              << ", timestamp from bbServer: " << (it->second).getServerTime();
                ++i;
            }
            LOG(bb,debug) << ">>>>>   End: " << heartbeatData.size() \
                          << (heartbeatData.size()==1 ? " reporting bbServer <<<<<" : " reporting bbServers <<<<<");
        }
        else if (!strcmp(pSev,"info"))
        {
            LOG(bb,info) << ">>>>> Start: " << (pPrefix ? pPrefix : "") << heartbeatData.size() \
                         << (heartbeatData.size()==1 ? " reporting bbServer <<<<<" : " reporting bbServers <<<<<");
            for (auto it=heartbeatData.begin(); it!=heartbeatData.end(); ++it)
            {
                LOG(bb,info) << i << ") " << it->first << " -> Count " << (it->second).getCount() \
                              << ", time reported: " << timevalToStr((it->second).getTime()) \
                              << ", timestamp from bbServer: " << (it->second).getServerTime();
                ++i;
            }
            LOG(bb,info) << ">>>>>   End: " << heartbeatData.size() \
                         << (heartbeatData.size()==1 ? " reporting bbServer <<<<<" : " reporting bbServers <<<<<");
        }
    }
    else
    {
        if (!strcmp(pSev,"debug"))
        {
            LOG(bb,debug) << ">>>>>   No other reporting bbServers";
        }
        else if (!strcmp(pSev,"info"))
        {
            LOG(bb,info) << ">>>>>   No other reporting bbServers";
        }
    }

    if (l_HP_TransferQueueLocked)
    {
        HPWrkQE->unlock((LVKey*)0, "WRKQMGR::dumpHeartbeatData");
    }

    return;
}

void WRKQMGR::endProcessingHP_Request(AsyncRequest& pRequest)
{
    string l_Data = pRequest.getData();
    for (auto it=inflightHP_Requests.begin(); it!=inflightHP_Requests.end(); ++it)
    {
        if (*it == l_Data)
        {
            inflightHP_Requests.erase(it);
            break;
        }
    }

    return;
}

uint64_t WRKQMGR::findAsyncRequestFileSize(const int pSeqNbr)
{
    int rc = 0;
    int64_t l_Size = -1;

    FILE* l_FilePtr = 0;
    char l_AsyncRequestFileName[PATH_MAX+1] = {'\0'};
    string l_DataStorePath = g_BBServer_Metadata_Path;

    uid_t l_Uid = getuid();
    gid_t l_Gid = getgid();

    becomeUser(0, 0);

    int l_Retry = DEFAULT_RETRY_VALUE;
    while (l_Retry--)
    {
        rc = 0;
        try
        {
            stringstream errorText;
            snprintf(l_AsyncRequestFileName, PATH_MAX+1, "%s/%s_%d", l_DataStorePath.c_str(), XBBSERVER_ASYNC_REQUEST_BASE_FILENAME.c_str(), pSeqNbr);
            threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fopensyscall, l_AsyncRequestFileName, __LINE__);
            l_FilePtr = ::fopen(l_AsyncRequestFileName, "rb");
            threadLocalTrackSyscallPtr->clearTrack();
            if (l_FilePtr != NULL)
            {
                threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fseeksyscall, l_FilePtr, __LINE__);
                rc = ::fseek(l_FilePtr, 0, SEEK_END);
                threadLocalTrackSyscallPtr->clearTrack();
                FL_Write(FLAsyncRqst, SeekEndForSize, "Seeking the end of async request file having seqnbr %ld, rc %ld.", pSeqNbr, rc, 0, 0);
                if (!rc)
                {
                    threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::ftellsyscall, l_FilePtr, __LINE__);
                    l_Size = (int64_t)::ftell(l_FilePtr);
                    threadLocalTrackSyscallPtr->clearTrack();
                    if (l_Size >= 0)
                    {
                        l_Retry = 0;
                    }
                    else
                    {
                        FL_Write(FLAsyncRqst, RtvEndOffsetForSize, "Failed ftell() request, sequence number %ld, errno %ld.", pSeqNbr, errno, 0, 0);
                        errorText << "findAsyncRequestFileSize(): Failed ftell() request, sequence number " << pSeqNbr \
                                  << (l_Retry ? ". Request will be retried." : "");
                        LOG_ERROR_TEXT_ERRNO(errorText, errno);
                        rc = -1;
                    }
                }
                else
                {
                    FL_Write(FLAsyncRqst, SeekEndFailForSize, "Failed fseek() request for end of file, sequence number %ld, ferror %ld.", pSeqNbr, ferror(l_FilePtr), 0, 0);
                    errorText << "findAsyncRequestFileSize(): Failed fseek() request for end of file, sequence number " << pSeqNbr \
                              << (l_Retry ? ". Request will be retried." : "");
                    LOG_ERROR_TEXT_ERRNO(errorText, ferror(l_FilePtr));
                    clearerr(l_FilePtr);
                    rc = -1;
                }
            }
            else
            {
                errorText << "findAsyncRequestFileSize(): Failed open request, sequence number " << pSeqNbr \
                             << (l_Retry ? ". Request will be retried." : "");
                LOG_ERROR_TEXT_ERRNO(errorText, errno);
                rc = -1;
            }
        }
        catch(exception& e)
        {
            LOG_ERROR_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e);
        }
    }

  	becomeUser(l_Uid, l_Gid);

    if (rc < 0)
    {
        LOG(bb,error) << "findAsyncRequestFileSize(): Failed to determine the current size for async request file sequence number " << pSeqNbr;
        endOnError();

        // If we continue, just pass back a zero indicating a file size of zero length...
        l_Size = 0;
    }

    return (uint64_t)l_Size;
}

int WRKQMGR::findOffsetToNextAsyncRequest(int &pSeqNbr, int64_t &pOffset)
{
    int rc = 0;

    pSeqNbr = 0;
    pOffset = 0;

    uid_t l_Uid = getuid();
    gid_t l_Gid = getgid();
    MAINTENANCE_OPTION l_Maintenance = NO_MAINTENANCE;

  	becomeUser(0, 0);

    int l_Retry = DEFAULT_RETRY_VALUE;
    while (l_Retry--)
    {
        rc = 0;
        try
        {
            stringstream errorText;
            FILE* fd = openAsyncRequestFile("rb", pSeqNbr, l_Maintenance);
            if (fd != NULL)
            {
                threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fseeksyscall, fd, __LINE__);
                rc = ::fseek(fd, 0, SEEK_END);
                threadLocalTrackSyscallPtr->clearTrack();
                FL_Write(FLAsyncRqst, SeekEnd, "Seeking the end of async request file having seqnbr %ld, rc %ld.", pSeqNbr, rc, 0, 0);
                if (!rc)
                {
                    int64_t l_Offset = -1;
                    threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::ftellsyscall, fd, __LINE__);
                    l_Offset = (int64_t)::ftell(fd);
                    threadLocalTrackSyscallPtr->clearTrack();
                    if (l_Offset >= 0)
                    {
                        l_Retry = 0;
                        pOffset = l_Offset;
                        FL_Write(FLAsyncRqst, RtvEndOffsetForFind, "End of async request file with seqnbr %ld is at offset %ld.", pSeqNbr, pOffset, 0, 0);
                        LOG(bb,debug) << "findOffsetToNextAsyncRequest(): SeqNbr: " << pSeqNbr << ", Offset: " << pOffset;
                    }
                    else
                    {
                        FL_Write(FLAsyncRqst, RtvEndOffsetForFindFail, "Failed ftell() request, sequence number %ld, errno %ld.", pSeqNbr, errno, 0, 0);
                        errorText << "findOffsetToNextAsyncRequest(): Failed ftell() request, sequence number " << pSeqNbr \
                                  << (l_Retry ? ". Request will be retried." : "");
                        LOG_ERROR_TEXT_ERRNO(errorText, errno);
                        rc = -1;
                    }
                }
                else
                {
                    FL_Write(FLAsyncRqst, SeekEndFail, "Failed fseek() request for end of file, sequence number %ld, ferror %ld.", pSeqNbr, ferror(fd), 0, 0);
                    errorText << "findOffsetToNextAsyncRequest(): Failed fseek() request for end of file, sequence number " << pSeqNbr \
                              << (l_Retry ? ". Request will be retried." : "");
                    LOG_ERROR_TEXT_ERRNO(errorText, ferror(fd));
                    clearerr(fd);
                    rc = -1;
                }
            }
            else
            {
                errorText << "findOffsetToNextAsyncRequest(): Failed open request, sequence number " << pSeqNbr \
                             << (l_Retry ? ". Request will be retried." : "");
                LOG_ERROR_TEXT_ERRNO(errorText, errno);
                rc = -1;
            }
        }
        catch(exception& e)
        {
            LOG_ERROR_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e);
        }

        l_Maintenance = FORCE_REOPEN;
    }

  	becomeUser(l_Uid, l_Gid);

    return rc;
}

int WRKQMGR::findWork(const LVKey* pLVKey, WRKQE* &pWrkQE)
{
    int rc = 0;
    pWrkQE = 0;
    bool l_LogAsyncActivity = false;
    string l_NextHP_WorkCommand = "";

    if (pLVKey)
    {
        // Request to return a specific work queue
        rc = getWrkQE(pLVKey, pWrkQE);
    }
    else
    {
        // Next, determine if we need to look for work queues with canceled extents.
        // NOTE: This takes a higher priority than taking any work off the high priority
        //       work queue because in restart cases we want to clear these extents as
        //       quickly as possible.  If we first took work off the high priorty work
        //       queue, it is possible to consume all the transfer threads waiting for
        //       canceled extents to be removed (due to stop transfer) without any
        //       transfer threads available to actually perform the remove of the canceled
        //       extents, yielding deadlock.
        if (getCheckForCanceledExtents())
        {
            // Search for any LVKey work queue with a canceled extent at the front...
            getWrkQE_WithCanceledExtents(pWrkQE);
        }

        if (!pWrkQE)
        {
            // No work queue exists with canceled extents.
            // Next, check the high priority work queue...
            if (HPWrkQE->getWrkQ()->size())
            {
                if (getNumberOfConcurrentHPRequests() < getNumberOfAllowedConcurrentHPRequests())
                {
                    if (getNumberOfConcurrentCancelRequests() >= getNumberOfAllowedConcurrentCancelRequests())
                    {
                        // Maximum number of concurrent cancel/stoprequest operations already being processed
                        l_NextHP_WorkCommand = peekAtNextAsyncRequest(HPWrkQE->getWrkQ()->front());
                        if (l_NextHP_WorkCommand != "cancel" && l_NextHP_WorkCommand != "stoprequest")
                        {
                            // High priority work exists...  Pass the high priority queue back...
                            pWrkQE = HPWrkQE;
                            rc = 1;
                        }
                        else
                        {
                            // Next HP request is another cancel/stoprequest
                            l_LogAsyncActivity = true;
                        }
                    }
                    else
                    {
                        // High priority work exists...  Pass the high priority queue back...
                        pWrkQE = HPWrkQE;
                        rc = 1;
                    }
                }
                else
                {
                    // Maximum number of concurrent async requests already being processed
                    l_LogAsyncActivity = true;
                }
            }

            if (!pWrkQE)
            {
                // No high priority work exists that we want to schedule.
                // Find 'real' work on one of the LVKey work queues...
                rc = getWrkQE((LVKey*)0, pWrkQE);

                // If we did not find any work that we wanted to return, but
                // work exists on the HP work queue, return 1.
                // NOTE:  Even if pWrkQE is not set by getWrkQE(),
                //        if any work did exist, rc is set to 1.
                if ((rc != 1) && HPWrkQE->getWrkQ()->size())
                {
                    rc = 1;
                }
            }
        }
        else
        {
            // Work queue exists with canceled extents.
            // Return that work queue...
            rc = 1;
        }
    }

    if (l_LogAsyncActivity && g_LogAllAsyncRequestActivity)
    {
        LOG(bb,info) << "AsyncRequest -> findWork(): Skipping HP queue work. getNumberOfConcurrentHPRequests() = " << getNumberOfConcurrentHPRequests() \
                     << ", getNumberOfAllowedConcurrentHPRequests() = " << getNumberOfAllowedConcurrentHPRequests() \
                     << ", getNumberOfConcurrentCancelRequests() = " << getNumberOfConcurrentCancelRequests() \
                     << ", getNumberOfAllowedConcurrentCancelRequests() = " << getNumberOfAllowedConcurrentCancelRequests() \
                     << ", peekAtNextAsyncRequest(HPWrkQE->getWrkQ()->front()) = " << peekAtNextAsyncRequest(HPWrkQE->getWrkQ()->front()) \
                     << ", rc = " << rc;
    }

    return rc;
}

int WRKQMGR::getAsyncRequest(WorkID& pWorkItem, AsyncRequest& pRequest)
{
    int rc = 0;

    char l_Buffer[sizeof(AsyncRequest)+1] = {'\0'};

    uid_t l_Uid = getuid();
    gid_t l_Gid = getgid();
    MAINTENANCE_OPTION l_Maintenance = NO_MAINTENANCE;

  	becomeUser(0, 0);

    // Default is to open the currrent async request file
    int l_SeqNbr = asyncRequestFileSeqNbr;
    if (pWorkItem.getTag() >= offsetToNextAsyncRequest)
    {
        // We need to open the prior async request file...
        l_SeqNbr -= 1;
    }

    int l_Retry = DEFAULT_RETRY_VALUE;
    while (l_Retry--)
    {
        rc = 0;
        try
        {
            stringstream errorText;

            FILE* fd = openAsyncRequestFile("rb", l_SeqNbr, l_Maintenance);
            if (fd != NULL)
            {
                threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fseeksyscall, fd, __LINE__);
                rc = ::fseek(fd, pWorkItem.getTag(), SEEK_SET);
                threadLocalTrackSyscallPtr->clearTrack();
                FL_Write(FLAsyncRqst, Position, "Positioning async request file having seqnbr %ld to offset %ld, rc %ld.", l_SeqNbr, pWorkItem.getTag(), rc, 0);
                if (!rc)
                {
                    threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::freadsyscall, fd, __LINE__, sizeof(AsyncRequest), pWorkItem.getTag());
                    size_t l_Size = ::fread(l_Buffer, sizeof(char), sizeof(AsyncRequest), fd);
                    threadLocalTrackSyscallPtr->clearTrack();
                    FL_Write6(FLAsyncRqst, Read, "Read async request file having seqnbr %ld starting at offset %ld for %ld bytes. File pointer %p, %ld bytes read.", l_SeqNbr, pWorkItem.getTag(), sizeof(AsyncRequest), (uint64_t)(void*)fd, l_Size, 0);

                    if (l_Size == sizeof(AsyncRequest))
                    {
                        l_Retry = 0;
                        pRequest = AsyncRequest(l_Buffer, l_Buffer+AsyncRequest::MAX_HOSTNAME_LENGTH);
                    }
                    else
                    {
                        FL_Write6(FLAsyncRqst, ReadFail, "Failed fread() request, sequence number %ld, offset %ld. Read %ld bytes, expected %ld bytes, errno %ld.", l_SeqNbr, pWorkItem.getTag(), l_Size, sizeof(AsyncRequest), errno, 0);
                        errorText << "getAsyncRequest(): Failed fread() request, sequence number " << l_SeqNbr << " for offset " \
                                  << pWorkItem.getTag() << ". Read " << l_Size << " bytes, expected " << sizeof(AsyncRequest) << " bytes." \
                                  << (l_Retry ? " Request will be retried." : "");
                        LOG_ERROR_TEXT_ERRNO(errorText, errno);
                        rc = -1;
                    }
                }
                else
                {
                    FL_Write(FLAsyncRqst, PositionFail, "Failed fseek() request for end of file, sequence number %ld, offset %ld, errno %ld.", l_SeqNbr, pWorkItem.getTag(), ferror(fd), 0);
                    errorText << "getAsyncRequest(): Failed fseek() request, sequence number " << l_SeqNbr << " for offset " << pWorkItem.getTag() \
                              << (l_Retry ? ". Request will be retried." : "");
                    LOG_ERROR_TEXT_ERRNO(errorText, ferror(fd));
                    clearerr(fd);
                    rc = -1;
                }
            }
            else
            {
                errorText << "getAsyncRequest(): Failed open request, sequence number " << l_SeqNbr \
                          << (l_Retry ? ". Request will be retried." : "");
                LOG_ERROR_TEXT_ERRNO(errorText, errno);
                rc = -1;
            }
        }
        catch(exception& e)
        {
            LOG_ERROR_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e);
        }

        l_Maintenance = FORCE_REOPEN;
    }

  	becomeUser(l_Uid, l_Gid);

    if (!rc)
    {
        if (g_LogAllAsyncRequestActivity)
        {
            LOG(bb,info) << "AsyncRequest -> getAsyncRequest(): Offset 0x" << hex << uppercase << setfill('0') \
                         << pWorkItem.getTag() << setfill(' ') << nouppercase << dec \
                         << ", from hostname " << pRequest.getHostName() << " => " << pRequest.getData();
        }
        else
        {
            LOG(bb,debug) << "AsyncRequest -> getAsyncRequest(): Offset 0x" << hex << uppercase << setfill('0') \
                          << pWorkItem.getTag() << setfill(' ') << nouppercase << dec \
                          << ", from hostname " << pRequest.getHostName() << " => " << pRequest.getData();
        }
    }

    return rc;
}

uint64_t WRKQMGR::getDeclareServerDeadCount(const BBJob pJob, const uint64_t pHandle, const int32_t pContribId)
{
    // NOTE: If the server has been declared dead, then we return a count of 1.
    //       Those routines that invoke this method use this count as a loop control
    //       variable and we want to make sure that we go through the loop at least
    //       once.
    uint64_t l_Count = 1;
    if (!isServerDead(pJob, pHandle, pContribId))
    {
        l_Count = getDeclareServerDeadCount();
    }

    return l_Count;
};

size_t WRKQMGR::getNumberOfWorkQueues()
{
    size_t l_Size = 0;

    int l_TransferQueueUnlocked = unlockTransferQueueIfNeeded((LVKey*)0, "getNumberOfWorkQueues");
    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "getNumberOfWorkQueues", &l_LocalMetadataUnlockedInd);

    l_Size = wrkqs.size();

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "getNumberOfWorkQueues", &l_LocalMetadataUnlockedInd);
    }

    if (l_TransferQueueUnlocked)
    {
        lockTransferQueue((LVKey*)0, "getNumberOfWorkQueues");
    }

    return l_Size;
}

size_t WRKQMGR::getSizeOfAllWorkQueues()
{
    size_t l_TotalSize = 0;

    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "getSizeOfAllWorkQueues", &l_LocalMetadataUnlockedInd);

    for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); qe != wrkqs.end(); qe++)
    {
        l_TotalSize += qe->second->getWrkQ_Size();
    }

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "getSizeOfAllWorkQueues", &l_LocalMetadataUnlockedInd);
    }

    return l_TotalSize;
}

int WRKQMGR::getThrottleRate(LVKey* pLVKey, uint64_t& pRate)
{
    int rc = 0;
    pRate = 0;

    lockWorkQueueMgr(pLVKey, "getThrottleRate");

    std::map<LVKey,WRKQE*>::iterator it = wrkqs.find(*pLVKey);
    if (it != wrkqs.end())
    {
        pRate = it->second->getRate();
    }
    else
    {
        // NOTE: This may be tolerated...  Set rc to -2
        rc = -2;
    }

    unlockWorkQueueMgr(pLVKey, "getThrottleRate");

    return rc;
}

int WRKQMGR::getWrkQE(const LVKey* pLVKey, WRKQE* &pWrkQE)
{
    int rc = 0;

    pWrkQE = 0;
    // NOTE: In the 'normal' case, we many be looking for a non-existent work queue in either
    //       the null or non-null LVKey paths.
    //
    //       If a job is ended, the extents are removed from the appropriate work queue, but the counting
    //       semaphore is not reduced by the number of extents removed.  Therefore, we may be sent into
    //       this routine without any remaining work queue entries for a given work queue, for a workqueue that
    //       no longer exists, or where there are no existing work queues.
    //
    //       In these cases, pWrkQE is always returned as NULL.  If ANY work queue exists with at least
    //       one entry, rc is returned as 1.  If no entry exists on any work queue,
    //       (except for the high priority work queue), rc is returned as 0.
    //
    //       A 1 return code indicates that even if a work queue entry was not found, a repost should
    //       be performed to the semaphore.  A return code of 0 indicates that a repost is not necessary.
    //
    // NOTE: Suspended work queues are returned by this method.  The reason for this is that an entry can
    //       still be removed from a suspended work queue if the extent(s) exist for a canceled transfer definition.
    //       Thus, we must return suspended work queues from this method.

    int l_TransferQueueUnlocked = 0;
    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueMgrLocked = 0;

    if (!workQueueMgrIsLocked())
    {
        l_TransferQueueUnlocked = unlockTransferQueueIfNeeded(pLVKey, "getWrkQE");
        lockWorkQueueMgr(pLVKey, "getWrkQE", &l_LocalMetadataUnlockedInd);
        l_WorkQueueMgrLocked = 1;
    }

    if (pLVKey == NULL || (pLVKey->second).is_null())
    {
//        verify();
        int64_t l_BucketValue = -1;
        bool l_RecalculateLastQueueWithEntries = (lastQueueWithEntries == LVKey_Null ? true : false);
        // NOTE: The HP work queue is always present, so there must be at least two work queues...
        if (wrkqs.size() > 1)
        {
            // We have one or more workqueues...
            LVKey l_LVKey;
            WRKQE* l_WrkQE = 0;

            // NOTE: We do not lock each transfer queue as we search for the work queue
            //       to return below.  The biggest exposure is the call to getWrkQ_Size()
            //       below to get the current size() of the work queue.  Even if we get
            //       an 'unpredictable' result from the size() operation due to a concurrent
            //       update and return an empty work queue, our invoker is tolerant of the
            //       situation and handles it properly.
            bool l_SelectNext = false;
            bool l_FoundFirstPositiveWorkQueueInMap = false;
            bool l_EarlyExit = false;
            for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); ((qe != wrkqs.end()) && (!l_EarlyExit)); ++qe)
            {
                // The value for lastQueueWithEntries is only useful for the NEXT
                // invocation of this method if the work queue returned on THIS invocation
                // was the 'last queue with entries' in the map.  In that case, we immediately
                // return the next 'non-negative bucket' work queue on that next invocation,
                // as that completes the round-robin distribution of returned queues
                // back to the top of the map.
                //
                // Therefore, only if
                //   A) We 'encounter' the queue currently in lastQueueWithEntries
                //      during this search -or-
                //   B) We go through the entire map without seeing the queue
                //      currently in lastQueueWithEntries -or-
                //   C) The value for lastQueueWithEntries is not currently set and
                //      we return a work queue on this invocation
                //      *AND*
                //   D) If returning a work queue entry, it has a non-negative bucket value.
                // If the above it true, we will recalculate/reset the lastQueueWithEntries
                // value.
                //
                // NOTE: If we are returning a work queue with a negative bucket value, we
                //       do not recalculate the lastQueueWithEntries value because we will
                //       go into a delay returning that work queue and then all worker threads
                //       come back to this method as part of finding their next work item.
                //       When this method is then invoked, at least one of the work queues will
                //       have a positive bucket value and then 'normal' logic will determine
                //       if the lastQueueWithEntries value needs to be recalculated.
                //
                // Stated another way, if we didn't encounter the current lastQueueWithEntries
                // value during this search, it can't effect the work queue to be returned
                // on the next invocation of this method.  Thus, it doesn't need to be
                // recalculated at the end of the current invocation of this method.
                if (qe->first == lastQueueWithEntries)
                {
                    l_RecalculateLastQueueWithEntries = true;
                }

                bool l_Switch = false;
                if (qe->second != HPWrkQE)
                {
                    // Not the HP workqueue...
                    if (qe->second->getWrkQ_Size())
                    {
                        // This workqueue has at least one entry...
                        int64_t l_CurrentQueueBucketValue = qe->second->getBucket();
                        if (qe->second->workQueueIsAssignable())
                        {
                            if (l_WrkQE)
                            {
                                // We already have a workqueue to return.  Only switch to this
                                // workqueue if the current saved bucket value is not positive
                                // and this workqueue has a better 'bucket' value.
                                // NOTE: The 'switch' logic below is generally only for throttled
                                //       work queues.  Non-throttled work queues are simply
                                //       round-robined.
                                // NOTE: If all of the workqueues end up having a negative bucket
                                //       value, we return the workqueue with the least negative
                                //       bucket value.  We want to 'delay' the smallest amount
                                //       of time.
                                // NOTE: If we end up delaying due to throttling, all threads
                                //       will be delaying on the same workqueue.  However, when the
                                //       delay time has expired, all of the threads will again return
                                //       to this routine to get the 'next' workqueue.
                                //       Returning to get the 'next' workqueue after a throttle delay
                                //       prevents a huge I/O spike for an individual workqueue.
                                if (l_BucketValue <= 0 && l_BucketValue < l_CurrentQueueBucketValue)
                                {
                                    l_Switch = true;
                                }
                            }
                            else
                            {
                                // We don't have a workqueue to return.
                                // If we can't find a 'next' WRKQE to return,
                                // we will return this one unless we find a better one...
                                l_Switch = true;
                            }

                            if (l_Switch)
                            {
                                // This is a better workqueue to return if we
                                // can't find a 'next' WRKQE...
                                l_LVKey = qe->first;
                                l_WrkQE = qe->second;
                                l_BucketValue = l_CurrentQueueBucketValue;

                                if (!l_FoundFirstPositiveWorkQueueInMap)
                                {
                                    if (l_BucketValue >= 0)
                                    {
                                        if (lastQueueProcessed == lastQueueWithEntries)
                                        {
                                            // Return this queue now as it is the next in round robin order
                                            l_EarlyExit = true;
//                                            LOG(bb,info) << "WRKQMGR::getWrkQE(): Early exit, top of map";
                                        }
                                        l_FoundFirstPositiveWorkQueueInMap = true;
                                    }
                                }
                            }

                            // Return this queue now as it is the next in round robin order
                            if (l_SelectNext && l_CurrentQueueBucketValue >= 0)
                            {
                                // Switch to this workqueue and return it...
                                l_LVKey = qe->first;
                                l_WrkQE = qe->second;
                                l_BucketValue = l_CurrentQueueBucketValue;
                                l_EarlyExit = true;
//                                LOG(bb,info) << "WRKQMGR::getWrkQE(): Early exit, next after last returned";
                            }
                        }
                        rc = 1;
                    }

                    if ((!l_SelectNext) && qe->first == lastQueueProcessed)
                    {
                        // Just found the entry we last returned.  Return the next workqueue that has a positive bucket value...
//                        LOG(bb,info) << "WRKQMGR::getWrkQE(): l_SelectNext = true";
                        l_SelectNext = true;
                    }
                }
            }

            if (l_WrkQE)
            {
                // NOTE: We don't update the last queue processed here because our invoker may choose to not take action on our returned
                //       data.  The last queue processed is updated just before an item of work is removed from a queue.
                pWrkQE = l_WrkQE;
                if ((!l_RecalculateLastQueueWithEntries) && (!l_EarlyExit))
                {
                    l_RecalculateLastQueueWithEntries = true;
                }
            }
            else
            {
                // WRKQMGR::getWrkQE(): No extents left on any workqueue
                LOG(bb,debug) << "WRKQMGR::getWrkQE(): No extents left on any workqueue";
            }
        }
        else
        {
            // WRKQMGR::getWrkQE(): No workqueue entries exist
            LOG(bb,debug) << "WRKQMGR::getWrkQE(): No workqueue entries exist";
        }

        if (rc == 1)
        {
            if (pWrkQE)
            {
                if (l_RecalculateLastQueueWithEntries && (l_BucketValue >= 0))
                {
                    calcLastWorkQueueWithEntries();
                }
#if 0
                if (l_BucketValue >= 0)
                {
                    dump("info", " Work Queue Mgr (Work queue entry returned with non-negative bucket)", DUMP_ALWAYS);
                }
#endif
            }
            else
            {
                // Update the last work with entries as NONE
                setLastQueueWithEntries(LVKey_Null);
            }
        }
        else
        {
            // Update the last work with entries as NONE
            setLastQueueWithEntries(LVKey_Null);
        }
    }
    else
    {
        std::map<LVKey,WRKQE*>::iterator it = wrkqs.find(*pLVKey);
        if (it != wrkqs.end())
        {
            // NOTE: In this path since a specific LVKey was passed, we do not interrogate the
            //       rate/bucket values.  We simply return the work queue associated with the LVKey.
            pWrkQE = it->second;
            rc = 1;
        }
        else
        {
            // WRKQMGR::getWrkQE(): Workqueue no longer exists
            //
            // NOTE: rc is returned as a zero in this case.  We are not concerned about
            //       other work queues in this path.  We return 0 if the requested work
            //       queue cannot be found.
            LOG(bb,info) << "WRKQMGR::getWrkQE(): Workqueue for " << *pLVKey << " no longer exists";
            dump("info", " Work Queue Mgr (Specific workqueue not found)", DUMP_ALWAYS);
        }
    }

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr(pLVKey, "getWrkQE", &l_LocalMetadataUnlockedInd);
    }

    if (l_TransferQueueUnlocked)
    {
        lockTransferQueue(pLVKey, "getWrkQE");
    }

    return rc;
}

void WRKQMGR::getWrkQE_WithCanceledExtents(WRKQE* &pWrkQE)
{
    LVKey l_Key;
    BBLV_Info* l_LV_Info = 0;

    pWrkQE = 0;
    if (wrkqs.size() > 1)
    {
        bool l_SelectNext = false;
        bool l_RecalculateLastQueueWithEntries = (lastQueueWithEntries == LVKey_Null ? true : false);
        for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); qe != wrkqs.end(); ++qe)
        {
            // For each of the work queues...
            if (qe->first == lastQueueWithEntries)
            {
                l_RecalculateLastQueueWithEntries = true;
            }

            if (qe->second != HPWrkQE)
            {
                // Not the HP workqueue...
                if (qe->second->wrkq->size())
                {
                    // This workqueue has at least one entry.
                    //
                    // Now, peek at the next extent to be transferred.  This work queue
                    // can be returned if the next extent is marked as canceled or if there
                    // are no additional extents remaining.
                    // NOTE:  If no extents remain, then treat the workqueue as if it has
                    //        a canceled extent.  Cancel extent processing only keep the
                    //        first and last extent for a file in allExtents to be processed
                    //        so that the appropriate metadata is updated.  All other canceled
                    //        extents are simply removed.  Therefore, all work items for the
                    //        non-first and non-last extents remain enqueued.  We want to
                    //        prioritize the removal of these enqueued work items, so we
                    //        treat them as canceled.
                    l_LV_Info = qe->second->getLV_Info();
                    if (l_LV_Info && ((!l_LV_Info->getNumberOfExtents()) || l_LV_Info->hasCanceledExtents()))
                    {
                        // Next extent to be transferred is canceled...
                        pWrkQE = qe->second;
                        pWrkQE->dump("debug", "getWrkQE_WithCanceledExtents(): Extent being cancelled ");
                        if (l_SelectNext || (lastQueueProcessed == lastQueueWithEntries))
                        {
                            // Return this queue now as it is the next in round robin order
                            break;
                        }
                    }

                    if ((!l_SelectNext) && qe->first == lastQueueProcessed)
                    {
                        // Just found the entry we last returned.  Return the next workqueue that has a canceled extent...
//                        LOG(bb,info) << "WRKQMGR::getWrkQE_WithCanceledExtents(): l_SelectNext = true";
                        l_SelectNext = true;
                    }
                }
            }
        }

        if (l_RecalculateLastQueueWithEntries && pWrkQE)
        {
            calcLastWorkQueueWithEntries();
        }
    }

    if (!pWrkQE)
    {
        // Indicate that we no longer need to look for canceled extents
        setCheckForCanceledExtents(0);
    }

    return;
}

void WRKQMGR::incrementNumberOfWorkItemsProcessed(WRKQE* pWrkQE, const WorkID& pWorkItem)
{
    // NOTE: The work item object contains an BBLV_Info*.  However, if this is/was the
    //       last item on the work queue, keep in mind that 'early' local metadata
    //       cleanup may yield this pointer invalid.  When passed to this method,
    //       only the HP work queue path can use the pWorkItem object.
    if (pWrkQE != HPWrkQE)
    {
        // Not the high priority work queue.
        // Simply, increment the number of work items processed.
        pWrkQE->incrementNumberOfWorkItemsProcessed();
    }
    else
    {
        // For the high priority work queue, we have to manage
        // how work items are recorded as being complete.
        manageWorkItemsProcessed(pWorkItem);
    }

    return;
};

int WRKQMGR::isServerDead(const BBJob pJob, const uint64_t pHandle, const int32_t pContribId)
{
    int rc = 0;

    HeartbeatEntry* l_HeartbeatEntry = getHeartbeatEntry(ContribIdFile::isServicedBy(pJob, pHandle, pContribId));
    if (l_HeartbeatEntry)
    {
        rc = l_HeartbeatEntry->serverDeclaredDead(declareServerDeadCount);
    }

    return rc;
}

HeartbeatEntry* WRKQMGR::getHeartbeatEntry(const string& pHostName)
{
    HPWrkQE->lock((LVKey*)0, "WRKQMGR::getHeartbeatEntry");

    HeartbeatEntry* l_HeartbeatEntry = (HeartbeatEntry*)0;
    for (auto it=heartbeatData.begin(); it!=heartbeatData.end() && (!l_HeartbeatEntry); ++it)
    {
        if (it->first == pHostName)
        {
            l_HeartbeatEntry = &(it->second);
        }
    }

    HPWrkQE->unlock((LVKey*)0, "WRKQMGR::getHeartbeatEntry");

    return l_HeartbeatEntry;
}

void WRKQMGR::loadBuckets()
{
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "loadBuckets");

    for (map<LVKey,WRKQE*>::iterator qe = wrkqs.begin(); qe != wrkqs.end(); ++qe)
    {
        if (qe->second != HPWrkQE)
        {
            qe->second->loadBucket();
        }
    }

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "loadBuckets");
    }

    return;
}

// NOTE: pLVKey is not currently used, but can come in as null.
void WRKQMGR::lockWorkQueueMgr(const LVKey* pLVKey, const char* pMethod, int* pLocalMetadataUnlockedInd)
{
    stringstream errorText;

    if (pLocalMetadataUnlockedInd)
    {
        *pLocalMetadataUnlockedInd = 0;
    }

    if (!workQueueMgrIsLocked())
    {
        // Verify lock protocol
        if (HPWrkQE)
        {
            if (HPWrkQE->transferQueueIsLocked())
            {
                FL_Write(FLError, lockPV_TQLock2, "WRKQMGR::lockWorkQueueMgr: Work queue mgr lock being obtained while the HP transfer queue lock is held",0,0,0,0);
                errorText << "WRKQMGR::lock: Work queue manager lock being obtained while the HP transfer queue lock is held";
                LOG_ERROR_TEXT_AND_RAS(errorText, bb.internal.lockprotocol.lockwqm2)
                endOnError();
            }
        }
        if (CurrentWrkQE)
        {
            if (CurrentWrkQE->transferQueueIsLocked())
            {
                FL_Write(FLError, lockPV_TQLock3, "WRKQMGR::lockWorkQueueMgr: Work queue mgr lock being obtained while the transfer queue lock is held",0,0,0,0);
                errorText << "WRKQMGR::lock: Work queue manager lock being obtained while the transfer queue lock is held";
                LOG_ERROR_TEXT_AND_RAS(errorText, bb.internal.lockprotocol.lockwqm3)
                endOnError();
            }
        }
        if (localMetadataIsLocked())
        {
            if (!pLocalMetadataUnlockedInd)
            {
                FL_Write(FLError, lockPV_MDLock3, "WRKQMGR::lockWorkQueueMgr: Work queue mgr lock being obtained while the local metadata lock is held",0,0,0,0);
                errorText << "WRKQMGR::lock: Work queue manager lock being obtained while the local metadata lock is held";
                LOG_ERROR_TEXT_AND_RAS(errorText, bb.internal.lockprotocol.lockwqm3)
                endOnError();
            }
            else
            {
                unlockLocalMetadata(pLVKey, "lockWorkQueueMgr");
                *pLocalMetadataUnlockedInd = 1;
            }
        }

        pthread_mutex_lock(&lock_workQueueMgr);
        workQueueMgrLocked = pthread_self();

        if (strstr(pMethod, "%") == NULL)
        {
            if (g_LockDebugLevel == "info")
            {
                if (pLVKey)
                {
                    LOG(bb,info) << " WQ_MGR:   LOCK <- " << pMethod << ", " << *pLVKey;
                }
                else
                {
                    LOG(bb,info) << " WQ_MGR:   LOCK <- " << pMethod << ", unknown LVKey";
                }
            }
            else
            {
                if (pLVKey)
                {
                    LOG(bb,debug) << " WQ_MGR:   LOCK <- " << pMethod << ", " << *pLVKey;
                }
                else
                {
                    LOG(bb,debug) << " WQ_MGR:   LOCK <- " << pMethod << ", unknown LVKey";
                }
            }
        }

        pid_t tid = syscall(SYS_gettid);  // \todo eventually remove this.  incurs syscall for each log entry
        FL_Write(FLMutex, lockWrkQMgr, "lockWorkQueueMgr.  threadid=%ld",tid,0,0,0);
    }
    else
    {
        FL_Write(FLError, lockWrkQMgrERROR, "lockWorkQueueMgr called when lock already owned by thread",0,0,0,0);
        flightlog_Backtrace(__LINE__);
        // For now, also to the console...
        LOG(bb,error) << " WQ_MGR: Request made to lock the work queue manager by " << pMethod << ", but the lock is already owned.";
        logBacktrace();
    }

    return;
}

int WRKQMGR::lockWorkQueueMgrIfNeeded(const LVKey* pLVKey, const char* pMethod, int* pLocalMetadataUnlockedInd)
{
    ENTRY(__FILE__,__FUNCTION__);

    int rc = 0;
    if (!workQueueMgrIsLocked())
    {
        lockWorkQueueMgr(pLVKey, pMethod, pLocalMetadataUnlockedInd);
        rc = 1;
    }

    EXIT(__FILE__,__FUNCTION__);
    return rc;
}

void WRKQMGR::manageWorkItemsProcessed(const WorkID& pWorkItem)
{
    // NOTE: This must be for the HPWrkQ.
    // NOTE: The ordering for processing of async requests is NOT guaranteed.
    //       This is because high priority work items are dispatched to multiple
    //       threads and there is no guarantee as to the order of their finish.
    //       Command requests can ensure that all prior high priority requests are
    //       completed before processing for their request starts by invoking the
    //       processAllOutstandingHP_Requests() method.
    //       Therefore, the purpose of this routine is to only record that a given
    //       work item (high priorty request) is complete if all prior work items
    //       are also complete. This processing enforces the promise given by the
    //       processAllOutstandingHP_Requests() method.
    // NOTE: The processing below only works if we assume that the number of
    //       outstanding out of order async requests does not exceed the number
    //       of async requests that can be contained within a given async request
    //       file.  Otherwise, we would have a duplicate offset in the outOfOrderOffsets
    //       vector.
    //
    // Determine the next offset to be marked as complete.
    // Prime the values passed to calcNextOffsetToProcess() with
    // the sequence number/offset of the last request processed.
    // calcNextOffsetToProcess() will return the sequence number/offset
    // to process next.
    int l_SeqNbr = asyncRequestFileSeqNbrLastProcessed;
    uint64_t l_TargetOffset = lastOffsetProcessed;
    calcNextOffsetToProcess(l_SeqNbr, l_TargetOffset);

    if (l_TargetOffset == pWorkItem.getTag())
    {
        // The work item just finished is for the next offset
        incrementNumberOfHP_WorkItemsProcessed(l_TargetOffset);

        // Now see if any work items that came out of order should also be marked as complete
        bool l_AllDone = false;
        while (!l_AllDone)
        {
            l_AllDone = true;
            if (outOfOrderOffsets.size())
            {
                calcNextOffsetToProcess(l_SeqNbr, l_TargetOffset);
                if (g_LogAllAsyncRequestActivity)
                {
                    LOG(bb,info) << "AsyncRequest -> manageWorkItemsProcessed(): TargetOffset 0x" << hex << uppercase << setfill('0') << setw(8) << l_TargetOffset << setfill(' ') << nouppercase << dec;
                }
                else
                {
                    LOG(bb,debug) << "AsyncRequest -> manageWorkItemsProcessed(): TargetOffset 0x" << hex << uppercase << setfill('0') << setw(8) << l_TargetOffset << setfill(' ') << nouppercase << dec;
                }

                for (auto it=outOfOrderOffsets.begin(); it!=outOfOrderOffsets.end(); ++it) {
                    if (*it == l_TargetOffset)
                    {
                        l_AllDone = false;
                        outOfOrderOffsets.erase(it);
                        incrementNumberOfHP_WorkItemsProcessed(l_TargetOffset);
                        LOG(bb,info) << "AsyncRequest -> manageWorkItemsProcessed(): Offset 0x" << hex << uppercase << setfill('0') << l_TargetOffset << setfill(' ') << nouppercase << dec << " removed from the outOfOrderOffsets vector";
                        break;
                    }
                }

                if (l_AllDone && g_LogAllAsyncRequestActivity)
                {
                    LOG(bb,info) << "AsyncRequest -> Offset 0x" << hex << uppercase << setfill('0') << l_TargetOffset << setfill(' ') << nouppercase << dec << " not found in the outOfOrderOffsets vector";
                }
            }
        }

        // Set the last processed sequence number
        asyncRequestFileSeqNbrLastProcessed = l_SeqNbr;
    }
    else
    {
        // Work item completed out of order...  Keep it for later processing...
        outOfOrderOffsets.push_back(pWorkItem.getTag());
        LOG(bb,info) << "AsyncRequest -> manageWorkItemsProcessed(): Offset 0x" << hex << uppercase << setfill('0') << pWorkItem.getTag() << setfill(' ') << nouppercase << dec << " pushed onto the outOfOrderOffsets vector";
    }

    return;
}

FILE* WRKQMGR::openAsyncRequestFile(const char* pOpenOption, int &pSeqNbr, const MAINTENANCE_OPTION pMaintenanceOption)
{
    FILE* l_FilePtr = 0;
    char* l_AsyncRequestFileNamePtr = 0;
    stringstream errorText;

    int rc = verifyAsyncRequestFile(l_AsyncRequestFileNamePtr, pSeqNbr, pMaintenanceOption);
    if ((!rc) && l_AsyncRequestFileNamePtr)
    {
        bool l_AllDone = false;
        while (!l_AllDone)
        {
            l_AllDone = true;
            if (pOpenOption[0] != 'a')
            {
                if (pMaintenanceOption != FORCE_REOPEN && pSeqNbr == asyncRequestFile_ReadSeqNbr)
                {
                    l_FilePtr = asyncRequestFile_Read;
                }
            }

            if (l_FilePtr == NULL)
            {
                threadLocalTrackSyscallPtr->nowTrack(TrackSyscall::fopensyscall, l_AsyncRequestFileNamePtr, __LINE__);
                l_FilePtr = ::fopen(l_AsyncRequestFileNamePtr, pOpenOption);
                threadLocalTrackSyscallPtr->clearTrack();
                if (pOpenOption[0] != 'a')
                {
                    if (l_FilePtr != NULL)
                    {
                        setbuf(l_FilePtr, NULL);
                        FL_Write(FLAsyncRqst, OpenRead, "Open async request file having seqnbr %ld using mode 'rb', maintenance option %ld. File pointer returned is %p.", pSeqNbr, pMaintenanceOption, (uint64_t)(void*)l_FilePtr, 0);
                        // NOTE:  All closes for fds opened for read are done via the cached
                        //        fd that has been stashed in asyncRequestFile_Read.
                        if (asyncRequestFile_Read != NULL)
                        {
                            LOG(bb,debug) << "openAsyncRequestFile(): Close cached file for read";
                            FL_Write(FLAsyncRqst, CloseForRead, "Close async request file having seqnbr %ld using mode 'rb', maintenance option %ld. File pointer is %p.", pSeqNbr, (uint64_t)(void*)asyncRequestFile_Read, pMaintenanceOption, 0);
                            ::fclose(asyncRequestFile_Read);
                            asyncRequestFile_Read = NULL;
                        }
                        LOG(bb,debug) << "openAsyncRequestFile(): Cache open for read, seqnbr " << pSeqNbr;
                        asyncRequestFile_ReadSeqNbr = pSeqNbr;
                        asyncRequestFile_Read = l_FilePtr;
                    }
                    else
                    {
                        FL_Write(FLAsyncRqst, OpenReadFailed, "Open async request file having seqnbr %ld using mode 'rb', maintenance option %ld failed, errno %ld.", pSeqNbr, pMaintenanceOption, errno, 0);
                        errorText << "Open async request file having seqnbr " << pSeqNbr << " using mode 'rb', maintenance option " << pMaintenanceOption << " failed.";
                        LOG_ERROR_TEXT_ERRNO(errorText, errno);
                    }
                }
                else
                {
                    if (l_FilePtr == NULL)
                    {
                        FL_Write(FLAsyncRqst, OpenAppendFailed, "Open async request file having seqnbr %ld using mode 'ab', maintenance option %ld failed.", pSeqNbr, pMaintenanceOption, 0, 0);
                        errorText << "Open async request file having seqnbr " << pSeqNbr << " using mode 'ab', maintenance option " << pMaintenanceOption << " failed.";
                        LOG_ERROR_TEXT_ERRNO(errorText, errno);
                    }
                }
            }
        }
    }

    if (l_AsyncRequestFileNamePtr)
    {
        delete [] l_AsyncRequestFileNamePtr;
        l_AsyncRequestFileNamePtr = 0;
    }

    return l_FilePtr;
}

string WRKQMGR::peekAtNextAsyncRequest(WorkID& pWorkItem)
{
    ENTRY(__FILE__,__FUNCTION__);

    int rc = 0;
    char l_Cmd[AsyncRequest::MAX_DATA_LENGTH] = {'\0'};

    AsyncRequest l_Request = AsyncRequest();
    rc = wrkqmgr.getAsyncRequest(pWorkItem, l_Request);

    if (!rc)
    {
        if (!l_Request.sameHostName())
        {
            // Peek the request
            char l_Str1[64] = {'\0'};
            char l_Str2[64] = {'\0'};
            uint64_t l_JobId = UNDEFINED_JOBID;
            uint64_t l_JobStepId = UNDEFINED_JOBSTEPID;
            uint64_t l_Handle = UNDEFINED_HANDLE;
            uint32_t l_ContribId = UNDEFINED_CONTRIBID;
            uint64_t l_CancelScope = 0;

            rc = sscanf(l_Request.getData(), "%s %lu %lu %lu %u %lu %s %s", l_Cmd, &l_JobId, &l_JobStepId, &l_Handle, &l_ContribId, &l_CancelScope, l_Str1, l_Str2);
            if (rc != 8)
            {
                // Failure when attempting to parse the request...  Log it and continue...
                LOG(bb,error) << "peekAtNextAsyncRequest: Failure when attempting to process async request from hostname " << l_Request.getHostName() << ", number of successfully parsed items " << rc << " => " << l_Request.getData();
            }
        }
    }
    else
    {
        // Error when retrieving the request
        LOG(bb,error) << "peekAtNextAsyncRequest: Error when attempting to retrieve the async request at offset " << pWorkItem.getTag();
    }

    EXIT(__FILE__,__FUNCTION__);
    return string(l_Cmd);
}

void WRKQMGR::post()
{
    // NOTE: It is a requirement that the invoker must have first
    //       unlocked the transfer queue.
    int l_HP_TransferQueueUnlocked = 0;
    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueLocked = 0;

    if (!workQueueMgrIsLocked())
    {
        if (HPWrkQE->transferQueueIsLocked())
        {
            HPWrkQE->unlock((LVKey*)0, "post - before");
            l_HP_TransferQueueUnlocked = 1;
        }
        l_WorkQueueLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "post - before", &l_LocalMetadataUnlockedInd);
    }

    sem_post(&sem_workqueue);

    if (l_WorkQueueLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "post - after", &l_LocalMetadataUnlockedInd);
    }
    if (l_HP_TransferQueueUnlocked)
    {
        HPWrkQE->lock((LVKey*)0, "post - after");
    }

    return;
}

void WRKQMGR::post_multiple(const size_t pCount)
{
    int l_TransferQueueUnlocked = unlockTransferQueueIfNeeded((LVKey*)0, "post_multiple");
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "post_multiple");

    for (size_t i=0; i<pCount; i++)
    {
        post();
    }
//    verify();

    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "post_multiple");
    }
    if (l_TransferQueueUnlocked)
    {
        lockTransferQueue((LVKey*)0, "post_multiple");
    }

    return;
}

void WRKQMGR::processAllOutstandingHP_Requests(const LVKey* pLVKey)
{
    uint32_t i = 0;
    bool l_AllDone= false;

    int l_LocalMetadataUnlockedInd = 0;
    lockWorkQueueMgr(pLVKey, "processAllOutstandingHP_Requests", &l_LocalMetadataUnlockedInd);

    HPWrkQE->lock(pLVKey, "processAllOutstandingHP_Requests");

    // First, check for any new appended HP work queue items...
    uint64_t l_NumberToProcess = checkForNewHPWorkItems();
    HPWrkQE->dump("info", "processAllOutstandingHP_Requests(): ");

    // Now, process all enqueued high priority work items...
    // NOTE: We assume it is not possible for so many async requests to be appended (and continue to be appended...)
    //       that we never process them all...
    while (!l_AllDone)
    {
        if (HPWrkQE->getNumberOfWorkItemsProcessed() >= l_NumberToProcess)
        {
            if (i >= 10)
            {
                LOG(bb,info) << "processAllOutstandingHP_Requests(): Completed -> HPWrkQE->getNumberOfWorkItemsProcessed() = " << HPWrkQE->getNumberOfWorkItemsProcessed() << ", l_NumberToProcess = " << l_NumberToProcess;
            }
            l_AllDone = true;
        }
        else
        {
            HPWrkQE->unlock(pLVKey, "processAllOutstandingHP_Requests - in delay");
            unlockWorkQueueMgr(pLVKey, "processAllOutstandingHP_Requests - in delay");
            {
                // NOTE: Currently set to log after 5 seconds of not being able to process all async requests, and every 10 seconds thereafter...
                if ((i++ % 20) == 10)
                {
                    FL_Write(FLDelay, OutstandingHP_Requests, "Processing all outstanding async requests. %ld of %ld work items processed.", (uint64_t)HPWrkQE->getNumberOfWorkItemsProcessed(), (uint64_t)l_NumberToProcess, 0, 0);
                    LOG(bb,info) << ">>>>> DELAY <<<<< processAllOutstandingHP_Requests(): HPWrkQE->getNumberOfWorkItemsProcessed() " << HPWrkQE->getNumberOfWorkItemsProcessed() \
                                 << ", l_NumberToProcess " << l_NumberToProcess << ", Last Processed Async Seq# " << asyncRequestFileSeqNbrLastProcessed << ", Async Seq# " << asyncRequestFileSeqNbr << ", LstOff 0x" \
                                 << hex << uppercase << setfill('0') << setw(8) << lastOffsetProcessed << ", NxtOff 0x" << setw(8) << offsetToNextAsyncRequest << setfill(' ') << nouppercase << dec \
                                 << "  #OutOfOrd " << outOfOrderOffsets.size();
                }
                usleep((useconds_t)500000);
            }
            lockWorkQueueMgr(pLVKey, "processAllOutstandingHP_Requests - in delay");
            HPWrkQE->lock(pLVKey, "processAllOutstandingHP_Requests - in delay");
        }
    }

    HPWrkQE->unlock(pLVKey, "processAllOutstandingHP_Requests");

    unlockWorkQueueMgr(pLVKey, "processAllOutstandingHP_Requests", &l_LocalMetadataUnlockedInd);

    return;
}

void WRKQMGR::processThrottle(LVKey* pLVKey, WRKQE* pWrkQE, BBLV_Info* pLV_Info, BBTagID& pTagId, ExtentInfo& pExtentInfo, Extent* pExtent, double& pThreadDelay, double& pTotalDelay)
{
    pThreadDelay = 0;
    pTotalDelay = 0;

    if (inThrottleMode())
    {
        if(pWrkQE)
        {
            pThreadDelay = pWrkQE->processBucket(pTagId, pExtentInfo);
            pTotalDelay = (pThreadDelay ? ((double)(g_ThrottleBucket_Controller.getTimerPoppedCount()-g_ThrottleBucket_Controller.getCount()-1) * (Throttle_TimeInterval*1000000)) + pThreadDelay : 0);
        }
    }

    return;
}

void WRKQMGR::processTurboFactorForFoundRequest()
{
    if (useAsyncRequestReadTurboMode)
    {
        if (round((double)g_RemoteAsyncRequest_Controller.getTimerPoppedCount() * (asyncRequestReadTurboFactor * DEFAULT_TURBO_FACTOR)) >= 1)
        {
            LOG(bb,debug) << "processTurboFactorForFoundRequest(): Increase turbo factor from " << asyncRequestReadTurboFactor << " to " << asyncRequestReadTurboFactor * DEFAULT_TURBO_FACTOR;
            asyncRequestReadTurboFactor *= DEFAULT_TURBO_FACTOR;
        }
        else
        {
            LOG(bb,debug) << "processTurboFactorForFoundRequest(): Floor reached. No change in turbo factor, current asyncRequestReadTurboFactor=" << asyncRequestReadTurboFactor;
        }
        asyncRequestReadConsecutiveNoNewRequests = 0;
    }

    return;
}

void WRKQMGR::processTurboFactorForNotFoundRequest()
{
    if (useAsyncRequestReadTurboMode)
    {
        if ((++asyncRequestReadConsecutiveNoNewRequests % DEFAULT_TURBO_CLIP_VALUE) == 0)
        {
            if (round((double)g_RemoteAsyncRequest_Controller.getTimerPoppedCount() * (asyncRequestReadTurboFactor / DEFAULT_TURBO_FACTOR)) <= round(AsyncRequestRead_TimeInterval / Throttle_TimeInterval))
            {
                LOG(bb,debug) << "processTurboFactorForNotFoundRequest(): Decrease turbo factor from " << asyncRequestReadTurboFactor << " to " << asyncRequestReadTurboFactor / DEFAULT_TURBO_FACTOR;
                asyncRequestReadTurboFactor /= DEFAULT_TURBO_FACTOR;
                asyncRequestReadConsecutiveNoNewRequests = 0;
            }
            else
            {
                LOG(bb,debug) << "processTurboFactorForNotFoundRequest(): Ceiling reached. No change in turbo factor, asyncRequestReadTurboFactor=" << asyncRequestReadTurboFactor << ", asyncRequestReadConsecutiveNoNewRequests=" << asyncRequestReadConsecutiveNoNewRequests;
            }
        }
        else
        {
            LOG(bb,debug) << "processTurboFactorForNotFoundRequest(): Clip value not reached. No change in turbo factor, asyncRequestReadTurboFactor=" << asyncRequestReadTurboFactor << ", asyncRequestReadConsecutiveNoNewRequests=" << asyncRequestReadConsecutiveNoNewRequests;
        }
    }

    return;
}

void WRKQMGR::removeWorkItem(WRKQE* pWrkQE, WorkID& pWorkItem, bool& pLastWorkItemRemoved)
{
    if (pWrkQE)
    {
        // Perform any dump operations...
        if (pWrkQE->getDumpOnRemoveWorkItem())
        {
            // NOTE: Only dump out the found work if for the high priority work queue -or-
            //       this is the last entry in the queue -or- there this a multiple of the number of entries interval
            queue<WorkID>* l_WrkQ = pWrkQE->getWrkQ();
            if ((l_WrkQ == HPWrkQE->getWrkQ()) || (pWrkQE->getWrkQ_Size() == 1) ||
                (getDumpOnRemoveWorkItemInterval() && getNumberOfWorkQueueItemsProcessed() % getDumpOnRemoveWorkItemInterval() == 0))
            {
                pWrkQE->dump("info", "Start: Current work item -> ");
                if (dumpOnRemoveWorkItem && (l_WrkQ != HPWrkQE->getWrkQ()))
                {
                    dump("info", " Work Queue Mgr (Not an error - Count Interval)", DUMP_ALWAYS);
                }
            }
            else
            {
                pWrkQE->dump("debug", "Start: Current work item -> ");
                if (dumpOnRemoveWorkItem && (pWrkQE != HPWrkQE))
                {
                    dump("debug", " Work Queue Mgr (Debug)", DUMP_ALWAYS);
                }
            }
        }

        // Remove the work item from the work queue
        pWrkQE->removeWorkItem(pWorkItem, DO_NOT_VALIDATE_WORK_QUEUE, pLastWorkItemRemoved);

        // Update the last processed work queue in the manager
        setLastQueueProcessed(pWrkQE->getLVKey());

        // If work item is not for the HP work queue, increment number of work items processed.
        // NOTE: We don't want to trigger the timer interval dump based solely on async requests.
        if (pWrkQE != HPWrkQE)
        {
            incrementNumberOfWorkItemsProcessed();
        }
    }

    return;
}

int WRKQMGR::rmvWrkQ(const LVKey* pLVKey)
{
    int rc = 0;

    stringstream l_Prefix;
    l_Prefix << " - rmvWrkQ() before removing" << *pLVKey;
    dump("debug", l_Prefix.str().c_str(), DUMP_UNCONDITIONALLY);

    unlockTransferQueueIfNeeded(pLVKey, "rmvWrkQ");
    int l_LocalMetadataUnlocked = unlockLocalMetadataIfNeeded(pLVKey, "rmvWrkQ");

    // NOTE: This mutex serializes with the BBLocalAsync BBCleanup methods
    pthread_mutex_lock(&lock_on_rmvWrkQ);
    lockWorkQueueMgrIfNeeded(pLVKey, "rmvWrkQ");

    try
    {
        std::map<LVKey,WRKQE*>::iterator it = wrkqs.find(*pLVKey);
        if (it != wrkqs.end())
        {
            // Remove the work queue from the map
            WRKQE* l_WrkQE = it->second;
            wrkqs.erase(it);

            // If the work queue being removed is currently
            // identified as the last work queue in the map
            // with work items (not likely...), then reset
            // lastQueueWithEntries so it is recalculated
            // next time we try to findWork().
            if (*pLVKey == lastQueueWithEntries)
            {
                lastQueueWithEntries = LVKey_Null;
            }

            if (l_WrkQE)
            {
                // Delete the work queue entry
                if (l_WrkQE->getRate())
                {
                    // Removing a work queue that had a transfer rate.
                    // Re-calculate the indication of throttle mode...
                    calcThrottleMode();
                }
                if(CurrentWrkQE == l_WrkQE)
                {
                    CurrentWrkQE = (WRKQE*)0;
                }
                delete l_WrkQE;
            }

            l_Prefix << " - rmvWrkQ() after removing " << *pLVKey;
            dump("debug", l_Prefix.str().c_str(), DUMP_UNCONDITIONALLY);
        }
        else
        {
            // NOTE: Tolerate the condition...
        }
    }
    catch(ExceptionBailout& e) { }
    catch(exception& e)
    {
        LOG_ERROR_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e);
    }

    // NOTE: The rmvWrkQ lock cannot be held when attempting to
    //       obtain the local metadata lock
    unlockWorkQueueMgr(pLVKey, "rmvWrkQ");
    pthread_mutex_unlock(&lock_on_rmvWrkQ);
    if (l_LocalMetadataUnlocked)
    {
        l_LocalMetadataUnlocked = 0;
        lockLocalMetadata(pLVKey, "rmvWrkQ");
    }

    // NOTE: If no error, we deleted the work queue, so no need to
    //       re-acquire the transfer queue lock.  Even if we had an error,
    //       we do not re-acquire the transfer queue lock because there
    //       is no way to access that work queue now.

    return rc;
}

void WRKQMGR::setDeclareServerDeadCount(const uint64_t pValue)
{
    declareServerDeadCount = pValue;

    return;
}

int WRKQMGR::setSuspended(const LVKey* pLVKey, LOCAL_METADATA_RELEASED &pLocal_Metadata_Lock_Released, const int pValue)
{
    int rc = 0;

    int l_LocalMetadataUnlockedInd = 0;

    if (pLVKey)
    {
        lockWorkQueueMgr(pLVKey, "setSuspended", &l_LocalMetadataUnlockedInd);

        std::map<LVKey,WRKQE*>::iterator it = wrkqs.find(*pLVKey);
        if (it != wrkqs.end())
        {
            if ((pValue && (!it->second->isSuspended())) || ((!pValue) && it->second->isSuspended()))
            {
                if (it->second)
                {
                    it->second->setSuspended(pValue);
                }
                else
                {
                    rc = -2;
                }
            }
            else
            {
                rc = 2;
            }
        }
        else
        {
            rc = -2;
        }

        if (l_LocalMetadataUnlockedInd)
        {
            pLocal_Metadata_Lock_Released = LOCAL_METADATA_LOCK_RELEASED;
        }
        unlockWorkQueueMgr(pLVKey, "setSuspended", &l_LocalMetadataUnlockedInd);
    }
    else
    {
        rc = -2;
    }

    return rc;
}


int WRKQMGR::setThrottleRate(const LVKey* pLVKey, const uint64_t pRate)
{
    int rc = 0;

    lockWorkQueueMgr(pLVKey, "setThrottleRate");

    std::map<LVKey,WRKQE*>::iterator it = wrkqs.find(*pLVKey);
    if (it != wrkqs.end())
    {
        it->second->setRate(pRate);
        calcThrottleMode();
    }
    else
    {
        rc = -2;
    }

    unlockWorkQueueMgr(pLVKey, "setThrottleRate");

    return rc;
}

int WRKQMGR::startProcessingHP_Request(AsyncRequest& pRequest)
{
    int rc = 0;

    string l_Data = pRequest.getData();
    for (auto it=inflightHP_Requests.begin(); it!=inflightHP_Requests.end(); ++it)
    {
        if (*it == l_Data)
        {
            rc = -1;
            break;
        }
    }

    if (!rc)
    {
        inflightHP_Requests.push_back(l_Data);
    }

    return rc;
}

// NOTE: pLVKey is not currently used, but can come in as null.
void WRKQMGR::unlockWorkQueueMgr(const LVKey* pLVKey, const char* pMethod, int* pLocalMetadataUnlockedInd)
{
    stringstream errorText;

    if (workQueueMgrIsLocked())
    {
        pid_t tid = syscall(SYS_gettid);  // \todo eventually remove this.  incurs syscall for each log entry
        FL_Write(FLMutex, unlockWrkQMgr, "unlockWorkQueueMgr.  threadid=%ld",tid,0,0,0);

        if (strstr(pMethod, "%") == NULL)
        {
            if (g_LockDebugLevel == "info")
            {
                if (pLVKey)
                {
                    LOG(bb,info) << " WQ_MGR: UNLOCK <- " << pMethod << ", " << *pLVKey;
                }
                else
                {
                    LOG(bb,info) << " WQ_MGR: UNLOCK <- " << pMethod << ", unknown LVKey";
                }
            }
            else
            {
                if (pLVKey)
                {
                    LOG(bb,debug) << " WQ_MGR: UNLOCK <- " << pMethod << ", " << *pLVKey;
                }
                else
                {
                    LOG(bb,debug) << " WQ_MGR: UNLOCK <- " << pMethod << ", unknown LVKey";
                }
            }
        }

        workQueueMgrLocked = 0;
        pthread_mutex_unlock(&lock_workQueueMgr);

        if (pLocalMetadataUnlockedInd && *pLocalMetadataUnlockedInd)
        {
            lockLocalMetadata(pLVKey, "unlockWorkQueueMgr");
            *pLocalMetadataUnlockedInd = 0;
        }
    }
    else
    {
        FL_Write(FLError, unlockWrkQMgrERROR, "unlockWorkQueueMgr called when lock not owned by thread",0,0,0,0);
        flightlog_Backtrace(__LINE__);
        // For now, also to the console...
        LOG(bb,error) << " WQ_MGR: Request made to unlock the work queue manager by " << pMethod << ", but the lock is not owned.";
        logBacktrace();
    }

    return;
}

int WRKQMGR::unlockWorkQueueMgrIfNeeded(const LVKey* pLVKey, const char* pMethod)
{
    ENTRY(__FILE__,__FUNCTION__);

    int rc = 0;
    if (workQueueMgrIsLocked())
    {
        unlockWorkQueueMgr(pLVKey, pMethod);
        rc = 1;
    }

    EXIT(__FILE__,__FUNCTION__);
    return rc;
}

// NOTE: Stageout End processing can 'discard' extents from a work queue.  Therefore, the number of
//       posts can exceed the current number of extents.  Such posts will eventually be consumed by the worker
//       threads and treated as no-ops.  Note that the stageout end processing cannot re-initialize
//       the semaphore to the 'correct' value as you cannot re-init a counting semaphore...  @DLH
void WRKQMGR::verify()
{
    int l_TotalExtents = getSizeOfAllWorkQueues();

    int l_NumberOfPosts = 0;

    int l_LocalMetadataUnlockedInd = 0;
    int l_WorkQueueMgrLocked = lockWorkQueueMgrIfNeeded((LVKey*)0, "WRKQMGR::verify", &l_LocalMetadataUnlockedInd);
    sem_getvalue(&sem_workqueue, &l_NumberOfPosts);
    if (l_WorkQueueMgrLocked)
    {
        unlockWorkQueueMgr((LVKey*)0, "WRKQMGR::verify", &l_LocalMetadataUnlockedInd);
    }

    // NOTE: l_NumberOfPosts+1 because for us to be invoking verify(), the current thread has already been dispatched...
    if (l_NumberOfPosts+1 != l_TotalExtents)
    {
        LOG(bb,info) << "WRKQMGR::verify(): MISMATCH: l_NumberOfPosts=" << l_NumberOfPosts << ", l_TotalExtents=" << l_TotalExtents;
        dump(const_cast<char*>("info"), " - After failed verification");
    }

    return;
}

void WRKQMGR::updateHeartbeatData(const string& pHostName)
{
    uint64_t l_Count = 0;

    bool l_HP_TransferQueueLocked = false;
    if (!HPWrkQE->transferQueueIsLocked())
    {
        HPWrkQE->lock((LVKey*)0, "WRKQMGR::updateHeartbeatData 1");
        l_HP_TransferQueueLocked = true;
    }

    struct timeval l_CurrentTime = timeval {.tv_sec=0, .tv_usec=0};
    HeartbeatEntry::getCurrentTime(l_CurrentTime);

    map<string, HeartbeatEntry>::iterator it = heartbeatData.find(pHostName);
    if (it != heartbeatData.end())
    {
        l_Count = (it->second).getCount();
        heartbeatData[pHostName] = HeartbeatEntry(++l_Count, l_CurrentTime, (it->second).getServerTime());
    }
    else
    {
        heartbeatData[pHostName] = HeartbeatEntry(++l_Count, l_CurrentTime, "");
    }

    if (l_HP_TransferQueueLocked)
    {
        HPWrkQE->unlock((LVKey*)0, "WRKQMGR::updateHeartbeatData 1");
    }

    return;
}

void WRKQMGR::updateHeartbeatData(const string& pHostName, const string& pServerTimeStamp)
{
    uint64_t l_Count = 0;

    bool l_HP_TransferQueueLocked = false;
    if (!HPWrkQE->transferQueueIsLocked())
    {
        HPWrkQE->lock((LVKey*)0, "WRKQMGR::updateHeartbeatData 2");
        l_HP_TransferQueueLocked = true;
    }

    map<string, HeartbeatEntry>::iterator it = heartbeatData.find(pHostName);
    if (it != heartbeatData.end())
    {
        l_Count = (it->second).getCount();
    }

    struct timeval l_CurrentTime = timeval {.tv_sec=0, .tv_usec=0};
    HeartbeatEntry::getCurrentTime(l_CurrentTime);
    HeartbeatEntry l_Entry = HeartbeatEntry(++l_Count, l_CurrentTime, pServerTimeStamp);

    heartbeatData[pHostName] = l_Entry;

    if (l_HP_TransferQueueLocked)
    {
        HPWrkQE->unlock((LVKey*)0, "WRKQMGR::updateHeartbeatData 2");
    }

    return;
}

#define ATTEMPTS 60
int WRKQMGR::verifyAsyncRequestFile(char* &pAsyncRequestFileName, int &pSeqNbr, const MAINTENANCE_OPTION pMaintenanceOption)
{
    int rc = 0;
    stringstream errorText;

    int l_SeqNbr = 0;
    int l_CurrentSeqNbr = 0;
    bool l_TransferQueueLocked = false;

    pAsyncRequestFileName = new char[PATH_MAX+1];
    string l_DataStorePath = g_BBServer_Metadata_Path;
    bfs::path datastore(l_DataStorePath);
    bool l_PerformDatastoreVerification = false;

    try
    {
        uint64_t l_AttemptsRemaining = ATTEMPTS;
        while (l_AttemptsRemaining--)
        {
            rc = -1;
            try
            {
                // NOTE: We used to always check for existance of the datastore, but
                //       that causes an expensive xstat64 operation for every
                //       timer interval.  Therefore, we just monitor for exceptions
                //       in this segment of code.
                if (!l_PerformDatastoreVerification)
                {
                    for (auto& asyncfile : boost::make_iterator_range(bfs::directory_iterator(datastore), {}))
                    {
                        if (pathIsDirectory(asyncfile)) continue;

                        int l_Count = sscanf(asyncfile.path().filename().c_str(),"asyncRequests_%d", &l_CurrentSeqNbr);
                        // NOTE: If pSeqNbr is passed in, that is the file we want to open...
                        if (l_Count == 1 && ((pSeqNbr && pSeqNbr == l_CurrentSeqNbr) || ((!pSeqNbr) && l_CurrentSeqNbr > l_SeqNbr)))
                        {
                            l_SeqNbr = l_CurrentSeqNbr;
                            strCpy(pAsyncRequestFileName, asyncfile.path().c_str(), PATH_MAX+1);
                        }
                    }
                    l_AttemptsRemaining = 0;
                    rc = 0;
                }
                else
                {
                    //
                    // Took an exception...  Perform datastore verification
                    //
                    int l_GpfsMountRequired = config.get("bb.requireMetadataOnParallelFileSystem", DEFAULT_REQUIRE_BBSERVER_METADATA_ON_PARALLEL_FILE_SYSTEM);

                    // If an exception was taken trying to access the datastore on the first iteration, continue.
                    // Otherwise, delay for 10 seconds to see if we can work through a possible GPFS glitch...
                    if (l_AttemptsRemaining != ATTEMPTS-2)
                    {
                        usleep(10000000);
                    }

                    // Start verification/creation of the datastore location
                    //
                    // First, verify the parent directory of the datastore
                    if (!access(datastore.parent_path().c_str(), F_OK))
                    {
                        // We can access the parent directory of the datastore
                        // NOTE: Even if a GPFS mount is not required, we still attempt to determine
                        //       if the parent directory is a GPFS mount.  We want to perform the
                        //       statfs to see if that errors out...
                        bool l_GpfsMount = false;
                        int rc2 = isGpfsFile(datastore.parent_path().c_str(), l_GpfsMount);
                        if (!rc2)
                        {
                            if (l_GpfsMount || (!l_GpfsMountRequired))
                            {
                                // We can access the parent directory of the datastore and,
                                // if required, it is a GPFS mount.
                                // Now, verify the datastore directory...
                                if (!access(datastore.c_str(), F_OK))
                                {
                                    // We can access the datastore
                                    bool l_GpfsMount = false;
                                    rc2 = isGpfsFile(datastore.c_str(), l_GpfsMount);
                                    if (!rc2)
                                    {
                                        if (l_GpfsMount || (!l_GpfsMountRequired))
                                        {
                                            // We can access the datastore and, if required, it is a GPFS mount.
                                            // Access to the datastore seems to be OK.
                                            // Iterate, attempting to verify the current async request file.
                                            l_PerformDatastoreVerification = false;
                                        }
                                        else
                                        {
                                            // Datastore is not currently a GPFS mount and it is required...
                                            // Simply iterate, delay, and try again...
                                        }
                                    }
                                    else
                                    {
                                        // Error attempting to determine if the datastore directory is a GPFS mount.
                                        // Most likely, a statfs could not be performed for the datastore directory.
                                        // Simply iterate, delay, and try again...
                                    }
                                }
                                else
                                {
                                    // We cannot access the datastore directory.  Attempt to create it...
                                    bfs::create_directories(datastore);
                                    l_PerformDatastoreVerification = false;
                                    LOG(bb,info) << "WRKQMGR: Directory " << datastore << " created to house the cross bbServer metadata after exception trying to access " << l_DataStorePath;
                                }
                            }
                            else
                            {
                                // Parent directory is not currently a GPFS mount and it is required...
                                // Simply iterate, delay, and try again...
                            }
                        }
                        else
                        {
                            // Error attempting to determine if the parent directory is a GPFS mount.
                            // Most likely, a statfs could not be performed for the parent directory.
                            // Simply iterate, delay, and try again...
                        }
                    }
                    else
                    {
                        // We cannot access the parent directory of the datastore.
                        // Simply iterate, delay, and try again...
                    }
                }
            }
            catch(ExceptionBailout& e) { }
            catch(exception& e)
            {
                if (l_AttemptsRemaining)
                {
                    l_PerformDatastoreVerification = true;
                }
                else
                {
                    LOG_ERROR_RC_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e, rc);
                }
            }
        }

        // When we get here, we trust that GPFS is stable.  Any GPFS failures below are not retried,
        // as they may have been in the above processing.
        if (!rc)
        {
            try
            {
                if (!l_SeqNbr)
                {
                    l_SeqNbr = 1;
                    snprintf(pAsyncRequestFileName, PATH_MAX+1, "%s/%s_%d", l_DataStorePath.c_str(), XBBSERVER_ASYNC_REQUEST_BASE_FILENAME.c_str(), l_SeqNbr);
                    rc = createAsyncRequestFile(pAsyncRequestFileName);
                    if (rc)
                    {
                        errorText << "Failure when attempting to create new cross bbserver async request file";
                        bberror << err("error.filename", pAsyncRequestFileName);
                        LOG_ERROR_TEXT_RC_AND_BAIL(errorText, rc);
                    }
                }

                if (!rc)
                {
                    switch (pMaintenanceOption)
                    {
                        case CREATE_NEW_FILE:
                        {
                            l_SeqNbr += 1;
                            snprintf(pAsyncRequestFileName, PATH_MAX+1, "%s/%s_%d", l_DataStorePath.c_str(), XBBSERVER_ASYNC_REQUEST_BASE_FILENAME.c_str(), l_SeqNbr);
                            rc = createAsyncRequestFile(pAsyncRequestFileName);
                            if (rc)
                            {
                                errorText << "Failure when attempting to create new cross bbserver async request file";
                                bberror << err("error.filename", pAsyncRequestFileName);
                                LOG_ERROR_TEXT_RC_AND_BAIL(errorText, rc);
                            }
                        }
                        // Fall through...

                        case START_BBSERVER:
                        {
                            if (pMaintenanceOption == START_BBSERVER)
                            {
                                // Ensure that the bbServer metadata is on a parallel file system
                                // NOTE:  We invoke isGpfsFile() even if we are not to enforce the condition so that
                                //        we flightlog the statfs() result...
                                bool l_GpfsMount = false;
                                rc = isGpfsFile(datastore.c_str(), l_GpfsMount);
                                if (!rc)
                                {
                                    if (!l_GpfsMount)
                                    {
                                        if (config.get("bb.requireMetadataOnParallelFileSystem", DEFAULT_REQUIRE_BBSERVER_METADATA_ON_PARALLEL_FILE_SYSTEM))
                                        {
                                            rc = -1;
                                            errorText << "bbServer metadata is required to be on a parallel file system. Current data store path is " << l_DataStorePath \
                                                      << ". Set bb.bbserverMetadataPath properly in the configuration.";
                                            bberror << err("error.asyncRequestFile", pAsyncRequestFileName);
                                            LOG_ERROR_TEXT_ERRNO_AND_BAIL(errorText, rc);
                                        }
                                        else
                                        {
                                            LOG(bb,info) << "WRKQMGR: bbServer metadata is NOT on a parallel file system, but is currently allowed";
                                        }
                                    }
                                }
                                else
                                {
                                    // bberror was filled in...
                                    BAIL;
                                }

                                // Unconditionally perform a chown to root:root for the cross-bbServer metatdata root directory.
                                rc = chown(l_DataStorePath.c_str(), 0, 0);
                                if (rc)
                                {
                                    errorText << "chown failed";
                                    bberror << err("error.path", l_DataStorePath.c_str());
                                    LOG_ERROR_TEXT_ERRNO_AND_BAIL(errorText, errno);
                                }

                                // Unconditionally perform a chmod to 0755 for the cross-bbServer metatdata root directory.
                                // NOTE:  root:root will insert jobid directories into this directory and then ownership
                                //        of those jobid directories will be changed to the uid:gid of the mountpoint.
                                //        The mode of the jobid directories are also created to be 0750.
                                rc = chmod(l_DataStorePath.c_str(), 0755);
                                if (rc)
                                {
                                    errorText << "chmod failed";
                                    bberror << err("error.path", l_DataStorePath.c_str());
                                    LOG_ERROR_TEXT_ERRNO_AND_BAIL(errorText, errno);
                                }

                                // Verify the correct permissions for all individual directories in the
                                // cross-bbServer metadata path.
                                bfs::path l_Path = datastore.parent_path();
                                while (l_Path.string().length())
                                {
                                    if (((bfs::status(l_Path)).permissions() & (bfs::others_read|bfs::others_exe)) != (bfs::others_read|bfs::others_exe))
                                    {
                                        rc = -1;
                                        stringstream l_Temp;
                                        l_Temp << "0" << oct << (bfs::status(l_Path)).permissions() << dec;
                                        errorText << "Verification of permissions failed for bbServer metadata directory " << l_Path.c_str() \
                                                  << ". Requires read and execute for all users, but permissions are " \
                                                  << l_Temp.str() << ".";
                                        bberror << err("error.path", l_Path.c_str()) << err("error.permissions", l_Temp.str());
                                        LOG_ERROR_TEXT_RC_AND_BAIL(errorText, rc);
                                    }
                                    l_Path = l_Path.parent_path();
                                }
                            }
                        }
                        // Fall through...

                        {
                            // Unconditionally perform a chown to root:root for the async request file.
                            rc = chown(pAsyncRequestFileName, 0, 0);
                            if (rc)
                            {
                                errorText << "chown failed";
                                bberror << err("error.path", pAsyncRequestFileName);
                                LOG_ERROR_TEXT_ERRNO_AND_BAIL(errorText, errno);
                            }

                            // Unconditionally perform a chmod to 0700 for the async request file.
                            // NOTE:  root is the only user of the async request file.
                            rc = chmod(pAsyncRequestFileName, 0700);
                            if (rc)
                            {
                                errorText << "chmod failed";
                                bberror << err("error.path", pAsyncRequestFileName);
                                LOG_ERROR_TEXT_ERRNO_AND_BAIL(errorText, errno);
                            }

                            if (HPWrkQE && (!HPWrkQE->transferQueueIsLocked()))
                            {
                                HPWrkQE->lock((LVKey*)0, "verifyAsyncRequestFile - FULL_MAINTENANCE");
                                l_TransferQueueLocked = true;
                            }

                            // Log where this instance of bbServer will start processing async requests
                            int l_AsyncRequestFileSeqNbr = 0;
                            int64_t l_OffsetToNextAsyncRequest = 0;
                            rc = findOffsetToNextAsyncRequest(l_AsyncRequestFileSeqNbr, l_OffsetToNextAsyncRequest);
                            if (!rc)
                            {
                                if (l_AsyncRequestFileSeqNbr > 0 && l_OffsetToNextAsyncRequest >= 0)
                                {
                                    if (pMaintenanceOption == START_BBSERVER)
                                    {
                                        setOffsetToNextAsyncRequest(l_AsyncRequestFileSeqNbr, l_OffsetToNextAsyncRequest);
                                        asyncRequestFileSeqNbrLastProcessed = l_AsyncRequestFileSeqNbr;
                                        if (l_OffsetToNextAsyncRequest)
                                        {
                                            // Set the last offset processed to one entry less than the next offset set above.
                                            lastOffsetProcessed = l_OffsetToNextAsyncRequest - (uint64_t)sizeof(AsyncRequest);
                                        }
                                        else
                                        {
                                            // Set the last offset processed to the maximum async request file size.
                                            // NOTE: This will cause the first expected target offset to be zero in method
                                            //       manageWorkItemsProcessed().
                                            lastOffsetProcessed = START_PROCESSING_AT_OFFSET_ZERO;
                                        }
                                    }
                                    LOG(bb,info) << "WRKQMGR: Current async request file " << pAsyncRequestFileName \
                                                 << hex << uppercase << setfill('0') << ", LstOff 0x" << setw(8) << lastOffsetProcessed \
                                                 << ", NxtOff 0x" << setw(8) << l_OffsetToNextAsyncRequest \
                                                 << setfill(' ') << nouppercase << dec << "  #OutOfOrd " << outOfOrderOffsets.size();
                                }
                                else
                                {
                                    rc = -1;
                                    errorText << "Failure when attempting to open the cross bbserver async request file, AsyncRequestFileSeqNbr = " \
                                              << l_AsyncRequestFileSeqNbr << ", OffsetToNextAsyncRequest = " << l_OffsetToNextAsyncRequest;
                                    LOG_ERROR_TEXT_RC_AND_BAIL(errorText, rc);
                                }
                            }
                            else
                            {
                                rc = -1;
                                errorText << "Failure when attempting to open the cross bbserver async request file, rc = " << rc;
                                LOG_ERROR_TEXT_RC_AND_BAIL(errorText, rc);
                            }

                            if (l_TransferQueueLocked)
                            {
                                HPWrkQE->unlock((LVKey*)0, "verifyAsyncRequestFile - FULL_MAINTENANCE");
                            }
                        }
                        // Fall through...

                        case FULL_MAINTENANCE:
                        case MINIMAL_MAINTENANCE:
                        case FORCE_REOPEN:
                        case NO_MAINTENANCE:
                        default:
                            break;
                    }
                }
            }
            catch(ExceptionBailout& e) { }
            catch(exception& e)
            {
                rc = -1;
                LOG_ERROR_RC_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e, rc);
            }

            LOG(bb,debug) << "verifyAsyncRequestFile(): File: " << pAsyncRequestFileName << ", SeqNbr: " << l_SeqNbr << ", Option: " << pMaintenanceOption;
        }
    }
    catch(ExceptionBailout& e) { }
    catch(exception& e)
    {
        rc = -1;
        LOG_ERROR_RC_WITH_EXCEPTION(__FILE__, __FUNCTION__, __LINE__, e, rc);
    }

    if (rc)
    {
        if (pAsyncRequestFileName)
        {
            delete [] pAsyncRequestFileName;
            pAsyncRequestFileName = 0;
        }
    }
    else
    {
        pSeqNbr = l_SeqNbr;
    }

    return rc;
}
#undef ATTEMPTS
