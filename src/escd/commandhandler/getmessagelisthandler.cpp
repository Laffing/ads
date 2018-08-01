#include "getmessagelisthandler.h"
#include "command/getmessagelist.h"
#include "../office.hpp"
#include "helper/hash.h"

GetMessageListHandler::GetMessageListHandler(office& office, boost::asio::ip::tcp::socket& socket)
    : CommandHandler(office, socket) {
}

void GetMessageListHandler::onInit(std::unique_ptr<IBlockCommand> command) {
    m_command = init<GetMessageList>(std::move(command));
}

void GetMessageListHandler::onExecute() {
    assert(m_command);

    const auto errorCode = m_command->prepareResponse();

    try {
        std::vector<boost::asio::const_buffer> response;
        response.emplace_back(boost::asio::buffer(&errorCode, ERROR_CODE_LENGTH));
        uint32_t no_of_msg = m_command->m_responseMessageList.size();
        if(!errorCode) {
            response.emplace_back(boost::asio::buffer(&no_of_msg, sizeof(no_of_msg)));
            response.emplace_back(boost::asio::buffer(m_command->m_responseTxnHash, sizeof(m_command->m_responseTxnHash)));
            response.emplace_back(boost::asio::buffer(m_command->m_responseMessageList));
        }
        boost::asio::write(m_socket, response);
    } catch (std::exception& e) {
        DLOG("Responding to client %08X error: %s\n", m_command->getUserId(), e.what());
    }
}

void GetMessageListHandler::onValidate() {
}
