/********************************************************************************
 * Copyright (C) 2019-2024 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#ifndef ODC_CORE_CONTROLLER
#define ODC_CORE_CONTROLLER

#include <odc/DDSSubmit.h>
#include <odc/Requests.h>
#include <odc/Session.h>
#include <odc/Topology.h>

#include <dds/Tools.h>
#include <dds/Topology.h>

#include <boost/uuid/uuid_io.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace odc::core
{

struct Partition
{
    Partition(const std::string& id)
        : mID(id)
    {}

    std::string mID;
    std::unique_ptr<Session> mSession = nullptr;
    std::unique_ptr<Topology> mTopology = nullptr;
};

class Controller
{
  public:
    Controller() {}
    // Disable copy constructors and assignment operators
    Controller(const Controller&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(const Controller&) = delete;
    Controller& operator=(Controller&&) = delete;

    /// \brief Set timeout of requests
    /// \param [in] timeout Timeout in seconds
    void setTimeout(const std::chrono::seconds& timeout) { mTimeout = timeout; }

    /// \brief Register resource plugins
    /// \param [in] pluginMap Map of plugin name to path
    void registerResourcePlugins(const PluginManager::PluginMap& pluginMap);

    /// \brief Restore sessions for the specified ID
    ///  The function has to be called before the service start accepting request.
    /// \param [in] id ID of the restore file
    /// \param [in] dir directory to store restore files in
    void restore(const std::string& id, const std::string& dir);

    /// \brief Set directory where history file is stored
    /// \param [in] dir directory path
    void setHistoryDir(const std::string& dir) { mHistoryDir = dir; }

    /// \brief Set zone configs
    /// \param [in] zonesStr string representations of zone configs: "<name>:<cfgFilePath>:<envFilePath>"
    void setZoneCfgs(const std::vector<std::string>& zonesStr);

    /// \brief Set resource management system to be used by DDS
    /// \param [in] rms name of the RMS
    void setRMS(const std::string& rms) { mRMS = rms; }

    // DDS topology and session requests

    template<typename R>
    RequestResult execWrapper(R& request)
    {
        RequestResult result;

        odc::core::Partition& partition = acquirePartition(request.mCommon);

        try {
            result = exec(request, partition);
        } catch (const odc::core::Error& e) {
            OLOG(error) << "Exception reached top of the " << request.name() << " request: odc::core::Error: " << e;
            result.mError = e;
        } catch (const odc::core::RuntimeError& re) {
            OLOG(fatal) << "Exception reached top of the " << request.name() << " request: odc::core::RuntimeError: " << re.what();
            result.mError = Error(MakeErrorCode(ErrorCode::RuntimeError), re.what());
        } catch (const std::exception& e) {
            OLOG(fatal) << "Exception reached top of the " << request.name() << " request: std::exception: " << e.what();
            result.mError = Error(MakeErrorCode(ErrorCode::RuntimeError), e.what());
        } catch (...) {
            OLOG(fatal) << "Exception reached top of the " << request.name() << " request: unknown exception";
            result.mError = Error(MakeErrorCode(ErrorCode::RuntimeError), "unknown exception");
        }

        result.mExecTime = request.mTimer.duration().count();
        return result;
    }

    /// \brief Initialize DDS session
    RequestResult exec(const InitializeRequest& request, Partition& partition);
    /// \brief Submit DDS agents. Can be called multiple times in order to submit more agents.
    RequestResult exec(const SubmitRequest& request, Partition& partition);
    /// \brief Activate topology
    RequestResult exec(const ActivateRequest& request, Partition& partition);
    /// \brief Run request combines Initialize, Submit and Activate
    RequestResult exec(const RunRequest& request, Partition& partition);
    /// \brief Update topology. Can be called multiple times in order to update topology.
    RequestResult exec(const UpdateRequest& request, Partition& partition);
    /// \brief Set properties
    RequestResult exec(const SetPropertiesRequest& request, Partition& partition);

    /// \brief Get state
    RequestResult exec(const GetStateRequest& request, Partition& partition);

    // change state requests

    /// \brief Configure devices: InitDevice->CompleteInit->Bind->Connect->InitTask
    RequestResult exec(const ConfigureRequest& request, Partition& partition);
    /// \brief Start devices: Run
    RequestResult exec(const StartRequest& request, Partition& partition);
    /// \brief Stop devices: Stop
    RequestResult exec(const StopRequest& request, Partition& partition);
    /// \brief Reset devices: ResetTask->ResetDevice
    RequestResult exec(const ResetRequest& request, Partition& partition);
    /// \brief Terminate devices: End
    RequestResult exec(const TerminateRequest& request, Partition& partition);
    /// \brief Shutdown DDS session
    RequestResult exec(const ShutdownRequest& request, Partition& partition);

    /// \brief Status request
    RequestResult exec(const StatusRequest& request);

    static void extractRequirements(const CommonParams& common, Session& session);

  private:
    std::map<std::string, Partition> mPartitions; ///< Map of partition ID to Partition object
    std::mutex mPartitionMtx;                     ///< Mutex for the partition map
    std::chrono::seconds mTimeout{ 30 };          ///< Request timeout in sec
    DDSSubmit mSubmit;                            ///< ODC to DDS submit resource converter
    std::string mRestoreId;                       ///< Restore ID
    std::string mRestoreDir;                      ///< Restore file directory
    std::string mHistoryDir;                      ///< History file directory
    std::map<std::string, ZoneConfig> mZoneCfgs;  ///< stores zones configuration (cfgFilePath/envFilePath) by zone name
    std::string mRMS{ "localhost" };              ///< resource management system to be used by DDS

    void updateRestore();
    void updateHistory(const CommonParams& common, const std::string& sessionId);

    template<typename R>
    std::unordered_set<std::string> submit(const R& req, Session& session, bool extractResources);
    template<typename R>
    void activate(const R& req, Partition& partition);

    bool createDDSSession(           const CommonParams& common, Session& session, Error& error);
    bool attachToDDSSession(         const CommonParams& common, Session& session, Error& error, const std::string& sessionID);
    bool shutdownDDSSession(         const CommonParams& common, Partition& partition, Error& error);
    std::string getActiveDDSTopology(const InitializeRequest& req, Session& session);

    template<typename R>
    bool submitDDSAgents(      const R& req, Session& session, const DDSSubmitParams& params);
    template<typename R>
    bool waitForNumActiveSlots(const R& req, Session& session, Error& error, size_t numSlots);
    template<typename R>
    void ShutdownDDSAgent(     const R& req, Session& session, uint64_t agentID);

    template<typename R>
    bool activateDDSTopology(const R& req, Session& session, dds::tools_api::STopologyRequest::request_t::EUpdateType updateType);
    bool createDDSTopology(  const CommonParams& common, Session& session, Error& error);

    bool createTopology(const CommonParams& common, Partition& partition, Error& error);
    bool resetTopology(Partition& partition);

    template<typename R>
    bool changeState(         const R& req, Partition& partition, const std::string& path, TopoTransition transition, TopologyState& topologyState);
    template<typename R>
    bool changeStateConfigure(const R& req, Partition& partition, const std::string& path, TopologyState& topologyState);
    template<typename R>
    bool changeStateReset(    const R& req, Partition& partition, const std::string& path, TopologyState& topologyState);
    template<typename R>
    bool waitForState(        const R& req, Partition& partition, const std::string& path, DeviceState expState);
    bool setProperties(       const SetPropertiesRequest& req, Partition& partition, TopologyState& topologyState);
    void getState(            const GetStateRequest& req, Partition& partition, TopologyState& state);

    void fillAndLogError(               const CommonParams& common, Error& error, ErrorCode errorCode, const std::string& msg);
    void fillAndLogFatalError(          const CommonParams& common, Error& error, ErrorCode errorCode, const std::string& msg);
    void fillAndLogFatalErrorLineByLine(const CommonParams& common, Error& error, ErrorCode errorCode, const std::string& msg);
    void logFatalLineByLine(            const CommonParams& common, const std::string& msg);
    void logLineWarningOrDetectedSev(   const CommonParams& common, const std::string& line);

    template<typename R>
    RequestResult createRequestResult(const R& req, const std::string& sessionId, const Error& error, const std::string& msg, TopologyState&& topologyState, const std::unordered_set<std::string>& hosts);
    AggregatedState aggregateStateForPath(const dds::topology_api::CTopology* ddsTopo, const TopoState& topoState, const std::string& path);

    Partition& acquirePartition(const CommonParams& common);
    void removePartition(const CommonParams& common);

    void stateSummaryOnFailure(const CommonParams& common, Session& session, const TopoState& topoState, DeviceState expectedState);
    template<typename R>
    void attemptSubmitRecovery(const R& req, Session& session, Error& error, const std::vector<DDSSubmitParams>& ddsParams, const std::map<std::string, uint32_t>& agentCounts);
    void updateTopology(const CommonParams& common, Session& session);

    template<typename R>
    std::string topoFilepath(const R& req, const std::string& topologyFile, const std::string& topologyContent, const std::string& topologyScript);

    template<typename R>
    std::chrono::seconds requestTimeout(const R& req, const std::string& op) const
    {
        static_assert(std::is_base_of<odc::core::Request, R>::value, "R must be derived from odc::core::Request.");
        std::chrono::seconds configuredTimeoutS = (req.mCommon.mTimeout == 0 ? mTimeout : std::chrono::seconds(req.mCommon.mTimeout));
        std::chrono::milliseconds configuredTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(configuredTimeoutS);
        // subtract time elapsed since the beginning of the request
        std::chrono::milliseconds realTimeoutMs = configuredTimeoutMs - req.mTimer.duration();
        OLOGR(debug, req) << op << ": configured request timeout: " << configuredTimeoutMs.count() << "ms "
            << (req.mCommon.mTimeout == 0 ? "(controller default)" : "(request parameter)")
            << ", remaining time: " << realTimeoutMs.count() << "ms";
        if (realTimeoutMs.count() < 0) {
            throw Error(MakeErrorCode(ErrorCode::RequestTimeout), toString("Request timeout. Remaining time is: ", realTimeoutMs.count(), "ms"));
        }
        return std::chrono::duration_cast<std::chrono::seconds>(realTimeoutMs);
    }

    template<typename R>
    uint32_t getNumSlots(const R& req, Session& session) const;
    template<typename R>
    dds::tools_api::SAgentInfoRequest::responseVector_t getAgentInfo(const R& req, Session& session) const;

    std::string getSessionIDStr(const Partition& partition)
    {
        return boost::uuids::to_string(partition.mSession->mDDSSession.getSessionID());
    }

    void printStateStats(const CommonParams& common, const TopoState& topoState, bool debugLog = false);
};

} // namespace odc::core

#endif /* defined(ODC_CORE_CONTROLLER) */
