/********************************************************************************
 * Copyright (C) 2019-2023 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#ifndef ODC_CORE_REQUESTS
#define ODC_CORE_REQUESTS

#include <odc/Error.h>
#include <odc/Timer.h>
#include <odc/TopologyDefs.h>

#include <iomanip>
#include <memory>
#include <string>
#include <string_view>
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

struct RequestResult
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
                  std::unordered_set<std::string> hosts)
        : mStatusCode(statusCode)
        , mMsg(msg)
        , mExecTime(execTime)
        , mError(error)
        , mPartitionID(partitionID)
        , mRunNr(runNr)
        , mDDSSessionID(sessionID)
        , mTopologyState(std::move(topologyState))
        , mHosts(std::move(hosts))
    {}

    StatusCode mStatusCode = StatusCode::unknown; ///< Operation status code
    std::string mMsg;                             ///< General message about the status
    size_t mExecTime = 0;                         ///< Execution time in milliseconds
    Error mError;                                 ///< In case of error containes information about the error

    std::string mPartitionID;     ///< Partition ID
    uint64_t mRunNr = 0;          ///< Run number
    std::string mDDSSessionID;    ///< Session ID of DDS
    TopologyState mTopologyState; ///< Topology state (aggregated + optional detailed)

    // Optional parameters
    std::unordered_set<std::string> mHosts; ///< List of used hosts
    std::vector<PartitionStatus> mPartitions; ///< Statuses of partitions
};

struct Request
{
    Request() = default;

    size_t mTimeout = 0;      ///< Request timeout in seconds. 0 = infinite
    Timer mTimer;             ///< Measuring the request processing time
    RequestResult mResult;    ///< Request result

    virtual std::string_view name() const = 0;

    friend std::ostream& operator<<(std::ostream& os, const Request& r)
    {
        return os << " PartitionID: "     << std::quoted(r.mResult.mPartitionID)
                  << "; RunNr: "          << r.mResult.mRunNr
                  << "; Timeout: "        << r.mTimeout;
    }

    virtual ~Request() = default;
};

struct InitializeRequest : public Request
{
    InitializeRequest() {}
    InitializeRequest(const std::string& sessionID)
        : mDDSSessionID(sessionID)
    {}

    std::string mDDSSessionID; ///< DDS session ID

    std::string_view name() const override { return "Initialize"; }

    friend std::ostream& operator<<(std::ostream& os, const InitializeRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; Target DDS Session ID: " << std::quoted(r.mDDSSessionID);
    }
};

struct SubmitRequest : public Request
{
    SubmitRequest() {}
    SubmitRequest(const std::string& plugin, const std::string& resources)
        : mPlugin(plugin)
        , mResources(resources)
    {}

    std::string mPlugin;    ///< ODC resource plugin name. Plugin has to be registered in ODC server.
    std::string mResources; ///< Parsable description of the requested resources.

    std::string_view name() const override { return "Submit"; }

    friend std::ostream& operator<<(std::ostream& os, const SubmitRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; plugin: "    << std::quoted(r.mPlugin)
            << "; resources: " << std::quoted(r.mResources);
    }
};

struct ActivateRequest : public Request
{
    ActivateRequest() {}
    ActivateRequest(const std::string& topoFile, const std::string& topoContent, const std::string& topoScript)
        : mTopoFile(topoFile)
        , mTopoContent(topoContent)
        , mTopoScript(topoScript)
    {}

    std::string mTopoFile;    ///< Path to the topology file
    std::string mTopoContent; ///< Content of the XML topology
    std::string mTopoScript;  ///< Script that generates topology content

    std::string_view name() const override { return "Activate"; }

    friend std::ostream& operator<<(std::ostream& os, const ActivateRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; topologyFile: "    << std::quoted(r.mTopoFile)
            << "; topologyContent: " << std::quoted(r.mTopoContent)
            << "; topologyScript: "  << std::quoted(r.mTopoScript);
    }
};

struct RunRequest : public Request
{
    RunRequest() {}
    RunRequest(const std::string& plugin,
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

    std::string_view name() const override { return "Run"; }

    friend std::ostream& operator<<(std::ostream& os, const RunRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; plugin: "               << std::quoted(r.mPlugin)
            << "; resource: "             << std::quoted(r.mResources)
            << "; topologyFile: "         << std::quoted(r.mTopoFile)
            << "; topologyContent: "      << std::quoted(r.mTopoContent)
            << "; topologyScript: "       << std::quoted(r.mTopoScript)
            << "; extractTopoResources: " << r.mExtractTopoResources;
    }
};

struct UpdateRequest : public Request
{
    UpdateRequest() {}
    UpdateRequest(const std::string& topoFile, const std::string& topoContent, const std::string& topoScript)
        : mTopoFile(topoFile)
        , mTopoContent(topoContent)
        , mTopoScript(topoScript)
    {}

    std::string mTopoFile;    ///< Path to the topology file
    std::string mTopoContent; ///< Content of the XML topology
    std::string mTopoScript;  ///< Script that generates topology content

    std::string_view name() const override { return "Update"; }

    friend std::ostream& operator<<(std::ostream& os, const UpdateRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; topologyFile: "    << std::quoted(r.mTopoFile)
            << "; topologyContent: " << std::quoted(r.mTopoContent)
            << "; topologyScript: "  << std::quoted(r.mTopoScript);
    }
};

struct GetStateRequest : public Request
{
    GetStateRequest() {}
    GetStateRequest(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    std::string_view name() const override { return "GetState"; }

    friend std::ostream& operator<<(std::ostream& os, const GetStateRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: "     << std::quoted(r.mPath)
            << "; detailed: " << r.mDetailed;
    }
};

struct SetPropertiesRequest : public Request
{
    using Prop = std::pair<std::string, std::string>;
    using Props = std::vector<Prop>;

    SetPropertiesRequest() {}
    SetPropertiesRequest(const Props& props, const std::string& path)
        : mPath(path)
        , mProperties(props)
    {}

    std::string mPath;        ///< Path in the topology
    Props mProperties; ///< List of device configuration properties

    std::string_view name() const override { return "SetProperties"; }

    friend std::ostream& operator<<(std::ostream& os, const SetPropertiesRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: " << std::quoted(r.mPath)
            << "; properties: {";
        for (const auto& v : r.mProperties) {
            os << " (" << v.first << ":" << v.second << ") ";
        }
        return os << "}";
    }
};

struct ConfigureRequest : public Request
{
    ConfigureRequest() {}
    ConfigureRequest(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    std::string_view name() const override { return "Configure"; }

    friend std::ostream& operator<<(std::ostream& os, const ConfigureRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: "     << std::quoted(r.mPath)
            << "; detailed: " << r.mDetailed;
    }
};

struct StartRequest : public Request
{
    StartRequest() {}
    StartRequest(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    std::string_view name() const override { return "Start"; }

    friend std::ostream& operator<<(std::ostream& os, const StartRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: "     << std::quoted(r.mPath)
            << "; detailed: " << r.mDetailed;
    }
};

struct StopRequest : public Request
{
    StopRequest() {}
    StopRequest(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    std::string_view name() const override { return "Stop"; }

    friend std::ostream& operator<<(std::ostream& os, const StopRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: "     << std::quoted(r.mPath)
            << "; detailed: " << r.mDetailed;
    }
};

struct ResetRequest : public Request
{
    ResetRequest() {}
    ResetRequest(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    std::string_view name() const override { return "Reset"; }

    friend std::ostream& operator<<(std::ostream& os, const ResetRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: "     << std::quoted(r.mPath)
            << "; detailed: " << r.mDetailed;
    }
};

struct TerminateRequest : public Request
{
    TerminateRequest() {}
    TerminateRequest(const std::string& path, bool detailed)
        : mPath(path)
        , mDetailed(detailed)
    {}

    std::string mPath;      ///< Path to the topology file
    bool mDetailed = false; ///< If True than return also detailed information

    std::string_view name() const override { return "Terminate"; }

    friend std::ostream& operator<<(std::ostream& os, const TerminateRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; path: "     << std::quoted(r.mPath)
            << "; detailed: " << r.mDetailed;
    }
};

struct ShutdownRequest : public Request
{
    ShutdownRequest() {}

    std::string_view name() const override { return "Shutdown"; }

    friend std::ostream& operator<<(std::ostream& os, const ShutdownRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r);
    }
};

struct StatusRequest : public Request
{
    StatusRequest() {}
    StatusRequest(bool running)
        : mRunning(running)
    {}

    bool mRunning = false; ///< Select only running DDS sessions

    std::string_view name() const override { return "Status"; }

    friend std::ostream& operator<<(std::ostream& os, const StatusRequest& r)
    {
        return os << r.name() << " Request: " << static_cast<const Request&>(r)
            << "; running: " << r.mRunning;
    }
};

} // namespace odc::core

#endif /* defined(ODC_CORE_REQUESTS) */
