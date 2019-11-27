var exec = require('child-process-promise').exec
const env = require('../.env')
const accounts = require('./accounts')
const err = (error) => console.error(error.toString())

async function init(contractName){
  try {
    var result
    if (!contractName) throw(new Error("Missing contract name!"))
    result = await exec(`wallet unlock --password ${env.walletPW}`)
    .catch(err)
    if (result) console.log(result.stdout)
    result = await exec(`
    cleos -u ${env.endpoints[env.network].rpc} set code ${accounts[env.network].contractName} ../_compiled_contracts/${contractName}/jungle/${contractName}.wasm
    `)
    if (result) console.log(result.stdout)


  } catch (error) { err(error) }
}


init(process.argv[2]).catch(console.log)
