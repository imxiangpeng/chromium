// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/animation/transform_operations.h"

#include <stddef.h>

#include <limits>
#include <vector>

#include "base/memory/ptr_util.h"
#include "cc/test/geometry_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/box_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/vector3d_f.h"

namespace cc {
namespace {

void ExpectTransformOperationEqual(const TransformOperation& lhs,
                                   const TransformOperation& rhs) {
  EXPECT_EQ(lhs.type, rhs.type);
  EXPECT_TRANSFORMATION_MATRIX_EQ(lhs.matrix, rhs.matrix);
  switch (lhs.type) {
    case TransformOperation::TRANSFORM_OPERATION_TRANSLATE:
      EXPECT_FLOAT_EQ(lhs.translate.x, rhs.translate.x);
      EXPECT_FLOAT_EQ(lhs.translate.y, rhs.translate.y);
      EXPECT_FLOAT_EQ(lhs.translate.z, rhs.translate.z);
      break;
    case TransformOperation::TRANSFORM_OPERATION_ROTATE:
      EXPECT_FLOAT_EQ(lhs.rotate.axis.x, rhs.rotate.axis.x);
      EXPECT_FLOAT_EQ(lhs.rotate.axis.y, rhs.rotate.axis.y);
      EXPECT_FLOAT_EQ(lhs.rotate.axis.z, rhs.rotate.axis.z);
      EXPECT_FLOAT_EQ(lhs.rotate.angle, rhs.rotate.angle);
      break;
    case TransformOperation::TRANSFORM_OPERATION_SCALE:
      EXPECT_FLOAT_EQ(lhs.scale.x, rhs.scale.x);
      EXPECT_FLOAT_EQ(lhs.scale.y, rhs.scale.y);
      EXPECT_FLOAT_EQ(lhs.scale.z, rhs.scale.z);
      break;
    case TransformOperation::TRANSFORM_OPERATION_SKEW:
      EXPECT_FLOAT_EQ(lhs.skew.x, rhs.skew.x);
      EXPECT_FLOAT_EQ(lhs.skew.y, rhs.skew.y);
      break;
    case TransformOperation::TRANSFORM_OPERATION_PERSPECTIVE:
      EXPECT_FLOAT_EQ(lhs.perspective_depth, rhs.perspective_depth);
      break;
    case TransformOperation::TRANSFORM_OPERATION_MATRIX:
    case TransformOperation::TRANSFORM_OPERATION_IDENTITY:
      break;
  }
}

TEST(TransformOperationTest, TransformTypesAreUnique) {
  std::vector<std::unique_ptr<TransformOperations>> transforms;

  std::unique_ptr<TransformOperations> to_add(
      base::MakeUnique<TransformOperations>());
  to_add->AppendTranslate(1, 0, 0);
  transforms.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendRotate(0, 0, 1, 2);
  transforms.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendScale(2, 2, 2);
  transforms.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendSkew(1, 0);
  transforms.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendPerspective(800);
  transforms.push_back(std::move(to_add));

  for (size_t i = 0; i < transforms.size(); ++i) {
    for (size_t j = 0; j < transforms.size(); ++j) {
      bool matches_type = transforms[i]->MatchesTypes(*transforms[j]);
      EXPECT_TRUE((i == j && matches_type) || !matches_type);
    }
  }
}

TEST(TransformOperationTest, MatchTypesSameLength) {
  TransformOperations translates;
  translates.AppendTranslate(1, 0, 0);
  translates.AppendTranslate(1, 0, 0);
  translates.AppendTranslate(1, 0, 0);

  TransformOperations skews;
  skews.AppendSkew(0, 2);
  skews.AppendSkew(0, 2);
  skews.AppendSkew(0, 2);

  TransformOperations translates2;
  translates2.AppendTranslate(0, 2, 0);
  translates2.AppendTranslate(0, 2, 0);
  translates2.AppendTranslate(0, 2, 0);

  TransformOperations translates3 = translates2;

  EXPECT_FALSE(translates.MatchesTypes(skews));
  EXPECT_TRUE(translates.MatchesTypes(translates2));
  EXPECT_TRUE(translates.MatchesTypes(translates3));
}

TEST(TransformOperationTest, MatchTypesDifferentLength) {
  TransformOperations translates;
  translates.AppendTranslate(1, 0, 0);
  translates.AppendTranslate(1, 0, 0);
  translates.AppendTranslate(1, 0, 0);

  TransformOperations skews;
  skews.AppendSkew(2, 0);
  skews.AppendSkew(2, 0);

  TransformOperations translates2;
  translates2.AppendTranslate(0, 2, 0);
  translates2.AppendTranslate(0, 2, 0);

  EXPECT_FALSE(translates.MatchesTypes(skews));
  EXPECT_FALSE(translates.MatchesTypes(translates2));
}

std::vector<std::unique_ptr<TransformOperations>> GetIdentityOperations() {
  std::vector<std::unique_ptr<TransformOperations>> operations;
  std::unique_ptr<TransformOperations> to_add(
      base::MakeUnique<TransformOperations>());
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendTranslate(0, 0, 0);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendTranslate(0, 0, 0);
  to_add->AppendTranslate(0, 0, 0);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendScale(1, 1, 1);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendScale(1, 1, 1);
  to_add->AppendScale(1, 1, 1);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendSkew(0, 0);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendSkew(0, 0);
  to_add->AppendSkew(0, 0);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendRotate(0, 0, 1, 0);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendRotate(0, 0, 1, 0);
  to_add->AppendRotate(0, 0, 1, 0);
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendMatrix(gfx::Transform());
  operations.push_back(std::move(to_add));

  to_add = base::MakeUnique<TransformOperations>();
  to_add->AppendMatrix(gfx::Transform());
  to_add->AppendMatrix(gfx::Transform());
  operations.push_back(std::move(to_add));

  return operations;
}

TEST(TransformOperationTest, MatchTypesOrder) {
  TransformOperations mix_order_identity;
  mix_order_identity.AppendTranslate(0, 0, 0);
  mix_order_identity.AppendScale(1, 1, 1);
  mix_order_identity.AppendTranslate(0, 0, 0);

  TransformOperations mix_order_one;
  mix_order_one.AppendTranslate(0, 1, 0);
  mix_order_one.AppendScale(2, 1, 3);
  mix_order_one.AppendTranslate(1, 0, 0);

  TransformOperations mix_order_two;
  mix_order_two.AppendTranslate(0, 1, 0);
  mix_order_two.AppendTranslate(1, 0, 0);
  mix_order_two.AppendScale(2, 1, 3);

  EXPECT_TRUE(mix_order_identity.MatchesTypes(mix_order_one));
  EXPECT_FALSE(mix_order_identity.MatchesTypes(mix_order_two));
  EXPECT_FALSE(mix_order_one.MatchesTypes(mix_order_two));
}

TEST(TransformOperationTest, NoneAlwaysMatches) {
  std::vector<std::unique_ptr<TransformOperations>> operations =
      GetIdentityOperations();

  TransformOperations none_operation;
  for (size_t i = 0; i < operations.size(); ++i)
    EXPECT_TRUE(operations[i]->MatchesTypes(none_operation));
}

TEST(TransformOperationTest, ApplyTranslate) {
  SkMScalar x = 1;
  SkMScalar y = 2;
  SkMScalar z = 3;
  TransformOperations operations;
  operations.AppendTranslate(x, y, z);
  gfx::Transform expected;
  expected.Translate3d(x, y, z);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, operations.Apply());
}

TEST(TransformOperationTest, ApplyRotate) {
  SkMScalar x = 1;
  SkMScalar y = 2;
  SkMScalar z = 3;
  SkMScalar degrees = 80;
  TransformOperations operations;
  operations.AppendRotate(x, y, z, degrees);
  gfx::Transform expected;
  expected.RotateAbout(gfx::Vector3dF(x, y, z), degrees);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, operations.Apply());
}

TEST(TransformOperationTest, ApplyScale) {
  SkMScalar x = 1;
  SkMScalar y = 2;
  SkMScalar z = 3;
  TransformOperations operations;
  operations.AppendScale(x, y, z);
  gfx::Transform expected;
  expected.Scale3d(x, y, z);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, operations.Apply());
}

TEST(TransformOperationTest, ApplySkew) {
  SkMScalar x = 1;
  SkMScalar y = 2;
  TransformOperations operations;
  operations.AppendSkew(x, y);
  gfx::Transform expected;
  expected.Skew(x, y);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, operations.Apply());
}

TEST(TransformOperationTest, ApplyPerspective) {
  SkMScalar depth = 800;
  TransformOperations operations;
  operations.AppendPerspective(depth);
  gfx::Transform expected;
  expected.ApplyPerspectiveDepth(depth);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, operations.Apply());
}

TEST(TransformOperationTest, ApplyMatrix) {
  SkMScalar dx = 1;
  SkMScalar dy = 2;
  SkMScalar dz = 3;
  gfx::Transform expected_matrix;
  expected_matrix.Translate3d(dx, dy, dz);
  TransformOperations matrix_transform;
  matrix_transform.AppendMatrix(expected_matrix);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected_matrix, matrix_transform.Apply());
}

TEST(TransformOperationTest, ApplyOrder) {
  SkMScalar sx = 2;
  SkMScalar sy = 4;
  SkMScalar sz = 8;

  SkMScalar dx = 1;
  SkMScalar dy = 2;
  SkMScalar dz = 3;

  TransformOperations operations;
  operations.AppendScale(sx, sy, sz);
  operations.AppendTranslate(dx, dy, dz);

  gfx::Transform expected_scale_matrix;
  expected_scale_matrix.Scale3d(sx, sy, sz);

  gfx::Transform expected_translate_matrix;
  expected_translate_matrix.Translate3d(dx, dy, dz);

  gfx::Transform expected_combined_matrix = expected_scale_matrix;
  expected_combined_matrix.PreconcatTransform(expected_translate_matrix);

  EXPECT_TRANSFORMATION_MATRIX_EQ(expected_combined_matrix, operations.Apply());
}

TEST(TransformOperationTest, BlendOrder) {
  SkMScalar sx1 = 2;
  SkMScalar sy1 = 4;
  SkMScalar sz1 = 8;

  SkMScalar dx1 = 1;
  SkMScalar dy1 = 2;
  SkMScalar dz1 = 3;

  SkMScalar sx2 = 4;
  SkMScalar sy2 = 8;
  SkMScalar sz2 = 16;

  SkMScalar dx2 = 10;
  SkMScalar dy2 = 20;
  SkMScalar dz2 = 30;

  TransformOperations operations_from;
  operations_from.AppendScale(sx1, sy1, sz1);
  operations_from.AppendTranslate(dx1, dy1, dz1);

  TransformOperations operations_to;
  operations_to.AppendScale(sx2, sy2, sz2);
  operations_to.AppendTranslate(dx2, dy2, dz2);

  gfx::Transform scale_from;
  scale_from.Scale3d(sx1, sy1, sz1);
  gfx::Transform translate_from;
  translate_from.Translate3d(dx1, dy1, dz1);

  gfx::Transform scale_to;
  scale_to.Scale3d(sx2, sy2, sz2);
  gfx::Transform translate_to;
  translate_to.Translate3d(dx2, dy2, dz2);

  SkMScalar progress = 0.25f;

  TransformOperations operations_expected;
  operations_expected.AppendScale(
      gfx::Tween::FloatValueBetween(progress, sx1, sx2),
      gfx::Tween::FloatValueBetween(progress, sy1, sy2),
      gfx::Tween::FloatValueBetween(progress, sz1, sz2));

  operations_expected.AppendTranslate(
      gfx::Tween::FloatValueBetween(progress, dx1, dx2),
      gfx::Tween::FloatValueBetween(progress, dy1, dy2),
      gfx::Tween::FloatValueBetween(progress, dz1, dz2));

  gfx::Transform blended_scale = scale_to;
  blended_scale.Blend(scale_from, progress);

  gfx::Transform blended_translate = translate_to;
  blended_translate.Blend(translate_from, progress);

  gfx::Transform expected = blended_scale;
  expected.PreconcatTransform(blended_translate);

  TransformOperations blended = operations_to.Blend(operations_from, progress);

  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, blended.Apply());
  EXPECT_TRANSFORMATION_MATRIX_EQ(operations_expected.Apply(), blended.Apply());
  EXPECT_EQ(operations_expected.size(), blended.size());
  for (size_t i = 0; i < operations_expected.size(); ++i) {
    TransformOperation expected_op = operations_expected.at(i);
    TransformOperation blended_op = blended.at(i);
    SCOPED_TRACE(i);
    ExpectTransformOperationEqual(expected_op, blended_op);
  }

  // Create a mismatch, forcing matrix interpolation.
  operations_to.AppendMatrix(gfx::Transform());

  blended = operations_to.Blend(operations_from, progress);

  expected = operations_to.Apply();
  expected.Blend(operations_from.Apply(), progress);

  operations_expected = TransformOperations();
  operations_expected.AppendMatrix(expected);

  EXPECT_TRANSFORMATION_MATRIX_EQ(expected, blended.Apply());
  EXPECT_TRANSFORMATION_MATRIX_EQ(operations_expected.Apply(), blended.Apply());
  EXPECT_EQ(operations_expected.size(), blended.size());
  for (size_t i = 0; i < operations_expected.size(); ++i) {
    TransformOperation expected_op = operations_expected.at(i);
    TransformOperation blended_op = blended.at(i);
    SCOPED_TRACE(i);
    ExpectTransformOperationEqual(expected_op, blended_op);
  }
}

static void CheckProgress(SkMScalar progress,
                          const gfx::Transform& from_matrix,
                          const gfx::Transform& to_matrix,
                          const TransformOperations& from_transform,
                          const TransformOperations& to_transform) {
  gfx::Transform expected_matrix = to_matrix;
  expected_matrix.Blend(from_matrix, progress);
  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected_matrix, to_transform.Blend(from_transform, progress).Apply());
}

TEST(TransformOperationTest, BlendProgress) {
  SkMScalar sx = 2;
  SkMScalar sy = 4;
  SkMScalar sz = 8;
  TransformOperations operations_from;
  operations_from.AppendScale(sx, sy, sz);

  gfx::Transform matrix_from;
  matrix_from.Scale3d(sx, sy, sz);

  sx = 4;
  sy = 8;
  sz = 16;
  TransformOperations operations_to;
  operations_to.AppendScale(sx, sy, sz);

  gfx::Transform matrix_to;
  matrix_to.Scale3d(sx, sy, sz);

  CheckProgress(-1, matrix_from, matrix_to, operations_from, operations_to);
  CheckProgress(0, matrix_from, matrix_to, operations_from, operations_to);
  CheckProgress(0.25f, matrix_from, matrix_to, operations_from, operations_to);
  CheckProgress(0.5f, matrix_from, matrix_to, operations_from, operations_to);
  CheckProgress(1, matrix_from, matrix_to, operations_from, operations_to);
  CheckProgress(2, matrix_from, matrix_to, operations_from, operations_to);
}

TEST(TransformOperationTest, BlendWhenTypesDoNotMatch) {
  SkMScalar sx1 = 2;
  SkMScalar sy1 = 4;
  SkMScalar sz1 = 8;

  SkMScalar dx1 = 1;
  SkMScalar dy1 = 2;
  SkMScalar dz1 = 3;

  SkMScalar sx2 = 4;
  SkMScalar sy2 = 8;
  SkMScalar sz2 = 16;

  SkMScalar dx2 = 10;
  SkMScalar dy2 = 20;
  SkMScalar dz2 = 30;

  TransformOperations operations_from;
  operations_from.AppendScale(sx1, sy1, sz1);
  operations_from.AppendTranslate(dx1, dy1, dz1);

  TransformOperations operations_to;
  operations_to.AppendTranslate(dx2, dy2, dz2);
  operations_to.AppendScale(sx2, sy2, sz2);

  gfx::Transform from;
  from.Scale3d(sx1, sy1, sz1);
  from.Translate3d(dx1, dy1, dz1);

  gfx::Transform to;
  to.Translate3d(dx2, dy2, dz2);
  to.Scale3d(sx2, sy2, sz2);

  SkMScalar progress = 0.25f;

  gfx::Transform expected = to;
  expected.Blend(from, progress);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, LargeRotationsWithSameAxis) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 0, 1, 0);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 0, 2, 360);

  SkMScalar progress = 0.5f;

  gfx::Transform expected;
  expected.RotateAbout(gfx::Vector3dF(0, 0, 1), 180);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, LargeRotationsWithSameAxisInDifferentDirection) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 0, 1, 180);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 0, -1, 180);

  SkMScalar progress = 0.5f;

  gfx::Transform expected;

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, LargeRotationsWithDifferentAxes) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 0, 1, 175);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 1, 0, 175);

  SkMScalar progress = 0.5f;
  gfx::Transform matrix_from;
  matrix_from.RotateAbout(gfx::Vector3dF(0, 0, 1), 175);

  gfx::Transform matrix_to;
  matrix_to.RotateAbout(gfx::Vector3dF(0, 1, 0), 175);

  gfx::Transform expected = matrix_to;
  expected.Blend(matrix_from, progress);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, RotationFromZeroDegDifferentAxes) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 0, 1, 0);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 1, 0, 450);

  SkMScalar progress = 0.5f;
  gfx::Transform expected;
  expected.RotateAbout(gfx::Vector3dF(0, 1, 0), 225);
  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, RotationFromZeroDegSameAxes) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 0, 1, 0);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 0, 1, 450);

  SkMScalar progress = 0.5f;
  gfx::Transform expected;
  expected.RotateAbout(gfx::Vector3dF(0, 0, 1), 225);
  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, RotationToZeroDegDifferentAxes) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 1, 0, 450);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 0, 1, 0);

  SkMScalar progress = 0.5f;
  gfx::Transform expected;
  expected.RotateAbout(gfx::Vector3dF(0, 1, 0), 225);
  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, RotationToZeroDegSameAxes) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0, 0, 1, 450);

  TransformOperations operations_to;
  operations_to.AppendRotate(0, 0, 1, 0);

  SkMScalar progress = 0.5f;
  gfx::Transform expected;
  expected.RotateAbout(gfx::Vector3dF(0, 0, 1), 225);
  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations_to.Blend(operations_from, progress).Apply());
}

TEST(TransformOperationTest, BlendRotationFromIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendRotate(0, 0, 1, 90);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.RotateAbout(gfx::Vector3dF(0, 0, 1), 45);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());

    progress = -0.5f;

    expected.MakeIdentity();
    expected.RotateAbout(gfx::Vector3dF(0, 0, 1), -45);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());

    progress = 1.5f;

    expected.MakeIdentity();
    expected.RotateAbout(gfx::Vector3dF(0, 0, 1), 135);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());
  }
}

TEST(TransformOperationTest, BlendTranslationFromIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendTranslate(2, 2, 2);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.Translate3d(1, 1, 1);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());

    progress = -0.5f;

    expected.MakeIdentity();
    expected.Translate3d(-1, -1, -1);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());

    progress = 1.5f;

    expected.MakeIdentity();
    expected.Translate3d(3, 3, 3);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());
  }
}

TEST(TransformOperationTest, BlendScaleFromIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendScale(3, 3, 3);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.Scale3d(2, 2, 2);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());

    progress = -0.5f;

    expected.MakeIdentity();
    expected.Scale3d(0, 0, 0);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());

    progress = 1.5f;

    expected.MakeIdentity();
    expected.Scale3d(4, 4, 4);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());
  }
}

TEST(TransformOperationTest, BlendSkewFromEmpty) {
  TransformOperations empty_operation;

  TransformOperations operations;
  operations.AppendSkew(2, 2);

  SkMScalar progress = 0.5f;

  gfx::Transform expected;
  expected.Skew(1, 1);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations.Blend(empty_operation, progress).Apply());

  progress = -0.5f;

  expected.MakeIdentity();
  expected.Skew(-1, -1);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations.Blend(empty_operation, progress).Apply());

  progress = 1.5f;

  expected.MakeIdentity();
  expected.Skew(3, 3);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, operations.Blend(empty_operation, progress).Apply());
}

TEST(TransformOperationTest, BlendPerspectiveFromIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendPerspective(1000);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.ApplyPerspectiveDepth(2000);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, operations.Blend(*identity_operations[i], progress).Apply());
  }
}

TEST(TransformOperationTest, BlendRotationToIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendRotate(0, 0, 1, 90);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.RotateAbout(gfx::Vector3dF(0, 0, 1), 45);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, identity_operations[i]->Blend(operations, progress).Apply());
  }
}

TEST(TransformOperationTest, BlendTranslationToIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendTranslate(2, 2, 2);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.Translate3d(1, 1, 1);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, identity_operations[i]->Blend(operations, progress).Apply());
  }
}

TEST(TransformOperationTest, BlendScaleToIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendScale(3, 3, 3);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.Scale3d(2, 2, 2);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, identity_operations[i]->Blend(operations, progress).Apply());
  }
}

TEST(TransformOperationTest, BlendSkewToEmpty) {
  TransformOperations empty_operation;

  TransformOperations operations;
  operations.AppendSkew(2, 2);

  SkMScalar progress = 0.5f;

  gfx::Transform expected;
  expected.Skew(1, 1);

  EXPECT_TRANSFORMATION_MATRIX_EQ(
      expected, empty_operation.Blend(operations, progress).Apply());
}

TEST(TransformOperationTest, BlendPerspectiveToIdentity) {
  std::vector<std::unique_ptr<TransformOperations>> identity_operations =
      GetIdentityOperations();

  for (size_t i = 0; i < identity_operations.size(); ++i) {
    TransformOperations operations;
    operations.AppendPerspective(1000);

    SkMScalar progress = 0.5f;

    gfx::Transform expected;
    expected.ApplyPerspectiveDepth(2000);

    EXPECT_TRANSFORMATION_MATRIX_EQ(
        expected, identity_operations[i]->Blend(operations, progress).Apply());
  }
}

TEST(TransformOperationTest, ExtrapolatePerspectiveBlending) {
  TransformOperations operations1;
  operations1.AppendPerspective(1000);

  TransformOperations operations2;
  operations2.AppendPerspective(500);

  gfx::Transform expected;
  expected.ApplyPerspectiveDepth(400);

  EXPECT_TRANSFORMATION_MATRIX_EQ(expected,
                                  operations1.Blend(operations2, -0.5).Apply());

  expected.MakeIdentity();
  expected.ApplyPerspectiveDepth(2000);

  EXPECT_TRANSFORMATION_MATRIX_EQ(expected,
                                  operations1.Blend(operations2, 1.5).Apply());
}

TEST(TransformOperationTest, ExtrapolateMatrixBlending) {
  gfx::Transform transform1;
  transform1.Translate3d(1, 1, 1);
  TransformOperations operations1;
  operations1.AppendMatrix(transform1);

  gfx::Transform transform2;
  transform2.Translate3d(3, 3, 3);
  TransformOperations operations2;
  operations2.AppendMatrix(transform2);

  gfx::Transform expected;
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected,
                                  operations1.Blend(operations2, 1.5).Apply());

  expected.Translate3d(4, 4, 4);
  EXPECT_TRANSFORMATION_MATRIX_EQ(expected,
                                  operations1.Blend(operations2, -0.5).Apply());
}

TEST(TransformOperationTest, BlendedBoundsWhenTypesDoNotMatch) {
  TransformOperations operations_from;
  operations_from.AppendScale(2.0, 4.0, 8.0);
  operations_from.AppendTranslate(1.0, 2.0, 3.0);

  TransformOperations operations_to;
  operations_to.AppendTranslate(10.0, 20.0, 30.0);
  operations_to.AppendScale(4.0, 8.0, 16.0);

  gfx::BoxF box(1.f, 1.f, 1.f);
  gfx::BoxF bounds;

  SkMScalar min_progress = 0.f;
  SkMScalar max_progress = 1.f;

  EXPECT_FALSE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
}

TEST(TransformOperationTest, BlendedBoundsForIdentity) {
  TransformOperations operations_from;
  operations_from.AppendIdentity();
  TransformOperations operations_to;
  operations_to.AppendIdentity();

  gfx::BoxF box(1.f, 2.f, 3.f);
  gfx::BoxF bounds;

  SkMScalar min_progress = 0.f;
  SkMScalar max_progress = 1.f;

  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(box.ToString(), bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsForTranslate) {
  TransformOperations operations_from;
  operations_from.AppendTranslate(3.0, -4.0, 2.0);
  TransformOperations operations_to;
  operations_to.AppendTranslate(7.0, 4.0, -2.0);

  gfx::BoxF box(1.f, 2.f, 3.f, 4.f, 4.f, 4.f);
  gfx::BoxF bounds;

  SkMScalar min_progress = -0.5f;
  SkMScalar max_progress = 1.5f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(2.f, -6.f, -1.f, 12.f, 20.f, 12.f).ToString(),
            bounds.ToString());

  min_progress = 0.f;
  max_progress = 1.f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(4.f, -2.f, 1.f, 8.f, 12.f, 8.f).ToString(),
            bounds.ToString());

  TransformOperations identity;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
        box, identity, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(1.f, 2.f, 1.f, 11.f, 8.f, 6.f).ToString(),
            bounds.ToString());

  EXPECT_TRUE(identity.BlendedBoundsForBox(
        box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(1.f, -2.f, 3.f, 7.f, 8.f, 6.f).ToString(),
            bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsForScale) {
  TransformOperations operations_from;
  operations_from.AppendScale(3.0, 0.5, 2.0);
  TransformOperations operations_to;
  operations_to.AppendScale(7.0, 4.0, -2.0);

  gfx::BoxF box(1.f, 2.f, 3.f, 4.f, 4.f, 4.f);
  gfx::BoxF bounds;

  SkMScalar min_progress = -0.5f;
  SkMScalar max_progress = 1.5f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(1.f, -7.5f, -28.f, 44.f, 42.f, 56.f).ToString(),
            bounds.ToString());

  min_progress = 0.f;
  max_progress = 1.f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(3.f, 1.f, -14.f, 32.f, 23.f, 28.f).ToString(),
            bounds.ToString());

  TransformOperations identity;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
        box, identity, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(1.f, 2.f, -14.f, 34.f, 22.f, 21.f).ToString(),
            bounds.ToString());

  EXPECT_TRUE(identity.BlendedBoundsForBox(
        box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(1.f, 1.f, 3.f, 14.f, 5.f, 11.f).ToString(),
            bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsWithZeroScale) {
  TransformOperations zero_scale;
  zero_scale.AppendScale(0.0, 0.0, 0.0);
  TransformOperations non_zero_scale;
  non_zero_scale.AppendScale(2.0, -4.0, 5.0);

  gfx::BoxF box(1.f, 2.f, 3.f, 4.f, 4.f, 4.f);
  gfx::BoxF bounds;

  SkMScalar min_progress = 0.f;
  SkMScalar max_progress = 1.f;
  EXPECT_TRUE(zero_scale.BlendedBoundsForBox(
      box, non_zero_scale, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(0.f, -24.f, 0.f, 10.f, 24.f, 35.f).ToString(),
            bounds.ToString());

  EXPECT_TRUE(non_zero_scale.BlendedBoundsForBox(
      box, zero_scale, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(0.f, -24.f, 0.f, 10.f, 24.f, 35.f).ToString(),
            bounds.ToString());

  EXPECT_TRUE(zero_scale.BlendedBoundsForBox(
      box, zero_scale, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF().ToString(), bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsForRotationTrivial) {
  TransformOperations operations_from;
  operations_from.AppendRotate(0.f, 0.f, 1.f, 0.f);
  TransformOperations operations_to;
  operations_to.AppendRotate(0.f, 0.f, 1.f, 360.f);

  float sqrt_2 = sqrt(2.f);
  gfx::BoxF box(
      -sqrt_2, -sqrt_2, 0.f, sqrt_2, sqrt_2, 0.f);
  gfx::BoxF bounds;

  // Since we're rotating 360 degrees, any box with dimensions between 0 and
  // 2 * sqrt(2) should give the same result.
  float sizes[] = { 0.f, 0.1f, sqrt_2, 2.f * sqrt_2 };
  for (size_t i = 0; i < arraysize(sizes); ++i) {
    box.set_size(sizes[i], sizes[i], 0.f);
    SkMScalar min_progress = 0.f;
    SkMScalar max_progress = 1.f;
    EXPECT_TRUE(operations_to.BlendedBoundsForBox(
        box, operations_from, min_progress, max_progress, &bounds));
    EXPECT_EQ(gfx::BoxF(-2.f, -2.f, 0.f, 4.f, 4.f, 0.f).ToString(),
              bounds.ToString());
  }
}

TEST(TransformOperationTest, BlendedBoundsForRotationAllExtrema) {
  // If the normal is out of the plane, we can have up to 6 extrema (a min/max
  // in each dimension) between the endpoints of the arc. This test ensures that
  // we consider all 6.
  TransformOperations operations_from;
  operations_from.AppendRotate(1.f, 1.f, 1.f, 30.f);
  TransformOperations operations_to;
  operations_to.AppendRotate(1.f, 1.f, 1.f, 390.f);

  gfx::BoxF box(1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  gfx::BoxF bounds;

  float min = -1.f / 3.f;
  float max = 1.f;
  float size = max - min;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, 0.f, 1.f, &bounds));
  EXPECT_EQ(gfx::BoxF(min, min, min, size, size, size).ToString(),
            bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsForRotationDifferentAxes) {
  // We can handle rotations about a single axis. If the axes are different,
  // we revert to matrix interpolation for which inflated bounds cannot be
  // computed.
  TransformOperations operations_from;
  operations_from.AppendRotate(1.f, 1.f, 1.f, 30.f);
  TransformOperations operations_to_same;
  operations_to_same.AppendRotate(1.f, 1.f, 1.f, 390.f);
  TransformOperations operations_to_opposite;
  operations_to_opposite.AppendRotate(-1.f, -1.f, -1.f, 390.f);
  TransformOperations operations_to_different;
  operations_to_different.AppendRotate(1.f, 3.f, 1.f, 390.f);

  gfx::BoxF box(1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  gfx::BoxF bounds;

  EXPECT_TRUE(operations_to_same.BlendedBoundsForBox(
      box, operations_from, 0.f, 1.f, &bounds));
  EXPECT_TRUE(operations_to_opposite.BlendedBoundsForBox(
      box, operations_from, 0.f, 1.f, &bounds));
  EXPECT_FALSE(operations_to_different.BlendedBoundsForBox(
      box, operations_from, 0.f, 1.f, &bounds));
}

TEST(TransformOperationTest, BlendedBoundsForRotationPointOnAxis) {
  // Checks that if the point to rotate is sitting on the axis of rotation, that
  // it does not get affected.
  TransformOperations operations_from;
  operations_from.AppendRotate(1.f, 1.f, 1.f, 30.f);
  TransformOperations operations_to;
  operations_to.AppendRotate(1.f, 1.f, 1.f, 390.f);

  gfx::BoxF box(1.f, 1.f, 1.f, 0.f, 0.f, 0.f);
  gfx::BoxF bounds;

  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, 0.f, 1.f, &bounds));
  EXPECT_EQ(box.ToString(), bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsForRotationProblematicAxes) {
  // Zeros in the components of the axis of rotation turned out to be tricky to
  // deal with in practice. This function tests some potentially problematic
  // axes to ensure sane behavior.

  // Some common values used in the expected boxes.
  float dim1 = 0.292893f;
  float dim2 = sqrt(2.f);
  float dim3 = 2.f * dim2;

  struct {
    float x;
    float y;
    float z;
    gfx::BoxF expected;
  } tests[] = {{0.f, 0.f, 0.f, gfx::BoxF(1.f, 1.f, 1.f, 0.f, 0.f, 0.f)},
               {1.f, 0.f, 0.f, gfx::BoxF(1.f, -dim2, -dim2, 0.f, dim3, dim3)},
               {0.f, 1.f, 0.f, gfx::BoxF(-dim2, 1.f, -dim2, dim3, 0.f, dim3)},
               {0.f, 0.f, 1.f, gfx::BoxF(-dim2, -dim2, 1.f, dim3, dim3, 0.f)},
               {1.f, 1.f, 0.f, gfx::BoxF(dim1, dim1, -1.f, dim2, dim2, 2.f)},
               {0.f, 1.f, 1.f, gfx::BoxF(-1.f, dim1, dim1, 2.f, dim2, dim2)},
               {1.f, 0.f, 1.f, gfx::BoxF(dim1, -1.f, dim1, dim2, 2.f, dim2)}};

  for (size_t i = 0; i < arraysize(tests); ++i) {
    float x = tests[i].x;
    float y = tests[i].y;
    float z = tests[i].z;
    TransformOperations operations_from;
    operations_from.AppendRotate(x, y, z, 0.f);
    TransformOperations operations_to;
    operations_to.AppendRotate(x, y, z, 360.f);
    gfx::BoxF box(1.f, 1.f, 1.f, 0.f, 0.f, 0.f);
    gfx::BoxF bounds;

    EXPECT_TRUE(operations_to.BlendedBoundsForBox(
        box, operations_from, 0.f, 1.f, &bounds));
    EXPECT_EQ(tests[i].expected.ToString(), bounds.ToString());
  }
}

static void ExpectBoxesApproximatelyEqual(const gfx::BoxF& lhs,
                                          const gfx::BoxF& rhs,
                                          float tolerance) {
  EXPECT_NEAR(lhs.x(), rhs.x(), tolerance);
  EXPECT_NEAR(lhs.y(), rhs.y(), tolerance);
  EXPECT_NEAR(lhs.z(), rhs.z(), tolerance);
  EXPECT_NEAR(lhs.width(), rhs.width(), tolerance);
  EXPECT_NEAR(lhs.height(), rhs.height(), tolerance);
  EXPECT_NEAR(lhs.depth(), rhs.depth(), tolerance);
}

static void EmpiricallyTestBounds(const TransformOperations& from,
                                  const TransformOperations& to,
                                  SkMScalar min_progress,
                                  SkMScalar max_progress,
                                  bool test_containment_only) {
  gfx::BoxF box(200.f, 500.f, 100.f, 100.f, 300.f, 200.f);
  gfx::BoxF bounds;
  EXPECT_TRUE(
      to.BlendedBoundsForBox(box, from, min_progress, max_progress, &bounds));

  bool first_time = true;
  gfx::BoxF empirical_bounds;
  static const size_t kNumSteps = 10;
  for (size_t step = 0; step < kNumSteps; ++step) {
    float t = step / (kNumSteps - 1.f);
    t = gfx::Tween::FloatValueBetween(t, min_progress, max_progress);
    gfx::Transform partial_transform = to.Blend(from, t).Apply();
    gfx::BoxF transformed = box;
    partial_transform.TransformBox(&transformed);

    if (first_time) {
      empirical_bounds = transformed;
      first_time = false;
    } else {
      empirical_bounds.Union(transformed);
    }
  }

  if (test_containment_only) {
    gfx::BoxF unified_bounds = bounds;
    unified_bounds.Union(empirical_bounds);
    // Convert to the screen space rects these boxes represent.
    gfx::Rect bounds_rect = ToEnclosingRect(
        gfx::RectF(bounds.x(), bounds.y(), bounds.width(), bounds.height()));
    gfx::Rect unified_bounds_rect =
        ToEnclosingRect(gfx::RectF(unified_bounds.x(),
                                   unified_bounds.y(),
                                   unified_bounds.width(),
                                   unified_bounds.height()));
    EXPECT_EQ(bounds_rect.ToString(), unified_bounds_rect.ToString());
  } else {
    // Our empirical estimate will be a little rough since we're only doing
    // 100 samples.
    static const float kTolerance = 1e-2f;
    ExpectBoxesApproximatelyEqual(empirical_bounds, bounds, kTolerance);
  }
}

static void EmpiricallyTestBoundsEquality(const TransformOperations& from,
                                          const TransformOperations& to,
                                          SkMScalar min_progress,
                                          SkMScalar max_progress) {
  EmpiricallyTestBounds(from, to, min_progress, max_progress, false);
}

static void EmpiricallyTestBoundsContainment(const TransformOperations& from,
                                             const TransformOperations& to,
                                             SkMScalar min_progress,
                                             SkMScalar max_progress) {
  EmpiricallyTestBounds(from, to, min_progress, max_progress, true);
}

TEST(TransformOperationTest, BlendedBoundsForRotationEmpiricalTests) {
  // Sets up various axis angle combinations, computes the bounding box and
  // empirically tests that the transformed bounds are indeed contained by the
  // computed bounding box.

  struct {
    float x;
    float y;
    float z;
  } axes[] = {{1.f, 1.f, 1.f},
              {-1.f, -1.f, -1.f},
              {-1.f, 2.f, 3.f},
              {1.f, -2.f, 3.f},
              {1.f, 2.f, -3.f},
              {0.f, 0.f, 0.f},
              {1.f, 0.f, 0.f},
              {0.f, 1.f, 0.f},
              {0.f, 0.f, 1.f},
              {1.f, 1.f, 0.f},
              {0.f, 1.f, 1.f},
              {1.f, 0.f, 1.f},
              {-1.f, 0.f, 0.f},
              {0.f, -1.f, 0.f},
              {0.f, 0.f, -1.f},
              {-1.f, -1.f, 0.f},
              {0.f, -1.f, -1.f},
              {-1.f, 0.f, -1.f}};

  struct {
    float theta_from;
    float theta_to;
  } angles[] = {{5.f, 10.f},
                {10.f, 5.f},
                {0.f, 360.f},
                {20.f, 180.f},
                {-20.f, -180.f},
                {180.f, -220.f},
                {220.f, 320.f}};

  // We can go beyond the range [0, 1] (the bezier might slide out of this range
  // at either end), but since the first and last knots are at (0, 0) and (1, 1)
  // we will never go within it, so these tests are sufficient.
  struct {
    float min_progress;
    float max_progress;
  } progress[] = {
        {0.f, 1.f}, {-.25f, 1.25f},
    };

  for (size_t i = 0; i < arraysize(axes); ++i) {
    for (size_t j = 0; j < arraysize(angles); ++j) {
      for (size_t k = 0; k < arraysize(progress); ++k) {
        float x = axes[i].x;
        float y = axes[i].y;
        float z = axes[i].z;
        TransformOperations operations_from;
        operations_from.AppendRotate(x, y, z, angles[j].theta_from);
        TransformOperations operations_to;
        operations_to.AppendRotate(x, y, z, angles[j].theta_to);
        EmpiricallyTestBoundsContainment(operations_from,
                                         operations_to,
                                         progress[k].min_progress,
                                         progress[k].max_progress);
      }
    }
  }
}

TEST(TransformOperationTest, PerspectiveMatrixAndTransformBlendingEquivalency) {
    TransformOperations from_operations;
    from_operations.AppendPerspective(200);

    TransformOperations to_operations;
    to_operations.AppendPerspective(1000);

    gfx::Transform from_transform;
    from_transform.ApplyPerspectiveDepth(200);

    gfx::Transform to_transform;
    to_transform.ApplyPerspectiveDepth(1000);

    static const int steps = 20;
    for (int i = 0; i < steps; ++i) {
      double progress = static_cast<double>(i) / (steps - 1);

      gfx::Transform blended_matrix = to_transform;
      EXPECT_TRUE(blended_matrix.Blend(from_transform, progress));

      gfx::Transform blended_transform =
          to_operations.Blend(from_operations, progress).Apply();

      EXPECT_TRANSFORMATION_MATRIX_EQ(blended_matrix, blended_transform);
    }
}

TEST(TransformOperationTest, BlendedBoundsForPerspective) {
  struct {
    float from_depth;
    float to_depth;
  } perspective_depths[] = {
        {600.f, 400.f},
        {800.f, 1000.f},
        {800.f, std::numeric_limits<float>::infinity()},
    };

  struct {
    float min_progress;
    float max_progress;
  } progress[] = {
        {0.f, 1.f}, {-0.1f, 1.1f},
    };

  for (size_t i = 0; i < arraysize(perspective_depths); ++i) {
    for (size_t j = 0; j < arraysize(progress); ++j) {
      TransformOperations operations_from;
      operations_from.AppendPerspective(perspective_depths[i].from_depth);
      TransformOperations operations_to;
      operations_to.AppendPerspective(perspective_depths[i].to_depth);
      EmpiricallyTestBoundsEquality(operations_from,
                                    operations_to,
                                    progress[j].min_progress,
                                    progress[j].max_progress);
    }
  }
}

TEST(TransformOperationTest, BlendedBoundsForSkew) {
  struct {
    float from_x;
    float from_y;
    float to_x;
    float to_y;
  } skews[] = {
        {1.f, 0.5f, 0.5f, 1.f}, {2.f, 1.f, 0.5f, 0.5f},
    };

  struct {
    float min_progress;
    float max_progress;
  } progress[] = {
        {0.f, 1.f}, {-0.1f, 1.1f},
    };

  for (size_t i = 0; i < arraysize(skews); ++i) {
    for (size_t j = 0; j < arraysize(progress); ++j) {
      TransformOperations operations_from;
      operations_from.AppendSkew(skews[i].from_x, skews[i].from_y);
      TransformOperations operations_to;
      operations_to.AppendSkew(skews[i].to_x, skews[i].to_y);
      EmpiricallyTestBoundsEquality(operations_from,
                                    operations_to,
                                    progress[j].min_progress,
                                    progress[j].max_progress);
    }
  }
}

TEST(TransformOperationTest, NonCommutativeRotations) {
  TransformOperations operations_from;
  operations_from.AppendRotate(1.0, 0.0, 0.0, 0.0);
  operations_from.AppendRotate(0.0, 1.0, 0.0, 0.0);
  TransformOperations operations_to;
  operations_to.AppendRotate(1.0, 0.0, 0.0, 45.0);
  operations_to.AppendRotate(0.0, 1.0, 0.0, 135.0);

  gfx::BoxF box(0, 0, 0, 1, 1, 1);
  gfx::BoxF bounds;

  SkMScalar min_progress = 0.0f;
  SkMScalar max_progress = 1.0f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  gfx::Transform blended_transform =
      operations_to.Blend(operations_from, max_progress).Apply();
  gfx::Point3F blended_point(0.9f, 0.9f, 0.0f);
  blended_transform.TransformPoint(&blended_point);
  gfx::BoxF expanded_bounds = bounds;
  expanded_bounds.ExpandTo(blended_point);
  EXPECT_EQ(bounds.ToString(), expanded_bounds.ToString());
}

TEST(TransformOperationTest, BlendedBoundsForSequence) {
  TransformOperations operations_from;
  operations_from.AppendTranslate(1.0, -5.0, 1.0);
  operations_from.AppendScale(-1.0, 2.0, 3.0);
  operations_from.AppendTranslate(2.0, 4.0, -1.0);
  TransformOperations operations_to;
  operations_to.AppendTranslate(13.0, -1.0, 5.0);
  operations_to.AppendScale(-3.0, -2.0, 5.0);
  operations_to.AppendTranslate(6.0, -2.0, 3.0);

  gfx::BoxF box(1.f, 2.f, 3.f, 4.f, 4.f, 4.f);
  gfx::BoxF bounds;

  SkMScalar min_progress = -0.5f;
  SkMScalar max_progress = 1.5f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(-57.f, -59.f, -1.f, 76.f, 112.f, 80.f).ToString(),
            bounds.ToString());

  min_progress = 0.f;
  max_progress = 1.f;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
      box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(-32.f, -25.f, 7.f, 42.f, 44.f, 48.f).ToString(),
            bounds.ToString());

  TransformOperations identity;
  EXPECT_TRUE(operations_to.BlendedBoundsForBox(
        box, identity, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(-33.f, -13.f, 3.f, 57.f, 19.f, 52.f).ToString(),
            bounds.ToString());

  EXPECT_TRUE(identity.BlendedBoundsForBox(
        box, operations_from, min_progress, max_progress, &bounds));
  EXPECT_EQ(gfx::BoxF(-7.f, -3.f, 2.f, 15.f, 23.f, 20.f).ToString(),
            bounds.ToString());
}

TEST(TransformOperationTest, IsTranslationWithSingleOperation) {
  TransformOperations empty_operations;
  EXPECT_TRUE(empty_operations.IsTranslation());

  TransformOperations identity;
  identity.AppendIdentity();
  EXPECT_TRUE(identity.IsTranslation());

  TransformOperations translate;
  translate.AppendTranslate(1.f, 2.f, 3.f);
  EXPECT_TRUE(translate.IsTranslation());

  TransformOperations rotate;
  rotate.AppendRotate(1.f, 2.f, 3.f, 4.f);
  EXPECT_FALSE(rotate.IsTranslation());

  TransformOperations scale;
  scale.AppendScale(1.f, 2.f, 3.f);
  EXPECT_FALSE(scale.IsTranslation());

  TransformOperations skew;
  skew.AppendSkew(1.f, 2.f);
  EXPECT_FALSE(skew.IsTranslation());

  TransformOperations perspective;
  perspective.AppendPerspective(1.f);
  EXPECT_FALSE(perspective.IsTranslation());

  TransformOperations identity_matrix;
  identity_matrix.AppendMatrix(gfx::Transform());
  EXPECT_TRUE(identity_matrix.IsTranslation());

  TransformOperations translation_matrix;
  gfx::Transform translation_transform;
  translation_transform.Translate3d(1.f, 2.f, 3.f);
  translation_matrix.AppendMatrix(translation_transform);
  EXPECT_TRUE(translation_matrix.IsTranslation());

  TransformOperations scaling_matrix;
  gfx::Transform scaling_transform;
  scaling_transform.Scale(2.f, 2.f);
  scaling_matrix.AppendMatrix(scaling_transform);
  EXPECT_FALSE(scaling_matrix.IsTranslation());
}

TEST(TransformOperationTest, IsTranslationWithMultipleOperations) {
  TransformOperations operations1;
  operations1.AppendSkew(1.f, 2.f);
  operations1.AppendTranslate(1.f, 2.f, 3.f);
  operations1.AppendIdentity();
  EXPECT_FALSE(operations1.IsTranslation());

  TransformOperations operations2;
  operations2.AppendIdentity();
  operations2.AppendTranslate(3.f, 2.f, 1.f);
  gfx::Transform translation_transform;
  translation_transform.Translate3d(1.f, 2.f, 3.f);
  operations2.AppendMatrix(translation_transform);
  EXPECT_TRUE(operations2.IsTranslation());
}

TEST(TransformOperationTest, ScaleComponent) {
  SkMScalar scale;

  // Scale.
  TransformOperations operations1;
  operations1.AppendScale(-3.f, 2.f, 5.f);
  EXPECT_TRUE(operations1.ScaleComponent(&scale));
  EXPECT_EQ(5.f, scale);

  // Translate.
  TransformOperations operations2;
  operations2.AppendTranslate(1.f, 2.f, 3.f);
  EXPECT_TRUE(operations2.ScaleComponent(&scale));
  EXPECT_EQ(1.f, scale);

  // Rotate.
  TransformOperations operations3;
  operations3.AppendRotate(1.f, 2.f, 3.f, 4.f);
  EXPECT_TRUE(operations3.ScaleComponent(&scale));
  EXPECT_EQ(1.f, scale);

  // Matrix that's only a translation.
  TransformOperations operations4;
  gfx::Transform translation_transform;
  translation_transform.Translate3d(1.f, 2.f, 3.f);
  operations4.AppendMatrix(translation_transform);
  EXPECT_TRUE(operations4.ScaleComponent(&scale));
  EXPECT_EQ(1.f, scale);

  // Matrix that includes scale.
  TransformOperations operations5;
  gfx::Transform matrix;
  matrix.RotateAboutZAxis(30.0);
  matrix.Scale(-7.f, 6.f);
  matrix.Translate3d(gfx::Vector3dF(3.f, 7.f, 1.f));
  operations5.AppendMatrix(matrix);
  EXPECT_TRUE(operations5.ScaleComponent(&scale));
  EXPECT_EQ(7.f, scale);

  // Matrix with perspective.
  TransformOperations operations6;
  matrix.ApplyPerspectiveDepth(2000.f);
  operations6.AppendMatrix(matrix);
  EXPECT_FALSE(operations6.ScaleComponent(&scale));

  // Skew.
  TransformOperations operations7;
  operations7.AppendSkew(30.f, 60.f);
  EXPECT_TRUE(operations7.ScaleComponent(&scale));
  EXPECT_EQ(2.f, scale);

  // Perspective.
  TransformOperations operations8;
  operations8.AppendPerspective(500.f);
  EXPECT_FALSE(operations8.ScaleComponent(&scale));

  // Translate + Scale.
  TransformOperations operations9;
  operations9.AppendTranslate(1.f, 2.f, 3.f);
  operations9.AppendScale(2.f, 5.f, 4.f);
  EXPECT_TRUE(operations9.ScaleComponent(&scale));
  EXPECT_EQ(5.f, scale);

  // Translate + Scale + Matrix with translate.
  operations9.AppendMatrix(translation_transform);
  EXPECT_TRUE(operations9.ScaleComponent(&scale));
  EXPECT_EQ(5.f, scale);

  // Scale + translate.
  TransformOperations operations10;
  operations10.AppendScale(2.f, 3.f, 2.f);
  operations10.AppendTranslate(1.f, 2.f, 3.f);
  EXPECT_TRUE(operations10.ScaleComponent(&scale));
  EXPECT_EQ(3.f, scale);

  // Two Scales.
  TransformOperations operations11;
  operations11.AppendScale(2.f, 3.f, 2.f);
  operations11.AppendScale(-3.f, -2.f, -3.f);
  EXPECT_TRUE(operations11.ScaleComponent(&scale));
  EXPECT_EQ(9.f, scale);

  // Scale + Matrix.
  TransformOperations operations12;
  operations12.AppendScale(2.f, 2.f, 2.f);
  gfx::Transform scaling_transform;
  scaling_transform.Scale(2.f, 2.f);
  operations12.AppendMatrix(scaling_transform);
  EXPECT_TRUE(operations12.ScaleComponent(&scale));
  EXPECT_EQ(4.f, scale);

  // Scale + Rotate.
  TransformOperations operations13;
  operations13.AppendScale(2.f, 2.f, 2.f);
  operations13.AppendRotate(1.f, 2.f, 3.f, 4.f);
  EXPECT_TRUE(operations13.ScaleComponent(&scale));
  EXPECT_EQ(2.f, scale);

  // Scale + Skew.
  TransformOperations operations14;
  operations14.AppendScale(2.f, 2.f, 2.f);
  operations14.AppendSkew(60.f, 45.f);
  EXPECT_TRUE(operations14.ScaleComponent(&scale));
  EXPECT_EQ(4.f, scale);

  // Scale + Perspective.
  TransformOperations operations15;
  operations15.AppendScale(2.f, 2.f, 2.f);
  operations15.AppendPerspective(1.f);
  EXPECT_FALSE(operations15.ScaleComponent(&scale));

  // Matrix with skew.
  TransformOperations operations16;
  gfx::Transform skew_transform;
  skew_transform.Skew(50.f, 60.f);
  operations16.AppendMatrix(skew_transform);
  EXPECT_TRUE(operations16.ScaleComponent(&scale));
  EXPECT_EQ(2.f, scale);
}

TEST(TransformOperationsTest, ApproximateEquality) {
  float noise = 1e-7f;
  float tolerance = 1e-5f;
  TransformOperations lhs;
  TransformOperations rhs;

  // Empty lists of operations are trivially equal.
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));

  rhs.AppendIdentity();
  rhs.AppendTranslate(0, 0, 0);
  rhs.AppendRotate(1, 0, 0, 0);
  rhs.AppendScale(1, 1, 1);
  rhs.AppendSkew(0, 0);
  rhs.AppendMatrix(gfx::Transform());

  // Even though both lists operations are effectively the identity matrix, rhs
  // has a different number of operations and is therefore different.
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));

  rhs.AppendPerspective(800);

  // Assignment should produce equal lists of operations.
  lhs = rhs;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));

  // Cannot affect identity operations.
  lhs.at(0).translate.x = 1;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));

  lhs.at(1).translate.x += noise;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));
  lhs.at(1).translate.x += 1;
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));

  lhs = rhs;
  lhs.at(2).rotate.angle += noise;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));
  lhs.at(2).rotate.angle = 1;
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));

  lhs = rhs;
  lhs.at(3).scale.x += noise;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));
  lhs.at(3).scale.x += 1;
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));

  lhs = rhs;
  lhs.at(4).skew.x += noise;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));
  lhs.at(4).skew.x = 2;
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));

  lhs = rhs;
  lhs.at(5).matrix.Translate3d(noise, 0, 0);
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));
  lhs.at(5).matrix.Translate3d(1, 1, 1);
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));

  lhs = rhs;
  lhs.at(6).perspective_depth += noise;
  EXPECT_TRUE(lhs.ApproximatelyEqual(rhs, tolerance));
  lhs.at(6).perspective_depth = 801;
  EXPECT_FALSE(lhs.ApproximatelyEqual(rhs, tolerance));
}

}  // namespace
}  // namespace cc
