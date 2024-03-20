/********************************************************************************
 * Copyright (C) 2019-2024 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#ifndef ODC_GRPCCLIENT
#define ODC_GRPCCLIENT

#include <odc/CliControllerHelper.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <grpcpp/grpcpp.h>
#include <odc/grpc/odc.grpc.pb.h>

class GrpcClient : public odc::core::CliControllerHelper<GrpcClient>
{
  public:
    GrpcClient(std::shared_ptr<grpc::Channel> channel)
        : mStub(odc::ODC::NewStub(channel))
    {}

    std::string requestInitialize(const odc::core::InitializeRequest& initRequest)
    {
        odc::InitializeRequest grpcRequest;
        updateCommonParams(initRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_sessionid(initRequest.mDDSSessionID);

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Initialize(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestSubmit(const odc::core::SubmitRequest& submitRequest)
    {
        odc::SubmitRequest grpcRequest;
        updateCommonParams(submitRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_plugin(submitRequest.mPlugin);
        grpcRequest.set_resources(submitRequest.mResources);

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Submit(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestActivate(const odc::core::ActivateRequest& activateRequest)
    {
        odc::ActivateRequest grpcRequest;
        updateCommonParams(activateRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_topology(activateRequest.mTopoFile);
        grpcRequest.set_content(activateRequest.mTopoContent);
        grpcRequest.set_script(activateRequest.mTopoScript);

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Activate(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestRun(const odc::core::RunRequest& runRequest)
    {
        odc::RunRequest grpcRequest;
        updateCommonParams(runRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_plugin(runRequest.mPlugin);
        grpcRequest.set_resources(runRequest.mResources);
        grpcRequest.set_topology(runRequest.mTopoFile);
        grpcRequest.set_content(runRequest.mTopoContent);
        grpcRequest.set_script(runRequest.mTopoScript);
        grpcRequest.set_extracttoporesources(runRequest.mExtractTopoResources);

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Run(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestUpdate(const odc::core::UpdateRequest& updateRequest)
    {
        odc::UpdateRequest grpcRequest;
        updateCommonParams(updateRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_topology(updateRequest.mTopoFile);

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Update(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestSetProperties(const odc::core::SetPropertiesRequest& setPropsRequest)
    {
        odc::SetPropertiesRequest grpcRequest;
        updateCommonParams(setPropsRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_path(setPropsRequest.mPath);
        for (const auto& v : setPropsRequest.mProperties) {
            auto prop = grpcRequest.add_properties();
            prop->set_key(v.first);
            prop->set_value(v.second);
        }

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->SetProperties(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestGetState(const odc::core::GetStateRequest& getStateRequest)
    {
        odc::StateRequest grpcRequest;
        updateCommonParams(getStateRequest.mCommonParams, &grpcRequest);
        grpcRequest.set_path(getStateRequest.mPath);
        grpcRequest.set_detailed(getStateRequest.mDetailed);

        odc::StateReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->GetState(&context, grpcRequest, &reply);
        return GetStateReplyString(status, reply);
    }

    std::string requestConfigure(const odc::core::ConfigureRequest& configureRequest)
    {
        return deviceRequest<odc::ConfigureRequest>(configureRequest, &odc::ODC::Stub::Configure);
    }
    std::string requestStart(const odc::core::StartRequest& startRequest)
    {
        return deviceRequest<odc::StartRequest>(startRequest, &odc::ODC::Stub::Start);
    }
    std::string requestStop(const odc::core::StopRequest& stopRequest)
    {
        return deviceRequest<odc::StopRequest>(stopRequest, &odc::ODC::Stub::Stop);
    }
    std::string requestReset(const odc::core::ResetRequest& resetRequest)
    {
        return deviceRequest<odc::ResetRequest>(resetRequest, &odc::ODC::Stub::Reset);
    }
    std::string requestTerminate(const odc::core::TerminateRequest& terminateRequest)
    {
        return deviceRequest<odc::TerminateRequest>(terminateRequest, &odc::ODC::Stub::Terminate);
    }

    std::string requestShutdown(const odc::core::ShutdownRequest& shutdownRequest)
    {
        odc::ShutdownRequest grpcRequest;
        updateCommonParams(shutdownRequest.mCommonParams, &grpcRequest);

        odc::GeneralReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Shutdown(&context, grpcRequest, &reply);
        return GetGeneralReplyString(status, reply);
    }

    std::string requestStatus(const odc::core::StatusRequest& statusRequest)
    {
        odc::StatusRequest grpcRequest;
        grpcRequest.set_running(statusRequest.mRunning);

        odc::StatusReply reply;
        grpc::ClientContext context;
        grpc::Status status = mStub->Status(&context, grpcRequest, &reply);
        return GetStatusReplyString(status, reply);
    }

  private:
    template<typename GRPCRequest, typename R, typename StubFunc>
    std::string deviceRequest(const R& request, StubFunc stubFunc)
    {
        // Protobuf message takes the ownership and deletes the object
        odc::StateRequest* stateChange = new odc::StateRequest();
        updateCommonParams(request.mCommonParams, stateChange);
        stateChange->set_path(request.mPath);
        stateChange->set_detailed(request.mDetailed);

        GRPCRequest grpcRequest;
        grpcRequest.set_allocated_request(stateChange);

        odc::StateReply reply;
        grpc::ClientContext context;
        grpc::Status status = (mStub.get()->*stubFunc)(&context, grpcRequest, &reply);
        return GetStateReplyString(status, reply);
    }

  private:
    std::string GetGeneralReplyString(const grpc::Status& status, const odc::GeneralReply& rep)
    {
        std::stringstream ss;
        if (status.ok()) {
            ss << "  msg: "            << rep.msg()
               << "; Partition ID: "   << rep.partitionid()
               << "; Run Nr.: "        << rep.runnr()
               << "; DDS Session ID: " << rep.sessionid()
               << "; topology state: " << rep.state()
               << "; execution time: " << rep.exectime() << "ms";
            if (!rep.hosts().empty()) {
                ss << "\n  Hosts:\n    ";
                for (int i = 0; i < rep.hosts().size(); ++i) {
                    ss << rep.hosts().at(i) << (i == (rep.hosts().size() - 1) ? "" : ", ");
                }
            }

            if (rep.status() == odc::ReplyStatus::ERROR) {
                ss << "; ERROR: " << rep.error().msg() << " (" << rep.error().code() << ")\n";
                return ss.str();
            } else if (rep.status() == odc::ReplyStatus::SUCCESS) {
                ss << "\n";
                return ss.str();
            } else {
                return rep.DebugString();
            }
        } else {
            ss << "  RPC failed with error code " << status.error_code() << ": " << status.error_message() << "\n";
            return ss.str();
        }
    }

    std::string GetStateReplyString(const grpc::Status& status, const odc::StateReply& rep)
    {
        std::stringstream ss;
        if (status.ok()) {
            ss << GetGeneralReplyString(status, rep.reply());
            if (!rep.devices().empty()) {
                ss << "  Devices:\n";
                for (const auto& d : rep.devices()) {
                    ss << "    id: " << d.id()
                    << "; state: "   << d.state()
                    << "; ignored: " << d.ignored()
                    << "; host: "    << d.host()
                    << "; path: "    << d.path() << "\n";
                }
            }
            return ss.str();
        } else {
            ss << "  RPC failed with error code " << status.error_code() << ": " << status.error_message() << std::endl;
            return ss.str();
        }
    }

    std::string GetStatusReplyString(const grpc::Status& status, const odc::StatusReply& rep)
    {
        std::stringstream ss;
        if (status.ok()) {
            if (rep.status() == odc::ReplyStatus::SUCCESS) {
                ss << "  msg: " << rep.msg() << "\n";
                ss << "  found " << rep.partitions().size() << " partition(s)" << (rep.partitions().size() > 0 ? ":" : "") << "\n";
                for (const auto& p : rep.partitions()) {
                    ss << "    Partition ID: " << p.partitionid()
                       << "; DDS session: " << odc::SessionStatus_Name(p.status())
                       << "; DDS session ID: " << p.sessionid()
                       << "; Run Nr.: " << p.runnr()
                       << "; topology state: " << p.state() << "\n";
                }
                ss << "  execution time: " << rep.exectime() << "ms\n";
            } else {
                ss << "Status: " << rep.DebugString();
            }
            return ss.str();
        } else {
            ss << "RPC failed with error code " << status.error_code() << ": " << status.error_message() << std::endl;
            return ss.str();
        }
    }

    template<typename Request>
    void updateCommonParams(const odc::core::CommonParams& common, Request* req)
    {
        req->set_partitionid(common.mPartitionID);
        req->set_runnr(common.mRunNr);
        req->set_timeout(common.mTimeout);
    }

  private:
    std::unique_ptr<odc::ODC::Stub> mStub;
};

#endif /* defined(ODC_GRPCCLIENT) */
