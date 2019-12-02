const eosjs = require('./eosjs')
const env = require('../.env')
const dacKey = env.keys.BOIDACCOUNTS
const api = eosjs([dacKey]).api
const tapos = { blocksBehind: 6, expireSeconds: 10 }
const per = require('./permissions')
const accts = require('./accounts.json')
const acct = (name) => accts[env.network][name]
const sleep = ms => new Promise(res => setTimeout(res, ms))

const authData = (account,permission,parent) => { 
  return {
    account,permission,parent,
    auth:
    {
      "threshold" : 1,
      "keys" : [{"key":"EOS7PE114KQs5MamHcH4LZTZN57NLwFekAQs89zzfMVkLfBtrxoZA", "weight":1}],
      "accounts": [],
      "waits": []
    }
  }
}

const resigntoAuth = async (contract) => {
  const authorization = [{actor:contract,permission: 'owner'}]
  const account = 'eosio'
  const result = await api.transact({
    actions: [
      { account, name: "updateauth", data:{
        account:contract,
        permission:"active", parent:"owner", auth:per.resign(acct('authority'))
      }, authorization },
      { account, name: "updateauth", data:{
        account:contract,
        permission:"owner", parent:"", auth:per.resign(acct('authority'))
      }, authorization }
    ]
  },tapos)
  console.log(result.transaction_id)
}

async function initAuth(){

  const authAcct = acct('authority')
  const authorization = [{actor:authAcct,permission: 'active'}]
  const account = 'eosio'
  const actionsList = [
    [{ account, name: "updateauth", data:authData(authAcct,"high","active"), authorization }],
    [{ account, name: "updateauth", data:authData(authAcct,"med","high"), authorization }],
    [{ account, name: "updateauth", data:authData(authAcct,"low","med"), authorization }],
    [{ account, name: "updateauth", data:authData(authAcct,"one","low"), authorization }]
  ]
  for (actions of actionsList) {
    try {
      const result = await api.transact({actions},tapos)
      console.log(result.transaction_id)
    } catch (error) {
      console.error(error.toString())
      return
    }
  }
}

async function resignToken(){
try {
  const contract = acct('token')
  const authorization = [{actor:contract,permission: 'owner'}]
  const account = 'eosio'
  const result = await api.transact({
    actions: [
      { account, name: "updateauth", 
        data:{ 
          account:contract, permission:"notify", parent:"active", 
          auth:per.tokenNotify(acct('token'))
        }, authorization 
      }
    ]
  },tapos)
  console.log(result.transaction_id)
  await sleep(5000)
  await resigntoAuth(acct('token'))
} catch (error) {
  return Promise.reject(error.toString())
}
}

async function resignMsig(){
  try {
    await resigntoAuth(acct('multisigs'))
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function resignOwner(){
  try {
    const contract = acct('owner')
    const authorization = [{actor:contract,permission: 'owner'}]
    const account = 'eosio'
    const result = await api.transact({
      actions: [
        { account, name: "updateauth", 
          data:{ 
            account:contract, permission:"xfer", parent:"active", 
            auth:per.custodianTransfer(acct('authority'),acct('custodians'),acct('proposals'))
          }, authorization 
        },
        { account, name: "linkauth",
          data:{
            account:contract, requirement:"xfer", code:'eosio.token', type:"transfer"
          }, authorization 
        }
      ]
    },tapos)
    console.log(result.transaction_id)
    await sleep(5000)
    await resigntoAuth(contract)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function resignCustodians(){
  try {
    const contract = acct('custodians')
    const authorization = [{actor:contract,permission: 'owner'}]
    const account = 'eosio'
    const result = await api.transact({
      actions: [
        { account, name: "updateauth", 
          data:{
            account:contract, permission:"xfer", parent:"active", 
            auth:per.custodianTransfer(acct('authority'),acct('custodians'),acct('proposals'))
          }, authorization 
        },
        { account, name: "linkauth",
          data:{
            account:contract, requirement:"xfer", code:acct('token'), type:"transfer"
          }, authorization 
        }
      ]
    },tapos)
    console.log(result.transaction_id)
    await sleep(5000)
    await resigntoAuth(contract)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function setupTierActions(){
  try {
    const contract = acct('authority')
    const authorization = [{actor:contract,permission: 'active'}]
    const account = 'eosio'
    const linkAuth = (requirement,code,type) => {
      return { account, name: "linkauth", data:{ account:contract, requirement, code, type }, authorization }
    }
    const result = await api.transact({
      actions: [ 
        linkAuth('high',acct('custodians'),''),
        linkAuth('med',acct('custodians'),'firecust'),
        linkAuth('med',acct('custodians'),'firecand'),
        linkAuth('one',acct('multisigs'),'')
      ]},tapos)
    console.log(result.transaction_id)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function setupTokenActions(){
  try {
    const contract = acct('token')
    const authorization = [{actor:contract,permission: 'active'}]
    const account = 'eosio'
    const linkAuth = (requirement,code,type) => {
      return { account, name: "linkauth", data:{ account:contract, requirement, code, type }, authorization }
    }
    const result = await api.transact({
      actions: [ 
        linkAuth('notify',acct('custodians'),'balanceobsv'),
        linkAuth('notify',acct('custodians'),'capturestake'),
        linkAuth('notify',acct('custodians'),'stakeobsv'),
        linkAuth('notify',acct('custodians'),'transferobsv'),
        linkAuth('notify',acct('custodians'),'refund')
      ]},tapos)
    console.log(result.transaction_id)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function finishAuth(){
  try {
    const contract = acct('authority')
    const authorization = [{actor:contract,permission: 'owner'}]
    const account = 'eosio'
    const result = await api.transact({
      actions: [
        { account, name: "updateauth", 
          data:{
            account:contract, permission:"active", parent:"owner", 
            auth:per.authorityActive(acct('authority'),'EOS7PE114KQs5MamHcH4LZTZN57NLwFekAQs89zzfMVkLfBtrxoZA')
          }, authorization 
        },
        { account, name: "updateauth", 
          data:{
            account:contract, permission:"owner", parent:"", 
            auth:per.authorityOwner(acct('custodians'),'EOS7PE114KQs5MamHcH4LZTZN57NLwFekAQs89zzfMVkLfBtrxoZA')
          }, authorization 
        },
      ]
    },tapos)
    console.log(result.transaction_id)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}


async function all(){
  try {
    console.log('\nSetup initial DAC authority tiers....')
    await initAuth()
    console.log('\nResign token authority to DAC authority...')
    await resignToken()
    console.log('\nResign msig authority to DAC authority...')
    await resignMsig()
    console.log('\nSetup xfer and resign owner authority to DAC authority...')
    await resignOwner()
    console.log('\nSetup xfer and resign custodians authority to DAC authority...')
    await resignCustodians()
    console.log('\nSetup Tier Actions...')
    await setupTierActions()
    console.log('\nSetup Token Actions...')
    await setupTokenActions()
    console.log('\nfinish Auth...')
    await finishAuth()
  } catch (error) {console.error(error.toString())}
}

// resignTokenAuth().catch(console.error)


// setupMsigPermissions(process.argv[2]).catch(console.log)
const methods = {initAuth,resignToken,resignMsig,all,resignOwner,resignCustodians,setupTierActions,finishAuth,setupTokenActions}

module.exports = methods


if (process.argv[2]) {
  if (Object.keys(methods).find(el => el === process.argv[2])) {
    console.log("starting",process.argv[2])
    methods[process.argv[2]]().catch(console.error)
    .then(()=>console.log('Finished'))
  }
}