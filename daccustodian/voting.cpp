#include "../_contract-shared-headers/migration_helpers.hpp"

void daccustodian::votecust(name voter, vector<name> newvotes) {
    check(false, "This action is deprecated call `votecuste` instead.");
}

void daccustodian::votecuste(name voter, vector<name> newvotes, name dac_id) {

    candidates_table registered_candidates(_self, dac_id.value);
    contr_config configs = contr_config::get_current_configs(_self, dac_id);

    require_auth(voter);
    assertValidMember(voter, dac_id);

    check(newvotes.size() <= configs.maxvotes, "ERR::VOTECUST_MAX_VOTES_EXCEEDED::Max number of allowed votes was exceeded.");
    std::set<name> dupSet{};
    for (name vote: newvotes) {
        check(dupSet.insert(vote).second, "ERR::VOTECUST_DUPLICATE_VOTES::Added duplicate votes for the same candidate.");
        auto candidate = registered_candidates.get(vote.value, "ERR::VOTECUST_CANDIDATE_NOT_FOUND::Candidate could not be found.");
        check(candidate.is_active, "ERR::VOTECUST_VOTING_FOR_INACTIVE_CAND::Attempting to vote for an inactive candidate.");
    }

    // Find a vote that has been cast by this voter previously.
    vector<name> oldvotes;
    votes_table votes_cast_by_members(_self, dac_id.value);
    auto existingVote = votes_cast_by_members.find(voter.value);
    if (existingVote != votes_cast_by_members.end()) {
        oldvotes = existingVote->candidates;

        modifyVoteWeights(voter, existingVote->candidates, newvotes, dac_id);

        if (newvotes.size() == 0) {
            // Remove the vote if the array of candidates is empty
            votes_cast_by_members.erase(existingVote);
            eosio::print("\n Removing empty vote.");
        } else {
            votes_cast_by_members.modify(existingVote, voter, [&](vote &v) {
                v.candidates = newvotes;
                v.proxy = name();
            });
        }
    } else {
        modifyVoteWeights(voter, {}, newvotes, dac_id);

        votes_cast_by_members.emplace(voter, [&](vote &v) {
            v.voter = voter;
            v.candidates = newvotes;
        });
    }


    // Send notification
    vote_notify n;
    n.voter = voter;
    n.new_votes = newvotes;
    n.old_votes = oldvotes;
    notifyListeners(n, dac_id);
}

//void daccustodian::voteproxy(name voter, name proxy) {
//
//    require_auth(voter);
//    assertValidMember(voter);
//
//    string error_msg = "Member cannot proxy vote for themselves: " + voter.to_string();
//    check(voter != proxy, error_msg.c_str());
//    auto destproxy = votes_cast_by_members.find(proxy);
//    if (destproxy != votes_cast_by_members.end()) {
//        error_msg = "Proxy voters cannot vote for another proxy: " + voter.to_string();
//        check(destproxy->proxy == 0, error_msg.c_str());
//    }
//
//    // Find a vote that has been cast by this voter previously.
//    auto existingVote = votes_cast_by_members.find(voter);
//    if (existingVote != votes_cast_by_members.end()) {
//
//        votes_cast_by_members.modify(existingVote, _self, [&](vote &v) {
//            v.candidates.clear();
//            v.proxy = proxy;
//        });
//    } else {
//        votes_cast_by_members.emplace(_self, [&](vote &v) {
//            v.voter = voter;
//            v.proxy = proxy;
//        });
//    }
//}
