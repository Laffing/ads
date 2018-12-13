#ifndef GETBLOCKHANDLER_H
#define GETBLOCKHANDLER_H

#include <memory>
#include <boost/asio.hpp>
#include "abstraction/interfaces.h"
#include "commandhandler.h"
#include "command/getblock.h"

class office;

class GetBlockHandler : public CommandHandler {
  public:
    GetBlockHandler(office& office, client& client);

    //ICommandHandler interface
    virtual void onInit(std::unique_ptr<IBlockCommand> command) override;
    virtual void onExecute() override;
    virtual void onValidate() override;

  private:
    std::unique_ptr<GetBlock>  m_command;
};

#endif // GETBLOCKHANDLER_H
