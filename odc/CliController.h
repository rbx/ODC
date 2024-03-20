/********************************************************************************
 * Copyright (C) 2019-2024 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#ifndef ODC_CLICONTROLLER
#define ODC_CLICONTROLLER

#include <odc/CliControllerHelper.h>
#include <odc/Controller.h>
#include <odc/DDSSubmit.h>

#include <chrono>
#include <sstream>

namespace odc {

class CliController : public odc::core::CliControllerHelper<CliController>
{
  public:
    CliController() {}

    void setTimeout(const std::chrono::seconds& timeout) { mCtrl.setTimeout(timeout); }
    void setHistoryDir(const std::string& dir) { mCtrl.setHistoryDir(dir); }
    void setZoneCfgs(const std::vector<std::string>& zonesStr) { mCtrl.setZoneCfgs(zonesStr); }
    void setRMS(const std::string& rms) { mCtrl.setRMS(rms); }

    void registerResourcePlugins(const core::PluginManager::PluginMap& pluginMap) { mCtrl.registerResourcePlugins(pluginMap); }
    void restore(const std::string& restoreId, const std::string& restoreDir) { mCtrl.restore(restoreId, restoreDir); }

    std::string requestInitialize(const core::InitializeRequest& req)       { return generalReply(mCtrl.exec(req)); }
    std::string requestSubmit(const core::SubmitRequest& req)               { return generalReply(mCtrl.exec(req)); }
    std::string requestActivate(const core::ActivateRequest& req)           { return generalReply(mCtrl.exec(req)); }
    std::string requestRun(const core::RunRequest& req)                     { return generalReply(mCtrl.exec(req)); }
    std::string requestUpdate(const core::UpdateRequest& req)               { return generalReply(mCtrl.exec(req)); }
    std::string requestSetProperties(const core::SetPropertiesRequest& req) { return generalReply(mCtrl.exec(req)); }
    std::string requestGetState(const core::GetStateRequest& req)           { return generalReply(mCtrl.exec(req)); }
    std::string requestConfigure(const core::ConfigureRequest& req)         { return generalReply(mCtrl.exec(req)); }
    std::string requestStart(const core::StartRequest& req)                 { return generalReply(mCtrl.exec(req)); }
    std::string requestStop(const core::StopRequest& req)                   { return generalReply(mCtrl.exec(req)); }
    std::string requestReset(const core::ResetRequest& req)                 { return generalReply(mCtrl.exec(req)); }
    std::string requestTerminate(const core::TerminateRequest& req)         { return generalReply(mCtrl.exec(req)); }
    std::string requestShutdown(const core::ShutdownRequest& req)           { return generalReply(mCtrl.exec(req)); }
    std::string requestStatus(const core::StatusRequest& req)               { return statusReply(mCtrl.exec(req)); }

  private:
    std::string generalReply(const core::RequestResult& result)
    {
        std::stringstream ss;

        if (result.mStatusCode == core::StatusCode::ok) {
            ss << "  Status code: SUCCESS\n  Message: " << result.mMsg << "\n";
        } else {
            ss << "  Status code: ERROR\n  Error code: " << result.mError.mCode.value()
               << "\n  Error message: " << result.mError.mCode.message() << " ("
               << result.mError.mDetails << ")\n";
        }

        ss << "  Aggregated state: " << result.mTopologyState.aggregated << "\n";
        ss << "  Partition ID: "     << result.mPartitionID << "\n";
        ss << "  Run Nr: "           << result.mRunNr << "\n";
        ss << "  Session ID: "       << result.mDDSSessionID << "\n";
        if (!result.mHosts.empty()) {
            ss << "  Hosts:\n    ";
            size_t i = 0;
            for (const auto& host : result.mHosts) {
                ss << host << (i == (result.mHosts.size() - 1) ? "\n" : ", ");
                ++i;
            }
        }

        if (result.mTopologyState.detailed.has_value()) {
            ss << "\n  Devices:\n";
            for (const auto& state : result.mTopologyState.detailed.value()) {
                ss << "    ID: "       << state.mStatus.taskId
                   << "; path: "       << state.mPath
                   << "; state: "      << state.mStatus.state
                   << "; ignored: "    << state.mStatus.ignored
                   << "; expendable: " << state.mStatus.expendable
                   << "; host: "       << state.mHost
                   << "\n";
            }
            ss << "\n";
        }

        ss << "  Execution time: " << result.mExecTime << " msec\n";

        return ss.str();
    }

    std::string statusReply(const core::RequestResult& result)
    {
        std::stringstream ss;
        if (result.mStatusCode == core::StatusCode::ok) {
            ss << "  Status code: SUCCESS\n  Message: " << result.mMsg << "\n";
        } else {
            ss << "  Status code: ERROR\n  Error code: " << result.mError.mCode.value()
               << "\n  Error message: " << result.mError.mCode.message() << " ("
               << result.mError.mDetails << ")\n";
        }
        ss << "  Partitions:\n";
        for (const auto& p : result.mPartitions) {
            ss << "    ID: " << p.mPartitionID
               << "; session ID: " << p.mDDSSessionID
               << "; status: " << ((p.mDDSSessionStatus == core::DDSSessionStatus::running) ? "RUNNING" : "STOPPED")
               << "; state: " << core::GetAggregatedStateName(p.mAggregatedState) << "\n";
        }
        ss << "  Execution time: " << result.mExecTime << " msec\n";
        return ss.str();
    }

  private:
    core::Controller mCtrl; ///< Core ODC service
};

} // namespace odc::cli

#endif // ODC_CLICONTROLLER
