var exec = require('child-process-promise').exec
const env = require('../.env')
const accounts = require('./accounts')
const path = require('path')
const err = (error) => console.error(error.toString())

async function init(contractName){
  try {
    var result
    if (!contractName) throw(new Error("Missing contract name!"))
    result = await exec(`cleos wallet unlock --password ${env.walletPW}`)
    .catch((err)=>{})
    if (result) console.log(result.stdout)
    result = await exec(`
    cleos -u ${env.endpoints[env.network].rpc} set code ${accounts[env.network][contractName]} \\
    ${path.join(__dirname,`../`,`_compiled_contracts`,contractName,`jungle`,contractName,`${contractName}.wasm`)}
    `)
    if (result) console.log('Contract Deployed:',result.stdout)


  } catch (error) { err(error) }
}


init(process.argv[2]).catch(console.log)
