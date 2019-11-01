#include <eosio/eosio.hpp>
#include <eosio/action.hpp>
#include "dacproposals.hpp"
#include <eosio/time.hpp>
#include <eosio/transaction.hpp>
#include <typeinfo>
#include <algorithm>

#include "../_contract-shared-headers/dacdirectory_shared.hpp"
#include "../_contract-shared-headers/eosdactokens_shared.hpp"

namespace eosdac {

    ACTION dacproposals::createprop(name proposer, string title, string summary, name arbitrator, extended_asset pay_amount, string content_hash, uint64_t id, uint16_t category, uint32_t job_duration, uint32_t approval_duration, name dac_id){
        require_auth(proposer);
        assertValidMember(proposer, dac_id);
        proposal_table proposals(_self, dac_id.value);

        check(proposer != arbitrator, "You cannot nominate yourself as the arbitrator for a proposal.");

        check(proposals.find(id) == proposals.end(), "ERR::CREATEPROP_DUPLICATE_ID::A Proposal with the id already exists. Try again with a different id.");

        check(title.length() > 3, "ERR::CREATEPROP_SHORT_TITLE::Title length is too short.");
        check(summary.length() > 3, "ERR::CREATEPROP_SHORT_SUMMARY::Summary length is too short.");
        check(pay_amount.quantity.symbol.is_valid(), "ERR::CREATEPROP_INVALID_SYMBOL::Invalid pay amount symbol.");
        check(pay_amount.quantity.amount > 0, "ERR::CREATEPROP_INVALID_PAY_AMOUNT::Invalid pay amount. Must be greater than 0.");
        check(is_account(arbitrator), "ERR::CREATEPROP_INVALID_ARBITRATOR::Invalid arbitrator.");

        auto treasury = dacdir::dac_for_id(dac_id).account_for_type(dacdir::TREASURY);
        check(arbitrator != proposer && arbitrator != treasury, "Arbitrator must be a third party");

        proposals.emplace(proposer, [&](proposal &p) {
            p.key = id;
            p.proposer = proposer;
            p.arbitrator = arbitrator;
            p.content_hash = content_hash;
            p.pay_amount = pay_amount;
            p.state = ProposalStatePending_approval;
            p.category = category;
            p.job_duration = job_duration;
            p.expiry = time_point_sec(current_time_point().sec_since_epoch()) + approval_duration;
        });
    }

    ACTION dacproposals::voteprop(name custodian, uint64_t proposal_id, uint8_t vote, name dac_id) {
        require_auth(custodian);

        auto auth_account = dacdir::dac_for_id(dac_id).account_for_type(dacdir::AUTH);
        require_auth(auth_account);

        assertValidMember(custodian, dac_id);

        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::VOTEPROP_PROPOSAL_NOT_FOUND::Proposal not found.﻿");
        switch (prop.state) {
            case ProposalStatePending_approval:
            case ProposalStateHas_enough_approvals_votes:
                check(prop.has_not_expired(),"ERR::PROPOSAL_EXPIRED::Proposal has expired.");
                check(vote == proposal_approve || vote == proposal_deny, "ERR::VOTEPROP_INVALID_VOTE::Invalid vote for the current proposal state.");
                break;
            case ProposalStatePending_finalize:
            case ProposalStateHas_enough_finalize_votes:
                check(vote == finalize_approve || vote == finalize_deny, "ERR::VOTEPROP_INVALID_VOTE::Invalid vote for the current proposal state.");
                break;
            default:
                check(false, "ERR::VOTEPROP_INVALID_PROPOSAL_STATE::Invalid proposal state to accept votes.");
        }
        
        proposal_vote_table prop_votes(_self, dac_id.value);
        auto by_prop_and_voter = prop_votes.get_index<"propandvoter"_n>();
        uint128_t joint_id = combine_ids(proposal_id, custodian.value);
        auto vote_idx = by_prop_and_voter.find(joint_id);
        if (vote_idx == by_prop_and_voter.end()) {
            prop_votes.emplace(_self, [&](proposalvote &v) {
                v.vote_id = prop_votes.available_primary_key();
                v.proposal_id = proposal_id;
                v.voter = custodian;
                v.vote = vote;
                v.delegatee = nullopt;
            });
        } else {
            by_prop_and_voter.modify(vote_idx, _self, [&](proposalvote &v) {
                v.vote = vote;
                v.delegatee = nullopt;
            });
        }

        updpropvotes(proposal_id, dac_id);
    }

    ACTION dacproposals::delegatevote(name custodian, uint64_t proposal_id, name delegatee_custodian, name dac_id) {
        require_auth(custodian);
        auto auth_account = dacdir::dac_for_id(dac_id).account_for_type(dacdir::AUTH);
        require_auth(auth_account);
        
        assertValidMember(custodian, dac_id);
        check(custodian != delegatee_custodian, "ERR::DELEGATEVOTE_DELEGATE_SELF::Cannot delegate voting to yourself.");

        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");
        check(prop.has_not_expired(),"ERR::PROPOSAL_EXPIRED::Proposal has expired.");

        proposal_vote_table prop_votes(_self, dac_id.value);
        auto by_prop_and_voter = prop_votes.get_index<"propandvoter"_n>();
        uint128_t joint_id = combine_ids(proposal_id, custodian.value);
        auto vote_idx = by_prop_and_voter.find(joint_id);
        if (vote_idx == by_prop_and_voter.end()) {
            prop_votes.emplace(_self, [&](proposalvote &v) {
                v.vote_id = prop_votes.available_primary_key();
                v.proposal_id = proposal_id;
                v.voter = custodian;
                v.vote = nullopt;
                v.delegatee = delegatee_custodian;
            });
        } else {
            by_prop_and_voter.modify(vote_idx, _self, [&](proposalvote &v) {
                v.vote = nullopt;
                v.delegatee = delegatee_custodian;
            });
        }

        updpropvotes(proposal_id, dac_id);
    }

    ACTION dacproposals::delegatecat(name custodian, uint64_t category, name delegatee_custodian, name dac_id) {
        require_auth(custodian);
        auto auth_account = dacdir::dac_for_id(dac_id).account_for_type(dacdir::AUTH);
        require_auth(auth_account);
        assertValidMember(custodian, dac_id);
        check(custodian != delegatee_custodian, "ERR::DELEGATEVOTE_DELEGATE_SELF::Cannot delegate voting to yourself.");

        proposal_vote_table prop_votes(_self, dac_id.value);

        auto by_cat_and_voter = prop_votes.get_index<"catandvoter"_n>();
        uint128_t joint_id = combine_ids(category, custodian.value);
        auto vote_idx = by_cat_and_voter.find(joint_id);
        if (vote_idx == by_cat_and_voter.end()) {
            prop_votes.emplace(_self, [&](proposalvote &v) {
                v.vote_id = prop_votes.available_primary_key();
                v.category_id = category;
                v.voter = custodian;
                v.delegatee = delegatee_custodian;
            });
        } else {
            by_cat_and_voter.modify(vote_idx, _self, [&](proposalvote &v) {
                v.delegatee = delegatee_custodian;
            });
        }
    }

    ACTION dacproposals::undelegateca(name custodian, uint64_t category, name dac_id) {
        require_auth(custodian);
    
        proposal_vote_table prop_votes(_self, dac_id.value);
        auto by_cat_and_voter = prop_votes.get_index<"catandvoter"_n>();

        uint128_t joint_id = combine_ids(category, custodian.value);
        auto vote_idx = by_cat_and_voter.find(joint_id);
        check(vote_idx != by_cat_and_voter.end(),"ERR::UNDELEGATECA_NO_EXISTING_VOTE::Cannot undelegate category vote with pre-existing vote.");
        by_cat_and_voter.erase(vote_idx);
    }

    ACTION dacproposals::arbapprove(name arbitrator, uint64_t proposal_id, name dac_id) {
        require_auth(arbitrator);
        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");

        check(prop.arbitrator == arbitrator, "ERR::NOT_ARBITRATOR::You are not the arbitrator for this proposal");

        clearprop(prop, dac_id);
    }

    ACTION dacproposals::startwork(uint64_t proposal_id, name dac_id){

        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");

        check(prop.state == ProposalStatePending_approval || prop.state == ProposalStateHas_enough_approvals_votes, "ERR::STARTWORK_WRONG_STATE::Proposal is not in the pending approval state therefore cannot start work.");
        check(prop.has_not_expired(),"ERR::PROPOSAL_EXPIRED::Proposal has expired.");
        require_auth(prop.proposer);
        assertValidMember(prop.proposer, dac_id);

        int16_t approved_count = count_votes(prop, proposal_approve, dac_id);

        config configs = current_configs(dac_id);

        check(approved_count >= configs.proposal_threshold, "ERR::STARTWORK_INSUFFICIENT_VOTES::Insufficient votes on worker proposal.");
        print_f("Worker proposal % to start work with: % votes\n", proposal_id, approved_count);
        
        proposals.modify(prop, prop.proposer, [&](proposal &p){
            p.state = ProposalStateWork_in_progress;
        });

        // print("Transfer funds to escrow account");
        string memo = prop.proposer.to_string() + ":" + to_string(proposal_id) + ":" + prop.content_hash;
        
        time_point_sec time_now = time_point_sec(current_time_point().sec_since_epoch());
        
        auto treasury = dacdir::dac_for_id(dac_id).account_for_type(dacdir::TREASURY);
        auto escrow = dacdir::dac_for_id(dac_id).account_for_type(dacdir::ESCROW);

        check(is_account(treasury), "ERR::TREASURY_ACCOUNT_NOT_FOUND::Treasury account not found");
        check(is_account(escrow), "ERR::ESCROW_ACCOUNT_NOT_FOUND::Escrow account not found");
        
        auto inittuple = make_tuple(treasury, prop.proposer, prop.arbitrator, time_now + prop.job_duration, memo, proposal_id, 0);

        eosio::action(
                eosio::permission_level{ treasury, "escrow"_n },
                escrow, "init"_n,
                inittuple
        ).send();

        transaction deferredTrans{};
        deferredTrans.actions.emplace_back(eosio::action(
                eosio::permission_level{treasury , "xfer"_n },
                prop.pay_amount.contract, "transfer"_n,
                make_tuple(treasury, escrow, prop.pay_amount.quantity, "payment for wp: " + to_string(proposal_id))
        ));
        deferredTrans.delay_sec = TRANSFER_DELAY;
        deferredTrans.send(uint128_t(proposal_id) << 64 | time_point_sec(current_time_point()).sec_since_epoch() , _self);
    }

    ACTION dacproposals::completework(uint64_t proposal_id, name dac_id){

        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");

        require_auth(prop.proposer);
        assertValidMember(prop.proposer, dac_id);
        check(prop.state == ProposalStateWork_in_progress, "ERR::COMPLETEWORK_WRONG_STATE::Worker proposal can only be completed from work_in_progress state");

        proposals.modify(prop, prop.proposer, [&](proposal &p){
            p.state = ProposalStatePending_finalize;
        });
    }

    ACTION dacproposals::finalize(uint64_t proposal_id, name dac_id) {

        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");

        check(prop.state == ProposalStatePending_finalize || prop.state == ProposalStateHas_enough_finalize_votes   , "ERR::FINALIZE_WRONG_STATE::Proposal is not in the pending_finalize state therefore cannot be finalized.");
        
        int16_t approved_count = count_votes(prop, finalize_approve, dac_id);

        print_f("Worker proposal % for finalizing with: % votes\n", proposal_id, approved_count);

        check(approved_count >= current_configs(dac_id).finalize_threshold, "ERR::FINALIZE_INSUFFICIENT_VOTES::Insufficient votes on worker proposal to be finalized.");

        transferfunds(prop, dac_id);
    }

    ACTION dacproposals::cancel(uint64_t proposal_id, name dac_id) {
        
        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");
        require_auth(prop.proposer);
        assertValidMember(prop.proposer, dac_id);
        clearprop(prop, dac_id);
    }

    ACTION dacproposals::comment(name commenter, uint64_t proposal_id, string comment, string comment_category, name dac_id) {
        require_auth(commenter);
        assertValidMember(commenter, dac_id);

        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");
        if (!has_auth(prop.proposer)) {
            auto auth_account = dacdir::dac_for_id(dac_id).account_for_type(dacdir::AUTH);
            require_auth(auth_account);        
        }
    }

    ACTION dacproposals::updateconfig(config new_config, name dac_id) {
        
        auto auth_account = dacdir::dac_for_id(dac_id).account_for_type(dacdir::AUTH);
        require_auth(auth_account);

        configs_table configs(_self, dac_id.value);
        configs.set(new_config, auth_account);
    }

    ACTION dacproposals::clearexpprop(uint64_t proposal_id, name dac_id) {
        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");
        check(!prop.has_not_expired(),"ERR::PROPOSAL_NOT_EXPIRED::The proposal has not expired so cannot be cleared yet.");
        clearprop(prop, dac_id);
    }

    ACTION dacproposals::updallprops(name dac_id) {
        proposal_table proposals(_self, dac_id.value);
        auto props_itr = proposals.begin();
        uint32_t delay = 1;
        for (auto props_itr: proposals) {
            transaction deferredTrans{};
            deferredTrans.actions.emplace_back(
                eosio::action(
                    eosio::permission_level{ _self, "active"_n },
                    _self, "updpropvotes"_n,
                    std::make_tuple(props_itr.key, dac_id)
                )
            );
            deferredTrans.delay_sec = delay++;
            auto sender_id = uint128_t(props_itr.key) << 64 | time_point_sec(current_time_point()).sec_since_epoch();
            deferredTrans.send(sender_id, _self);
            print("\n adding transaction: ", sender_id, "delay: ", deferredTrans.delay_sec.value);
        }
    }

    ACTION dacproposals::updpropvotes(uint64_t proposal_id, name dac_id) {
        proposal_table proposals(_self, dac_id.value);

        const proposal& prop = proposals.get(proposal_id, "ERR::DELEGATEVOTE_PROPOSAL_NOT_FOUND::Proposal not found.");

        int16_t approved_count;
        ProposalState newPropState;

        switch (prop.state) {
            case ProposalStatePending_approval:
            case ProposalStateHas_enough_approvals_votes:
                if (!prop.has_not_expired()) {
                    newPropState = ProposalStateExpired;
                } else {
                    approved_count = count_votes(prop, proposal_approve, dac_id);
                    newPropState = (approved_count >= current_configs(dac_id).proposal_threshold) ? ProposalStateHas_enough_approvals_votes : ProposalStatePending_approval;
                }
                break;
            case ProposalStatePending_finalize:
            case ProposalStateHas_enough_finalize_votes:
                approved_count = count_votes(prop, finalize_approve, dac_id);
                newPropState = (approved_count >= current_configs(dac_id).finalize_threshold) ? ProposalStateHas_enough_finalize_votes: ProposalStatePending_finalize;
            break;
        }
        if (prop.state != newPropState) {
            proposals.modify(prop, prop.proposer, [&](proposal &p) {
                p.state = newPropState;
            });
        }
    }

    void dacproposals::transferfunds(const proposal &prop, name dac_id) {
        proposal_table proposals(_self, dac_id.value);
        config configs = current_configs(dac_id);
        auto treasury = dacdir::dac_for_id(dac_id).account_for_type(dacdir::TREASURY);
        auto escrow = dacdir::dac_for_id(dac_id).account_for_type(dacdir::ESCROW);

        eosio::action(
                eosio::permission_level{ treasury, "escrow"_n },
                escrow, "approve"_n,
                make_tuple( prop.key, treasury)
            ).send();

        clearprop(prop, dac_id);
    }

    void dacproposals::clearprop(const proposal& proposal, name dac_id){

        proposal_table proposals(_self, dac_id.value);

        // Remove all the votes associated with that proposal.
        proposal_vote_table prop_votes(_self, dac_id.value);
        auto by_voters = prop_votes.get_index<"proposal"_n>();
        auto itr = by_voters.find(proposal.key);
        while(itr != by_voters.end() && itr->proposal_id == proposal.key) {
            print(itr->voter);
            itr = by_voters.erase(itr);
        }
        auto prop_to_erase = proposals.find(proposal.key);
        
        proposals.erase(prop_to_erase);
    }

    int16_t dacproposals::count_votes(proposal prop, VoteType vote_type, name dac_id){
        auto custodian_data_src = dacdir::dac_for_id(dac_id).account_for_type(dacdir::CUSTODIAN);

        print("count votes with account:: ", custodian_data_src, " scope:: ", dac_id);

        custodians_table custodians(custodian_data_src, dac_id.value);
        std::set<eosio::name> current_custodians;
        // Needed for the category vote fallback to avoid duplicate votes.
        std::set<eosio::name> voted_custodians;
        for(custodian cust: custodians) {
            current_custodians.insert(cust.cust_name);
        }
        print_f("\ncurrent custodians: ");
        for(auto name: current_custodians) { print(name,", "); }

        // Find the delegated and direct votes for the current proposal 
        proposal_vote_table prop_votes(_self, dac_id.value);
        auto by_voters = prop_votes.get_index<"proposal"_n>();
        std::map<eosio::name, uint16_t> delegated_proposal_votes;
        std::set<eosio::name> approval_proposal_votes;

        auto direct_vote_itr = by_voters.find(prop.key);

        // Iterate through all votes on proposal
        while(
            direct_vote_itr != by_voters.end() && 
            direct_vote_itr->proposal_id == prop.key
            ) {
            // Check if the voter is a current custodian
            if (current_custodians.find(direct_vote_itr->voter) != current_custodians.end()) {
                voted_custodians.insert(direct_vote_itr->voter);
                // Assign vote to either a direct approved vote or a delegated vote.
                if (direct_vote_itr->delegatee) {
                    delegated_proposal_votes[direct_vote_itr->delegatee.value()]++;
                } else if (direct_vote_itr->vote && direct_vote_itr->vote.value() == vote_type) {
                    approval_proposal_votes.insert(direct_vote_itr->voter);
                }
            } else {
                //TODO: Remove vote since they are no longer a custodian.
            }
            direct_vote_itr++;
        }

        print_f("\n direct approval_proposal_votes: ");
        for(auto name: approval_proposal_votes) {print(name, ", "); }
        
        print_f("\ndelegated_proposal_vote weight: ");
        for(pair<const eosio::name, unsigned short> entry : delegated_proposal_votes) {
            print("(name: ", entry.first, " with added weight: ", entry.second,"), "); 
        }

        // Find matching category votes for the current custodians

        // Find the difference between current custodians and the ones that have already voted to avoid double votes.
        std::vector<name> nonvoting_custodians(current_custodians.size());                   
        auto end_itr = std::set_difference( current_custodians.begin(), current_custodians.end(), 
                                            voted_custodians.begin(), voted_custodians.end(), 
                                            nonvoting_custodians.begin());

        nonvoting_custodians.resize(end_itr-nonvoting_custodians.begin());
        
        print_f("\ncustodians that have not yet voted: ");
        
        for(auto name: nonvoting_custodians) { print(name, ", "); }

        // Collect category votes from custodians that have not yet voted into a map.
        auto by_category = prop_votes.get_index<"catandvoter"_n>();

        std::map<eosio::name, uint16_t> category_delegate_votes;

        for (auto custodian: nonvoting_custodians) {
            uint128_t joint_id = combine_ids(prop.category, custodian.value);
            auto vote_idx = by_category.find(joint_id);
            if (
                vote_idx != by_category.end() &&
                vote_idx->category_id &&
                vote_idx->delegatee && 
                vote_idx->voter == custodian
                ) {
                category_delegate_votes[vote_idx->delegatee.value()]++;
            }
        }

        print("\nCategory votes delegated from custodians that have not yet voted for this proposal: ");
        print("\n( based on the proposal having category: ", prop.category, " )");
        print("\n( Total should not exceed the number of non voting custodians from the previous step. )\n");
        for(auto entry: category_delegate_votes) { print("(name: ", entry.first, " vote: ", entry.second, "), "); }

        // Tally all the direct + delegated proposal + delegated category vote values
        print("\n Tally votes:\n");
        int16_t approved_count = 0;
        for (auto approval : approval_proposal_votes) {
            auto addedPropWeight = delegated_proposal_votes[approval];
            auto addedCategoryWeight = category_delegate_votes[approval];
            int16_t weight_to_add = (1 + addedPropWeight + addedCategoryWeight);
            print("\n\n Approver: ", approval, "   1 " 
                    "\n + delegated for proposal: ", addedPropWeight, 
                    "\n + delegated for category: ", addedCategoryWeight);
            approved_count += weight_to_add;
        }
        
        print("\napproved_count: ", approved_count, "\n");

        return approved_count;
    }

    // void dacproposals::assertValidMember(name member, name dac_id) {
    
    //     auto member_terms_account = dacdir::dac_for_id(dac_id).account_for_type(dacdir::TOKEN);

    //     regmembers reg_members(member_terms_account.account_name, member_terms_account.dac_id.value);
    //     memterms memberterms(member_terms_account.account_name, member_terms_account.dac_id.value);

    //     const auto &regmem = reg_members.get(member.value, "ERR::GENERAL_REG_MEMBER_NOT_FOUND::Account is not registered with members.");
    //     check((regmem.agreedterms != 0), "ERR::GENERAL_MEMBER_HAS_NOT_AGREED_TO_ANY_TERMS::Account has not agreed to any terms");
    //     auto latest_member_terms = (--memberterms.end());
    //     check(latest_member_terms->version == regmem.agreedterms, "ERR::GENERAL_MEMBER_HAS_NOT_AGREED_TO_LATEST_TERMS::Agreed terms isn't the latest.");
    // }

}