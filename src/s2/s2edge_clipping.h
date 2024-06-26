// Copyright 2005 Google Inc. All Rights Reserved.
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
//
// Defines a collection of functions for:
//
//   (1) Robustly clipping geodesic edges to the faces of the S2 biunit cube
//       (see s2coords.h), and
//
//   (2) Robustly clipping 2D edges against 2D rectangles.
//
// These functions can be used to efficiently find the set of S2CellIds that
// are intersected by a geodesic edge (e.g., see S2CrossingEdgeQuery).

#ifndef S2_S2EDGE_CLIPPING_H_
#define S2_S2EDGE_CLIPPING_H_

#include <cfloat>
#include <cmath>

#include "absl/container/inlined_vector.h"
#include "absl/log/absl_check.h"
#include "s2/_fp_contract_off.h"
#include "s2/r2.h"
#include "s2/r2rect.h"
#include "s2/s2point.h"

namespace S2 {

// FaceSegment represents an edge AB clipped to an S2 cube face.  It is
// represented by a face index and a pair of (u,v) coordinates.
struct FaceSegment {
  int face;
  R2Point a, b;
};
using FaceSegmentVector = absl::InlinedVector<FaceSegment, 6>;

// Subdivides the given edge AB at every point where it crosses the boundary
// between two S2 cube faces and returns the corresponding FaceSegments.  The
// segments are returned in order from A toward B.  The input points must be
// unit length.
//
// This method guarantees that the returned segments form a continuous path
// from A to B, and that all vertices are within kFaceClipErrorUVDist of the
// line AB.  All vertices lie within the [-1,1]x[-1,1] cube face rectangles.
// The results are consistent with s2pred::Sign(), i.e. the edge is
// well-defined even its endpoints are antipodal.
void GetFaceSegments(const S2Point& a, const S2Point& b,
                     FaceSegmentVector* segments);

// Given an edge AB and a face, returns the (u,v) coordinates for the portion
// of AB that intersects that face.  This method guarantees that the clipped
// vertices lie within the [-1,1]x[-1,1] cube face rectangle and are within
// kFaceClipErrorUVDist of the line AB, but the results may differ from
// those produced by GetFaceSegments.
//
// Returns false if AB does not intersect the given face.
//
// The test for face intersection is exact, so if this function returns false
// then the edge definitively does not intersect the face.
bool ClipToFace(const S2Point& a, const S2Point& b, int face,
                R2Point* a_uv, R2Point* b_uv);

// Like ClipToFace, but rather than clipping to the square [-1,1]x[-1,1]
// in (u,v) space, this method clips to [-R,R]x[-R,R] where R=(1+padding).
bool ClipToPaddedFace(const S2Point& a, const S2Point& b, int face,
                      double padding, R2Point* a_uv, R2Point* b_uv);

// The maximum error in the vertices returned by GetFaceSegments and
// ClipToFace (compared to an exact calculation):
//
//  - kFaceClipErrorRadians is the maximum angle between a returned vertex
//    and the nearest point on the exact edge AB.  It is equal to the
//    maximum directional error in S2::RobustCrossProd, plus the error when
//    projecting points onto a cube face.
//
//  - kFaceClipErrorDist is the same angle expressed as a maximum distance
//    in (u,v)-space.  In other words, a returned vertex is at most this far
//    from the exact edge AB projected into (u,v)-space.

//  - kFaceClipErrorUVCoord is the same angle expressed as the maximum error
//    in an individual u- or v-coordinate.  In other words, for each
//    returned vertex there is a point on the exact edge AB whose u- and
//    v-coordinates differ from the vertex by at most this amount.

constexpr double kFaceClipErrorRadians = 3 * DBL_EPSILON;
constexpr double kFaceClipErrorUVDist = 9 * DBL_EPSILON;
constexpr double kFaceClipErrorUVCoord = 9 * M_SQRT1_2 * DBL_EPSILON;

// Returns true if the edge AB intersects the given (closed) rectangle to
// within the error bound below.
bool IntersectsRect(const R2Point& a, const R2Point& b, const R2Rect& rect);

// The maximum error in IntersectRect.  If some point of AB is inside the
// rectangle by at least this distance, the result is guaranteed to be true;
// if all points of AB are outside the rectangle by at least this distance,
// the result is guaranteed to be false.  This bound assumes that "rect" is
// a subset of the rectangle [-1,1]x[-1,1] or extends slightly outside it
// (e.g., by 1e-10 or less).
constexpr double kIntersectsRectErrorUVDist = 3 * M_SQRT2 * DBL_EPSILON;

// Given an edge AB, returns the portion of AB that is contained by the given
// rectangle "clip".  Returns false if there is no intersection.
bool ClipEdge(const R2Point& a, const R2Point& b, const R2Rect& clip,
              R2Point* a_clipped, R2Point* b_clipped);

// Given an edge AB and a rectangle "clip", returns the bounding rectangle of
// the portion of AB intersected by "clip".  The resulting bound may be
// empty.  This is a convenience function built on top of ClipEdgeBound.
R2Rect GetClippedEdgeBound(const R2Point& a, const R2Point& b,
                           const R2Rect& clip);

// This function can be used to clip an edge AB to sequence of rectangles
// efficiently.  It represents the clipped edges by their bounding boxes
// rather than as a pair of endpoints.  Specifically, let A'B' be some
// portion of an edge AB, and let "bound" be a tight bound of A'B'.  This
// function updates "bound" (in place) to be a tight bound of A'B'
// intersected with a given rectangle "clip".  If A'B' does not intersect
// "clip", returns false and does not necessarily update "bound".
//
// REQUIRES: "bound" is a tight bounding rectangle for some portion of AB.
// (This condition is automatically satisfied if you start with the bounding
// box of AB and clip to a sequence of rectangles, stopping when the method
// returns false.)
bool ClipEdgeBound(const R2Point& a, const R2Point& b,
                   const R2Rect& clip, R2Rect* bound);

// The maximum error in the vertices generated by ClipEdge and the bounds
// generated by ClipEdgeBound (compared to an exact calculation):
//
//  - kEdgeClipErrorUVCoord is the maximum error in a u- or v-coordinate
//    compared to the exact result, assuming that the points A and B are in
//    the rectangle [-1,1]x[1,1] or slightly outside it (by 1e-10 or less).
//
//  - kEdgeClipErrorUVDist is the maximum distance from a clipped point to
//    the corresponding exact result.  It is equal to the error in a single
//    coordinate because at most one coordinate is subject to error.

constexpr double kEdgeClipErrorUVCoord = 2.25 * DBL_EPSILON;
constexpr double kEdgeClipErrorUVDist = 2.25 * DBL_EPSILON;

// Given a value x that is some linear combination of a and b, returns the
// value x1 that is the same linear combination of a1 and b1.  This function
// makes the following guarantees:
//  - If x == a, then x1 = a1 (exactly).
//  - If x == b, then x1 = b1 (exactly).
//  - If a <= x <= b and a1 <= b1, then a1 <= x1 <= b1 (even if a1 == b1).
//  - More generally, if x is between a and b, then x1 is between a1 and b1.
// REQUIRES: a != b
//
// When a <= x <= b or b <= x <= a we can prove the error bound on the resulting
// value is 2.25*DBL_EPSILON.  The error for extrapolating an x value outside of
// a and b can be much worse.  See the gappa proof at the end of the file.
double InterpolateDouble(double x, double a, double b, double a1, double b1);


//////////////////   Implementation details follow   ////////////////////


inline bool ClipToFace(const S2Point& a, const S2Point& b, int face,
                       R2Point* a_uv, R2Point* b_uv) {
  return ClipToPaddedFace(a, b, face, 0.0, a_uv, b_uv);
}

inline double InterpolateDouble(double x, double a, double b,
                                double a1, double b1) {
  // If A == B == X all we can return is the single point.
  if (a == b) {
    ABSL_DCHECK(x == a && a1 == b1);
    return a1;
  }

  ABSL_DCHECK_NE(a, b);
  // To get results that are accurate near both A and B, we interpolate
  // starting from the closer of the two points.
  if (std::fabs(a - x) <= std::fabs(b - x)) {
    return a1 + (b1 - a1) * ((x - a) / (b - a));
  } else {
    return b1 + (a1 - b1) * ((x - b) / (a - b));
  }
}

// Gappa proof of bounds for InterpolateDouble
//
// NOTE: this proof is only valid for a <= x <= b or b <= x <= a, not for
// extrapolating values outside of the input range.
// -----------------------------------------------------------------------------
//
// # Use IEEE754 double precision, round-to-nearest by default.
// @rnd = float<ieee_64, ne>;
//
// # Define values to be floating point numbers (rounded reals).
// x  = rnd(x_ex);
// a  = rnd(a_ex);
// b  = rnd(b_ex);
// a1 = rnd(a1_ex);
// b1 = rnd(b1_ex);
//
// # Compute answer in floating point and exact arithmetic.
// InterpolateDouble_fp rnd = a1 + (b1-a1)*((x-a)/(b-a));
// InterpolateDouble_ex     = a1 + (b1-a1)*((x-a)/(b-a));
//
// {
//   # We operate in UV space so inputs are always in [-1,1].
//   |x|  in [0,1] /\
//   |a|  in [0,1] /\
//   |b|  in [0,1] /\
//   |a1| in [0,1] /\
//   |b1| in [0,1] /\
//
//   # b != a is asserted by the algorithm.
//   b-a <> 0 /\
//
//   # Either a <= x <= b or b <= x <= a, and we either do (x-a) or (x-b)
//   # depending on which endpoint is closer to x.  So the ratio (x-a)/(b-a) can
//   # only be up to one half of the total interval before we switch.
//   rnd(x-a)/rnd(b-a) in [0,0.5]
//
//   # Estimate absolute error.
//   -> InterpolateDouble_fp - InterpolateDouble_ex in ?
// }
//
// -----------------------------------------------------------------------------
// > gappa interpolate.gappa
// Results:
//   InterpolateDouble_fp - InterpolateDouble_ex in
//       [-324259173170675769b-109 {-4.996e-16, -2^(-50.8301)},
//         324259173170675769b-109 {+4.996e-16, +2^(-50.8301)}]
//
// 324259173170675769*2**-109/DBL_EPSILON == 2.25

}  // namespace S2

#endif  // S2_S2EDGE_CLIPPING_H_
