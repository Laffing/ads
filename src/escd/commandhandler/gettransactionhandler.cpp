#include "gettransactionhandler.h"
#include "command/gettransaction.h"
#include "../office.hpp"
#include "helper/hash.h"
#include "helper/servers.h"
#include "servers.hpp"

GetTransactionHandler::GetTransactionHandler(office& office, boost::asio::ip::tcp::socket& socket)
    : CommandHandler(office, socket) {
}

void GetTransactionHandler::onInit(std::unique_ptr<IBlockCommand> command) {
    m_command = init<GetTransaction>(std::move(command));
}

void GetTransactionHandler::onExecute() {
    assert(m_command);
    auto errorCode = ErrorCodes::Code::eNone;
    std::vector<hash_s> hashes;
    GetTransactionResponse res;
    uint32_t mnum;

    message_ptr msg(new message());
    msg->hash.dat[1] = MSGTYPE_MSG; //prevent assert in hash_tree_get()
    msg->svid = m_command->getDestinationNode();
    msg->msid = m_command->getNodeMsgId();
    msg->load_path();
    if (!msg->path || msg->path > m_offi.last_path()) {
        errorCode = ErrorCodes::Code::eFailToProvideTxnInfo;
    } else if (!msg->hash_tree_get(m_command->getPosition(), hashes, mnum)) {
        errorCode = ErrorCodes::Code::eFailToReadTxnInfo;
    } else  {
        //Helper::Servers servers;
        //servers.setNow(msg->path);
        servers srvs_;
        srvs_.now=msg->path;
        //if (!servers.getMsglHashTree(msg->svid, msg->msid, mnum, hashes)) {
        if(!srvs_.msgl_hash_tree_get(msg->svid,msg->msid,mnum,hashes))
        {
            errorCode = ErrorCodes::Code::eFailToGetHashTree;
        }
        else {
            res.path=msg->path;
            res.msid=msg->msid;
            res.node=msg->svid;
            res.tnum=m_command->getPosition();
            res.len=msg->len;
            res.hnum=(uint16_t)hashes.size();
        }
    }

    try {
        std::vector<boost::asio::const_buffer> response;
        response.emplace_back(boost::asio::buffer(&errorCode, ERROR_CODE_LENGTH));
        if (!errorCode) {
            response.emplace_back(boost::asio::buffer(&res, sizeof(GetTransactionResponse)));
            response.emplace_back(boost::asio::buffer(msg->data, msg->len));
            for (auto &it : hashes) {
                response.emplace_back(boost::asio::buffer(it.hash, sizeof(it.hash)));
            }
        }
        boost::asio::write(m_socket, response);
    } catch (std::exception& e) {
        DLOG("Responding to client %08X error: %s\n", m_command->getUserId(), e.what());
    }
}

void GetTransactionHandler::onValidate() {
}
