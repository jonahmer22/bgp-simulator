// WARNING
// ngl I hate c++ and didn't write these, this is where I took liberty with the "you can use AI"
// I don't know what they do other than that they test for cycles and other stuff like announcements actually propogating

#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "bgp_utils.h"
#include "cycle_checker.h"
#include "relationships_loader.h"
#include "string_pool.h"
}

class GraphTest : public ::testing::Test {
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

TEST_F(GraphTest, LoadsRelationshipsIntoGraph) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *data = "# Sample\n1|2|-1\n2|3|-1\n2|4|0\n";
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    const ASNode *asn1 = as_graph_find_const(&graph, 1);
    const ASNode *asn2 = as_graph_find_const(&graph, 2);
    const ASNode *asn3 = as_graph_find_const(&graph, 3);
    ASSERT_NE(nullptr, asn1);
    ASSERT_NE(nullptr, asn2);
    ASSERT_NE(nullptr, asn3);

    EXPECT_EQ(1u, asn1->customers.size);
    EXPECT_EQ(2u, asn1->customers.data[0]);

    EXPECT_EQ(1u, asn2->providers.size);
    EXPECT_EQ(1u, asn2->providers.data[0]);

    EXPECT_EQ(1u, asn3->providers.size);
    EXPECT_EQ(2u, asn3->providers.data[0]);

    char *cycle_error = NULL;
    EXPECT_EQ(0, cycle_checker_ensure_acyclic(&graph, &cycle_error));
    EXPECT_EQ(nullptr, cycle_error);

    as_graph_free(&graph);
}

TEST_F(GraphTest, DetectsSimpleCycle) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *cyclic_data = "10|11|-1\n11|10|-1\n";
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(cyclic_data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    char *cycle_error = NULL;
    EXPECT_NE(0, cycle_checker_ensure_acyclic(&graph, &cycle_error));
    EXPECT_NE(nullptr, cycle_error);

    as_graph_free(&graph);
}

TEST_F(GraphTest, DetectsCustomerCycleWithMultipleHops) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *data =
        "1|2|-1\n"
        "2|3|-1\n"
        "3|1|-1\n";
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    char *cycle_error = NULL;
    EXPECT_NE(0, cycle_checker_ensure_acyclic(&graph, &cycle_error));
    ASSERT_NE(nullptr, cycle_error);
    std::string cycle_message(cycle_error);
    EXPECT_NE(std::string::npos, cycle_message.find("1"));
    EXPECT_NE(std::string::npos, cycle_message.find("2"));
    EXPECT_NE(std::string::npos, cycle_message.find("3"));

    as_graph_free(&graph);
}

TEST_F(GraphTest, HandlesPeerAndBidirectionalRelations) {
    ASGraph graph;
    as_graph_init(&graph);

    const char *data =
        "1|2|-1\n"
        "3|2|1\n"
        "2|4|0\n";
    char *error = NULL;
    ASSERT_EQ(0, relationships_loader_load_string(data, &graph, &error));
    EXPECT_EQ(nullptr, error);

    const ASNode *provider = as_graph_find_const(&graph, 1);
    ASSERT_NE(nullptr, provider);
    EXPECT_EQ(1u, provider->customers.size);
    EXPECT_EQ(2u, provider->customers.data[0]);

    const ASNode *center = as_graph_find_const(&graph, 2);
    ASSERT_NE(nullptr, center);
    EXPECT_EQ(1u, center->providers.size);
    EXPECT_EQ(1u, center->providers.data[0]);
    EXPECT_EQ(1u, center->customers.size);
    EXPECT_EQ(3u, center->customers.data[0]);
    EXPECT_EQ(1u, center->peers.size);
    EXPECT_EQ(4u, center->peers.data[0]);

    const ASNode *customer = as_graph_find_const(&graph, 3);
    ASSERT_NE(nullptr, customer);
    EXPECT_EQ(1u, customer->providers.size);
    EXPECT_EQ(2u, customer->providers.data[0]);

    const ASNode *peer = as_graph_find_const(&graph, 4);
    ASSERT_NE(nullptr, peer);
    EXPECT_EQ(1u, peer->peers.size);
    EXPECT_EQ(2u, peer->peers.data[0]);

    as_graph_free(&graph);
}
