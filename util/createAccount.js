const eosjs = require('./eosjs')
const env = require('../.env')
const pubkey = 'EOS7PE114KQs5MamHcH4LZTZN57NLwFekAQs89zzfMVkLfBtrxoZA'
async function createAccount(accountName){
  try {
    const dacKey = env.keys.BOIDACCOUNTS
    const creator = 'boiddac'
    var createData = { creator, name:accountName }
    createData.owner = {threshold:1,keys:[{key:pubkey,weight:1}],accounts:[],waits:[]}
    createData.active = createData.owner
    const ramData = { bytes:20000, payer:creator, receiver:accountName }
    const delegateData = {from:creator, receiver:accountName, stake_cpu_quantity:"1.0000 EOS", stake_net_quantity:"1.0000 EOS", transfer:false}
    const tapos = { blocksBehind: 12, expireSeconds: 30 }
    const authorization = [{actor:creator,permission: 'active'},{actor:"bo1ddactoken",permission:'active'}]
    const account = 'eosio'
    const api = eosjs([dacKey]).api
    const accountCreated = await api.transact({
      actions: [
        // { account, name: "newaccount", data:createData, authorization },
        // { account, name: "buyrambytes", data:ramData, authorization },
        // { account, name: "delegatebw", data:delegateData, authorization },
        { account:"bo1ddactoken", name: "issue", data:{to:accountName,quantity:"5000000.0000 BOID", memo:"welcome to boiddac"}, authorization },
        // { account:"bo1ddactoken", name: "issue", data:{to:accountName,quantity:"10000000.0000 BOID", memo:"welcome to boiddac"}, authorization }
      ]
    },tapos)
    delete accountCreated.processed.action_traces

    console.log(accountCreated)
  } catch (error) {
    return Promise.reject(error.toString())
  }
}

createAccount(process.argv[2]).catch(console.log)