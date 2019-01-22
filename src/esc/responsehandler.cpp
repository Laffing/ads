#include "responsehandler.h"
#include "command/pods.h"
#include "command/errorcodes.h"
#include "helper/json.h"
#include "helper/ascii.h"
#include "helper/hash.h"

using namespace Helper;

ResponseHandler::ResponseHandler(settings& sts)
    :m_sts(sts) {
}

void ResponseHandler::onDryRun(std::unique_ptr<IBlockCommand> command) {
    m_pt.clear();
    initLogs(command);
    boost::property_tree::write_json(std::cout, m_pt, m_sts.nice);
    m_sts.msid++;
    Helper::create256signhash(command->getSignature(), command->getSignatureSize(), m_sts.ha, m_sts.ha);
}

void ResponseHandler::onExecute(std::unique_ptr<IBlockCommand> command) {
    m_pt.clear();
    initLogs(command);

    switch(command->getType()) {
    case TXSTYPE_INF:
    case TXSTYPE_KEY:
    case TXSTYPE_BNK:
    case TXSTYPE_PUT:
    case TXSTYPE_MPT:
    case TXSTYPE_USR:
    case TXSTYPE_NOD:
    case TXSTYPE_BRO:
    case TXSTYPE_BLG:
    case TXSTYPE_BKY:
    case TXSTYPE_NDS:
    case TXSTYPE_MGS:
    case TXSTYPE_MSG:
    case TXSTYPE_LOG:
    case TXSTYPE_TXS:
    case TXSTYPE_SUS:
    case TXSTYPE_SBS:
    case TXSTYPE_UUS:
    case TXSTYPE_UBS:
    case TXSTYPE_GFI:
    case TXSTYPE_SIG:
    case TXSTYPE_GET:
    case TXSTYPE_VIP:
    case TXSTYPE_BLK:
    case TXSTYPE_SAV:
        commonResponse(std::move(command));
        break;
    default:
        DLOG("WARNING: response might be not defined for this command, check %s:%d\n", __FILE__, __LINE__);
        commonResponse(std::move(command));
        break;
    }
}

void ResponseHandler::initLogs(std::unique_ptr<IBlockCommand>& txs) {
    uint32_t now = time(NULL);
    now -= now%BLOCKSEC;
    m_pt.put("current_block_time", now);
    m_pt.put("previous_block_time", now - BLOCKSEC);

    std::stringstream tx_hex, sign_hex, tx_data;
    tx_data.write((char*)txs->getData(), txs->getDataSize());
    tx_data.write((char*)txs->getAdditionalData(), txs->getAdditionalDataSize());
    //tx_data.write((char*)txs->getSignature(), txs->getSignatureSize());
    Helper::ed25519_key2text(tx_hex, (uint8_t*)tx_data.str().c_str(), txs->getDataSize() + txs->getAdditionalDataSize());
    Helper::ed25519_key2text(sign_hex, (uint8_t*)txs->getSignature(), txs->getSignatureSize());
    m_pt.put("tx.data",tx_hex.str());
    m_logpt.put("tx.data",tx_hex.str());

    if(!m_sts.without_secret || m_sts.signature_provided) {
      m_pt.put("tx.signature",sign_hex.str());
      m_logpt.put("tx.signature",sign_hex.str());
    }
    m_pt.put("tx.time",txs->getTime());
    m_logpt.put("tx.time",txs->getTime());

    int type = txs->getType();

    if(type != TXSTYPE_INF && type != TXSTYPE_BLG && type != TXSTYPE_NDS && type != TXSTYPE_MGS && type != TXSTYPE_MSG && type != TXSTYPE_SIG && type != TXSTYPE_VIP && type != TXSTYPE_BLK) {
       if(!m_sts.signature_provided) {
          m_pt.put("tx.account_msid", m_sts.msid);
          m_logpt.put("tx.account_msid",m_sts.msid);

          std::stringstream tx_user_hashin;
          Helper::ed25519_key2text(tx_user_hashin, m_sts.ha.data(), SHA256_DIGEST_LENGTH);


          m_pt.put("tx.account_hashin", tx_user_hashin.str());
          m_logpt.put("tx.account_hashin", tx_user_hashin.str());

          std::array<uint8_t, SHA256_DIGEST_LENGTH> hashout;
          Helper::create256signhash(txs->getSignature(), txs->getSignatureSize(), m_sts.ha, hashout);

          std::stringstream tx_user_hashout;
          Helper::ed25519_key2text(tx_user_hashout, hashout.data(), SHA256_DIGEST_LENGTH);
          m_pt.put("tx.account_hashout",  std::move(tx_user_hashout.str()));
        }
        //FIXME calculate deduction and fee
        m_pt.put("tx.deduct", print_amount(txs->getDeduct()+txs->getFee()));
        m_pt.put("tx.fee", print_amount(txs->getFee()));

        if(m_sts.msid == 1) {
            std::stringstream tx_user_public_key;
            Helper::ed25519_key2text(tx_user_public_key, m_sts.pk, SHA256_DIGEST_LENGTH);
            m_logpt.put("tx.account_public_key", std::move(tx_user_public_key.str()));
        }
    }
}

void ResponseHandler::commonResponse(std::unique_ptr<IBlockCommand> command) {
    if (!command->m_responseError) {
        command->saveResponse(m_sts);
    }
    command->toJson(m_pt);
    if(redirect_host.length() > 0) {
        m_pt.put("redirect", redirect_host + ":" + redirect_port);
    }
    boost::property_tree::write_json(std::cout, m_pt, m_sts.nice);
}
