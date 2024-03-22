// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Author: ericv@google.com (Eric Veach)

#include "s2/s2lax_polygon_shape.h"

#include <algorithm>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "s2/base/casts.h"
#include <gtest/gtest.h>
#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"
#include "s2/util/coding/coder.h"
#include "s2/mutable_s2shape_index.h"
#include "s2/s1angle.h"
#include "s2/s2cap.h"
#include "s2/s2coder.h"
#include "s2/s2coder_testing.h"
#include "s2/s2contains_point_query.h"
#include "s2/s2error.h"
#include "s2/s2fractal.h"
#include "s2/s2latlng.h"
#include "s2/s2lax_loop_shape.h"
#include "s2/s2loop.h"
#include "s2/s2point.h"
#include "s2/s2pointutil.h"
#include "s2/s2polygon.h"
#include "s2/s2shape.h"
#include "s2/s2shapeutil_contains_brute_force.h"
#include "s2/s2shapeutil_testing.h"
#include "s2/s2testing.h"
#include "s2/s2text_format.h"

using absl::string_view;
using s2textformat::MakePolygonOrDie;
using std::make_unique;
using std::unique_ptr;
using std::vector;

// Verifies that EncodedS2LaxPolygonShape behaves identically to
// S2LaxPolygonShape. Also supports testing that the encoded form is identical
// to the re-encoded form.

void TestEncodedS2LaxPolygonShape(const S2LaxPolygonShape& original) {
  Encoder encoder;
  original.Encode(&encoder, s2coding::CodingHint::COMPACT);
  Decoder decoder(encoder.base(), encoder.length());
  EncodedS2LaxPolygonShape encoded;
  ASSERT_TRUE(encoded.Init(&decoder));
  EXPECT_EQ(encoded.num_loops(), original.num_loops());
  EXPECT_EQ(encoded.num_vertices(), original.num_vertices());
  EXPECT_EQ(encoded.num_edges(), original.num_edges());
  EXPECT_EQ(encoded.num_chains(), original.num_chains());
  EXPECT_EQ(encoded.dimension(), original.dimension());
  EXPECT_EQ(encoded.is_empty(), original.is_empty());
  EXPECT_EQ(encoded.is_full(), original.is_full());
  EXPECT_EQ(encoded.GetReferencePoint(), original.GetReferencePoint());
  for (int i = 0; i < original.num_loops(); ++i) {
    EXPECT_EQ(encoded.num_loop_vertices(i), original.num_loop_vertices(i));
    EXPECT_EQ(encoded.chain(i), original.chain(i));
    for (int j = 0; j < original.num_loop_vertices(i); ++j) {
      EXPECT_EQ(encoded.loop_vertex(i, j), original.loop_vertex(i, j));
      EXPECT_EQ(encoded.chain_edge(i, j), original.chain_edge(i, j));
    }
  }
  // Now test all the edges in a random order in order to exercise the cases
  // involving prev_loop_.
  vector<int> edge_ids(original.num_edges());
  std::iota(edge_ids.begin(), edge_ids.end(), 0);
  std::shuffle(edge_ids.begin(), edge_ids.end(), std::mt19937_64());
  for (int e : edge_ids) {
    EXPECT_EQ(encoded.chain_position(e), original.chain_position(e));
    EXPECT_EQ(encoded.edge(e), original.edge(e));
  }

  // Let's also test that the encoded form can be encoded, yielding the same
  // bytes as the originally encoded form.
  Encoder reencoder;
  encoded.Encode(&reencoder, s2coding::CodingHint::COMPACT);
  ASSERT_EQ(string_view(encoder.base(), encoder.length()),
            string_view(reencoder.base(), reencoder.length()));
}

TEST(S2LaxPolygonShape, EmptyPolygon) {
  ABSL_LOG(INFO) << "sizeof(S2LaxPolygonShape) == "
                 << sizeof(S2LaxPolygonShape);
  ABSL_LOG(INFO) << "sizeof(EncodedS2LaxPolygonShape) == "
                 << sizeof(EncodedS2LaxPolygonShape);

  S2LaxPolygonShape shape((S2Polygon()));
  EXPECT_EQ(0, shape.num_loops());
  EXPECT_EQ(0, shape.num_vertices());
  EXPECT_EQ(0, shape.num_edges());
  EXPECT_EQ(0, shape.num_chains());
  EXPECT_EQ(2, shape.dimension());
  EXPECT_TRUE(shape.is_empty());
  EXPECT_FALSE(shape.is_full());
  EXPECT_FALSE(shape.GetReferencePoint().contained);
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, Move) {
  // Construct a shape to use as the correct answer and a second identical shape
  // to be moved.
  const vector<S2LaxPolygonShape::Loop> loops = {
      s2textformat::ParsePointsOrDie("0:0, 0:3, 3:3"),
      s2textformat::ParsePointsOrDie("1:1, 2:2, 1:2")};
  const S2LaxPolygonShape correct(loops);
  S2LaxPolygonShape to_move(loops);
  s2testing::ExpectEqual(correct, to_move);

  // Test the move constructor.
  S2LaxPolygonShape move1(std::move(to_move));
  s2testing::ExpectEqual(correct, move1);
  TestEncodedS2LaxPolygonShape(move1);
  ASSERT_EQ(loops.size(), move1.num_loops());
  ASSERT_EQ(6, move1.num_vertices());
  for (int i = 0; i < loops.size(); ++i) {
    for (int j = 0; j < loops[i].size(); ++j) {
      EXPECT_EQ(loops[i][j], move1.loop_vertex(i, j));
    }
  }

  // Test the move-assignment operator.
  S2LaxPolygonShape move2;
  move2 = std::move(move1);
  s2testing::ExpectEqual(correct, move2);
  TestEncodedS2LaxPolygonShape(move2);
  ASSERT_EQ(loops.size(), move2.num_loops());
  ASSERT_EQ(6, move2.num_vertices());
  for (int i = 0; i < loops.size(); ++i) {
    for (int j = 0; j < loops[i].size(); ++j) {
      EXPECT_EQ(loops[i][j], move2.loop_vertex(i, j));
    }
  }
}

TEST(S2LaxPolygonShape, FullPolygon) {
  S2LaxPolygonShape shape(S2Polygon(s2textformat::MakeLoopOrDie("full")));
  EXPECT_EQ(1, shape.num_loops());
  EXPECT_EQ(0, shape.num_vertices());
  EXPECT_EQ(0, shape.num_edges());
  EXPECT_EQ(1, shape.num_chains());
  EXPECT_EQ(2, shape.dimension());
  EXPECT_FALSE(shape.is_empty());
  EXPECT_TRUE(shape.is_full());
  EXPECT_TRUE(shape.GetReferencePoint().contained);
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, SingleVertexPolygon) {
  // S2Polygon doesn't support single-vertex loops, so we need to construct
  // the S2LaxPolygonShape directly.
  vector<vector<S2Point>> loops;
  loops.push_back(s2textformat::ParsePointsOrDie("0:0"));
  S2LaxPolygonShape shape(loops);
  EXPECT_EQ(1, shape.num_loops());
  EXPECT_EQ(1, shape.num_vertices());
  EXPECT_EQ(1, shape.num_edges());
  EXPECT_EQ(1, shape.num_chains());
  EXPECT_EQ(0, shape.chain(0).start);
  EXPECT_EQ(1, shape.chain(0).length);
  auto edge = shape.edge(0);
  EXPECT_EQ(loops[0][0], edge.v0);
  EXPECT_EQ(loops[0][0], edge.v1);
  EXPECT_TRUE(edge == shape.chain_edge(0, 0));
  EXPECT_EQ(2, shape.dimension());
  EXPECT_FALSE(shape.is_empty());
  EXPECT_FALSE(shape.is_full());
  EXPECT_FALSE(shape.GetReferencePoint().contained);
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, SingleLoopPolygon) {
  // Test S2Polygon constructor.
  vector<S2Point> vertices =
      s2textformat::ParsePointsOrDie("0:0, 0:1, 1:1, 1:0");
  S2LaxPolygonShape shape(S2Polygon(make_unique<S2Loop>(vertices)));
  EXPECT_EQ(1, shape.num_loops());
  EXPECT_EQ(vertices.size(), shape.num_vertices());
  EXPECT_EQ(vertices.size(), shape.num_loop_vertices(0));
  EXPECT_EQ(vertices.size(), shape.num_edges());
  EXPECT_EQ(1, shape.num_chains());
  EXPECT_EQ(0, shape.chain(0).start);
  EXPECT_EQ(vertices.size(), shape.chain(0).length);
  for (int i = 0; i < vertices.size(); ++i) {
    EXPECT_EQ(vertices[i], shape.loop_vertex(0, i));
    auto edge = shape.edge(i);
    EXPECT_EQ(vertices[i], edge.v0);
    EXPECT_EQ(vertices[(i + 1) % vertices.size()], edge.v1);
    EXPECT_EQ(edge.v0, shape.chain_edge(0, i).v0);
    EXPECT_EQ(edge.v1, shape.chain_edge(0, i).v1);
  }
  EXPECT_EQ(2, shape.dimension());
  EXPECT_FALSE(shape.is_empty());
  EXPECT_FALSE(shape.is_full());
  EXPECT_FALSE(s2shapeutil::ContainsBruteForce(shape, S2::Origin()));
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, MultiLoopPolygon) {
  // Test vector<vector<S2Point>> constructor.  Make sure that the loops are
  // oriented so that the interior of the shape is always on the left.
  vector<S2LaxPolygonShape::Loop> loops = {
      s2textformat::ParsePointsOrDie("0:0, 0:3, 3:3"),  // CCW
      s2textformat::ParsePointsOrDie("1:1, 2:2, 1:2")   // CW
  };
  S2LaxPolygonShape shape(loops);

  EXPECT_EQ(loops.size(), shape.num_loops());
  int num_vertices = 0;
  EXPECT_EQ(loops.size(), shape.num_chains());
  for (int i = 0; i < loops.size(); ++i) {
    EXPECT_EQ(loops[i].size(), shape.num_loop_vertices(i));
    EXPECT_EQ(num_vertices, shape.chain(i).start);
    EXPECT_EQ(loops[i].size(), shape.chain(i).length);
    for (int j = 0; j < loops[i].size(); ++j) {
      EXPECT_EQ(loops[i][j], shape.loop_vertex(i, j));
      auto edge = shape.edge(num_vertices + j);
      EXPECT_EQ(loops[i][j], edge.v0);
      EXPECT_EQ(loops[i][(j + 1) % loops[i].size()], edge.v1);
    }
    num_vertices += loops[i].size();
  }
  EXPECT_EQ(num_vertices, shape.num_vertices());
  EXPECT_EQ(num_vertices, shape.num_edges());
  EXPECT_EQ(2, shape.dimension());
  EXPECT_FALSE(shape.is_empty());
  EXPECT_FALSE(shape.is_full());
  EXPECT_FALSE(s2shapeutil::ContainsBruteForce(shape, S2::Origin()));
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, MultiLoopS2Polygon) {
  // Verify that the orientation of loops representing holes is reversed when
  // converting from an S2Polygon to an S2LaxPolygonShape.
  auto polygon = MakePolygonOrDie("0:0, 0:3, 3:3; 1:1, 1:2, 2:2");
  S2LaxPolygonShape shape(*polygon);
  for (int i = 0; i < polygon->num_loops(); ++i) {
    S2Loop* loop = polygon->loop(i);
    for (int j = 0; j < loop->num_vertices(); ++j) {
      EXPECT_EQ(loop->oriented_vertex(j),
                shape.loop_vertex(i, j));
    }
  }
}

TEST(S2LaxPolygonShape, ManyLoopPolygon) {
  // Test a polygon with enough loops so that binary search is used to find
  // the loop containing a given edge.
  vector<vector<S2Point>> loops;
  for (int i = 0; i < 100; ++i) {
    S2Point center(S2LatLng::FromDegrees(0, i));
    loops.push_back(
        S2Testing::MakeRegularPoints(center, S1Angle::Degrees(0.1),
                                     S2Testing::rnd.Uniform(3)));
  }
  S2LaxPolygonShape shape(loops);
  EXPECT_EQ(loops.size(), shape.num_loops());
  int num_vertices = 0;
  EXPECT_EQ(loops.size(), shape.num_chains());
  for (int i = 0; i < loops.size(); ++i) {
    EXPECT_EQ(loops[i].size(), shape.num_loop_vertices(i));
    EXPECT_EQ(num_vertices, shape.chain(i).start);
    EXPECT_EQ(loops[i].size(), shape.chain(i).length);
    for (int j = 0; j < loops[i].size(); ++j) {
      EXPECT_EQ(loops[i][j], shape.loop_vertex(i, j));
      int e = num_vertices + j;
      EXPECT_EQ(shape.chain_position(e), S2Shape::ChainPosition(i, j));
      EXPECT_EQ(loops[i][j], shape.edge(e).v0);
      EXPECT_EQ(loops[i][(j + 1) % loops[i].size()], shape.edge(e).v1);
    }
    num_vertices += loops[i].size();
  }
  EXPECT_EQ(num_vertices, shape.num_vertices());
  EXPECT_EQ(num_vertices, shape.num_edges());

  // Now test all the edges in a random order in order to exercise the cases
  // involving prev_loop_.
  vector<std::tuple<int, int, int>> edges;
  for (int i = 0, e = 0; i < loops.size(); ++i) {
    for (int j = 0; j < loops[i].size(); ++j, ++e) {
      edges.push_back({e, i, j});
    }
  }
  std::shuffle(edges.begin(), edges.end(), std::mt19937_64());
  // TODO(user,b/210097200): Use structured bindings when we require
  // C++17 in opensource.
  for (const auto t : edges) {
    int e, i, j;
    std::tie(e, i, j) = t;
    EXPECT_EQ(shape.chain_position(e), S2Shape::ChainPosition(i, j));
    auto v0 = loops[i][j];
    auto v1 = loops[i][(j + 1) % loops[i].size()];
    EXPECT_EQ(shape.edge(e), S2Shape::Edge(v0, v1));
  }
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, DegenerateLoops) {
  vector<S2LaxPolygonShape::Loop> loops = {
      s2textformat::ParsePointsOrDie("1:1, 1:2, 2:2, 1:2, 1:3, 1:2, 1:1"),
      s2textformat::ParsePointsOrDie("0:0, 0:3, 0:6, 0:9, 0:6, 0:3, 0:0"),
      s2textformat::ParsePointsOrDie("5:5, 6:6")};
  S2LaxPolygonShape shape(loops);
  EXPECT_FALSE(shape.GetReferencePoint().contained);
  TestEncodedS2LaxPolygonShape(shape);
}

TEST(S2LaxPolygonShape, InvertedLoops) {
  vector<S2LaxPolygonShape::Loop> loops = {
      s2textformat::ParsePointsOrDie("1:2, 1:1, 2:2"),
      s2textformat::ParsePointsOrDie("3:4, 3:3, 4:4")};
  S2LaxPolygonShape shape(loops);
  EXPECT_TRUE(s2shapeutil::ContainsBruteForce(shape, S2::Origin()));
  TestEncodedS2LaxPolygonShape(shape);
}

void CompareS2LoopToShape(const S2Loop& loop, unique_ptr<S2Shape> shape) {
  MutableS2ShapeIndex index;
  index.Add(std::move(shape));
  S2Cap cap = loop.GetCapBound();
  auto query = MakeS2ContainsPointQuery(&index);
  for (int iter = 0; iter < 100; ++iter) {
    S2Point point = S2Testing::SamplePoint(cap);
    EXPECT_EQ(loop.Contains(point), query.ShapeContains(0, point));
  }
}

TEST(S2LaxPolygonShape, CompareToS2Loop) {
  for (int iter = 0; iter < 100; ++iter) {
    S2Fractal fractal;
    fractal.set_max_level(S2Testing::rnd.Uniform(5));
    fractal.set_fractal_dimension(1 + S2Testing::rnd.RandDouble());
    S2Point center = S2Testing::RandomPoint();
    unique_ptr<S2Loop> loop(fractal.MakeLoop(
        S2Testing::GetRandomFrameAt(center), S1Angle::Degrees(5)));

    // Compare S2Loop to S2LaxLoopShape.
    CompareS2LoopToShape(*loop, make_unique<S2LaxLoopShape>(*loop));

    // Compare S2Loop to S2LaxPolygonShape.
    vector<S2LaxPolygonShape::Loop> loops(
        1, vector<S2Point>(&loop->vertex(0),
                           &loop->vertex(0) + loop->num_vertices()));
    CompareS2LoopToShape(*loop, make_unique<S2LaxPolygonShape>(loops));
  }
}

// TODO(b/222446546): Decoding EncodedS2PointVector on ARM isn't currently
// supported, so comment out S2Coder test on ARM for now.
#ifndef __arm__
TEST(S2LaxPolygonShape, S2CoderWorks) {
  vector<S2LaxPolygonShape::Loop> loops = {
      s2textformat::ParsePointsOrDie("1:1, 1:2, 2:2, 1:2, 1:3, 1:2, 1:1"),
      s2textformat::ParsePointsOrDie("0:0, 0:3, 0:6, 0:9, 0:6, 0:3, 0:0"),
      s2textformat::ParsePointsOrDie("5:5, 6:6")};
  S2LaxPolygonShape shape(loops);

  S2Error error;
  auto decoded = s2coding::RoundTrip(S2LaxPolygonShape::Coder(), shape, error);
  s2testing::ExpectEqual(decoded, shape);
}
#endif

TEST(S2LaxPolygonShape, ChainIteratorWorks) {
  vector<S2LaxPolygonShape::Loop> loops;
  loops.push_back(s2textformat::ParsePointsOrDie("0:0, 0:5, 5:5, 5:2.5, 5:0"));
  loops.push_back(s2textformat::ParsePointsOrDie("1:1, 1:4, 4:4, 4:1"));
  loops.push_back(s2textformat::ParsePointsOrDie("2:2, 2:3, 3:2"));

  S2LaxPolygonShape shape(loops);
  S2Shape::ChainIterator it = shape.chains().begin();
  S2Shape::ChainIterator it1(&shape, 1);
  S2Shape::ChainIterator end = shape.chains().end();

  int chain_counter = 0;
  for (auto chain : shape.chains()) {
    EXPECT_EQ(chain.length, 5 - chain_counter);
    ++chain_counter;
  }

  EXPECT_EQ(chain_counter, shape.num_chains());
  EXPECT_NE(it, end);
  EXPECT_EQ((*it).start, 0);
  EXPECT_EQ((*it).length, 5);
  EXPECT_EQ((*(++it)).start, 5);
  EXPECT_EQ((*it).length, 4);
  EXPECT_EQ(it, it1);
  EXPECT_EQ((*(++it)).start, 9);
  EXPECT_EQ((*it).length, 3);
  EXPECT_EQ(++it, end);
}

TEST(S2LaxPolygonShape, ChainVertexIteratorWorks) {
  vector<S2LaxPolygonShape::Loop> loops;
  loops.push_back(s2textformat::ParsePointsOrDie("0:0, 0:5, 5:5, 5:2.5, 5:0"));
  loops.push_back(s2textformat::ParsePointsOrDie("1:1, 1:4, 4:4, 4:1"));
  loops.push_back(s2textformat::ParsePointsOrDie("2:2, 2:3, 3:2"));
  loops.push_back(s2textformat::ParsePointsOrDie("2.05:2.05, 2.1:2.1"));

  S2LaxPolygonShape shape(loops);

  int chain_counter = 0;
  for (auto chain : shape.chains()) {
    S2Shape::ChainVertexRange vertices(&shape, chain);
    EXPECT_EQ(vertices.num_vertices(), loops[chain_counter].size());

    auto it1 = vertices.begin();
    auto it2 = it1;
    int vertex_index = 0;
    for (S2Point p : vertices) {
      EXPECT_EQ(p, loops[chain_counter][vertex_index]);
      EXPECT_EQ(p, *S2Shape::ChainVertexIterator(&shape, chain, vertex_index));

      EXPECT_NE(it1, vertices.end());
      EXPECT_NE(it2, vertices.end());
      EXPECT_NE(it1++, vertices.end());
      ++it2;
      ++vertex_index;
    }
    EXPECT_EQ(it1, vertices.end());
    EXPECT_EQ(it2, vertices.end());

    // Testing with STL algorithms and containers.
    vector<S2Point> copy1(vertices.begin(), vertices.end());
    vector<S2Point> copy2(vertices.num_vertices());
    std::copy(vertices.begin(), vertices.end(), copy2.begin());
    for (int i = 0; i < vertices.num_vertices(); ++i) {
      EXPECT_EQ(copy1[i], loops[chain_counter][i]);
      EXPECT_EQ(copy2[i], loops[chain_counter][i]);
    }

    ++chain_counter;
  }
}
