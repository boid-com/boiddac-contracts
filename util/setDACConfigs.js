const eosjs = require('./eosjs')
const env = require('../.env')
const dacKey = env.keys.BOIDACCOUNTS
const api = eosjs([dacKey]).api
const tapos = { blocksBehind: 6, expireSeconds: 10 }
const per = require('./permissions')
const accts = require('./accounts.json')
const acct = (name) => accts[env.network][name]
const sleep = ms => new Promise(res => setTimeout(res, ms))
const ms = require('human-interval')

const dac_id = 'boidtestdac2'


const electionsConfig = {
  dac_id,
  newconfig: {
    auth_threshold_high:4,
    auth_threshold_mid:3,
    auth_threshold_low:2,
    lockup_release_time_delay:100,
    lockupasset:{
      contract:"bo1ddactoken",
      quantity:"1000000.0000 BOID"
    },
    maxvotes:1,
    numelected:5,
    periodlength:6048,
    requested_pay_max:{
      contract:"eosio.token",
      quantity:"0.0000 EOS"
    },
    should_pay_via_service_provider:false,
    vote_quorum_percent:1,
    initial_vote_quorum_percent:1
  }
}

const proposalsConfig = {
  dac_id,
  new_config:{
    approval_expiry: 2592000,
    escrow_expiry: 2592000,
    finalize_threshold: 1,
    proposal_threshold: 4
  }
}

const terms = {
  dac_id: "boidtestdac2",
  hash:"1df37bdb72c0be963ef2bdfe9b7ef10b",
  terms:"https://raw.githubusercontent.com/eosdac/eosdac-constitution/master/boilerplate_constitution.md"
}

async function setConfig(account,name,data) {
  const authorization = [{actor:acct('authority'),permission: 'active'}]
  const result = await api.transact({
    actions: [
      { name,data,account,authorization }
    ]
  },tapos)
  console.log(result.transaction_id)
}


async function configElections() {
  try {
    await setConfig(acct('custodians'),"updateconfige",electionsConfig)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function configProposals() {
  try {
    await setConfig(acct('proposals'),"updateconfig",proposalsConfig)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function updateTerms() {
  try {
    await setConfig(acct('token'),"newmemtermse",terms)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

async function all () {
  await configElections()
  await configProposals()
  await updateTerms()
}

const methods = {all,configElections,configProposals,updateTerms}

module.exports = methods


if (process.argv[2]) {
  if (Object.keys(methods).find(el => el === process.argv[2])) {
    console.log("starting",process.argv[2])
    methods[process.argv[2]]().catch(console.error)
    .then(()=>console.log('Finished'))
  }
}