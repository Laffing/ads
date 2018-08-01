#include "getbroadcastmsghandler.h"

#include <fstream>
#include "command/getbroadcastmsg.h"
#include "../office.hpp"
#include "helper/hash.h"
#include "helper/blocks.h"
#include "helper/blockfilereader.h"


GetBroadcastMsgHandler::GetBroadcastMsgHandler(office& office, boost::asio::ip::tcp::socket& socket)
    : CommandHandler(office, socket) {
}

void GetBroadcastMsgHandler::onInit(std::unique_ptr<IBlockCommand> command) {
    m_command = init<GetBroadcastMsg>(std::move(command));
}

void GetBroadcastMsgHandler::onExecute() {
    assert(m_command);

    auto errorCode = ErrorCodes::Code::eNone;
    uint32_t blockTime = m_command->getBlockTime();
    uint32_t path, lpath, size = 0;

    lpath = m_offi.last_path();
    if(!blockTime) {
        path = lpath;
    } else {
        path = blockTime - blockTime%BLOCKSEC;
        if (path > lpath) {
            errorCode = ErrorCodes::Code::eBroadcastNotReady;
        }
    }

    std::vector<std::string> fileBuffer;
    if (!errorCode) {
        char filename[64];
        Helper::FileName::getName(filename, path, "bro.log");

        Helper::BlockFileReader broadcastFile(filename);
        if (!broadcastFile.isOpen()) {
            size = 0;
            //errorCode = ErrorCodes::Code::eNoBroadcastFile;
        } else {
            size = broadcastFile.getSize();
            int remainData = (size > MAX_BLG_SIZE) ? MAX_BLG_SIZE : size;
            while (remainData > 0) {
                int sizeOfBlock = (remainData > MESSAGE_CHUNK) ? MESSAGE_CHUNK : remainData;
                char messageBlock[sizeOfBlock];
                broadcastFile.read(messageBlock, sizeOfBlock);
                remainData -= sizeOfBlock;
                fileBuffer.emplace_back(std::string(messageBlock, sizeOfBlock));
            }
        }
    }

    try {
        boost::asio::write(m_socket, boost::asio::buffer(&errorCode, ERROR_CODE_LENGTH));
        if(!errorCode) {
            BroadcastFileHeader header{path, lpath, size};
            boost::asio::write(m_socket, boost::asio::buffer(&header, sizeof(header)));
            // message is sending in parts of max size MESSAGE_CHUNK each block
            for (auto &it : fileBuffer) {
                boost::asio::write(m_socket, boost::asio::buffer(it));
            }
        }
    } catch (std::exception& e) {
        DLOG("Responding to client %08X error: %s\n", m_command->getUserId(), e.what());
    }
}

void GetBroadcastMsgHandler::onValidate() {
}
