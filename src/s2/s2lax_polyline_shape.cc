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

#include "s2/s2lax_polyline_shape.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"
#include "absl/types/span.h"
#include "absl/utility/utility.h"
#include "s2/util/coding/coder.h"
#include "s2/encoded_s2point_vector.h"
#include "s2/s2coder.h"
#include "s2/s2error.h"
#include "s2/s2point.h"
#include "s2/s2polyline.h"
#include "s2/s2shape.h"

using absl::MakeSpan;
using absl::Span;
using std::make_unique;

S2LaxPolylineShape::S2LaxPolylineShape(S2LaxPolylineShape&& other) noexcept
    : num_vertices_(std::exchange(other.num_vertices_, 0)),
      vertices_(std::move(other.vertices_)) {}

S2LaxPolylineShape& S2LaxPolylineShape::operator=(
    S2LaxPolylineShape&& other) noexcept {
  num_vertices_ = std::exchange(other.num_vertices_, 0);
  vertices_ = std::move(other.vertices_);
  return *this;
}

S2LaxPolylineShape::S2LaxPolylineShape(Span<const S2Point> vertices) {
  Init(vertices);
}

S2LaxPolylineShape::S2LaxPolylineShape(const S2Polyline& polyline) {
  Init(polyline);
}

void S2LaxPolylineShape::Init(Span<const S2Point> vertices) {
  num_vertices_ = vertices.size();
  ABSL_LOG_IF(WARNING, num_vertices_ == 1)
      << "s2shapeutil::S2LaxPolylineShape with one vertex has no edges";
  vertices_ = make_unique<S2Point[]>(num_vertices_);
  std::copy(vertices.begin(), vertices.end(), vertices_.get());
}

void S2LaxPolylineShape::Init(const S2Polyline& polyline) {
  num_vertices_ = polyline.num_vertices();
  ABSL_LOG_IF(WARNING, num_vertices_ == 1)
      << "s2shapeutil::S2LaxPolylineShape with one vertex has no edges";
  vertices_ = make_unique<S2Point[]>(num_vertices_);
  std::copy(&polyline.vertex(0), &polyline.vertex(0) + num_vertices_,
            vertices_.get());
}

void S2LaxPolylineShape::Encode(Encoder* encoder,
                                s2coding::CodingHint hint) const {
  s2coding::EncodeS2PointVector(MakeSpan(vertices_.get(), num_vertices_),
                                hint, encoder);
}

bool S2LaxPolylineShape::Init(Decoder* decoder) {
  s2coding::EncodedS2PointVector vertices;
  if (!vertices.Init(decoder)) return false;
  num_vertices_ = vertices.size();
  vertices_ = make_unique<S2Point[]>(vertices.size());
  return vertices.Decode(absl::MakeSpan(vertices_.get(), vertices.size()));
}

bool S2LaxPolylineShape::Init(Decoder* decoder, S2Error& error) {
  s2coding::EncodedS2PointVector vertices;
  if (!vertices.Init(decoder, error)) return false;
  num_vertices_ = vertices.size();
  vertices_ = make_unique<S2Point[]>(vertices.size());
  vertices.Decode(absl::MakeSpan(vertices_.get(), vertices.size()), error);
  return error.ok();
}

S2Shape::Edge S2LaxPolylineShape::edge(int e) const {
  ABSL_DCHECK_LT(e, num_edges());
  return Edge(vertex(e), vertex(e + 1));
}

int S2LaxPolylineShape::num_chains() const {
  return std::min(1, S2LaxPolylineShape::num_edges());  // Avoid virtual call.
}

S2Shape::Chain S2LaxPolylineShape::chain(int i) const {
  return Chain(0, S2LaxPolylineShape::num_edges());  // Avoid virtual call.
}

S2Shape::Edge S2LaxPolylineShape::chain_edge(int i, int j) const {
  ABSL_DCHECK_EQ(i, 0);
  ABSL_DCHECK_LT(j, num_edges());
  return Edge(vertex(j), vertex(j + 1));
}

S2Shape::ChainPosition S2LaxPolylineShape::chain_position(int e) const {
  return S2Shape::ChainPosition(0, e);
}

bool EncodedS2LaxPolylineShape::Init(Decoder* decoder) {
  return vertices_.Init(decoder);
}

// The encoding must be identical to S2LaxPolylineShape::Encode().
void EncodedS2LaxPolylineShape::Encode(Encoder* encoder,
                                       s2coding::CodingHint) const {
  vertices_.Encode(encoder);
}

S2Shape::Edge EncodedS2LaxPolylineShape::edge(int e) const {
  ABSL_DCHECK_LT(e, num_edges());
  return Edge(vertex(e), vertex(e + 1));
}

int EncodedS2LaxPolylineShape::num_chains() const {
  return std::min(1, EncodedS2LaxPolylineShape::num_edges());
}

S2Shape::Chain EncodedS2LaxPolylineShape::chain(int i) const {
  return Chain(0, EncodedS2LaxPolylineShape::num_edges());
}

S2Shape::Edge EncodedS2LaxPolylineShape::chain_edge(int i, int j) const {
  ABSL_DCHECK_EQ(i, 0);
  ABSL_DCHECK_LT(j, num_edges());
  return Edge(vertex(j), vertex(j + 1));
}

S2Shape::ChainPosition EncodedS2LaxPolylineShape::chain_position(int e) const {
  return S2Shape::ChainPosition(0, e);
}
