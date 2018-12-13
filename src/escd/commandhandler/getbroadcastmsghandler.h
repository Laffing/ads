#ifndef GETBROADCASTMSGHANDLER_H
#define GETBROADCASTMSGHANDLER_H

#include <memory>
#include <boost/asio.hpp>
#include "abstraction/interfaces.h"
#include "commandhandler.h"
#include "command/getbroadcastmsg.h"

class office;

class GetBroadcastMsgHandler : public CommandHandler {
  public:
    GetBroadcastMsgHandler(office& office, client& client);

    //ICommandHandler interface
    virtual void onInit(std::unique_ptr<IBlockCommand> command) override;
    virtual void onExecute() override;
    virtual void onValidate() override;

  private:
    std::unique_ptr<GetBroadcastMsg>  m_command;
};


#endif // GETBROADCASTMSGHANDLER_H
