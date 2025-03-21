/********************************************************************************
 * Copyright (C) 2019-2023 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#ifndef ODC_CORE_DEFS
#define ODC_CORE_DEFS

#include <odc/Error.h>
#include <odc/Timer.h>
#include <odc/TopologyDefs.h>

#include <memory>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace odc::core
{

enum StatusCode
{
    unknown = 0,
    ok,
    error
};

enum class DDSSessionStatus
{
    unknown = 0,
    running = 1,
    stopped = 2
};

struct PartitionStatus
{
    PartitionStatus() {}
    PartitionStatus(const std::string& partitionID, const std::string& sessionID, DDSSessionStatus sessionStatus, AggregatedState aggregatedState)
        : mPartitionID(partitionID)
        , mDDSSessionID(sessionID)
        , mDDSSessionStatus(sessionStatus)
        , mAggregatedState(aggregatedState)
    {}

    std::string mPartitionID;                                       ///< Partition ID
    std::string mDDSSessionID;                                      ///< Session ID of DDS
    DDSSessionStatus mDDSSessionStatus = DDSSessionStatus::unknown; ///< DDS session status
    AggregatedState mAggregatedState = AggregatedState::Undefined;  ///< Aggregated state of the affected divices
};

struct BaseRequestResult
{
    BaseRequestResult() {}
    BaseRequestResult(StatusCode statusCode, const std::string& msg, size_t execTime, const Error& error)
        : mStatusCode(statusCode)
        , mMsg(msg)
        , mExecTime(execTime)
        , mError(error)
    {}

    StatusCode mStatusCode = StatusCode::unknown; ///< Operation status code
    std::string mMsg;                             ///< General message about the status
    size_t mExecTime = 0;                         ///< Execution time in milliseconds
    Error mError;                                 ///< In case of error containes information about the error
};

struct RequestResult : public BaseRequestResult
{
    RequestResult() {}
    RequestResult(StatusCode statusCode,
                  const std::string& msg,
                  size_t execTime,
                  const Error& error,
                  const std::string& partitionID,
                  uint64_t runNr,
                  const std::string& sessionID,
                  TopologyState topologyState,
                  const std::string& rmsJobIDs,
                  std::unordered_set<std::string> hosts)
        : BaseRequestResult(statusCode, msg, execTime, error)
        , mPartitionID(partitionID)
        , mRunNr(runNr)
        , mDDSSessionID(sessionID)
        , mTopologyState(std::move(topologyState))
        , mRMSJobIDs(rmsJobIDs)
        , mHosts(std::move(hosts))
    {}

    std::string mPartitionID;     ///< Partition ID
    uint64_t mRunNr = 0;          ///< Run number
    std::string mDDSSessionID;    ///< Session ID of DDS
    TopologyState mTopologyState; ///< Topology state (aggregated + optional detailed)
    std::string mRMSJobIDs;       ///< RMS job IDs

    // Optional parameters
    std::unordered_set<std::string> mHosts; ///< List of used hosts
};

struct StatusRequestResult : public BaseRequestResult
{
    StatusRequestResult() {}
    StatusRequestResult(StatusCode statusCode, const std::string& msg, size_t execTime, const Error& error)
        : BaseRequestResult(statusCode, msg, execTime, error)
    {}

    std::vector<PartitionStatus> mPartitions; ///< Statuses of partitions
};

struct CommonParams
{
    CommonParams() {}
    CommonParams(const std::string& partitionID, uint64_t runNr, int timeout)
        : mPartitionID(partitionID)
        , mRunNr(runNr)
        , mTimeout(timeout)
    {}

    std::string mPartitionID; ///< Partition ID.
    uint64_t mRunNr = 0;      ///< Run number.
    size_t mTimeout = 0;      ///< Request timeout in seconds. 0 means "not set"
    Timer mTimer; // TODO: put this into a wrapper "Request" class that encompases Params + timer

    friend std::ostream& operator<<(std::ostream& os, const CommonParams& p)
    {
        return os << "CommonParams: partitionID: " << quoted(p.mPartitionID)
                  << "; runNr: "                   << p.mRunNr
                  << "; timeout: "                 << p.mTimeout;
    }
};

struct InitializeParams
{
    InitializeParams() {}
    InitializeParams(const std::string& sessionID)
        : mDDSSessionID(sessionID)
    {}

    std::string mDDSSessionID; ///< DDS session ID

    friend std::ostream& operator<<(std::ostream& os, const InitializeParams& p)
    {
        return os << "InitilizeParams: sid: " << quoted(p.mDDSSessionID);
    }
};

struct SubmitParams
{
    SubmitParams() {}
    SubmitParams(const std::string& plugin, const std::string& resources)
        : mPlugin(plugin)
        , mResources(resources)
    {}

    std::string mPlugin;    ///< ODC resource plugin name. Plugin has to be registered in ODC server.
    std::string mResources; ///< Parsable description of the requested resources.

    friend std::ostream& operator<<(std::ostream& os, const SubmitParams& p)
    {
        return os << "SubmitParams: plugin: " << quoted(p.mPlugin)
                  << "; resources: "          << quoted(p.mResources);
    }
};

struct ActivateParams
{
    ActivateParams() {}
    ActivateParams(const std::string& topoFile, const std::string& topoContent, const std::string& topoScript)
        : mTopoFile(topoFile)
        , mTopoContent(topoContent)
        , mTopoScript(topoScript)
    {}

    std::string mTopoFile;    ///< Path to the topology file
    std::string mTopoContent; ///< Content of the XML topology
    std::string mTopoScript;  ///< Script that generates topology content

    friend std::ostream& operator<<(std::ostream& os, const ActivateParams& p)
    {
        return os << "ActivateParams"
                  << ": topologyFile: "    << quoted(p.mTopoFile)
                  << "; topologyContent: " << quoted(p.mTopoContent)
                  << "; topologyScript: "  << quoted(p.mTopoScript);
    }
};

struct RunParams
{
    RunParams() {}
    RunParams(const std::string& plugin,
              const std::string& resources,
              const std::string& topoFile,
              const std::string& topoContent,
              const std::string& topoScript,
              bool extractTopoResources)
        : mPlugin(plugin)
        , mResources(resources)
        , mTopoFile(topoFile)
        , mTopoContent(topoContent)
        , mTopoScript(topoScript)
        , mExtractTopoResources(extractTopoResources)
    {}

    std::string mPlugin;                ///< ODC resource plugin name. Plugin has to be registered in ODC server.
    std::string mResources;             ///< Parsable description of the requested resources.
    std::string mTopoFile;              ///< Path to the topology file
    std::string mTopoContent;           ///< Content of the XML topology
    std::string mTopoScript;            ///< Script that generates topology content
    bool mExtractTopoResources = false; ///< Submit resource request based on topology content

    friend std::ostream& operator<<(std::ostream& os, const RunParams& p)
    {
        return os << "RunParams"
                  << ": plugin: "               << quoted(p.mPlugin)
                  << "; resource: "             << quoted(p.mResources)
                  << "; topologyFile: "         << quoted(p.mTopoFile)
                  << "; topologyContent: "      << quoted(p.mTopoContent)
                  << "; topologyScript: "       << quoted(p.mTopoScript)
                  << "; extractTopoResources: " << p.mExtractTopoResources;
    }
};

struct UpdateParams
{
    UpdateParams() {}
    UpdateParams(const std::string& topoFile, const std::string& topoContent, const std::string& topoScript)
        : mTopoFile(topoFile)
        , mTopoContent(topoContent)
        , mTopoScript(topoScript)
    {}

    std::string mTopoFile;    ///< Path to the topology file
    std::string mTopoContent; ///< Content of the XML topology
    std::string mTopoScript;  ///< Script that generates topology content

    friend std::ostream& operator<<(std::ostream& os, const UpdateParams& p)
    {
        return os << "UpdateParams: topologyFile: " << quoted(p.mTopoFile)
                  << "; topologyContent: " << quoted(p.mTopoContent)
                  << "; topologyScript: " << quoted(p.mTopoScript);
    }
};

struct SetPropertiesParams
{
    using Prop = std::pair<std::string, std::string>;
    using Props = std::vector<Prop>;

    SetPropertiesParams() {}
    SetPropertiesParams(const Props& props, const std::string& path)
        : mPath(path)
        , mProperties(props)
    {}

    std::string mPath;        ///< Path in the topology
    Props mProperties; ///< List of device configuration properties

    friend std::ostream& operator<<(std::ostream& os, const SetPropertiesParams& p)
    {
        os << "SetPropertiesParams: path: " << quoted(p.mPath) << "; properties: {";
        for (const auto& v : p.mProperties) {
            os << " (" << v.first << ":" << v.second << ") ";
        }
        return os << "}";
    }
};

struct DeviceParams
{
    DeviceParams() {}
    DeviceParams(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    friend std::ostream& operator<<(std::ostream& os, const DeviceParams& p)
    {
        return os << "DeviceParams: path: " << quoted(p.mPath)
                  << "; detailed: " << p.mDetailed;
    }
};

struct StatusParams
{
    StatusParams() {}
    StatusParams(bool running)
        : mRunning(running)
    {}

    bool mRunning = false; ///< Select only running DDS sessions
    Timer mTimer; // TODO: put this into a wrapper "Request" class that encompases Params + timer

    friend std::ostream& operator<<(std::ostream& os, const StatusParams& p)
    {
        return os << "StatusParams: running: " << p.mRunning;
    }
};

struct ZoneConfig
{
    std::string cfgPath;
    std::string envPath;
};

} // namespace odc::core

#endif /* defined(ODC_CORE_DEFS) */