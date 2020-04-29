#ifndef VPR_TIMING_UTIL_H
#define VPR_TIMING_UTIL_H
#include <vector>

#include "tatum/timing_analyzers.hpp"
#include "tatum/TimingConstraints.hpp"
#include "tatum/timing_paths.hpp"
#include "histogram.h"
#include "timing_info_fwd.h"
#include "DomainPair.h"
#include "globals.h"

#include "vpr_utils.h"
#include "clustered_netlist_utils.h"

double sec_to_nanosec(double seconds);

double sec_to_mhz(double seconds);

/*
 * Setup-time related statistics
 */

//Returns the path delay of the longest critical timing path (i.e. across all domains)
tatum::TimingPathInfo find_longest_critical_path_delay(const tatum::TimingConstraints& constraints, const tatum::SetupTimingAnalyzer& setup_analyzer);

//Returns the path delay of the least-slack critical timing path (i.e. across all domains)
tatum::TimingPathInfo find_least_slack_critical_path_delay(const tatum::TimingConstraints& constraints, const tatum::SetupTimingAnalyzer& setup_analyzer);

//Returns the total negative slack (setup) of all timing end-points and clock domain pairs
float find_setup_total_negative_slack(const tatum::SetupTimingAnalyzer& setup_analyzer);

//Returns the worst negative slack (setup) across all timing end-points and clock domain pairs
float find_setup_worst_negative_slack(const tatum::SetupTimingAnalyzer& setup_analyzer);

//Returns the slack at a particular node for the specified clock domains (if found), otherwise NAN
float find_node_setup_slack(const tatum::SetupTimingAnalyzer& setup_analyzer, tatum::NodeId node, tatum::DomainId launch_domain, tatum::DomainId capture_domain);

//Returns a setup slack histogram
std::vector<HistogramBucket> create_setup_slack_histogram(const tatum::SetupTimingAnalyzer& setup_analyzer, size_t num_bins = 10);

//Returns a criticality histogram
std::vector<HistogramBucket> create_criticality_histogram(const SetupTimingInfo& setup_timing,
                                                          const ClusteredPinAtomPinsLookup& netlist_pin_lookup,
                                                          size_t num_bins = 10);

//Print a useful summary of timing information
void print_setup_timing_summary(const tatum::TimingConstraints& constraints, const tatum::SetupTimingAnalyzer& setup_analyzer);

/*
 * Hold-time related statistics
 */
//Returns the total negative slack (hold) of all timing end-points and clock domain pairs
float find_hold_total_negative_slack(const tatum::HoldTimingAnalyzer& hold_analyzer);

//Returns the worst negative slack (hold) across all timing end-points and clock domain pairs
float find_hold_worst_negative_slack(const tatum::HoldTimingAnalyzer& hold_analyzer);

//Returns the worst slack (hold) between the specified launch and capture clock domains
float find_hold_worst_slack(const tatum::HoldTimingAnalyzer& hold_analyzer, const tatum::DomainId launch, const tatum::DomainId capture);

//Returns a setup slack histogram
std::vector<HistogramBucket> create_hold_slack_histogram(const tatum::HoldTimingAnalyzer& hold_analyzer, size_t num_bins = 10);

//Print a useful summary of timing information
void print_hold_timing_summary(const tatum::TimingConstraints& constraints, const tatum::HoldTimingAnalyzer& hold_analyzer);

float find_total_negative_slack_within_clb_blocks(const tatum::HoldTimingAnalyzer& hold_analyzer);

tatum::NodeId find_origin_node_for_hold_slack(const tatum::TimingTags::tag_range arrival_tags,
                                              const tatum::TimingTags::tag_range required_tags,
                                              float slack);

/*
 * General utilities
 */

//Returns the a map of domain's and their clock fanout (i.e. logical outputs at which the clock captures)
std::map<tatum::DomainId, size_t> count_clock_fanouts(const tatum::TimingGraph& timing_graph, const tatum::SetupTimingAnalyzer& setup_analyzer);

//Invalidates all timing graph edge delays related to the specified clustered netlist pin
template<class TimingInfo>
void invalidate_delays(const ClusterPinId clb_pin, const ClusteredPinAtomPinsLookup& clb_atom_pin_lookup, TimingInfo& timing_info) {
    auto& atom_ctx = g_vpr_ctx.atom();
    auto& atom_nlist = atom_ctx.nlist;
    auto& atom_lookup = atom_ctx.lookup;
    const auto& tg = timing_info.timing_graph();

    for (const AtomPinId atom_pin : clb_atom_pin_lookup.connected_atom_pins(clb_pin)) {
        tatum::NodeId pin_tnode = atom_lookup.atom_pin_tnode(atom_pin);
        VTR_ASSERT_SAFE(pin_tnode);

        AtomNetId atom_net = atom_nlist.pin_net(atom_pin);
        VTR_ASSERT_SAFE(atom_net);

        AtomPinId atom_net_driver = atom_nlist.net_driver(atom_net);
        VTR_ASSERT_SAFE(atom_net_driver);

        tatum::NodeId driver_tnode = atom_lookup.atom_pin_tnode(atom_net_driver);
        VTR_ASSERT_SAFE(driver_tnode);

        //Find and invalidate the incoming timing edge corresponding
        //to the connection between the net driver and sink pin
        bool found = false;
        for (tatum::EdgeId edge : tg->node_in_edges(pin_tnode)) {
            if (tg->edge_src_node(edge) == driver_tnode) {
                timing_info.invalidate_delay(edge);
                found = true;
                break;
            }
        }
        VTR_ASSERT_SAFE(found);
    }
}

//Invalidates all timing graph edge delays related to the 'ipin'th pin of 'net'
template<class TimingInfo>
void invalidate_delays(const ClusterNetId net, int ipin, const ClusteredPinAtomPinsLookup& clb_atom_pin_lookup, TimingInfo& timing_info) {
    auto& cluster_ctx = g_vpr_ctx.clustering();
    auto& clb_nlist = cluster_ctx.clb_nlist;

    ClusterPinId pin = clb_nlist.net_pin(net, ipin);

    invalidate_delays(pin, clb_atom_pin_lookup, timing_info);
}

/*
 * Slack and criticality calculation utilities
 */

//Return the criticality of a net's pin in the CLB netlist
float calculate_clb_net_pin_criticality(const SetupTimingInfo& timing_info, const ClusteredPinAtomPinsLookup& pin_lookup, ClusterPinId clb_pin);

//Returns the worst (maximum) criticality of the set of slack tags specified. Requires the maximum
//required time and worst slack for all domain pairs represent by the slack tags
//
// Criticality (in [0., 1.]) represents how timing-critical something is,
// 0. is non-critical and 1. is most-critical.
//
// This returns 'relaxed per constraint' criticaly as defined in:
//
//     M. Wainberg and V. Betz, "Robust Optimization of Multiple Timing Constraints,"
//         IEEE CAD, vol. 34, no. 12, pp. 1942-1953, Dec. 2015. doi: 10.1109/TCAD.2015.2440316
//
// which handles the trade-off between different timing constraints in multi-clock circuits.
float calc_relaxed_criticality(const std::map<DomainPair, float>& domains_max_req,
                               const std::map<DomainPair, float>& domains_worst_slack,
                               const tatum::TimingTags::tag_range tags);

/*
 * Debug
 */
void print_tatum_cpds(std::vector<tatum::TimingPathInfo> cpds);

tatum::NodeId id_or_pin_name_to_tnode(std::string name_or_id);
tatum::NodeId pin_name_to_tnode(std::string name);

void write_setup_timing_graph_dot(std::string filename, SetupTimingInfo& timing_info, tatum::NodeId debug_node = tatum::NodeId::INVALID());
void write_hold_timing_graph_dot(std::string filename, HoldTimingInfo& timing_info, tatum::NodeId debug_node = tatum::NodeId::INVALID());

#endif
