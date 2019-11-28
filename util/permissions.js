module.exports = {
  resign:(dacauthority) => JSON.parse(`{
    "threshold" : 1,
    "keys" : [],
    "accounts": [{"permission":{"actor":"${dacauthority}", "permission":"active"}, "weight":1}],
    "waits": []
  }`),
  custodianTransfer:(authority,custodians,proposals) => {
    var accounts = []
    const arranged = [authority,custodians,proposals].sort()
    for (account of arranged) {
      if (account === authority) accounts.push({"permission":{"actor":authority, "permission":"med"}, "weight":2})
      else if (account === custodians) accounts.push({"permission":{"actor":custodians, "permission":"eosio.code"}, "weight":1})
      else if (account === proposals) accounts.push({"permission":{"actor":proposals, "permission":"eosio.code"}, "weight":1})
    }
    return {
      "threshold": 2,
      "keys": [],
      accounts,
      "waits": [{"wait_sec":2, "weight":1}]
    }
  },
  authorityActive:(dacauthority,authPubKey) => JSON.parse(`{
      "threshold": 1,
      "keys": [{"key":"${authPubKey}", "weight":1}],
      "accounts": [
          {"permission":{"actor":"${dacauthority}", "permission":"high"}, "weight":1}
      ],
      "waits": []
  }`),
  dacauthorityOwner:(daccustodian,authPubKey) => JSON.parse(`{
      "threshold": 1,
      "keys": [{"key":"${authPubKey}", "weight":1}],
      "accounts": [
          {"permission":{"actor":"${daccustodian}", "permission":"eosio.code"}, "weight":1}
      ],
      "waits": []
  }`)
}