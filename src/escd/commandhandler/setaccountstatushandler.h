#ifndef SETACCOUNTSTATUSHANDLER_H
#define SETACCOUNTSTATUSHANDLER_H

#include <memory>
#include <boost/asio.hpp>
#include "abstraction/interfaces.h"
#include "commandhandler.h"
#include "command/setaccountstatus.h"

class office;

class SetAccountStatusHandler : public CommandHandler {
  public:
    SetAccountStatusHandler(office& office, client& client);

    virtual void onInit(std::unique_ptr<IBlockCommand> command) override;
    virtual void onExecute()  override;
    virtual void onValidate() override;

  private:
    std::unique_ptr<SetAccountStatus>  m_command;
};


#endif // SETACCOUNTSTATUSHANDLER_H
