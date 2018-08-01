#include <iostream>
#include <boost/property_tree/json_parser.hpp>
#include "user.hpp"
#include "settings.hpp"
#include "networkclient.h"
#include "responsehandler.h"
#include "helper/ascii.h"
#include "helper/hash.h"
#include "helper/json.h"

using namespace std;


ErrorCodes::Code talk(NetworkClient& netClient, settings sts, ResponseHandler& respHandler, std::unique_ptr<IBlockCommand> command) {
    if(sts.drun && command->getCommandType() == CommandType::eModifying) {
      respHandler.onDryRun(std::move(command));
      return ErrorCodes::Code::eNone;
    }

    if(!netClient.isConnected()) {
        if(!netClient.connect()) {
            ELOG("Error: %s", ErrorCodes().getErrorMsg(ErrorCodes::Code::eConnectServerError));
            return ErrorCodes::Code::eConnectServerError;
        }
        int32_t version = CLIENT_PROTOCOL_VERSION;
        int32_t node_version;
#ifdef DEBUG
        version = -version;
#endif
        netClient.sendData(reinterpret_cast<unsigned char*>(&version), sizeof(version));

        if(!netClient.readData(&node_version, sizeof(node_version))) {
            netClient.disConnect();
            return ErrorCodes::Code::eConnectServerError;
        }

        uint8_t version_error;
        if(!netClient.readData(&version_error, 1) || version_error) {
            ELOG("Version mismatch client(%d) != node(%d)\n", version, node_version);
            netClient.disConnect();
            return ErrorCodes::Code::eProtocolMismatch;
        }
    }

    if(command->send(netClient) ) {
        respHandler.onExecute(std::move(command));
    } else {
        ELOG("ERROR reading global info talk\n");
        netClient.disConnect();
        return command->m_responseError ? command->m_responseError : ErrorCodes::Code::eConnectServerError;
    }

    return ErrorCodes::Code::eNone;
}

int main(int argc, char* argv[]) {

#ifndef NDEBUG
    std::setbuf(stdout,NULL);
#endif

    auto workdir = settings::get_workdir(argc, argv, false);
    if(workdir != ".") {
        settings::change_working_dir(workdir);
    }

    settings sts;
    sts.get(argc,argv);
    NetworkClient netClient(sts.host, std::to_string(sts.port), sts.nice);
    ResponseHandler respHandler(sts);

#if INTPTR_MAX == INT64_MAX
    assert(sizeof(long double)==16);
#endif
    try {
        std::string line;

        while (std::getline(std::cin, line)) {
            DLOG("GOT REQUEST %s\n", line.c_str());

            ErrorCodes::Code responseError = ErrorCodes::Code::eNone;

            if(line.at(0) == '.') {
                break;
            }
            if(line.at(0) == '\n' || line.at(0) == '#') {
                continue;
            }

            std::string json_run;
            auto command = run_json(sts, line, json_run);

            if(command) {
                if(json_run == "decode_raw") {
                    boost::property_tree::ptree pt;
                    command->txnToJson(pt);
                    boost::property_tree::write_json(std::cout, pt, sts.nice);
                } else {
                    responseError = talk(netClient, sts, respHandler, std::move(command));
                }
            }
            else {
                responseError = ErrorCodes::Code::eCommandParseError;
            }

            if(responseError)
            {
                boost::property_tree::ptree pt;
                pt.put(ERROR_TAG, ErrorCodes().getErrorMsg(responseError));
                boost::property_tree::write_json(std::cout, pt, sts.nice);
            }

        }
    } catch (std::exception& e) {
        ELOG("Main Exception: %s\n", e.what());
        Helper::printErrorJson("Unknown error occured", sts.nice);
    }

    netClient.disConnect();

    return 0;
}
