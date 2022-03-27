/********************************************************************************
 * Copyright (C) 2019-2022 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#include <odc/Controller.h>
#include <odc/DDSSubmit.h>
#include <odc/Error.h>
#include <odc/Logger.h>
#include <odc/Process.h>
#include <odc/Restore.h>
#include <odc/Stats.h>
#include <odc/TimeMeasure.h>
#include <odc/Topology.h>

#include <dds/dds.h>
#include <dds/TopoVars.h>
#include <dds/Tools.h>
#include <dds/Topology.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>

using namespace odc;
using namespace odc::core;
using namespace std;
namespace bfs = boost::filesystem;

void Controller::execRequestTrigger(const string& plugin, const CommonParams& common)
{
    if (m_triggers.isPluginRegistered(plugin)) {
        try {
            OLOG(debug, common) << "Executing request trigger " << quoted(plugin);
            string out{ m_triggers.execPlugin(plugin, "", common.mPartitionID, common.mRunNr) };
            OLOG(debug, common) << "Request trigger " << quoted(plugin) << " done: " << out;
        } catch (exception& e) {
            OLOG(error, common) << "Request trigger " << quoted(plugin) << " failed: " << e.what();
        }
    } else {
        // OLOG(debug, common) << "No plugins registered for " << quoted(plugin);
    }
}

void Controller::updateRestore()
{
    if (m_restoreId.empty()) {
        return;
    }

    OLOG(info) << "Updating restore file " << quoted(m_restoreId) << "...";
    SRestoreData data;

    lock_guard<mutex> lock(mSessionsMtx);
    for (const auto& v : mSessions) {
        const auto& info{ v.second };
        try {
            if (info->mDDSSession->IsRunning()) {
                data.m_partitions.push_back(SRestorePartition(info->mPartitionID, to_string(info->mDDSSession->getSessionID())));
            }
        } catch (exception& e) {
            OLOG(warning, info->mPartitionID, 0) << "Failed to get session ID or session status: " << e.what();
        }
    }

    // Write the the file is locked by mSessionsMtx
    // This is done in order to prevent write failure in case of a parallel execution.
    CRestoreFile(m_restoreId, data).write();
}

RequestResult Controller::createRequestResult(const CommonParams& common, const Error& error, const string& msg, size_t execTime, AggregatedState aggrState, unique_ptr<DetailedState> detailedState/*  = nullptr */)
{
    auto& info = getOrCreateSessionInfo(common);
    string sidStr{ to_string(info.mDDSSession->getSessionID()) };
    StatusCode status{ error.m_code ? StatusCode::error : StatusCode::ok };
    return RequestResult(status, msg, execTime, error, common.mPartitionID, common.mRunNr, sidStr, aggrState, move(detailedState));
}

bool Controller::createDDSSession(const CommonParams& common, Error& error)
{
    try {
        auto& info = getOrCreateSessionInfo(common);
        boost::uuids::uuid sessionID{ info.mDDSSession->create() };
        OLOG(info, common) << "DDS session created with session ID: " << to_string(sessionID);
    } catch (exception& e) {
        fillError(common, error, ErrorCode::DDSCreateSessionFailed, toString("Failed to create a DDS session: ", e.what()));
        return false;
    }
    return true;
}

bool Controller::attachToDDSSession(const CommonParams& common, Error& error, const string& sessionID)
{
    try {
        auto& info = getOrCreateSessionInfo(common);
        info.mDDSSession->attach(sessionID);
        OLOG(info, common) << "Attach to a DDS session with session ID: " << sessionID;
    } catch (exception& e) {
        fillError(common, error, ErrorCode::DDSAttachToSessionFailed, toString("Failed to attach to a DDS session: ", e.what()));
        return false;
    }
    return true;
}

bool Controller::submitDDSAgents(const CommonParams& common, Error& error, const CDDSSubmit::SParams& params)
{
    bool success(true);

    dds::tools_api::SSubmitRequest::request_t requestInfo;
    requestInfo.m_rms = params.m_rmsPlugin;
    requestInfo.m_instances = params.m_numAgents;
    requestInfo.m_slots = params.m_numSlots;
    requestInfo.m_config = params.m_configFile;
    requestInfo.m_groupName = params.m_agentGroup;
    OLOG(info) << "dds::tools_api::SSubmitRequest: " << requestInfo;

    condition_variable cv;

    dds::tools_api::SSubmitRequest::ptr_t requestPtr = dds::tools_api::SSubmitRequest::makeRequest(requestInfo);

    requestPtr->setMessageCallback([&success, &error, &common, this](const dds::tools_api::SMessageResponseData& msg) {
        if (msg.m_severity == dds::intercom_api::EMsgSeverity::error) {
            success = false;
            fillError(common, error, ErrorCode::DDSSubmitAgentsFailed, toString("Submit error: ", msg.m_msg));
        } else {
            OLOG(info, common) << "Submit: " << msg.m_msg;
        }
    });

    requestPtr->setDoneCallback([&cv, &common]() {
        OLOG(info, common) << "Agent submission done";
        cv.notify_all();
    });

    auto& info = getOrCreateSessionInfo(common);
    info.mDDSSession->sendRequest<dds::tools_api::SSubmitRequest>(requestPtr);

    mutex mtx;
    unique_lock<mutex> lock(mtx);
    cv_status waitStatus = cv.wait_for(lock, requestTimeout(common));

    if (waitStatus == cv_status::timeout) {
        success = false;
        fillError(common, error, ErrorCode::RequestTimeout, "Timed out waiting for agent submission");
    } else {
        OLOG(info, common) << "Agent submission done successfully";
    }
    return success;
}

bool Controller::requestCommanderInfo(const CommonParams& common, Error& error, dds::tools_api::SCommanderInfoRequest::response_t& commanderInfo)
{
    try {
        stringstream ss;
        auto& info = getOrCreateSessionInfo(common);
        info.mDDSSession->syncSendRequest<dds::tools_api::SCommanderInfoRequest>(dds::tools_api::SCommanderInfoRequest::request_t(),
                                                                               commanderInfo,
                                                                               requestTimeout(common),
                                                                               &ss);
        OLOG(info, common) << ss.str();
        OLOG(debug, common) << "Commander info: " << commanderInfo;
        return true;
    } catch (exception& e) {
        fillError(common, error, ErrorCode::DDSCommanderInfoFailed, toString("Error getting DDS commander info: ", e.what()));
        return false;
    }
}

bool Controller::waitForNumActiveAgents(const CommonParams& common, Error& error, size_t numAgents)
{
    try {
        auto& info = getOrCreateSessionInfo(common);
        info.mDDSSession->waitForNumAgents<dds::tools_api::CSession::EAgentState::active>(numAgents, requestTimeout(common));
    } catch (exception& e) {
        fillError(common, error, ErrorCode::RequestTimeout, toString("Timeout waiting for DDS agents: ", e.what()));
        return false;
    }
    return true;
}

bool Controller::activateDDSTopology(const CommonParams& common, Error& error, const string& topologyFile, dds::tools_api::STopologyRequest::request_t::EUpdateType updateType)
{
    bool success = true;

    dds::tools_api::STopologyRequest::request_t topoInfo;
    topoInfo.m_topologyFile = topologyFile;
    topoInfo.m_disableValidation = true;
    topoInfo.m_updateType = updateType;

    condition_variable cv;
    auto& sessionInfo = getOrCreateSessionInfo(common);

    dds::tools_api::STopologyRequest::ptr_t requestPtr = dds::tools_api::STopologyRequest::makeRequest(topoInfo);

    requestPtr->setMessageCallback([&success, &error, &common, this](const dds::tools_api::SMessageResponseData& msg) {
        if (msg.m_severity == dds::intercom_api::EMsgSeverity::error) {
            success = false;
            fillError(common, error, ErrorCode::DDSActivateTopologyFailed, toString("Activate error: ", msg.m_msg));
        } else {
            OLOG(debug, common) << "Activate: " << msg.m_msg;
        }
    });

    requestPtr->setProgressCallback([&common](const dds::tools_api::SProgressResponseData& progress) {
        uint32_t completed{ progress.m_completed + progress.m_errors };
        if (completed == progress.m_total) {
            OLOG(info, common) << "Activated tasks (" << progress.m_completed << "), errors (" << progress.m_errors << "), total (" << progress.m_total << ")";
        }
    });

    requestPtr->setResponseCallback([&common, &sessionInfo](const dds::tools_api::STopologyResponseData& res) {
        OLOG(debug, common) << "Activate: " << res;

        // We are not interested in stopped tasks
        if (res.m_activated) {
            TopoTaskInfo task{res.m_agentID, res.m_slotID, res.m_taskID, res.m_path, res.m_host, res.m_wrkDir};
            sessionInfo.addToTaskCache(move(task));

            if (res.m_collectionID > 0) {
                TopoCollectionInfo collection{res.m_agentID, res.m_slotID, res.m_collectionID, res.m_path, res.m_host, res.m_wrkDir};
                auto pos = collection.mPath.rfind('/');
                if (pos != string::npos) {
                    collection.mPath.erase(pos);
                }
                sessionInfo.addToCollectionCache(move(collection));
            }
        }
    });

    requestPtr->setDoneCallback([&cv]() { cv.notify_all(); });

    sessionInfo.mDDSSession->sendRequest<dds::tools_api::STopologyRequest>(requestPtr);

    mutex mtx;
    unique_lock<mutex> lock(mtx);
    cv_status waitStatus = cv.wait_for(lock, requestTimeout(common));

    if (waitStatus == cv_status::timeout) {
        success = false;
        fillError(common, error, ErrorCode::RequestTimeout, "Timed out waiting for topology activation");
        OLOG(error, common) << error;
    }

    sessionInfo.debug();

    OLOG(info, common) << "Topology " << quoted(topologyFile) << ((success) ? " activated successfully" : " failed to activate");
    return success;
}

bool Controller::shutdownDDSSession(const CommonParams& common, Error& error)
{
    try {
        auto& info = getOrCreateSessionInfo(common);
        info.mDDSTopo.reset();
        info.mTopology.reset();
        // We stop the session anyway if session ID is not nil.
        // Session can already be stopped by `dds-session stop` but session ID is not yet reset to nil.
        // If session is already stopped dds::tools_api::CSession::shutdown will reset pointers.
        if (info.mDDSSession->getSessionID() != boost::uuids::nil_uuid()) {
            info.mDDSSession->shutdown();
            if (info.mDDSSession->getSessionID() == boost::uuids::nil_uuid()) {
                OLOG(info, common) << "DDS session shutted down";
            } else {
                fillError(common, error, ErrorCode::DDSShutdownSessionFailed, "Failed to shut down DDS session");
                return false;
            }
        }
    } catch (exception& _e) {
        fillError(common, error, ErrorCode::DDSShutdownSessionFailed, toString("Shutdown failed: ", _e.what()));
        return false;
    }
    return true;
}

bool Controller::createDDSTopology(const CommonParams& common, Error& error, const string& topologyFile)
{
    using namespace dds::topology_api;
    try {
        auto& sInfo = getOrCreateSessionInfo(common);

        // extract the nmin variables and detect corresponding collections
        sInfo.mNmin.clear();
        CTopoVars vars;
        vars.initFromXML(topologyFile);
        for (const auto& [var, nmin] : vars.getMap()) {
            if (0 == var.compare(0, 9, "odc_nmin_")) {
                sInfo.mNmin.emplace(var.substr(9), TopoGroupInfo{0, stoull(nmin)});
            }
        }

        sInfo.mDDSTopo = make_unique<CTopology>(topologyFile);

        auto groups = sInfo.mDDSTopo->getMainGroup()->getElementsByType(CTopoBase::EType::GROUP);
        for (const auto& group : groups) {
            CTopoGroup::Ptr_t g = dynamic_pointer_cast<CTopoGroup>(group);
            try {
                sInfo.mNmin.at(g->getName()).n = g->getN();
            } catch (out_of_range&) {
                continue;
            }
        }

        OLOG(info, common) << "Groups:";
        for (const auto& [group, nmin] : sInfo.mNmin) {
            OLOG(info, common) << "Group '" << group << "', n: " << nmin.n << ", nmin: " << nmin.nmin;
        }

        OLOG(info, common) << "DDS topology " << quoted(topologyFile) << " created successfully";
    } catch (exception& e) {
        fillError(common, error, ErrorCode::DDSCreateTopologyFailed, toString("Failed to initialize DDS topology: ", e.what()));
        return false;
    }
    return true;
}

bool Controller::resetTopology(const CommonParams& common)
{
    auto& info = getOrCreateSessionInfo(common);
    info.mTopology.reset();
    return true;
}

bool Controller::createTopology(const CommonParams& common, Error& error, const string& topologyFile)
{
    auto& info = getOrCreateSessionInfo(common);
    try {
        info.mTopology.reset();
        info.mTopology = make_unique<Topology>(dds::topology_api::CTopology(topologyFile), info.mDDSSession);
    } catch (exception& _e) {
        info.mTopology = nullptr;
        fillError(common, error, ErrorCode::FairMQCreateTopologyFailed, toString("Failed to initialize FairMQ topology: ", _e.what()));
    }
    return info.mTopology != nullptr;
}

bool Controller::changeState(const CommonParams& common, Error& error, TopologyTransition transition, const string& path, AggregatedState& aggrState, DetailedState* detailedState)
{
    auto& info = getOrCreateSessionInfo(common);
    if (info.mTopology == nullptr) {
        fillError(common, error, ErrorCode::FairMQChangeStateFailed, "FairMQ topology is not initialized");
        return false;
    }

    auto it{ gExpectedState.find(transition) };
    DeviceState expState{ it != gExpectedState.end() ? it->second : DeviceState::Undefined };
    if (expState == DeviceState::Undefined) {
        fillError(common, error, ErrorCode::FairMQChangeStateFailed, toString("Unexpected FairMQ transition ", transition));
        return false;
    }

    bool success = true;

    try {
        const auto [errorCode, fmqTopoState] = info.mTopology->ChangeState(transition, path, requestTimeout(common));

        success = !errorCode;
        if (success) {
            try {
                aggrState = AggregateState(fmqTopoState);
            } catch (exception& e) {
                auto failedCollections = stateSummaryOnFailure(common, fmqTopoState, expState, info);
                success = false; // attemptRecovery(failedCollections, info, common);
                if (!success) {
                    fillError(common, error, ErrorCode::FairMQChangeStateFailed, toString("Aggregate topology state failed: ", e.what()));
                }
            }
            if (detailedState != nullptr) {
                getDetailedState(info.mDDSTopo.get(), fmqTopoState, detailedState);
            }
        } else {
            auto failedCollections = stateSummaryOnFailure(common, info.mTopology->GetCurrentState(), expState, info);
            success = false; // attemptRecovery(failedCollections, info, common);
            if (!success) {
                switch (static_cast<ErrorCode>(errorCode.value())) {
                    case ErrorCode::OperationTimeout:
                        fillError(common, error, ErrorCode::RequestTimeout, toString("Timed out waiting for ", transition, " transition"));
                        break;
                    default:
                        fillError(common, error, ErrorCode::FairMQChangeStateFailed, toString("FairMQ change state failed: ", errorCode.message()));
                        break;
                }
            }
        }
    } catch (exception& e) {
        auto failedCollections = stateSummaryOnFailure(common, info.mTopology->GetCurrentState(), expState, info);
        success = false; // attemptRecovery(failedCollections, info, common);
        if (!success) {
            fillError(common, error, ErrorCode::FairMQChangeStateFailed, toString("Change state failed: ", e.what()));
        }
    }

    if (success) {
        OLOG(info, common) << "State changed to " << aggrState << " via " << transition << " transition";
    }

    const auto stats{ StateStats(info.mTopology->GetCurrentState()) };
    OLOG(info, common) << stats.tasksString();
    OLOG(info, common) << stats.collectionsString();

    return success;
}

bool Controller::changeStateConfigure(const CommonParams& common, Error& error, const string& path, AggregatedState& aggrState, DetailedState* detailedState)
{
    return changeState(common, error, TopologyTransition::InitDevice,   path, aggrState, detailedState)
        && changeState(common, error, TopologyTransition::CompleteInit, path, aggrState, detailedState)
        && changeState(common, error, TopologyTransition::Bind,         path, aggrState, detailedState)
        && changeState(common, error, TopologyTransition::Connect,      path, aggrState, detailedState)
        && changeState(common, error, TopologyTransition::InitTask,     path, aggrState, detailedState);
}

bool Controller::changeStateReset(const CommonParams& common, Error& error, const string& path, AggregatedState& aggrState, DetailedState* detailedState)
{
    return changeState(common, error, TopologyTransition::ResetTask, path, aggrState, detailedState)
        && changeState(common, error, TopologyTransition::ResetDevice, path, aggrState, detailedState);
}

bool Controller::getState(const CommonParams& common, Error& error, const string& path, AggregatedState& aggrState, DetailedState* detailedState)
{
    auto& info = getOrCreateSessionInfo(common);
    if (info.mTopology == nullptr) {
        error.m_code = MakeErrorCode(ErrorCode::FairMQGetStateFailed);
        error.m_details = "FairMQ topology is not initialized";
        return false;
    }

    bool success(true);
    auto const state(info.mTopology->GetCurrentState());

    try {
        aggrState = aggregateStateForPath(info.mDDSTopo.get(), state, path);
    } catch (exception& _e) {
        success = false;
        fillError(common, error, ErrorCode::FairMQGetStateFailed, toString("Get state failed: ", _e.what()));
    }
    if (detailedState != nullptr)
        getDetailedState(info.mDDSTopo.get(), state, detailedState);

    const auto stats{ StateStats(state) };
    OLOG(info, common) << stats.tasksString();
    OLOG(info, common) << stats.collectionsString();

    return success;
}

bool Controller::setProperties(const CommonParams& common, Error& error, const SetPropertiesParams& params)
{
    auto& info = getOrCreateSessionInfo(common);
    if (info.mTopology == nullptr) {
        fillError(common, error, ErrorCode::FairMQSetPropertiesFailed, "FairMQ topology is not initialized");
        return false;
    }

    bool success(true);

    try {
        auto [errorCode, failedDevices] = info.mTopology->SetProperties(params.m_properties, params.m_path, requestTimeout(common));

        success = !errorCode;
        if (success) {
            OLOG(info, common) << "Set property finished successfully";
        } else {
            switch (static_cast<ErrorCode>(errorCode.value())) {
                case ErrorCode::OperationTimeout:
                    fillError(common, error, ErrorCode::RequestTimeout, toString("Timed out waiting for set property: ", errorCode.message()));
                    break;
                default:
                    fillError(common, error, ErrorCode::FairMQSetPropertiesFailed, toString("Set property error message: ", errorCode.message()));
                    break;
            }
            stringstream ss;
            size_t count = 0;
            ss << "List of failed devices for SetProperties request (" << failedDevices.size() << "):" << endl;
            for (auto taskId : failedDevices) {
                try {
                    TopoTaskInfo& taskInfo = info.getFromTaskCache(taskId);
                    ss << right << setw(7) << count << "   Task: " << taskInfo << endl;
                } catch (const exception& e) {
                    OLOG(error, common) << "Set properties error: " << e.what();
                }
                count++;
            }
            OLOG(error, common) << ss.str();
        }
    } catch (exception& e) {
        success = false;
        fillError(common, error, ErrorCode::FairMQSetPropertiesFailed, toString("Set properties failed: ", e.what()));
    }

    return success;
}

AggregatedState Controller::aggregateStateForPath(const dds::topology_api::CTopology* ddsTopo, const TopologyState& topoState, const string& path)
{
    if (path.empty())
        return AggregateState(topoState);

    if (ddsTopo == nullptr)
        throw runtime_error("DDS topology is not initialized");

    try {
        // Check if path points to a single task
        // If task is found than return it's state as an aggregated topology state

        // Throws if task not found for path
        const auto& task{ ddsTopo->getRuntimeTask(path) };
        auto it{ find_if(topoState.cbegin(), topoState.cend(), [&](const TopologyState::value_type& v) { return v.taskId == task.m_taskId; }) };
        if (it != topoState.cend())
            return static_cast<AggregatedState>(it->state);

        throw runtime_error("Device not found for path " + path);
    } catch (exception& /* e */) {
        // In case of exception check that path contains multiple tasks

        // Collect all task IDs to a set for fast search
        set<dds::topology_api::Id_t> taskIds;
        auto it{ ddsTopo->getRuntimeTaskIteratorMatchingPath(path) };
        for_each(it.first, it.second, [&](const dds::topology_api::STopoRuntimeTask::FilterIterator_t::value_type& v) {
            taskIds.insert(v.second.m_taskId);
        });

        if (taskIds.empty())
            throw runtime_error("No tasks found matching the path " + path);

        // Find a state of a first task
        auto firstIt{ find_if(topoState.cbegin(), topoState.cend(), [&](const TopologyState::value_type& v) {
            return v.taskId == *(taskIds.begin());
        }) };
        if (firstIt == topoState.cend()) {
            throw runtime_error("No states found for path " + path);
        }

        // Check that all selected devices have the same state
        AggregatedState first{ static_cast<AggregatedState>(firstIt->state) };
        if (all_of(topoState.cbegin(), topoState.cend(), [&](const TopologyState::value_type& v) {
                return (taskIds.count(v.taskId) > 0) ? v.state == first : true;
            })) {
            return first;
        }

        return AggregatedState::Mixed;
    }
}

void Controller::getDetailedState(const dds::topology_api::CTopology* ddsTopo, const TopologyState& topoState, DetailedState* detailedState)
{
    if (detailedState == nullptr || ddsTopo == nullptr)
        return;

    detailedState->reserve(topoState.size());
    for (const auto& state : topoState) {
        const auto& task = ddsTopo->getRuntimeTaskById(state.taskId);
        detailedState->push_back(DetailedTaskStatus(state, task.m_taskPath));
    }
}

void Controller::fillError(const CommonParams& common, Error& error, ErrorCode errorCode, const string& msg)
{
    error.m_code = MakeErrorCode(errorCode);
    error.m_details = msg;
    OLOG(error, common) << error;
}

void Controller::fillFatalError(const CommonParams& common, Error& error, ErrorCode errorCode, const string& msg)
{
    error.m_code = MakeErrorCode(errorCode);
    error.m_details = msg;
    stringstream ss(error.m_details);
    string line;
    OLOG(fatal, common) << error.m_code;
    while (getline(ss, line, '\n')) {
        OLOG(fatal, common) << line;
    }
}

Controller::SessionInfo& Controller::getOrCreateSessionInfo(const CommonParams& common)
{
    lock_guard<mutex> lock(mSessionsMtx);
    auto it = mSessions.find(common.mPartitionID);
    if (it == mSessions.end()) {
        auto newSessionInfo = make_unique<SessionInfo>();
        newSessionInfo->mDDSSession = make_unique<dds::tools_api::CSession>();
        newSessionInfo->mPartitionID = common.mPartitionID;
        auto ret = mSessions.emplace(common.mPartitionID, move(newSessionInfo));
        OLOG(debug, common) << "Created session info for partition ID " << quoted(common.mPartitionID);
        return *(ret.first->second);
    }
    OLOG(debug, common) << "Found session info for partition ID " << quoted(common.mPartitionID);
    return *(it->second);
}

Error Controller::checkSessionIsRunning(const CommonParams& common, ErrorCode errorCode)
{
    Error error;
    auto& info = getOrCreateSessionInfo(common);
    if (!info.mDDSSession->IsRunning()) {
        fillError(common, error, errorCode, "DDS session is not running. Use Init or Run to start the session.");
    }
    return error;
}

vector<TopoCollectionInfo*> Controller::stateSummaryOnFailure(const CommonParams& common, const TopologyState& topoState, DeviceState expectedState, SessionInfo& sessionInfo)
{
    size_t numTasks = topoState.size();
    size_t numFailedTasks = 0;
    stringstream ss;
    for (const auto& status : topoState) {
        // Print only failed devices
        if (status.state == expectedState) {
            continue;
        }

        numFailedTasks++;
        if (numFailedTasks == 1) {
            ss << "Following devices failed to transition to " << expectedState << " state:";
        }
        ss << "\n" << right << setw(7) << numFailedTasks << " Device: state: " << status.state << ", previous state: " << status.lastState
           << ", taskID: " << status.taskId << ", collectionID: " << status.collectionId << ", "
           << "subscribed: " << boolalpha << status.subscribedToStateChanges;

        try {
            TopoTaskInfo& taskInfo = sessionInfo.getFromTaskCache(status.taskId);
            ss << ", " << taskInfo;
        } catch (const exception& e) {
            OLOG(error, common) << "State summary error: " << e.what();
        }
    }

    if (numFailedTasks > 0) {
        OLOG(error, common) << ss.str();
        ss.str(string());
    }

    vector<TopoCollectionInfo*> failedCollections;

    auto collectionMap{ GroupByCollectionId(topoState) };
    size_t numCollections = collectionMap.size();
    size_t numFailedCollections = 0;
    for (const auto& [collectionId, states] : collectionMap) {
        try {
            auto collectionState = AggregateState(states);
            // Print only failed collections
            if (collectionState == expectedState) {
                continue;
            }

            numFailedCollections++;
            if (numFailedCollections == 1) {
                ss << "\n" << "Following collections failed to transition to " << expectedState << " state:";
            }
            ss << "\n" << right << setw(7) << numFailedCollections << " Collection: state: " << collectionState;

            TopoCollectionInfo& collectionInfo = sessionInfo.getFromCollectionCache(collectionId);
            failedCollections.push_back(&collectionInfo);
            ss << ", " << collectionInfo;
        } catch (const exception& e) {
            OLOG(error, common) << "State summary error: " << e.what();
        }
    }

    if (numFailedCollections > 0) {
        OLOG(error, common) << ss.str();
        ss.str(string());
    }

    size_t numSuccessfulTasks = numTasks - numFailedTasks;
    size_t numSuccessfulCollections = numCollections - numFailedCollections;
    OLOG(error, common) << "\nSummary after transitioning to " << expectedState << " state:\n"
       << "  [tasks] total: " << numTasks << ", successful: " << numSuccessfulTasks << ", failed: " << numFailedTasks << "\n"
       << "  [collections] total: " << numCollections << ", successful: " << numSuccessfulCollections << ", failed: " << numFailedCollections;

    return failedCollections;
}

bool Controller::attemptRecovery(vector<TopoCollectionInfo*>& failedCollections, SessionInfo& sessionInfo, const CommonParams& common)
{
    if (!failedCollections.empty() && !sessionInfo.mNmin.empty()) {
        OLOG(info, common) << "Attemping recovery";
        // get failed collections and determine if recovery makes sense:
        //   - are failed collections inside of a group?
        //   - do all failed collections have nMin parameter defined?
        //   - is the nMin parameter satisfied?
        map<string, uint64_t> failedCollectionsCount;
        for (const auto& c : failedCollections) {
            OLOG(info, common) << "Checking collection '" << c->mPath << "' with agend id " << c->mAgentID;
            string collectionParentName = sessionInfo.mDDSTopo->getRuntimeCollectionById(c->mCollectionID).m_collection->getParent()->getName();
            for (const auto& [groupName, groupInfo] : sessionInfo.mNmin) {
                if (collectionParentName != groupName) {
                    // stop recovery, conditions not satisifed
                    OLOG(error, common) << "Failed collection '" << c->mPath << "' is not in a group that has the nmin parameter specified";
                    return false;
                } else {
                    // increment failed collection count per group name
                    if (failedCollectionsCount.find(groupName) == failedCollectionsCount.end()) {
                        failedCollectionsCount.emplace(groupName, 1);
                    } else {
                        failedCollectionsCount[groupName]++;
                    }
                    OLOG(info, common) << "Group '" << groupName << "', failed collection count " << failedCollectionsCount.at(groupName);
                }
            }
        }

        // proceed only if remaining collections > nmin.
        for (const auto& [groupName, groupInfo] : sessionInfo.mNmin) {
            uint64_t failedCount = failedCollectionsCount.at(groupName);
            uint64_t remainingCount = groupInfo.n - failedCount;
            OLOG(info, common) << "Group '" << groupName << "' with n: " << groupInfo.n << " and nmin: " << groupInfo.nmin << ". Failed count: " << failedCount;
            if (remainingCount < groupInfo.nmin) {
                OLOG(error, common) << "Number of remaining collections in group '" << groupName << "' (" << remainingCount << ") is below nmin (" << groupInfo.nmin << ")";
                return false;
            }
        }

        // destroy Topology
        // OLOG(info, common) << "Resetting Topology...";
        // sessionInfo.mTopology.reset();
        // OLOG(info, common) << "Done!";

        // shutdown agents responsible for failed collections (https://github.com/FairRootGroup/DDS/blob/master/dds-tools-lib/tests/TestSession.cpp#L632)

        using namespace dds::tools_api;
        using namespace dds::topology_api;

        try {
            size_t currentAgentsCount = getNumAgents(sessionInfo.mDDSSession);
            size_t expectedAgentsCount = currentAgentsCount - failedCollections.size();
            OLOG(info, common) << "Current number of agents: " << currentAgentsCount << ", expecting to reduce to " << expectedAgentsCount;


            const chrono::seconds timeout{ 30 };
            for (const auto& c : failedCollections) {
                OLOG(info, common) << "Sending shutdown signal to agent " << c->mAgentID << ", responsible for " << c->mPath;
                SAgentCommandRequest::request_t agentCmd;
                agentCmd.m_commandType = SAgentCommandRequestData::EAgentCommandType::shutDownByID;
                agentCmd.m_arg1 = c->mAgentID;
                sessionInfo.mDDSSession->syncSendRequest<SAgentCommandRequest>(agentCmd, timeout);
            }

            // TODO: notification on agent shutdown in development in DDS
            OLOG(info, common) << "Current number of agents: " << getNumAgents(sessionInfo.mDDSSession);
            currentAgentsCount = getNumAgents(sessionInfo.mDDSSession);
            size_t attempts = 0;
            while (currentAgentsCount != expectedAgentsCount && attempts < 400) {
                this_thread::sleep_for(chrono::milliseconds(50));
                currentAgentsCount = getNumAgents(sessionInfo.mDDSSession);
                OLOG(info, common) << "Current number of agents: " << currentAgentsCount;
                ++attempts;
            }
            if (expectedAgentsCount != currentAgentsCount) {
                OLOG(info, common) << "Could not reduce the number of agents to " << expectedAgentsCount << ", current count is: " << currentAgentsCount;
            } else {
                OLOG(info, common) << "Successfully reduced number of agents to " << currentAgentsCount;
            }
        } catch (exception& e) {
            OLOG(error, common) << "Failed updating nubmer of agents: " << e.what();
            return false;
        }

        // create new DDS topology with updated n = remaining collections

        OLOG(info, common) << "Updating current topology file (" << sessionInfo.mTopoFilePath << ") to reflect the reduced number of groups";

        CTopoCreator creator;
        creator.getMainGroup()->initFromXML(sessionInfo.mTopoFilePath);
        auto groups = creator.getMainGroup()->getElementsByType(CTopoBase::EType::GROUP);
        for (const auto& group : groups) {
            CTopoGroup::Ptr_t g = dynamic_pointer_cast<CTopoGroup>(group);
            try {
                uint64_t failedCount = failedCollectionsCount.at(g->getName());
                uint64_t n = sessionInfo.mNmin.at(g->getName()).n;
                uint64_t remainingCount = n - failedCount;
                g->setN(remainingCount);
            } catch (out_of_range&) {
                continue;
            }
        }

        string name("topo_" + sessionInfo.mPartitionID + "_reduced.xml");
        const bfs::path tmpPath{ bfs::temp_directory_path() / bfs::unique_path() };
        bfs::create_directories(tmpPath);
        const bfs::path filepath{ tmpPath / name };
        creator.save(filepath.string());

        // re-add the nmin variables
        CTopoVars vars;
        vars.initFromXML(filepath.string());
        for (const auto& [groupName, groupInfo] : sessionInfo.mNmin) {
            string varName("odc_nmin_" + groupName);
            vars.add(varName, std::to_string(groupInfo.nmin));
        }
        vars.saveToXML(filepath.string());

        OLOG(info, common) << "Saved updated topology file as " << filepath.string();

        sessionInfo.mTopoFilePath = filepath.string();

        // issue DDS topology update
        Error error;
        if (!activateDDSTopology(common, error, sessionInfo.mTopoFilePath, STopologyRequest::request_t::EUpdateType::UPDATE)) {
            OLOG(error, common) << "Failed to update running topology";
            return false;
        }

        if (!createDDSTopology(common, error, sessionInfo.mTopoFilePath)) {
            OLOG(error, common) << "Failed to reinitialize dds::topology_api::CTopology";
            return false;
        }

        // update dds::topology_api::CTopology
        if (!createTopology(common, error, sessionInfo.mTopoFilePath)) {
            OLOG(error, common) << "Failed recreate Topology";
            return false;
        }

        if (error.m_code) {
            OLOG(error, common) << "Recovery failed";
            fillError(common, error, ErrorCode::TopologyFailed, "Recovery of the remaining collections failed");
            return false;
        }

        return true;
    }

    return false;
}

uint64_t Controller::getNumAgents(shared_ptr<dds::tools_api::CSession> ddsSession)
{
    using namespace dds::tools_api;
    // const chrono::seconds timeout{ 30 };
    // SAgentCountRequest::response_t agentCountInfo;
    // ddsSession->syncSendRequest<SAgentCountRequest>(SAgentCountRequest::request_t(), agentCountInfo, timeout, &cout);
    // agentCountInfo.
    // return agentInfo.size();

    const chrono::seconds timeout{ 30 };
    SAgentInfoRequest::request_t agentInfoRequest;
    SAgentInfoRequest::responseVector_t agentInfo;
    ddsSession->syncSendRequest<SAgentInfoRequest>(agentInfoRequest, agentInfo, timeout);

    return agentInfo.size();
}

bool Controller::subscribeToDDSSession(const CommonParams& common, Error& error)
{
    try {
        auto& info = getOrCreateSessionInfo(common);
        if (info.mDDSSession->IsRunning()) {
            // Subscrube on TaskDone events
            auto request{ dds::tools_api::SOnTaskDoneRequest::makeRequest(dds::tools_api::SOnTaskDoneRequest::request_t()) };
            request->setResponseCallback([common](const dds::tools_api::SOnTaskDoneResponseData& i) {
                stringstream ss;
                ss << "Task " << i.m_taskID << " with path '" << i.m_taskPath << "' exited with code " << i.m_exitCode << " and signal " << i.m_signal << " on host "
                   << i.m_host << " in working directory '" << i.m_wrkDir << "'";
                if (i.m_exitCode != 0 || i.m_signal != 0) {
                    OLOG(fatal, common) << ss.str();
                } else {
                    OLOG(debug, common) << ss.str();
                }
            });
            info.mDDSSession->sendRequest<dds::tools_api::SOnTaskDoneRequest>(request);
            OLOG(info, common) << "Subscribed to task done event from session " << quoted(to_string(info.mDDSSession->getSessionID()));
        } else {
            fillError(common, error, ErrorCode::DDSSubscribeToSessionFailed, "Failed to subscribe to task done events: session is not running");
            return false;
        }
    } catch (exception& _e) {
        fillError(common, error, ErrorCode::DDSSubscribeToSessionFailed, string("Failed to subscribe to task done events: ") + _e.what());
        return false;
    }
    return true;
}

string Controller::topoFilepath(const CommonParams& common, const string& topologyFile, const string& topologyContent, const string& topologyScript)
{
    int count{ (topologyFile.empty() ? 0 : 1) + (topologyContent.empty() ? 0 : 1) + (topologyScript.empty() ? 0 : 1) };
    if (count != 1) {
        throw runtime_error("Either topology filepath, content or script has to be set");
    }
    if (!topologyFile.empty()) {
        return topologyFile;
    }

    string content{ topologyContent };

    // Execute topology script if needed
    if (!topologyScript.empty()) {
        stringstream ssCmd;
        ssCmd << boost::process::search_path("bash").string() << " -c " << quoted(topologyScript);
        const chrono::seconds timeout{ 30 };
        string out;
        string err;
        int exitCode{ EXIT_SUCCESS };
        string cmd{ ssCmd.str() };
        OLOG(info, common) << "Executing topology script " << quoted(cmd);
        execute(cmd, timeout, &out, &err, &exitCode);

        if (exitCode != EXIT_SUCCESS) {
            throw runtime_error(toString("Topology generation script ", quoted(cmd), " failed with exit code: ", exitCode, "; stderr: ", quoted(err), "; stdout: ", quoted(out)));
        }

        const string sout{ out.substr(0, min(out.length(), size_t(20))) };
        OLOG(info, common) << "Topology script executed successfully: stdout (" << quoted(sout) << "...) stderr (" << quoted(err) << ")";

        content = out;
    }

    // Create temp topology file with `content`
    const bfs::path tmpPath{ bfs::temp_directory_path() / bfs::unique_path() };
    bfs::create_directories(tmpPath);
    const bfs::path filepath{ tmpPath / "topology.xml" };
    ofstream file(filepath.string());
    if (!file.is_open()) {
        throw runtime_error(toString("Failed to create temp topology file ", quoted(filepath.string())));
    }
    file << content;
    OLOG(info, common) << "Temp topology file " << quoted(filepath.string()) << " created successfully";
    return filepath.string();
}

chrono::seconds Controller::requestTimeout(const CommonParams& common) const
{
    auto timeout{ (common.mTimeout == 0) ? mTimeout : chrono::seconds(common.mTimeout) };
    OLOG(debug, common) << "Request timeout: " << timeout.count() << " sec";
    return timeout;
}

void Controller::registerResourcePlugins(const CDDSSubmit::PluginMap_t& pluginMap)
{
    for (const auto& v : pluginMap) {
        m_submit.registerPlugin(v.first, v.second);
    }
}

void Controller::registerRequestTriggers(const CPluginManager::PluginMap_t& triggerMap)
{
    const set<string> avail{ "Initialize", "Submit", "Activate", "Run", "Update", "Configure", "SetProperties", "GetState", "Start", "Stop", "Reset", "Terminate", "Shutdown", "Status" };
    for (const auto& v : triggerMap) {
        if (avail.count(v.first) == 0) {
            throw runtime_error(toString("Failed to add request trigger ", quoted(v.first), ". Invalid request name. Valid names are: ", quoted(boost::algorithm::join(avail, ", "))));
        }
        m_triggers.registerPlugin(v.first, v.second);
    }
}

void Controller::restore(const string& id)
{
    m_restoreId = id;

    OLOG(info) << "Restoring sessions for " << quoted(id);
    auto data{ CRestoreFile(id).read() };
    for (const auto& v : data.m_partitions) {
        OLOG(info, v.mPartitionID, 0) << "Restoring (" << quoted(v.mPartitionID) << "/" << quoted(v.mDDSSessionId) << ")";
        auto result{ execInitialize(CommonParams(v.mPartitionID, 0, 0), SInitializeParams(v.mDDSSessionId)) };
        if (result.m_error.m_code) {
            OLOG(info, v.mPartitionID, 0) << "Failed to attach to the session. Executing Shutdown trigger for (" << quoted(v.mPartitionID) << "/" << quoted(v.mDDSSessionId) << ")";
            execRequestTrigger("Shutdown", CommonParams(v.mPartitionID, 0, 0));
        } else {
            OLOG(info, v.mPartitionID, 0) << "Successfully attached to the session (" << quoted(v.mPartitionID) << "/" << quoted(v.mDDSSessionId) << ")";
        }
    }
}

RequestResult Controller::execInitialize(const CommonParams& common, const SInitializeParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;

    Error error;
    if (params.mDDSSessionID.empty()) {
        // Create new DDS session
        // Shutdown DDS session if it is running already
        shutdownDDSSession(common, error)
            && createDDSSession(common, error)
            && subscribeToDDSSession(common, error);
    } else {
        // Attach to an existing DDS session
        // Shutdown DDS session if it is running already
        bool success = shutdownDDSSession(common, error)
            && attachToDDSSession(common, error, params.mDDSSessionID)
            && subscribeToDDSSession(common, error);

        // Request current active topology, if any
        // If topology is active, create DDS and FairMQ topology
        if (success) {
            dds::tools_api::SCommanderInfoRequest::response_t commanderInfo;
            requestCommanderInfo(common, error, commanderInfo)
                && !commanderInfo.m_activeTopologyPath.empty()
                && createDDSTopology(common, error, commanderInfo.m_activeTopologyPath)
                && createTopology(common, error, commanderInfo.m_activeTopologyPath);
        }
    }
    execRequestTrigger("Initialize", common);
    updateRestore();
    return createRequestResult(common, error, "Initialize done", measure.duration(), AggregatedState::Undefined);
}

RequestResult Controller::execSubmit(const CommonParams& common, const SSubmitParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;

    Error error{ checkSessionIsRunning(common, ErrorCode::DDSSubmitAgentsFailed) };

    // Get DDS submit parameters from ODC resource plugin
    vector<CDDSSubmit::SParams> ddsParams;
    if (!error.m_code) {
        try {
            ddsParams = m_submit.makeParams(params.m_plugin, params.m_resources, common.mPartitionID, common.mRunNr);
        } catch (exception& _e) {
            fillError(common, error, ErrorCode::ResourcePluginFailed, toString("Resource plugin failed: ", _e.what()));
        }
    }

    OLOG(info) << "Preparing to submit " << ddsParams.size() << " configurations";

    if (!error.m_code) {
        size_t totalRequiredSlots = 0;
        for (const auto& param : ddsParams) {
            // OLOG(info) << "Submitting " << param;
            // Submit DDS agents
            if (submitDDSAgents(common, error, param)) {
                totalRequiredSlots += param.m_numRequiredSlots;
                OLOG(info) << "Done waiting for slots.";
            } else {
                OLOG(error) << "Submission failed";
                break;
            }
        }
        if (!error.m_code) {
            OLOG(info) << "Waiting for " << totalRequiredSlots << " slots...";
            waitForNumActiveAgents(common, error, totalRequiredSlots);
        }
    }
    execRequestTrigger("Submit", common);
    return createRequestResult(common, error, "Submit done", measure.duration(), AggregatedState::Undefined);
}

RequestResult Controller::execActivate(const CommonParams& common, const SActivateParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    auto& sessionInfo = getOrCreateSessionInfo(common);
    // Activate DDS topology
    // Create Topology
    Error error{ checkSessionIsRunning(common, ErrorCode::DDSActivateTopologyFailed) };
    if (!error.m_code) {
        try {
            sessionInfo.mTopoFilePath = topoFilepath(common, params.mDDSTopologyFile, params.mDDSTopologyContent, params.mDDSTopologyScript);
        } catch (exception& _e) {
            fillFatalError(common, error, ErrorCode::TopologyFailed, toString("Incorrect topology provided: ", _e.what()));
        }
        if (!error.m_code) {
            activateDDSTopology(common, error, sessionInfo.mTopoFilePath, dds::tools_api::STopologyRequest::request_t::EUpdateType::ACTIVATE)
                && createDDSTopology(common, error, sessionInfo.mTopoFilePath)
                && createTopology(common, error, sessionInfo.mTopoFilePath);
        }
    }
    AggregatedState state{ !error.m_code ? AggregatedState::Idle : AggregatedState::Undefined };
    execRequestTrigger("Activate", common);
    return createRequestResult(common, error, "Activate done", measure.duration(), state);
}

RequestResult Controller::execRun(const CommonParams& common, const SInitializeParams& initializeParams, const SSubmitParams& submitParams, const SActivateParams& activateParams)
{
    STimeMeasure<chrono::milliseconds> measure;
    // Run request doesn't support attachment to a DDS session.
    // Execute consecuently Initialize, Submit and Activate.

    Error error;
    if (!initializeParams.mDDSSessionID.empty()) {
        error = Error(MakeErrorCode(ErrorCode::RequestNotSupported), "Attachment to a DDS session not supported");
    } else {
        error = execInitialize(common, initializeParams).m_error;
        if (!error.m_code) {
            error = execSubmit(common, submitParams).m_error;
            if (!error.m_code) {
                error = execActivate(common, activateParams).m_error;
            }
        }
    }
    AggregatedState state{ !error.m_code ? AggregatedState::Idle : AggregatedState::Undefined };
    execRequestTrigger("Run", common);
    return createRequestResult(common, error, "Run done", measure.duration(), state);
}

RequestResult Controller::execUpdate(const CommonParams& common, const SUpdateParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    auto& sessionInfo = getOrCreateSessionInfo(common);
    AggregatedState state{ AggregatedState::Undefined };
    // Reset devices' state
    // Update DDS topology
    // Create Topology
    // Configure devices' state
    Error error;

    try {
        sessionInfo.mTopoFilePath = topoFilepath(common, params.mDDSTopologyFile, params.mDDSTopologyContent, params.mDDSTopologyScript);
    } catch (exception& _e) {
        fillFatalError(common, error, ErrorCode::TopologyFailed, toString("Incorrect topology provided: ", _e.what()));
    }

    if (!error.m_code) {
        changeStateReset(common, error, "", state) &&
        resetTopology(common) &&
        activateDDSTopology(common, error, sessionInfo.mTopoFilePath, dds::tools_api::STopologyRequest::request_t::EUpdateType::UPDATE) &&
        createDDSTopology(common, error, sessionInfo.mTopoFilePath) &&
        createTopology(common, error, sessionInfo.mTopoFilePath) &&
        changeStateConfigure(common, error, "", state);
    }
    execRequestTrigger("Update", common);
    return createRequestResult(common, error, "Update done", measure.duration(), state);
}

RequestResult Controller::execShutdown(const CommonParams& common)
{
    STimeMeasure<chrono::milliseconds> measure;
    Error error;
    shutdownDDSSession(common, error);
    execRequestTrigger("Shutdown", common);
    return createRequestResult(common, error, "Shutdown done", measure.duration(), AggregatedState::Undefined);
}

RequestResult Controller::execSetProperties(const CommonParams& common, const SetPropertiesParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    Error error;
    setProperties(common, error, params);
    execRequestTrigger("SetProperties", common);
    return createRequestResult(common, error, "SetProperties done", measure.duration(), AggregatedState::Undefined);
}

RequestResult Controller::execGetState(const CommonParams& common, const SDeviceParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    AggregatedState state{ AggregatedState::Undefined };
    unique_ptr<DetailedState> detailedState = params.m_detailed ? make_unique<DetailedState>() : nullptr;
    Error error;
    getState(common, error, params.m_path, state, detailedState.get());
    execRequestTrigger("GetState", common);
    return createRequestResult(common, error, "GetState done", measure.duration(), state, move(detailedState));
}

RequestResult Controller::execConfigure(const CommonParams& common, const SDeviceParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    AggregatedState state{ AggregatedState::Undefined };
    unique_ptr<DetailedState> detailedState = params.m_detailed ? make_unique<DetailedState>() : nullptr;
    Error error;
    changeStateConfigure(common, error, params.m_path, state, detailedState.get());
    execRequestTrigger("Configure", common);
    return createRequestResult(common, error, "Configure done", measure.duration(), state, move(detailedState));
}

RequestResult Controller::execStart(const CommonParams& common, const SDeviceParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    AggregatedState state{ AggregatedState::Undefined };
    unique_ptr<DetailedState> detailedState = params.m_detailed ? make_unique<DetailedState>() : nullptr;
    Error error;
    changeState(common, error, TopologyTransition::Run, params.m_path, state, detailedState.get());
    execRequestTrigger("Start", common);
    return createRequestResult(common, error, "Start done", measure.duration(), state, move(detailedState));
}

RequestResult Controller::execStop(const CommonParams& common, const SDeviceParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    AggregatedState state{ AggregatedState::Undefined };
    unique_ptr<DetailedState> detailedState = params.m_detailed ? make_unique<DetailedState>() : nullptr;
    Error error;
    changeState(common, error, TopologyTransition::Stop, params.m_path, state, detailedState.get());
    execRequestTrigger("Stop", common);
    return createRequestResult(common, error, "Stop done", measure.duration(), state, move(detailedState));
}

RequestResult Controller::execReset(const CommonParams& common, const SDeviceParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    AggregatedState state{ AggregatedState::Undefined };
    unique_ptr<DetailedState> detailedState = params.m_detailed ? make_unique<DetailedState>() : nullptr;
    Error error;
    changeStateReset(common, error, params.m_path, state, detailedState.get());
    execRequestTrigger("Reset", common);
    return createRequestResult(common, error, "Reset done", measure.duration(), state, move(detailedState));
}

RequestResult Controller::execTerminate(const CommonParams& common, const SDeviceParams& params)
{
    STimeMeasure<chrono::milliseconds> measure;
    AggregatedState state{ AggregatedState::Undefined };
    unique_ptr<DetailedState> detailedState = params.m_detailed ? make_unique<DetailedState>() : nullptr;
    Error error;
    changeState(common, error, TopologyTransition::End, params.m_path, state, detailedState.get());
    execRequestTrigger("Terminate", common);
    return createRequestResult(common, error, "Terminate done", measure.duration(), state, move(detailedState));
}

StatusRequestResult Controller::execStatus(const SStatusParams& params)
{
    lock_guard<mutex> lock(mSessionsMtx);
    STimeMeasure<chrono::milliseconds> measure;
    StatusRequestResult result;
    for (const auto& v : mSessions) {
        const auto& info{ v.second };
        PartitionStatus status;
        status.mPartitionID = info->mPartitionID;
        try {
            status.mDDSSessionID = to_string(info->mDDSSession->getSessionID());
            status.mDDSSessionStatus = (info->mDDSSession->IsRunning()) ? DDSSessionStatus::running : DDSSessionStatus::stopped;
        } catch (exception& _e) {
            OLOG(warning, status.mPartitionID, 0) << "Failed to get session ID or session status: " << _e.what();
        }

        // Filter running sessions if needed
        if ((params.m_running && status.mDDSSessionStatus == DDSSessionStatus::running) || (!params.m_running)) {
            try {
                status.m_aggregatedState = (info->mTopology != nullptr && info->mDDSTopo != nullptr)
                ?
                aggregateStateForPath(info->mDDSTopo.get(), info->mTopology->GetCurrentState(), "")
                :
                AggregatedState::Undefined;
            } catch (exception& _e) {
                OLOG(warning, status.mPartitionID, 0) << "Failed to get an aggregated state: " << _e.what();
            }
            result.m_partitions.push_back(status);
        }
    }
    result.m_statusCode = StatusCode::ok;
    result.m_msg = "Status done";
    result.m_execTime = measure.duration();
    execRequestTrigger("Status", CommonParams());
    return result;
}
