/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "cc/eval.h"
#include "cc/utils.h"
#include "importcoin.h"
#include "crosschain.h"
#include "primitives/transaction.h"


/*
 * CC Eval method for import coin.
 *
 * This method should control every parameter of the ImportCoin transaction, since it has no signature
 * to protect it from malleability.
 
 ##### 0xffffffff is a special CCid for single chain/dual daemon imports
 */

extern std::string ASSETCHAINS_SELFIMPORT;
extern uint16_t ASSETCHAINS_CODAPORT,ASSETCHAINS_BEAMPORT;
int32_t komodo_nextheight();
bool myGetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock);
uint8_t ASSETCHAINS_OVERRIDE_PUBKEY33[33];

int32_t GetSelfimportProof(CMutableTransaction &mtx,CScript &scriptPubKey,TxProof &proof,uint64_t burnAmount,std::vector<uint8_t> rawtx,uint256 txid,std::vector<uint8_t> rawproof) // find burnTx with hash from "other" daemon
{
    MerkleBranch newBranch; CMutableTransaction &tmpmtx; CTransaction tx,vintx; uint256 blockHash; char destaddr[64],pkaddr[64];
    tmpmtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(),komodo_nextheight());
    if ( ASSETCHAINS_SELFIMPORT == "BEAM" )
    {
        // confirm via ASSETCHAINS_BEAMPORT that burnTx/hash is a valid BEAM burn
            return(-1);
    }
    else if ( ASSETCHAINS_SELFIMPORT == "CODA" )
    {
        // confirm via ASSETCHAINS_CODAPORT that burnTx/hash is a valid CODA burn
            return(-1);
    }
    else
    {
        if ( !E_UNMARSHAL(rawtx, ss >> tx) )
            return(-1);
        scriptPubKey = tx.vout[0].scriptPubKey;
        mtx = tx;
        mtx.fOverwintered = tmpmtx.fOverwintered;
        mtx.nExpiryHeight = tmpmtx.nExpiryHeight;
        mtx.nVersionGroupId = tmpmtx.nVersionGroupId;
        mtx.nVersion = tmpmtx.nVersion;
        mtx.vout.clear();
        mtx.vout.resize(1);
        mtx.vout[0].nValue = burnAmount;
        mtx.vout[0].scriptPubKey = scriptPubKey;
        if ( tx.GetHash() != txid )
            return(-1);
        if ( ASSETCHAINS_SELFIMPORT == "PUBKEY" )
        {
            // make sure vin0 is signed by ASSETCHAINS_OVERRIDE_PUBKEY33
            if ( myGetTransaction(tx.vin[0].prevout,vintx,blockHash) == 0 )
                return(-1);
            if ( tx.vin[0].n < vintx.vout.size() && Getscriptaddress(destaddr,vintx.vout[tx.vin[0].n].scriptPubKey) != 0 )
            {
                pubkey2addr(pkaddr,ASSETCHAINS_OVERRIDE_PUBKEY33.data());
                if ( strcmp(pkaddr,destaddr) == 0 )
                {
                    proof = std::make_pair(txid,newBranch);
                    return(0);
                }
                fprintf(stderr,"vin0[%d] -> %s vs %s\n",tx.vin[0].n,destaddr,pkaddr);
            }
            return(-1);
        }
        else
        {
            // ASSETCHAINS_SELFIMPORT and external coin is the assetchains symbol in the burnTx OP_RETURN
            // burnAmount, rawtx and rawproof should be enough for gatewaysdeposit equivalent
            return(-1);
        }
    }
    else return(-1);
    return(0);
}

// use proof from the above functions to validate the import

int32_t CheckBEAMimport(TxProof proof,CTransaction burnTx,std::vector<CTxOut> payouts)
{
    // check with dual-BEAM daemon via ASSETCHAINS_BEAMPORT for validity of burnTx
    return(-1);
}

int32_t CheckCODAimport(TxProof proof,CTransaction burnTx,std::vector<CTxOut> payouts)
{
    // check with dual-CODA daemon via ASSETCHAINS_CODAPORT for validity of burnTx
    return(-1);
}

int32_t CheckGATEWAYimport(std::string coin,TxProof proof,CTransaction burnTx,std::vector<CTxOut> payouts)
{
    // check for valid burn from external coin blockchain and if valid return(0);
    return(-1);
}

int32_t CheckPUBKEYimport(TxProof proof,CTransaction burnTx,std::vector<CTxOut> payouts)
{
    // if burnTx has ASSETCHAINS_PUBKEY vin, it is valid return(0);
    fprintf(stderr,"proof txid.%s\n",proof.first.GetHex().ToString().c_str());
    return(0);
    return(-1);
}

bool Eval::ImportCoin(const std::vector<uint8_t> params,const CTransaction &importTx,unsigned int nIn)
{
    TxProof proof; CTransaction burnTx; std::vector<CTxOut> payouts; uint64_t txfee = 10000;
    uint32_t targetCcid; std::string targetSymbol; uint256 payoutsHash;
    if (importTx.vout.size() < 2)
        return Invalid("too-few-vouts");
    // params
    if (!UnmarshalImportTx(importTx, proof, burnTx, payouts))
        return Invalid("invalid-params");
    
    // Control all aspects of this transaction
    // It should not be at all malleable
    if (MakeImportCoinTransaction(proof, burnTx, payouts).GetHash() != importTx.GetHash())
        return Invalid("non-canonical");
    // burn params
    if (!UnmarshalBurnTx(burnTx, targetSymbol, &targetCcid, payoutsHash))
        return Invalid("invalid-burn-tx");
    // check burn amount
    {
        uint64_t burnAmount = burnTx.vout.back().nValue;
        if (burnAmount == 0)
            return Invalid("invalid-burn-amount");
        uint64_t totalOut = 0;
        for (int i=0; i<importTx.vout.size(); i++)
            totalOut += importTx.vout[i].nValue;
        if (totalOut > burnAmount || totalOut < burnAmount-txfee )
            return Invalid("payout-too-high");
    }
    // Check burntx shows correct outputs hash
    if (payoutsHash != SerializeHash(payouts))
        return Invalid("wrong-payouts");
    if (targetCcid < KOMODO_FIRSTFUNGIBLEID)
        return Invalid("chain-not-fungible");
    // Check proof confirms existance of burnTx
    if ( targetCcid != 0xffffffff )
    {
        if ( targetCcid != GetAssetchainsCC() || targetSymbol != GetAssetchainsSymbol() )
            return Invalid("importcoin-wrong-chain");
        uint256 target = proof.second.Exec(burnTx.GetHash());
        if (!CheckMoMoM(proof.first, target))
            return Invalid("momom-check-fail");
    }
    else if ( ASSETCHAINS_SELFIMPORT == targetSymbol ) // various selfchain imports
    {
        if ( ASSETCHAINS_SELFIMPORT == "BEAM" )
        {
            if ( CheckBEAMimport(proof,burnTx,payouts) < 0 )
                return Invalid("BEAM-import-failure");
        }
        else if ( ASSETCHAINS_SELFIMPORT == "CODA" )
        {
            if ( CheckCODAimport(proof,burnTx,payouts) < 0 )
                return Invalid("CODA-import-failure");
        }
        else if ( ASSETCHAINS_SELFIMPORT == "PUBKEY" )
        {
            if ( CheckPUBKEYimport(proof,burnTx,payouts) < 0 )
                return Invalid("PUBKEY-import-failure");
        }
        else
        {
            if ( CheckGATEWAYimport(targetSymbol,proof,burnTx,payouts) < 0 )
                return Invalid("GATEWAY-import-failure");
        }
    }
    return Valid();
}


