/********************************************************************************
 * Copyright (C) 2019-2024 GSI Helmholtzzentrum fuer Schwerionenforschung GmbH  *
 *                                                                              *
 *              This software is distributed under the terms of the             *
 *              GNU Lesser General Public Licence (LGPL) version 3,             *
 *                  copied verbatim in the file "LICENSE"                       *
 ********************************************************************************/

#ifndef __ODC__CliHelper__
#define __ODC__CliHelper__

#include <odc/BuildConstants.h>
#include <odc/Logger.h>
#include <odc/Requests.h>
#include <odc/PluginManager.h>

#include <boost/algorithm/string.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace odc::core
{

class CmdsFile
{
  public:
    static std::vector<std::string> getCmds(const std::string& filepath)
    {
        std::ifstream fs(filepath, std::ifstream::in);
        if (!fs) {
            std::stringstream ss;
            ss << "Failed to open commands file " << std::quoted(filepath);
            throw std::runtime_error(ss.str());
        }

        std::vector<std::string> cmds;

        std::string line;
        while (getline(fs, line)) {
            if (line.length() > 0) {
                cmds.push_back(line);
            }
        }
        return cmds;
    }
};

class CliHelper
{
  public:
    struct BatchOptions
    {
        std::vector<std::string> mCmds;
        std::string mCmdsFilepath;
        std::vector<std::string> mOutputCmds; ///> Filled after parseOptions
    };

    struct SleepOptions
    {
        size_t mMs; ///> Sleep time in milliseconds
    };

    /// \brief Fills BatchOptions::m_outputCmds
    static void batchCmds(const boost::program_options::variables_map& vm, bool batch, BatchOptions& batchOptions)
    {
        if (vm.count("cmds") && vm.count("cf")) {
            throw std::runtime_error("Only --cmds OR --cf can be specified at the same time, not both.");
        }
        if (batch) {
            if (vm.count("cmds")) {
                batchOptions.mOutputCmds = batchOptions.mCmds;
            } else if (vm.count("cf")) {
                batchOptions.mOutputCmds = CmdsFile::getCmds(batchOptions.mCmdsFilepath);
            }
        } else {
            batchOptions.mOutputCmds = std::vector<std::string>();
        }
    }

    // Generic options

    static void addLogOptions(boost::program_options::options_description& options, Logger::Config& config)
    {
        using namespace boost::program_options;
        std::string defaultLogDir{ smart_path(std::string("$HOME/.ODC/log")) };
        options.add_options()
            ("logdir", value<std::string>(&config.mLogDir)->default_value(defaultLogDir), "Log files directory")
            ("severity", value<ESeverity>(&config.mSeverity)->default_value(ESeverity::info), "Log severity level (trc/dbg/inf/wrn/err/fat)")
            ("infologger", bool_switch(&config.mInfologger)->default_value(false), "Enable InfoLogger (ODC needs to be compiled with InfoLogger support)")
            ("infologger-severity", value<ESeverity>(&config.mInfologgerSeverity)->default_value(ESeverity::info), "Log severity level for InfoLogger (dbg/inf/wrn/err/fat)")
            ("infologger-system", value<std::string>(&config.mInfologgerSystem)->default_value("ODC"), "Fills the InfoLogger 'System' field")
            ("infologger-facility", value<std::string>(&config.mInfologgerFacility)->default_value("ODC"), "Fills the InfoLogger 'Facility' field")
            ("infologger-role", value<std::string>(&config.mInfologgerRole)->default_value("production"), "Fills the InfoLogger 'Role' field");
    }

    static void addOptions(boost::program_options::options_description& options, BatchOptions& batchOptions)
    {
        options.add_options()
            ("cmds", boost::program_options::value<std::vector<std::string>>(&batchOptions.mCmds)->multitoken(), "Array of command to be executed in batch mode")
            ("cf", boost::program_options::value<std::string>(&batchOptions.mCmdsFilepath), "Config file containing an array of command to be executed in batch mode");
    }

    static void addBatchOptions(boost::program_options::options_description& options, BatchOptions& batchOptions, bool& batch)
    {
        options.add_options()
            ("batch", boost::program_options::bool_switch(&batch)->default_value(false), "Non interactive batch mode");
        addOptions(options, batchOptions);
    }

    static void addOptions(boost::program_options::options_description& options, SleepOptions& sleepOptions)
    {
        options.add_options()
            ("ms", boost::program_options::value<size_t>(&sleepOptions.mMs)->default_value(1000), "Sleep time in ms");
    }

    // Request specific options

    template<typename Request>
    static void addCommonOptions(boost::program_options::options_description& options, Request& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("id", value<std::string>(&request.mCommonParams.mPartitionID)->default_value(""), "Partition ID")
            ("run", value<uint64_t>(&request.mCommonParams.mRunNr)->default_value(0), "Run Nr")
            ("timeout", value<size_t>(&request.mCommonParams.mTimeout)->default_value(0), "Request timeout");
    }

    static void addCommonOptions(boost::program_options::options_description& /* options */, BatchOptions& /* request */)
    {
        // Nothing to do for BatchOptions
    }

    static void addCommonOptions(boost::program_options::options_description& /* options */, SleepOptions& /* request */)
    {
        // Nothing to do for SleepOptions
    }

    static void addOptions(boost::program_options::options_description& options, InitializeRequest& request)
    {
        options.add_options()
            ("sid", boost::program_options::value<std::string>(&request.mDDSSessionID)->default_value(""), "DDS session ID");
    }

    static void addOptions(boost::program_options::options_description& options, SubmitRequest& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("plugin,p", value<std::string>(&request.mPlugin), "ODC resource plugin name.")
            ("resources,r", value<std::string>(&request.mResources), "A resource description for a corresponding ODC resource plugin.");
    }

    static void addOptions(boost::program_options::options_description& options, ActivateRequest& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("topo", value<std::string>(&request.mTopoFile)->implicit_value(""), "Topology filepath")
            ("content", value<std::string>(&request.mTopoContent)->implicit_value(""), "Topology content")
            ("script", value<std::string>(&request.mTopoScript)->implicit_value(""), "Topology script");
    }

    static void addOptions(boost::program_options::options_description& options, RunRequest& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("plugin,p", value<std::string>(&request.mPlugin), "ODC resource plugin name.")
            ("resources,r", value<std::string>(&request.mResources), "A resource description for a corresponding ODC resource plugin.")
            ("topo", value<std::string>(&request.mTopoFile)->implicit_value(""), "Topology filepath")
            ("content", value<std::string>(&request.mTopoContent)->implicit_value(""), "Topology content")
            ("script", value<std::string>(&request.mTopoScript)->implicit_value(""), "Topology script")
            ("extract-topo-resources", bool_switch(&request.mExtractTopoResources)->default_value(false), "Extract required resources from the topology file (plugin & resources fields are ignored)");
    }

    static void addOptions(boost::program_options::options_description& options, UpdateRequest& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("topo", value<std::string>(&request.mTopoFile), "Topology filepath")
            ("content", value<std::string>(&request.mTopoContent), "Topology content")
            ("script", value<std::string>(&request.mTopoScript)->implicit_value(""), "Topology script");
    }

    static void addOptions(boost::program_options::options_description& options, SetPropertiesRequest& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("prop", value<std::vector<std::string>>()->multitoken(), "Key-value pairs for a set properties request ( key1:value1 key2:value2 )")
            ("path", value<std::string>(&request.mPath)->default_value(""), "Path for a set property request");
    }

    static void addOptions(boost::program_options::options_description& options, GetStateRequest& request)  { addDeviceOptions(options, request); }
    static void addOptions(boost::program_options::options_description& options, ConfigureRequest& request) { addDeviceOptions(options, request); }
    static void addOptions(boost::program_options::options_description& options, StartRequest& request)     { addDeviceOptions(options, request); }
    static void addOptions(boost::program_options::options_description& options, StopRequest& request)      { addDeviceOptions(options, request); }
    static void addOptions(boost::program_options::options_description& options, ResetRequest& request)     { addDeviceOptions(options, request); }
    static void addOptions(boost::program_options::options_description& options, TerminateRequest& request) { addDeviceOptions(options, request); }

    static void addOptions(boost::program_options::options_description& /* options */, ShutdownRequest& /* request */)
    {
        // No options for the shutdown request
    }

    // Requests with path + detailed options
    template<typename Request>
    static void addDeviceOptions(boost::program_options::options_description& options, Request& request)
    {
        using namespace boost::program_options;
        options.add_options()
            ("path", value<std::string>(&request.mPath)->default_value(""), "Topology path of devices")
            ("detailed", bool_switch(&request.mDetailed)->default_value(false), "Detailed reply of devices");
    }

    static void addOptions(boost::program_options::options_description& options, StatusRequest& request)
    {
        options.add_options()
            ("running", boost::program_options::bool_switch(&request.mRunning)->default_value(false), "Select only running sessions");
    }

    // Options parsing

    static void parsePluginMapOptions(const boost::program_options::variables_map& vm, PluginManager::PluginMap& pluginMap, const std::string& option)
    {
        if (vm.count(option)) {
            const auto& kvp(vm[option].as<std::vector<std::string>>());
            pluginMap.clear();
            for (const auto& v : kvp) {
                const auto idx = v.find_first_of(':');
                if (idx != std::string::npos) {
                    pluginMap.insert(std::make_pair(v.substr(0, idx), v.substr(idx + 1)));
                } else {
                    throw std::runtime_error(toString("Wrong plugin map format for string ", std::quoted(v), ". Use ", std::quoted("name:value")));
                }
            }
        } else {
            pluginMap.clear();
        }
    }

    // Default implementation does nothing
    template<typename Request>
    static void parseOptions(const boost::program_options::variables_map& /*vm*/, Request&& /*req*/) {}

    static void parseOptions(const boost::program_options::variables_map& vm, SetPropertiesRequest& request)
    {
        if (vm.count("prop")) {
            const auto& kvp(vm["prop"].as<std::vector<std::string>>());
            SetPropertiesRequest::Props props;
            for (const auto& v : kvp) {
                std::vector<std::string> strs;
                boost::split(strs, v, boost::is_any_of(":"));
                if (strs.size() == 2) {
                    props.push_back({ strs[0], strs[1] });
                } else {
                    throw std::runtime_error("Wrong property format for string '" + v + "'. Use 'key:value'.");
                }
            }
            request.mProperties = props;
        }
    }

    static void parseOptions(const boost::program_options::variables_map& vm, BatchOptions& params) { batchCmds(vm, true, params); }
};

} // namespace odc::core

#endif /* defined(__ODC__CliHelper__) */
