// WARNING
// ngl I hate c++ and didn't write these, this is where I took liberty with the "you can use AI"
// I don't know what they do other than that they test for cycles and other stuff like announcements actually propogating

#include <gtest/gtest.h>

extern "C" {
#include "announcement.h"
#include "as_graph.h"
#include "bgp_utils.h"
#include "cycle_checker.h"
#include "policy.h"
#include "propagation.h"
#include "relationships_loader.h"
#include "rov_loader.h"
#include "string_pool.h"
}

class SystemTest : public ::testing::Test {
  protected:
    void SetUp() override {
        bgp_arena_reset();
        string_pool_reset();
    }

    void TearDown() override {
        bgp_arena_reset();
        string_pool_reset();
    }
};

TEST_F(SystemTest, ROVNodePrefersValidPaths) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *data =
        "2|1|-1\n"
        "2|3|-1\n"
        "4|2|-1\n"
        "4|5|-1\n"
        "2|6|0\n";
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    ROVSet rov_set;
    rov_set_init(&rov_set);
    rov_set_insert(&rov_set, 2);

    size_t node_count = 0;
    ASNode **nodes = as_graph_nodes(&graph, &node_count);
    for (size_t i = 0; i < node_count; ++i) {
        bool use_rov = rov_set_contains(&rov_set, nodes[i]->asn);
        nodes[i]->policy = policy_create(use_rov);
    }

    uint32_t prefix_id = string_pool_intern("10.0.0.0/24");
    uint32_t origins[] = {1, 3};
    bool invalid_flags[] = {false, true};
    for (size_t i = 0; i < 2; ++i) {
        ASNode *node = as_graph_get_or_create(&graph, origins[i]);
        if (!node->policy) {
            bool use_rov = rov_set_contains(&rov_set, node->asn);
            node->policy = policy_create(use_rov);
        }
        Announcement ann;
        announcement_init(&ann);
        announcement_set_prefix_id(&ann, prefix_id);
        announcement_push_asn(&ann, origins[i]);
        ann.next_hop_asn = origins[i];
        ann.received_relation = RELATION_ORIGIN;
        ann.rov_invalid = invalid_flags[i];
        policy_add_origin(node->policy, &ann, origins[i]);
        announcement_free(&ann);
    }

    char *cycle_error = NULL;
    ASSERT_EQ(0, cycle_checker_ensure_acyclic(&graph, &cycle_error));
    EXPECT_EQ(nullptr, cycle_error);

    as_graph_compute_propagation_ranks(&graph);
    propagation_run(&graph);

    ASNode *rov_node = as_graph_find(&graph, 2);
    ASSERT_NE(nullptr, rov_node);
    ASSERT_NE(nullptr, rov_node->policy);
    const UInt32Map *rov_rib = policy_local_rib(rov_node->policy);
    auto *rov_entry = (Announcement *)uint32_map_get(rov_rib, prefix_id);
    ASSERT_NE(nullptr, rov_entry);
    EXPECT_EQ(2u, rov_entry->as_path.size);
    EXPECT_EQ(2u, rov_entry->as_path.data[0]);
    EXPECT_EQ(1u, rov_entry->as_path.data[1]);

    ASNode *provider = as_graph_find(&graph, 4);
    ASSERT_NE(nullptr, provider);
    ASSERT_NE(nullptr, provider->policy);
    auto *provider_entry =
        (Announcement *)uint32_map_get(policy_local_rib(provider->policy), prefix_id);
    ASSERT_NE(nullptr, provider_entry);
    EXPECT_EQ(4u, provider_entry->as_path.data[0]);

    ASNode *downstream = as_graph_find(&graph, 5);
    ASSERT_NE(nullptr, downstream);
    ASSERT_NE(nullptr, downstream->policy);
    auto *downstream_entry =
        (Announcement *)uint32_map_get(policy_local_rib(downstream->policy), prefix_id);
    ASSERT_NE(nullptr, downstream_entry);
    EXPECT_EQ(5u, downstream_entry->as_path.data[0]);

    ASNode *peer = as_graph_find(&graph, 6);
    ASSERT_NE(nullptr, peer);
    ASSERT_NE(nullptr, peer->policy);
    auto *peer_entry =
        (Announcement *)uint32_map_get(policy_local_rib(peer->policy), prefix_id);
    ASSERT_NE(nullptr, peer_entry);
    EXPECT_GE(peer_entry->as_path.size, 2u);
    EXPECT_EQ(2u, peer_entry->as_path.data[1]);

    rov_set_free(&rov_set);
    as_graph_free(&graph);
}

TEST_F(SystemTest, CustomerRoutesPreferredOverPeersAndProviders) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *data =
        "2|1|-1\n"  // 1 is a customer of 2
        "2|3|0\n"   // peer relationship
        "4|2|-1\n"; // 4 is a provider of 2
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    ROVSet rov_set;
    rov_set_init(&rov_set);

    size_t node_count = 0;
    ASNode **nodes = as_graph_nodes(&graph, &node_count);
    for (size_t i = 0; i < node_count; ++i) {
        nodes[i]->policy = policy_create(false);
    }

    uint32_t prefix_id = string_pool_intern("192.0.2.0/24");
    uint32_t origins[] = {1, 3, 4};
    for (size_t i = 0; i < 3; ++i) {
        ASNode *node = as_graph_get_or_create(&graph, origins[i]);
        if (!node->policy) {
            node->policy = policy_create(false);
        }
        Announcement ann;
        announcement_init(&ann);
        announcement_set_prefix_id(&ann, prefix_id);
        announcement_push_asn(&ann, origins[i]);
        ann.next_hop_asn = origins[i];
        ann.received_relation = RELATION_ORIGIN;
        ann.rov_invalid = false;
        policy_add_origin(node->policy, &ann, origins[i]);
        announcement_free(&ann);
    }

    char *cycle_error = NULL;
    ASSERT_EQ(0, cycle_checker_ensure_acyclic(&graph, &cycle_error));
    EXPECT_EQ(nullptr, cycle_error);

    as_graph_compute_propagation_ranks(&graph);
    propagation_run(&graph);

    ASNode *center = as_graph_find(&graph, 2);
    ASSERT_NE(nullptr, center);
    ASSERT_NE(nullptr, center->policy);
    const UInt32Map *rib = policy_local_rib(center->policy);
    auto *entry = (Announcement *)uint32_map_get(rib, prefix_id);
    ASSERT_NE(nullptr, entry);
    ASSERT_GE(entry->as_path.size, 2u);
    EXPECT_EQ(2u, entry->as_path.data[0]);
    EXPECT_EQ(1u, entry->as_path.data[1]);
    EXPECT_EQ(RELATION_CUSTOMER, entry->received_relation);
    EXPECT_EQ(1u, entry->next_hop_asn);

    rov_set_free(&rov_set);
    as_graph_free(&graph);
}

TEST_F(SystemTest, OriginIgnoresAnnouncementsContainingSelf) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *data = "1|2|0\n";
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    ROVSet rov_set;
    rov_set_init(&rov_set);

    size_t node_count = 0;
    ASNode **nodes = as_graph_nodes(&graph, &node_count);
    for (size_t i = 0; i < node_count; ++i) {
        nodes[i]->policy = policy_create(false);
    }

    uint32_t prefix_id = string_pool_intern("203.0.113.0/24");
    ASNode *origin = as_graph_find(&graph, 1);
    ASSERT_NE(nullptr, origin);
    Announcement ann;
    announcement_init(&ann);
    announcement_set_prefix_id(&ann, prefix_id);
    announcement_push_asn(&ann, origin->asn);
    ann.next_hop_asn = origin->asn;
    ann.received_relation = RELATION_ORIGIN;
    ann.rov_invalid = false;
    policy_add_origin(origin->policy, &ann, origin->asn);
    announcement_free(&ann);

    char *cycle_error = NULL;
    ASSERT_EQ(0, cycle_checker_ensure_acyclic(&graph, &cycle_error));
    EXPECT_EQ(nullptr, cycle_error);

    as_graph_compute_propagation_ranks(&graph);
    propagation_run(&graph);

    ASNode *peer = as_graph_find(&graph, 2);
    ASSERT_NE(nullptr, peer);
    auto *peer_entry =
        (Announcement *)uint32_map_get(policy_local_rib(peer->policy), prefix_id);
    ASSERT_NE(nullptr, peer_entry);
    EXPECT_EQ(2u, peer_entry->as_path.size);
    EXPECT_EQ(2u, peer_entry->as_path.data[0]);
    EXPECT_EQ(1u, peer_entry->as_path.data[1]);

    auto *origin_entry =
        (Announcement *)uint32_map_get(policy_local_rib(origin->policy), prefix_id);
    ASSERT_NE(nullptr, origin_entry);
    EXPECT_EQ(1u, origin_entry->as_path.size);
    EXPECT_EQ(1u, origin_entry->as_path.data[0]);
    EXPECT_EQ(origin->asn, origin_entry->next_hop_asn);

    rov_set_free(&rov_set);
    as_graph_free(&graph);
}
