#include <referendum.hpp>

void referendum::receive(name from, name to, asset quantity, string memo){
    if (to != _self || from == "eosio"_n || from == "eosio.ram"_n || from == "eosio.stake"_n){
        return;
    }

    deposits_table deposits(get_self(), get_self().value);
    auto existing = deposits.find(from.value);

    check(existing == deposits.end(), "ERR:EXISTING_DEPOSIT::There is an existing deposit, please call refund and try again");

    deposits.emplace(get_self(), [&](deposit_info& d){
        d.account = from;
        d.deposit = extended_asset(quantity, get_first_receiver());
    });
}

void referendum::refund(name account){
    deposits_table deposits(get_self(), get_self().value);
    auto existing = deposits.find(account.value);
    check(existing != deposits.end(), "ERR::NO_DEPOSIT::This account does not have any deposit");
//    print(existing->deposit.contract, " ", account, " ", existing->deposit.quantity);

    string memo = "Return of referendum deposit.";
    eosio::action(
            eosio::permission_level{ get_self(), "active"_n },
            existing->deposit.contract, "transfer"_n,
            make_tuple(get_self(), account, existing->deposit.quantity, memo)
    ).send();

    deposits.erase(existing);
}

void referendum::updateconfig(config_item config, name dac_id){
    checkDAC(dac_id);
    require_auth(get_self());
    config.save(get_self(), dac_id, get_self());
}

void referendum::propose(
        name proposer,
        uint8_t type,
        uint8_t voting_type,
        string title,
        string content,
        name dac_id,
        vector<action> acts){

    checkDAC(dac_id);
    require_auth(proposer);
    assertValidMember(proposer, dac_id);
#if ENABLE_BINDING_VOTE == 0
    check(type != vote_type::TYPE_BINDING, "ERR::CONTRACT_NOT_COMPILED::Contract was not compiled to allow binding votes");
#endif

    auto config = config_item::get_current_configs(get_self(), dac_id);

    // Get transaction hash for content_ref and next id
    auto size = transaction_size();
    char* buffer = (char*)( 512 < size ? malloc(size) : alloca(size) );
    uint32_t read = read_transaction( buffer, size );
    check( size == read, "ERR::READ_TRANSACTION_FAILED::read_transaction failed");
    checksum256 trx_id = sha256(buffer, read);

    // Calculate a referendum id
    uint64_t referendum_id = nextID(trx_id);

    check(type < vote_type::TYPE_INVALID, "ERR::TYPE_INVALID::Referendum type is invalid");
    check(voting_type < count_type::COUNT_INVALID, "ERR::COUNT_TYPE_INVALID::Referendum vote counting type is invalid");

    // Do checks if it is account based voting
    if (voting_type == count_type::COUNT_ACCOUNT){
        string msg = "ERR::ACCOUNT_VOTE_NOT_ALLOWED::Account vote is not allowed for this type of referendum";
        check(config.allow_per_account_voting.at(type), msg);
    }

    // If the type is binding or semi binding then it must contain the action
    if (acts.size() == 0){
        check(type == vote_type::TYPE_OPINION, "ERR::TYPE_REQUIRES_ACTION::This type of referendum requires an action to be executed");
    }
    else {
        check(type < vote_type::TYPE_OPINION, "ERR::CANT_SEND_ACT::Can't supply an action with opinion based referendum");
        check(hasAuth(acts), "ERR::PERMS_FAILED::The authorization supplied with the action does not pass");
    }

    // Check the fee has been paid
    deposits_table deposits(get_self(), get_self().value);
    auto dep = deposits.find(proposer.value);
    extended_asset fee_required = config.fee[type];
    if (fee_required.quantity.amount > 0){
        check(dep != deposits.end(), "ERR::FEE_REQUIRED::A fee is required to propose this type of referenda.  Please send the correct fee to this contract and try again.");
        check(dep->deposit.quantity >= fee_required.quantity &&
              dep->deposit.contract == fee_required.contract, "ERR::INSUFFICIENT_FEE::Fee provided is insufficient");

        if (dep->deposit == fee_required){
            deposits.erase(dep);
        }
        else {
            deposits.modify(*dep, same_payer, [&](auto& d){
                d.deposit -= fee_required;
            });
        }

        // transfer fee to treasury account
        auto dac = dacdir::dac_for_id(dac_id);
        auto treasury_account = dac.account_for_type(dacdir::TREASURY);
        string fee_memo = "Fee for referendum id " + to_string(referendum_id);
        eosio::action(
                eosio::permission_level{ get_self(), "active"_n },
                fee_required.contract, "transfer"_n,
                make_tuple(get_self(), treasury_account, fee_required.quantity, fee_memo)
        ).send();
    }


    // Calculate expiry
    uint32_t time_now = current_time_point().sec_since_epoch();
    uint32_t expiry_time = time_now + (60 * 60 * 24 * 30);


    // Save to database
    referenda_table referenda(get_self(), dac_id.value);
    referenda.emplace(proposer, [&](referendum_data& r){
        std::map<uint8_t, uint64_t> token_votes = {
                {vote_choice::VOTE_YES, 0},
                {vote_choice::VOTE_NO, 0},
                {vote_choice::VOTE_ABSTAIN, 0}
        };
        std::map<uint8_t, uint64_t> account_votes = {
                {vote_choice::VOTE_YES, 0},
                {vote_choice::VOTE_NO, 0},
                {vote_choice::VOTE_ABSTAIN, 0}
        };

        r.referendum_id = referendum_id;
        r.proposer =      proposer;
        r.type =          type;
        r.voting_type =   voting_type;
        r.title =         title;
        r.content_ref =   trx_id;
        r.token_votes =   token_votes;
        r.account_votes = account_votes;
        r.expires =       time_point_sec(expiry_time);
        r.acts =          acts;
    });

}

void referendum::vote(name voter, uint64_t referendum_id, uint8_t vote, name dac_id){

    checkDAC(dac_id);
    assertValidMember(voter, dac_id);

    referenda_table referenda(get_self(), dac_id.value);
    auto ref = referenda.get(referendum_id, "ERR::REFERENDUM_NOT_FOUND::Referendum not found");
    check(ref.status != referendum_status::STATUS_EXPIRED, "ERR::REFERENDUM_EXPIRED::Referendum has expired");

    uint64_t current_votes_token = 0;
    if (ref.token_votes.find(vote) != ref.token_votes.end()){
        current_votes_token = ref.token_votes[vote];
    }
    uint64_t current_votes_account = 0;
    if (ref.account_votes.find(vote) != ref.account_votes.end()){
        current_votes_account = ref.account_votes[vote];
    }

    uint64_t new_votes_token = current_votes_token;
    uint64_t new_votes_accounts = current_votes_account;

    if (vote == vote_choice::VOTE_REMOVE){ // remove vote
        new_votes_accounts--;
    }

    // get vote weight from token (staked balance - unstaking balance)
    uint64_t weight = 1000000;
    uint8_t old_vote = vote_choice::VOTE_REMOVE;
    // get existing vote
    votes_table votes(get_self(), dac_id.value);
    auto existing_vote_data = votes.find(voter.value);
    if (existing_vote_data != votes.end()){
        if (existing_vote_data->votes.find(referendum_id) != existing_vote_data->votes.end()){
            old_vote = uint8_t(existing_vote_data->votes.at(referendum_id));
        }

        auto ev = *existing_vote_data;

        print("Going to modify votes");

        auto existing_votes = ev.votes;
        if (vote == vote_choice::VOTE_REMOVE){
            existing_votes.erase(referendum_id);
        }
        else {
            if (old_vote == vote_choice::VOTE_REMOVE){
                // new vote, check that they havent voted for more than 20 to avoid timeouts during clean and
                // when updating vote weight
                check(existing_votes.size() < 20, "ERR::::Can only vote on 20 referenda at a time, try using the clean action to remove old votes");
            }
            existing_votes[referendum_id] = vote;
        }

        votes.modify(existing_vote_data, same_payer, [&](vote_info& v) {
            v.votes = existing_votes;
        });

        print("Modified vote");
    }
    else {
        votes.emplace(voter, [&](vote_info& v) {
            v.voter = voter;
            map<uint64_t, uint8_t> votes;
            votes.emplace(referendum_id, vote);
            v.votes = votes;
        });
    }

    check(old_vote < vote_choice::VOTE_INVALID, "ERR::OLD_INVALID::Old vote is invalid");
    check(vote < vote_choice::VOTE_INVALID, "ERR::NEW_INVALID::New vote is invalid");


    auto token_votes = ref.token_votes;
    auto account_votes = ref.account_votes;
    if (old_vote > vote_choice::VOTE_REMOVE){
        token_votes[old_vote] -= weight;
        account_votes[old_vote]--;
    }
    if (vote > vote_choice::VOTE_REMOVE){
        token_votes[vote] += weight;
        account_votes[vote]++;
    }

    auto ref2 = referenda.find(referendum_id);
    referenda.modify(*ref2, same_payer, [&](auto& r){
        r.token_votes = token_votes;
        r.account_votes = account_votes;
    });


    action(
            eosio::permission_level{ get_self(), "active"_n },
            get_self(), "updatestatus"_n,
            make_tuple( referendum_id, dac_id )
    ).send();

}


void referendum::updatestatus(uint64_t referendum_id, name dac_id){
    checkDAC(dac_id);
    referenda_table referenda(get_self(), dac_id.value);

    uint8_t new_status = calculateStatus(referendum_id, dac_id);

    auto ref = referenda.find(referendum_id);
    check(ref != referenda.end(), "ERR::REFERENDUM_NOT_FOUND::Referendum not found");
    referenda.modify(*ref, same_payer, [&](auto& r){
        r.status = new_status;
    });
}


void referendum::cancel(uint64_t referendum_id, name dac_id){
    checkDAC(dac_id);
    referenda_table referenda(get_self(), dac_id.value);
    auto ref = referenda.get(referendum_id, "ERR::REFERENDUM_NOT_FOUND::Referendum not found");

    require_auth(ref.proposer);

    referenda.erase(ref);
}

void referendum::exec(uint64_t referendum_id, name dac_id){
    checkDAC(dac_id);
    referenda_table referenda(get_self(), dac_id.value);
    auto ref = referenda.find(referendum_id);

    uint8_t calculated_status = calculateStatus(referendum_id, dac_id);

    check(ref != referenda.end(), "ERR:REFERENDUM_NOT_FOUND::Referendum not found");
    check(ref->type < vote_type::TYPE_OPINION, "ERR::CANNOT_EXEC::Cannot exec this type of referendum");
    check(ref->acts.size(), "ERR::NO_ACTION::No action to execute");
    check(ref->status == referendum_status::STATUS_PASSING, "ERR:REFERENDUM_NOT_PASSED::Referendum has not passed required number of yes votes");
    check(calculated_status == referendum_status::STATUS_PASSING, "ERR:REFERENDUM_NOT_PASSED::Referendum has not passed required number of yes votes");


    if (ref->type == vote_type::TYPE_BINDING){
        for (auto a: ref->acts) {
            a.send();
        }
    }
    else if (ref->type == vote_type::TYPE_SEMI_BINDING){
        proposeMsig(*ref, dac_id);
    }

    referenda.erase(ref);
}

void referendum::stakeobsv(vector<account_stake_delta> stake_deltas, name dac_id){
    checkDAC(dac_id);
    auto dac = dacdir::dac_for_id(dac_id);
    auto router_account = dac.account_for_type(dacdir::VOTE_WEIGHT);
    require_auth(router_account);

    referenda_table referenda(get_self(), dac_id.value);

    for (auto asd: stake_deltas){
        votes_table votes(get_self(), dac_id.value);
        auto existing_vote_data = votes.find(asd.account.value);

        if (existing_vote_data != votes.end()){
            for (auto v: existing_vote_data->votes){
                auto ref = referenda.find(v.first);

                if (ref != referenda.end()){
                    referenda.modify(ref, same_payer, [&](referendum_data& r){
                        r.token_votes[v.second] += asd.stake_delta.amount;
                    });
                }
            }
        }
    }
}

void referendum::clean(name account, name dac_id){
    checkDAC(dac_id);
    require_auth(account);

    referenda_table referenda(get_self(), dac_id.value);
    votes_table votes(get_self(), dac_id.value);
    auto existing_vote_data = votes.find(account.value);

    map<uint64_t, uint8_t> new_votes;
    if (existing_vote_data != votes.end()){
        for (auto vd: existing_vote_data->votes){
            if (referenda.find(vd.first) != referenda.end()){
                new_votes[vd.first] = vd.second;
            }
        }
    }

    votes.modify(*existing_vote_data, same_payer, [&new_votes](vote_info& v){
        v.votes = new_votes;
    });
}


// Private

bool referendum::hasAuth(vector<action> acts){
    // TODO : this only checks if the permissions provided in the actions can authenticate the action
    // not if this contract will be able to execute it
    transaction trx;
    trx.actions = acts;

    auto check_perms = std::vector<permission_level>();
    for (auto a: acts){
        check_perms.insert(check_perms.end(), a.authorization.begin(), a.authorization.end());
    }

    auto packed_trx = pack(trx);
    auto packed_perms = pack(check_perms);

    bool res = referendum::_check_transaction_authorization(packed_trx.data(), packed_trx.size(), (const char*)0, 0, packed_perms.data(), packed_perms.size());

    return res;
}

void referendum::checkDAC(name dac_id){
#ifdef RESTRICT_DAC
    check(dac_id == name(RESTRICT_DAC), "DAC not permitted");
#endif
}

uint8_t referendum::calculateStatus(uint64_t referendum_id, name dac_id) {
    referenda_table referenda(get_self(), dac_id.value);
    auto ref = referenda.find(referendum_id);
    check(ref != referenda.end(), "ERR:REFERENDUM_NOT_FOUND::Referendum not found");

    config_item config = config_item::get_current_configs(get_self(), dac_id);
    uint64_t quorum = 0, current_yes = 0, current_no = 0, current_abstain = 0, current_all = 0;
    uint16_t pass_rate = 10000; // 100%
    uint8_t status = referendum_status::STATUS_OPEN;
    uint32_t time_now = current_time_point().sec_since_epoch();
    uint64_t yes_percentage = 0;

    map<uint8_t, uint64_t> votes;

    if (ref->expires.sec_since_epoch() >= time_now)
    {
        if (ref->voting_type == count_type::COUNT_TOKEN) {
            quorum = config.quorum_token[ref->type];
            votes = ref->token_votes;
        } else {
            quorum = config.quorum_account[ref->type];
            votes = ref->account_votes;
        }

        uint64_t total = 0;
        for (auto v : votes)
        {
            total += v.second;
        }

        current_yes = votes.at(vote_choice::VOTE_YES);
        current_no = votes.at(vote_choice::VOTE_NO);
        current_abstain = votes.at(vote_choice::VOTE_ABSTAIN);
        current_all = current_yes + current_no + current_abstain;

        // check we have made quorum
        if (total >= quorum){
            // quorum has been reached, check we have passed
            status = referendum_status::STATUS_ATTENTION;

            uint64_t yes_percentage = uint64_t((double(current_yes) / double(current_all)) * 10000.0); // multiply by 10000 to get integer with 2 dp
            pass_rate = config.pass[ref->type];
            if (yes_percentage >= pass_rate){
                status = referendum_status::STATUS_PASSING;
            }
        }

        print("referendum id : ", referendum_id, ", dac id : ", dac_id, ", quorum : ", quorum, ", pass rate : ", pass_rate, ", current rate : ", current_all, ", total : ", total, ", yes : ", current_yes, ", yes% : ", yes_percentage);
    }
    else {
        // Expired
        status = referendum_status::STATUS_EXPIRED;
    }


    return status;
}


void referendum::proposeMsig(referendum_data ref, name dac_id){
    auto dac = dacdir::dac_for_id(dac_id);
    auto custodian_contract = dac.account_for_type(dacdir::CUSTODIAN);
    auto msig_contract = dac.account_for_type(dacdir::MSIGS);
    auto auth_account = dac.account_for_type(dacdir::AUTH);

    transaction trx;
    trx.actions = ref.acts;

    // Calculate expiry as 30 days
    uint32_t time_now = current_time_point().sec_since_epoch();
    trx.expiration = time_point_sec(time_now + (60 * 60 * 24 * 30));

    // Get required auths
    candidates_table candidates(custodian_contract, dac_id.value);
    candperms_table  candperms(custodian_contract, dac_id.value);
    auto cand_idx = candidates.get_index<"byvotesrank"_n>();
    auto cand_itr = cand_idx.begin();

    name proposal_name = name{ref.referendum_id};

    vector<permission_level> reqd_perms;

    contr_config custodian_config = contr_config::get_current_configs(custodian_contract, dac_id);

    uint8_t count = 0;
    uint8_t num_reqs = min(255, custodian_config.numelected * 2);

    while (count < num_reqs && cand_itr != cand_idx.end()){
        name perm_name = "active"_n;
        auto custom_perm = candperms.find(cand_itr->candidate_name.value);
        if (custom_perm != candperms.end()){
            perm_name = custom_perm->permission;
        }
        reqd_perms.push_back(permission_level{cand_itr->candidate_name, perm_name});
//        print(" Adding ", cand_itr->candidate_name);

        cand_itr++;
        count++;
    }

    action(
            permission_level{auth_account, "referendum"_n},
            name(SYSTEM_MSIG_CONTRACT), "propose"_n,
            make_tuple( auth_account, proposal_name, reqd_perms, trx )
    ).send();

    string metadata = "{\"title\":\"REFERENDUM: " + ref.title + "\", \"description\":\"Automated submission of passing referendum number " + to_string(ref.referendum_id) + "\"}";
    vector<permission_level> perms = {
            permission_level{auth_account, "admin"_n},
            permission_level{auth_account, "referendum"_n}
    };
    action(
            perms,
            msig_contract, "proposede"_n,
            make_tuple( auth_account, proposal_name, metadata, dac_id )
    ).send();

}

uint64_t referendum::nextID(checksum256 trxid){
    uint64_t id = 0;

    uint32_t time_now = current_time_point().sec_since_epoch();
    id |= uint64_t{time_now} << 32;
    id |= static_cast<uint32_t>(trxid.data()[0]) << 24;
    id |= static_cast<uint32_t>(trxid.data()[1]) << 16;
    id |= static_cast<uint32_t>(trxid.data()[2]) << 8;
    id |= static_cast<uint32_t>(trxid.data()[3]);

    return id;
}
