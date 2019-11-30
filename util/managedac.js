const eosjs = require('./eosjs')
const env = require('../.env')
const accts = require('./accounts')
const acct = (name) => accts[env.network][name]
const dacKey = env.keys.BOIDACCOUNTS
const api = eosjs([dacKey]).api
const tapos = { blocksBehind: 6, expireSeconds: 10 }

// AUTH = 0,
// TREASURY = 1,
// CUSTODIAN = 2,
// MSIGS = 3,
// SERVICE = 5,
// PROPOSALS = 6,
// ESCROW = 7,
// VOTE_WEIGHT = 8,
// ACTIVATION = 9,
// EXTERNAL = 254,
// OTHER = 255

const accounts = [
  {key:0,value:acct('authority')},
  {key:1,value:acct('funds')},
  {key:2,value:acct('custodians')},
  {key:3,value:acct('multisigs')},
  {key:5,value:acct('service')},
  {key:6,value:acct('proposals')},
  {key:7,value:acct('escrow')},
  {key:9,value:acct('authority')}
]

const refs = [

]

const data = {
  title: 'Boid Test DAC',
  dac_id:'boidtestdac2',
  dac_symbol:{contract:acct('token'), symbol:"4,BOID"},
  owner:acct('owner'),
  accounts,refs
}

const statusData = {dac_id:data.dac_id,value:1}

const authorization = [
  {actor:acct('authority'),permission: 'active'},
  {actor:acct('owner'),permission: 'active'},
  {actor:acct('funds'),permission: 'active'},
  {actor:acct('directory'),permission: 'active'}
]


async function init(accountName){
  try {
    const account = acct('directory')
    const accountCreated = await api.transact({
      actions: [
        // { account, name: "unregdac", data:{dac_id:data.dac_id}, authorization }
        { account, name: "regdac", data, authorization },
        { account, name: "setstatus", data:statusData, authorization }
      ]
    },tapos)
    delete accountCreated.processed.action_traces

    console.log(accountCreated.processed.receipt)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

init(process.argv[2]).catch(console.log)