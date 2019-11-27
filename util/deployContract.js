var exec = require('child-process-promise').exec
const env = require('../.env')
const accounts = require('./accounts')
async function init(contractName){
  try {
    if (!contractName) throw(new Error("Missing contract name!"))
    const result = await exec(` cleos -u ${env.endpoints[env.network].rpc} wallet unlock --password ${env.walletpw}`)
    console.log(result)
  } catch (error) {
    console.error(error)
  }
}


init(process.argv[2]).catch(console.log)
